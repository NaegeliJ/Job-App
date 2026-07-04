#include "app_state.h"
#include "db.h"
#include "json.hpp"
#include "html.h"
#include "response.h"
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include "fitcheck.h"

using json = nlohmann::json;

static constexpr int kOnboardingQuestionCount = 9;
static const std::string kAiNotConfigured = "AI not configured — set provider and API key in Settings.";

static void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    size_t pos;
    while ((pos = text.find(from)) != std::string::npos)
        text.replace(pos, from.length(), to);
}

static std::string buildFitcheckPrompt(const std::string& system_prompt_template,
                                        const std::string& profile, const std::string& job_text) {
    std::string prompt = system_prompt_template;
    replaceAll(prompt, "{{profile}}", profile);
    replaceAll(prompt, "{{jobText}}", job_text);
    return prompt;
}

static std::string generateManualJobId(const std::string& text) {
    size_t hash = std::hash<std::string>{}(text.substr(0, 500));
    std::stringstream ss;
    ss << "m" << std::hex << hash;
    return ss.str();
}

static std::string todayIsoDate() {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return buf;
}

static int parseEmploymentGrade(const json& fields) {
    if (!fields.contains("employment_grade")) return 0;
    const json& grade = fields["employment_grade"];
    if (grade.is_number()) return grade.get<int>();
    if (grade.is_string()) {
        std::string text = grade.get<std::string>();
        auto digit = std::find_if(text.begin(), text.end(), ::isdigit);
        return digit != text.end() ? std::stoi(std::string(digit, text.end())) : 0;
    }
    return 0;
}

static Job buildJobFromExtraction(const json& fields, const std::string& job_id, const std::string& source_text) {
    Job job;
    job.job_id           = job_id;
    job.title            = fields.value("title", "");
    job.company_name     = fields.value("company_name", "");
    job.place            = fields.value("place", "");
    job.zipcode          = fields.value("zipcode", "");
    job.canton_code      = "N/A";
    job.employment_grade = parseEmploymentGrade(fields);
    job.application_url  = fields.value("application_url", "");
    job.detail_url       = "";
    job.pub_date         = fields.value("pub_date", "");
    if (job.pub_date.empty())
        job.pub_date = todayIsoDate();
    job.end_date         = fields.value("end_date", "");

    std::string description = fields.value("description", "");
    job.template_text    = description.empty() ? source_text : description;
    return job;
}

FitcheckResult runFitcheck(const std::string& cleaned_text, const std::string& profile,
                           const std::string& system_prompt_template, const AiSnapshot& ai,
                           const std::string& api_key) {
    std::string prompt = buildFitcheckPrompt(system_prompt_template, profile, cleaned_text);
    json request = buildAiRequest(ai.provider, ai.model, prompt, ai.max_tokens,
                                  ai.temperature, ai.top_p, ai.top_k);
    std::string response = httpPostAI(ai.endpoint, api_key, request.dump());
    std::string accumulated = parseStreamingResponse(response);
    if (accumulated.empty())
        throw std::runtime_error("Empty parsed response from AI (httpPostAI succeeded but parse failed)");
    json fitData = extractJsonFromResponse(accumulated);
    return {
        fitData.value("fit_score", 0),
        fitData.value("fit_label", "Unknown"),
        fitData.value("fit_summary", ""),
        fitData.value("fit_reasoning", "")
    };
}

FitcheckResult checkAndSave(AppState& state, const std::string& job_id, const std::string& cleaned_text,
                            const std::string& profile, const AiSnapshot& ai, const std::string& api_key,
                            const std::string& source) {
    FitcheckResult result = runFitcheck(cleaned_text, profile, state.system_prompt_template, ai, api_key);
    std::lock_guard<std::mutex> lock(state.db_mutex);
    save_fit_result_v2(state.db, job_id, result.score, result.label, result.summary, result.reasoning, source);
    return result;
}

std::optional<AiSnapshot> requireAi(AppState& state, httplib::Response& res) {
    std::string key = readApiKey(state.api_key, state.api_key_mutex);
    auto ai = getReadyAi(key, state.config_v2, state.config_v2_mutex);
    if (!ai)
        sendError(res, 500, kAiNotConfigured);
    return ai;
}

