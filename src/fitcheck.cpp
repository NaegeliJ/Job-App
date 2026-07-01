#include "app_state.h"
#include "json.hpp"
#include "html.h"
#include <string>
#include "fitcheck.h"

using json = nlohmann::json;


static std::string buildFitcheckPrompt(const std::string& system_prompt_template,
                                        const std::string& profile, const std::string& job_text) {
    std::string result = system_prompt_template;
    size_t pos;
    while ((pos = result.find("{{profile}}")) != std::string::npos)
        result.replace(pos, 11, profile);
    while ((pos = result.find("{{jobText}}")) != std::string::npos)
        result.replace(pos, 11, job_text);
    return result;
}

static std::string generateManualJobId(const std::string& text) {
    size_t hash = std::hash<std::string>{}(text.substr(0, 500));
    std::stringstream ss;
    ss << "m" << std::hex << hash;
    return ss.str();
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

std::optional<AiSnapshot> resolveAi(AppState& state, httplib::Response& res) {
      std::string key = readApiKey(state.api_key, state.api_key_mutex);
      auto ai_opt = getReadyAi(key, state.config_v2, state.config_v2_mutex);
      if (!ai_opt) {
          res.status = 500;
          res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
      }
      return ai_opt;

  }


  void importJobFromText(AppState& state, httplib::Response& res, const AiSnapshot& ai, const std::string& key, const std::string& text){
      std::string jobId = generateManualJobId(text);
      std::cout << "[INFO] Import: generated job_id=" << jobId << " from " << text.size() << " chars" << std::endl;

      std::string truncated = text.substr(0, 8000);
      std::string extractionPrompt =
        "Extract structured data from the job posting text below. The text may have been copied from a Swiss job board "
        "(e.g. jobs.ch) where values appear BEFORE their labels on separate lines. Common label words to recognize:\n"
        "- 'Ort' = location/city (the value before it is the place, NOT company)\n"
        "- 'Lohn', 'CHF', 'Gehalt', 'Salaire' = salary (IGNORE — do not put in any field)\n"
        "- 'Pensum', 'Arbeitspensum' = workload % (the value before it is employment_grade)\n"
        "- 'Anstellungsart' = employment type (ignore)\n"
        "- 'Bewerben' = apply button (ignore)\n"
        "- 'icon' lines before benefit text = UI artifacts (ignore)\n"
        "- Bare numbers like '482551' = job IDs (ignore, NOT zipcode)\n\n"
        "Return ONLY valid JSON with exactly these keys:\n"
        "- title: job title (string)\n"
        "- company_name: name of the hiring company (string — NOT a city, NOT a location, NOT empty if identifiable)\n"
        "- place: city or town of the job location (string)\n"
        "- zipcode: 4-digit Swiss postal code or equivalent — digits only. Empty if not explicitly present. NEVER salary, NEVER job ID.\n"
        "- employment_grade: workload as integer 0-100. Use the lower bound if a range (e.g. '80-100%' → 80). 100 for full-time. 0 if unknown.\n"
        "- application_url: direct URL to apply or view the posting (string, empty if not found)\n"
        "- pub_date: publication date YYYY-MM-DD (string, empty if unknown)\n"
        "- end_date: application deadline YYYY-MM-DD (string, empty if unknown)\n"
        "- description: clean plain-text reconstruction of the job content — role summary, responsibilities, "
        "qualifications, benefits. Rules: (1) remove ALL lines that are a single word or short label like 'icon', "
        "'Ort', 'Lohn', 'Pensum', 'Anstellungsart', 'Bewerben', 'decore', 'recruiter', 'Login', 'FAQ', etc. "
        "(2) remove navigation, footer, similar job listings, recruiter bios, legal text, salary figures, copyright. "
        "(3) benefits: write as '- Benefit name' per line, NOT 'icon\\nBenefit name'. "
        "(4) section headings on their own line in title case. "
        "(5) output must be readable prose and bullet points — no isolated single words, no label fragments. "
        "If nothing meaningful found, empty string.\n"
        "Unknown fields: empty string or 0. No extra keys. No salary anywhere.\n\nText:\n" + truncated;

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

        Job job;
        job.job_id           = jobId;
        job.title            = jobFields.value("title", "");
        job.company_name     = jobFields.value("company_name", "");
        job.place            = jobFields.value("place", "");
        job.zipcode          = jobFields.value("zipcode", "");
        job.canton_code      = "N/A";
        {
            auto& eg = jobFields["employment_grade"];
            if (eg.is_number()) {
                job.employment_grade = eg.get<int>();
            } else if (eg.is_string()) {
                std::string s = eg.get<std::string>();
                auto it = std::find_if(s.begin(), s.end(), ::isdigit);
                job.employment_grade = (it != s.end()) ? std::stoi(std::string(it, s.end())) : 0;
            } else {
                job.employment_grade = 0;
            }
        }
        job.application_url  = jobFields.value("application_url", "");
        job.detail_url       = "";
        job.pub_date         = jobFields.value("pub_date", "");
        if (job.pub_date.empty()) {
            std::time_t now = std::time(nullptr);
            std::tm tm_buf{};
#ifdef _WIN32
            localtime_s(&tm_buf, &now);
#else
            localtime_r(&now, &tm_buf);
#endif
            char buf[11];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
            job.pub_date = buf;
        }
        job.end_date         = jobFields.value("end_date", "");
        std::string description = jobFields.value("description", "");
        job.template_text    = description.empty() ? text : description;

        {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            insert_or_update_job(state.db, job);
        }

        std::cout << "[INFO] Import: job inserted — " << jobId << " — " << job.title << std::endl;

        std::string profileContent = loadProfileMarkdown(state.profile_path);
        if (!profileContent.empty()) {
            std::cout << "[INFO] Import: running fit-check for " << jobId << "..." << std::endl;
            std::string cleaned = cleanTemplateText(job.template_text);
            if (!cleaned.empty()) {
                try {
                    auto result = runFitcheck(cleaned, profileContent, state.system_prompt_template, ai, key);
                    std::lock_guard<std::mutex> lock(state.db_mutex);
                    save_fit_result_v2(state.db, jobId, result.score, result.label, result.summary, result.reasoning, "md_file_profile");
                    std::cout << "[INFO] Import: fit-check complete for " << jobId << " — " << result.label << " (" << result.score << ")" << std::endl;
                } catch (const std::exception& e2) {
                    std::cerr << "[WARN] Import: fit-check failed for " << jobId << ": " << e2.what() << std::endl;
                }
            }
        }

        std::cout << "[INFO] Import: complete — " << jobId << " — " << job.title << std::endl;
        res.set_content(json{{"ok", true}, {"job_id", jobId}, {"title", job.title}}.dump(), "application/json");

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Import failed: " << e.what() << std::endl;
        res.status = 500;
        res.set_content(json{{"error", std::string("Import failed: ") + e.what()}}.dump(), "application/json");
    }

}