std::optional<FitcheckResult> fitcheckSingleJob(AppState& state, httplib::Response& res,
                                                const std::string& job_id, bool clear_first,
                                                const std::string& source) {
    std::string profile = loadProfileMarkdown(state.profile_path);
    if (profile.empty()) {
        sendError(res, 400, "No profile found. Complete onboarding first.");
        return std::nullopt;
    }

    if (clear_first) {
        std::lock_guard<std::mutex> lock(state.db_mutex);
        clear_fit_data(state.db, job_id);
    }

    std::optional<std::string> template_text;
    {
        std::lock_guard<std::mutex> lock(state.db_mutex);
        template_text = get_job_template_text(state.db, job_id);
    }
    if (!template_text) {
        sendError(res, 404, "Job not found");
        return std::nullopt;
    }

    std::string cleaned = cleanTemplateText(*template_text);
    if (cleaned.empty()) {
        sendError(res, 400, "Job has no description text");
        return std::nullopt;
    }

    auto ai = requireAi(state, res);
    if (!ai) return std::nullopt;
    std::string key = readApiKey(state.api_key, state.api_key_mutex);

    return checkAndSave(state, job_id, cleaned, profile, *ai, key, source);
}

static void recordFailure(ProgressTracker& progress) {
    progress.failed++;
    progress.done++;
}

// Returns true if the job was checked. Transient errors count as failures;
// fatal AI errors (bad key, no credits) propagate to abort the batch.
static bool fitcheckOneJob(AppState& state, const JobRecord& job, const std::string& profile,
                           const AiSnapshot& ai, const std::string& api_key) {
    std::string cleaned = cleanTemplateText(job.template_text);
    if (cleaned.empty()) {
        std::cerr << "[WARN] Empty template for job: " << job.job_id << std::endl;
        recordFailure(state.fitcheck_progress);
        return false;
    }
    try {
        checkAndSave(state, job.job_id, cleaned, profile, ai, api_key, "md_file_profile");
        state.fitcheck_progress.done++;
        return true;
    } catch (const FatalAiError& e) {
        if (e.code() == "invalid_api_key" || e.code() == "no_credits")
            throw;
        std::cerr << "[WARN] Transient AI error for " << job.job_id << ": " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed fit-check for " << job.job_id << ": " << e.what() << std::endl;
    }
    recordFailure(state.fitcheck_progress);
    return false;
}

BatchFitcheckResult runBatchFitcheckCore(AppState& state, const std::string& profile,
                                         const AiSnapshot& ai, const std::string& api_key) {
    BatchFitcheckResult result;

    bool expected = false;
    if (!state.fitcheck_progress.running.compare_exchange_strong(expected, true)) {
        result.already_running = true;
        return result;
    }

    int fitcheck_limit;
    { std::shared_lock<std::shared_mutex> lock(state.config_v2_mutex); fitcheck_limit = state.config_v2.fitcheck_limit; }

    std::vector<JobRecord> jobs;
    {
        std::lock_guard<std::mutex> lock(state.db_mutex);
        jobs = get_jobs_needing_fitcheck_v2(state.db, fitcheck_limit);
    }

    std::cout << "[INFO] Starting fit-check for " << jobs.size() << " jobs" << std::endl;

    state.fitcheck_progress.done   = 0;
    state.fitcheck_progress.failed = 0;
    state.fitcheck_progress.total  = static_cast<int>(jobs.size());

    try {
        for (auto& job : jobs) {
            if (fitcheckOneJob(state, job, profile, ai, api_key)) {
                result.checked++;
                std::cout << "[INFO] Fit-checked [" << result.checked << "/" << jobs.size() << "]: " << job.job_id << std::endl;
            } else {
                result.failed++;
            }
        }
    } catch (const FatalAiError& e) {
        std::cerr << "[ERROR] Fatal AI error during fit-check: " << e.what() << std::endl;
        result.fatal_code  = e.code();
        result.fatal_error = e.what();
    }

    state.fitcheck_progress.running = false;
    return result;
}

void runBatchFitcheck(AppState& state, httplib::Response& res) {
    std::string profile = loadProfileMarkdown(state.profile_path);
    if (profile.empty()) {
        sendError(res, 400, "No profile found. Complete onboarding first.");
        return;
    }

    auto ai_opt = requireAi(state, res);
    if (!ai_opt) return;
    std::string key = readApiKey(state.api_key, state.api_key_mutex);

    BatchFitcheckResult result = runBatchFitcheckCore(state, profile, *ai_opt, key);

    if (result.already_running) {
        sendJson(res, {{"ok", false}, {"error", "fit-check already running"}}, 409);
        return;
    }
    if (!result.fatal_code.empty()) {
        sendJson(res, {{"ok", false}, {"error_code", result.fatal_code}, {"error", result.fatal_error}}, 500);
        return;
    }
    sendJson(res, {{"ok", true}, {"checked", result.checked}, {"failed", result.failed}});
}

void importJobFromText(AppState& state, httplib::Response& res, const AiSnapshot& ai,
                       const std::string& key, const std::string& text) {
    std::string jobId = generateManualJobId(text);
    std::cout << "[INFO] Import: generated job_id=" << jobId << " from " << text.size() << " chars" << std::endl;

    std::string truncated = text.substr(0, 8000);
    std::string extractionPrompt = state.import_prompt + truncated;

    try {
        std::cout << "[INFO] Import: calling AI to extract fields..." << std::endl;
        // Lower temperature for deterministic field extraction vs. creative text generation.
        json extractionRequest = buildAiRequest(ai.provider, ai.model, extractionPrompt, ai.max_tokens,
                                                0.3, ai.top_p, ai.top_k);

        std::string extractionResponse = httpPostAI(ai.endpoint, key, extractionRequest.dump());
        std::string accumulated = parseStreamingResponse(extractionResponse);
        if (accumulated.empty()) throw std::runtime_error("Empty response from extraction AI");
        std::cout << "[INFO] Import: extraction AI responded (" << accumulated.size() << " chars)" << std::endl;
        std::cout << "[DEBUG] Import: extraction raw (first 500): " << accumulated.substr(0, 500) << std::endl;

        json jobFields;
        try {
            jobFields = extractJsonFromResponse(accumulated);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Import: extraction JSON parse failed: " << e.what() << std::endl;
            try { jobFields = json::parse(accumulated); } catch (...) {
                throw std::runtime_error(std::string("Extraction parse failed: ") + e.what());
            }
        }

        Job job = buildJobFromExtraction(jobFields, jobId, text);
        {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            insert_or_update_job(state.db, job);
        }
        std::cout << "[INFO] Import: job inserted — " << jobId << " — " << job.title << std::endl;

        std::string profile = loadProfileMarkdown(state.profile_path);
        std::string cleaned = cleanTemplateText(job.template_text);
        if (!profile.empty() && !cleaned.empty()) {
            std::cout << "[INFO] Import: running fit-check for " << jobId << "..." << std::endl;
            try {
                auto result = checkAndSave(state, jobId, cleaned, profile, ai, key, "md_file_profile");
                std::cout << "[INFO] Import: fit-check complete for " << jobId << " — " << result.label << " (" << result.score << ")" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[WARN] Import: fit-check failed for " << jobId << ": " << e.what() << std::endl;
            }
        }

        std::cout << "[INFO] Import: complete — " << jobId << " — " << job.title << std::endl;
        sendJson(res, {{"ok", true}, {"job_id", jobId}, {"title", job.title}});

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Import failed: " << e.what() << std::endl;
        sendError(res, 500, std::string("Import failed: ") + e.what());
    }
}

void generateProfile(AppState& state, const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);

        if (!body.contains("answers") || !body["answers"].is_array() ||
            body["answers"].size() != kOnboardingQuestionCount) {
            sendError(res, 400, "Expected " + std::to_string(kOnboardingQuestionCount) + " answers");
            return;
        }

        auto ai_opt = requireAi(state, res);
        if (!ai_opt) return;
        const AiSnapshot& ai = *ai_opt;
        const auto& answers = body["answers"];

        const std::string questions[kOnboardingQuestionCount] = {
            "CV Drop",
            "Career Goal (3–5 Years)",
            "Intrinsic Motivation",
            "No-Gos",
            "Tech Skills: Build vs. Tolerate",
            "Company Type & Region",
            "Hard Constraints",
            "Work Style",
            "What Should the LLM Know That's Not in the CV?"
        };

        std::string profileText = "Candidate Onboarding Answers:\n\n";
        for (int i = 0; i < kOnboardingQuestionCount; i++) {
            profileText += "Q" + std::to_string(i + 1) + ": " + questions[i] + "\n";
            std::string answer = answers[i].is_string() ? answers[i].get<std::string>() : answers[i].dump();
            profileText += "A" + std::to_string(i + 1) + ": " + answer + "\n\n";
        }

        std::string prompt = state.onboarding_prompt + profileText;
        json request = buildAiRequest(ai.provider, ai.model, prompt, ai.max_tokens, ai.temperature, ai.top_p, ai.top_k);
        if (ai.provider != "ollama_local") request["response_format"] = {{"type", "text"}};

        std::string key = readApiKey(state.api_key, state.api_key_mutex);
        std::string response = httpPostAI(ai.endpoint, key, request.dump());
        std::string parsedResponse = parseStreamingResponse(response);
        if (parsedResponse.empty())
            throw std::runtime_error("Empty parsed response from AI (httpPostAI succeeded but parse failed)");

        std::string profileMarkdown = extractBlock(parsedResponse, "markdown");

        std::ofstream file(state.profile_path);
        if (!file.is_open())
            throw std::runtime_error("Failed to open profile file");
        file << profileMarkdown;

        sendJson(res, {{"ok", true}});

    } catch (const std::exception& e) {
        sendError(res, 500, e.what());
    }
}
