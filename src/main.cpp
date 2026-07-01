#include <optional>
#include <string>
#define _WIN32_WINNT 0x0A00
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#endif
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <curl/curl.h>
#include "httplib.h"
#include "sqlite3.h"
#include "json.hpp"
#include "db.h"
#include "http.h"
#include "html.h"
#include "config.h"
#include "ai.h"
#include "app_state.h"
#include "scraper.h"
#include "fitcheck.h"
#include "response.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── JSON / JOB HELPERS ───────────────────────────────────────────────────────

json jobRecordToJson(const JobRecord& job) {
    json jobJson;
    jobJson["job_id"]              = job.job_id;
    jobJson["title"]               = job.title;
    jobJson["company_name"]        = job.company_name;
    jobJson["place"]               = job.place;
    jobJson["zipcode"]             = job.zipcode;
    jobJson["canton_code"]         = job.canton_code;
    jobJson["employment_grade"]    = job.employment_grade;
    jobJson["application_url"]     = job.application_url;
    jobJson["user_status"]         = job.user_status;
    jobJson["rating"]              = job.rating;
    jobJson["notes"]               = job.notes;
    jobJson["availability_status"] = job.availability_status;
    jobJson["detail_url"]          = job.detail_url;
    jobJson["pub_date"]            = job.pub_date;
    jobJson["end_date"]            = job.end_date;
    jobJson["template_text"]       = job.template_text;

    jobJson["fit_score"]           = job.fit_score;
    jobJson["fit_label"]           = job.fit_label;
    jobJson["fit_summary"]         = job.fit_summary;
    jobJson["fit_reasoning"]       = job.fit_reasoning;
    jobJson["fit_checked_at"]      = job.fit_checked_at;
    jobJson["fit_profile_hash"]    = job.fit_profile_hash;
    jobJson["source"]              = job.source.empty() ? "jobs_ch" : job.source;

    return jobJson;
}

// ── MAIN ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    curl_global_init(CURL_GLOBAL_ALL);

    AppState appState;

    fs::path root;
    try {
        root = fs::canonical(argv[0]).parent_path();
    } catch (...) {
        root = fs::current_path();
    }
    if (root.filename().string().rfind("cmake-build-", 0) == 0) { // CLion output dir, step up
        root = root.parent_path();
    }
    appState.base_dir = root.string();
    appState.profile_path = appState.base_dir + "/config/user_profile.md";

    appState.config_path = appState.base_dir + "/config/config_v2.json";
    appState.system_prompt_path = appState.base_dir + "/config/system_prompt.txt";
    appState.onboarding_prompt_path = appState.base_dir + "/config/onboarding_prompt.txt";
    appState.import_prompt_path = appState.base_dir + "/config/import_prompt.txt";

    try {
        std::ifstream f(appState.base_dir + "/config/api_keys.json");
        json keys = json::parse(f);
        appState.api_key = keys.value("api_key", "");
        std::cout << "[INFO] API keys loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load API keys: " << e.what() << std::endl;
    }

    std::error_code ec;
    fs::create_directories(appState.base_dir + "/data", ec);  // sqlite creates the file, not the dir
    if (sqlite3_open((appState.base_dir + "/data/jobs_v2.db").c_str(), &appState.db) != SQLITE_OK) {
        std::cerr << "[Error] Cannot open database v2: " << sqlite3_errmsg(appState.db) << std::endl;
        return 1;
    }
    std::cout << "[INFO] Database v2 opened" << std::endl;
    db_init(appState.db);
    db_v2_init(appState.db);

    try {
        appState.config_v2 = loadConfigV2(appState.config_path);
        std::cout << "[INFO] Config v2 loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load config_v2.json: " << e.what() << std::endl;
    }

    {
        std::ifstream f(appState.system_prompt_path);
        if (!f.is_open()) {
            std::cerr << "[ERROR] Cannot open " << appState.system_prompt_path << std::endl;
            return 1;
        }
        appState.system_prompt_template.assign((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
        if (appState.system_prompt_template.find("{{profile}}") == std::string::npos ||
            appState.system_prompt_template.find("{{jobText}}") == std::string::npos) {
            std::cerr << "[ERROR] " << appState.system_prompt_path << " missing {{profile}} or {{jobText}} placeholders" << std::endl;
            return 1;
        }
        std::cout << "[INFO] System prompt loaded" << std::endl;
    }

    {
        struct { std::string* dest; const std::string* path; const char* label; } prompts[] = {
            { &appState.onboarding_prompt, &appState.onboarding_prompt_path, "Onboarding prompt" },
            { &appState.import_prompt,     &appState.import_prompt_path,     "Import prompt" },
        };
        for (const auto& p : prompts) {
            std::ifstream f(*p.path);
            if (!f.is_open()) {
                std::cerr << "[ERROR] Cannot open " << *p.path << std::endl;
                return 1;
            }
            p.dest->assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::cout << "[INFO] " << p.label << " loaded" << std::endl;
        }
    }

    // ── SERVER ───────────────────────────────────────────────────────────────

    httplib::Server server;

    server.set_mount_point("/", (appState.base_dir + "/frontend").c_str());
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/index.html");
    });

    server.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, {{"version", APP_VERSION}});
    });

    server.Get("/api/jobs", [&appState](const httplib::Request&, httplib::Response& res) {
        std::vector<JobRecord> jobs;
        {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            jobs = get_all_jobs(appState.db);
        }
        json result = json::array();
        for (const auto& job : jobs)
            result.push_back(jobRecordToJson(job));
        sendJson(res, result);
    });

    server.Post("/api/jobs/update", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string job_id = body["job_id"];

            std::lock_guard<std::mutex> lock(appState.db_mutex);
            if (body.contains("user_status")) {
                std::string status = body["user_status"];
                if (status != "unseen" && status != "interested" && status != "applied" && status != "skipped" && status != "deleted")
                    throw std::runtime_error("Invalid user_status: " + status);
                update_job_field(appState.db, job_id, "user_status", status);
            }
            if (body.contains("rating")) {
                int rating = body["rating"].get<int>();
                if (rating < 0 || rating > 5)
                    throw std::runtime_error("Rating must be 0-5, got: " + std::to_string(rating));
                update_job_field(appState.db, job_id, "rating", std::to_string(rating));
            }
            if (body.contains("notes")) {
                std::string notes = body["notes"];
                if (notes.size() > 10000)
                    throw std::runtime_error("Notes too long (max 10000 chars)");
                update_job_field(appState.db, job_id, "notes", notes);
            }
            if (body.contains("application_url")) {
                std::string url = body["application_url"];
                if (!url.empty() && url.rfind("http", 0) != 0)
                    throw std::runtime_error("Invalid URL");
                if (url.size() > 2048)
                    throw std::runtime_error("URL too long");
                update_job_field(appState.db, job_id, "application_url", url);
            }

            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "bad request"}, {"detail", e.what()}}, 400);
        }
    });

    server.Delete("/api/jobs/bulk", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            int deleted = 0;

            if (body.contains("fit_label")) {
                std::string fit_label = body["fit_label"];
                if (fit_label.empty())
                    throw std::runtime_error("Missing fit_label value");
                std::lock_guard<std::mutex> lock(appState.db_mutex);
                deleted = bulk_soft_delete_by_fit_label(appState.db, fit_label);
            } else {
                std::string status = body.value("status", "");
                int older_than_days = body.value("older_than_days", 0);

                if (status.empty())
                    throw std::runtime_error("Missing 'status' or 'fit_label' field");

                std::lock_guard<std::mutex> lock(appState.db_mutex);
                deleted = bulk_soft_delete_by_status(appState.db, status, older_than_days);
            }

            sendJson(res, {{"ok", true}, {"deleted", deleted}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "bad request"}, {"detail", e.what()}}, 400);
        }
    });

    server.Delete("/api/jobs/:id", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            delete_job(appState.db, req.path_params.at("id"));
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "database error"}, {"detail", e.what()}}, 500);
        }
    });

    server.Post("/api/jobs/:id/soft-delete", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            update_job_field(appState.db, req.path_params.at("id"), "user_status", "deleted");
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "database error"}, {"detail", e.what()}}, 500);
        }
    });

    server.Post("/api/jobs/restore-all", [&appState](const httplib::Request&, httplib::Response& res) {
        try {
            int restored;
            {
                std::lock_guard<std::mutex> lock(appState.db_mutex);
                restored = restore_all_deleted(appState.db);
            }
            sendJson(res, {{"ok", true}, {"restored", restored}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "database error"}, {"detail", e.what()}}, 500);
        }
    });

    server.Post("/api/scrape/jobs", [&appState](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scrape operation" << std::endl;
        int inserted = 0;

        std::vector<std::string> queries;
        int rows;
        bool jobsch_enabled;
        ConfigV2 linkedInConfig;
        {
            std::shared_lock<std::shared_mutex> lock(appState.config_v2_mutex);
            jobsch_enabled = appState.config_v2.scrape_enabled;
            queries        = appState.config_v2.scrape_queries;
            rows           = appState.config_v2.scrape_rows;
            linkedInConfig = appState.config_v2;
        }

        if (jobsch_enabled) for (const auto& q : queries) {
            rateLimitSleep();
            std::string url = "https://job-search-api.jobs.ch/search/semantic?query="
                + urlEncode(q) + "&rows=" + std::to_string(rows) + "&page=1";
            try {
                json searchData = json::parse(httpGet(url));
                auto documents  = searchData["documents"];
                std::cout << "[INFO] Query: " << q << " - " << documents.size() << " results" << std::endl;

                for (auto& doc : documents) {
                    std::lock_guard<std::mutex> lock(appState.db_mutex);
                    insert_or_update_job(appState.db, jobFromJson(doc));
                    inserted++;
                }
                {
                    std::lock_guard<std::mutex> lock(appState.db_mutex);
                    delete_expired_jobs(appState.db);
                }

            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to process search results for query '" << q
                          << "': " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ERROR] Unknown error processing query: " << q << std::endl;
            }
        }

        if (linkedInConfig.linkedin_enabled) {
            std::cout << "[LI] Starting LinkedIn scrape" << std::endl;
            try {
                auto linkedInJobs = scrapeLinkedIn(linkedInConfig);
                for (auto& job : linkedInJobs) {
                    std::lock_guard<std::mutex> lock(appState.db_mutex);
                    insert_or_update_job(appState.db, job);
                    inserted++;
                }
                std::cout << "[LI] Inserted " << linkedInJobs.size() << " LinkedIn jobs" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[LI] Scrape error: " << e.what() << std::endl;
            }
        }

        std::cout << "[INFO] Scrape completed: " << inserted << " jobs processed" << std::endl;
        sendJson(res, {{"ok", true}, {"count", inserted}});
    });

    server.Post("/api/scrape/details", [&appState](const httplib::Request&, httplib::Response& res) {
        std::vector<Job> jobs;
        {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            jobs = get_jobs_needing_details(appState.db);
        }
        bool expected = false;
        if (!appState.detail_progress.running.compare_exchange_strong(expected, true)) {
            sendJson(res, {{"ok", false}, {"error", "detail fetch already running"}}, 409);
            return;
        }

        int total = static_cast<int>(jobs.size());
        std::cout << "[INFO] Launching background detail fetch for " << total << " jobs" << std::endl;

        appState.detail_progress.done   = 0;
        appState.detail_progress.failed = 0;
        appState.detail_progress.total  = total;

        std::thread(fetchJobDetails, std::move(jobs), appState.db, std::ref(appState.db_mutex), std::ref(appState.detail_progress)).detach();

        sendJson(res, {{"ok", true}, {"status", "background"}, {"count", total}});
    });

    server.Get("/api/scrape/details/progress", [&appState](const httplib::Request&, httplib::Response& res) {
        sendJson(res, {
            {"running", appState.detail_progress.running.load()},
            {"done",    appState.detail_progress.done.load()},
            {"total",   appState.detail_progress.total.load()},
            {"failed",  appState.detail_progress.failed.load()}
        });
    });

    server.Get("/api/config", [&appState](const httplib::Request&, httplib::Response& res) {
        try {
            std::ifstream f(appState.config_path);
            if (!f.is_open()) throw std::runtime_error("Could not open config_v2.json");
            std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            res.set_content(body, "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/config", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            json incoming = json::parse(req.body);
            validateConfigV2(incoming);

            std::ofstream f(appState.config_path);
            if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
            f << incoming.dump(2);
            f.close();

            {
                std::unique_lock<std::shared_mutex> lock(appState.config_v2_mutex);
                appState.config_v2 = loadConfigV2(appState.config_path);
            }
            std::cout << "[INFO] Config reloaded" << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "config error"}, {"detail", e.what()}}, 400);
        }
    });

    server.Get("/api/config/ai", [&appState](const httplib::Request&, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> cfglock(appState.config_v2_mutex);
        std::lock_guard<std::mutex> keylock(appState.api_key_mutex);
        json result = {
            {"provider", appState.config_v2.provider},
            {"endpoint", appState.config_v2.ai_endpoint},
            {"model",    appState.config_v2.model},
            {"key_set",  !appState.api_key.empty()}
        };
        sendJson(res, result);
    });

    server.Post("/api/config/ai", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);

            std::string provider = body.value("provider", "");
            std::string endpoint = body.value("endpoint", "");
            std::string model    = body.value("model", "");
            std::string apiKey   = body.value("api_key", "");

            if (provider.empty()) throw std::runtime_error("provider required");
            if (endpoint.empty()) throw std::runtime_error("endpoint required");
            if (model.empty())    throw std::runtime_error("model required");

            // Always persist for ollama_local so we can clear a previously saved key.
            if (provider == "ollama_local" || !apiKey.empty()) {
                std::ofstream keyFile(appState.base_dir + "/config/api_keys.json");
                if (!keyFile.is_open()) throw std::runtime_error("Could not write api_keys.json");
                keyFile << json{{"api_key", apiKey}}.dump(2);
            }

            json configJson;
            {
                std::ifstream f(appState.config_path);
                if (!f.is_open()) throw std::runtime_error("Could not read config_v2.json");
                configJson = json::parse(f);
            }
            configJson["fitcheck"]["provider"] = provider;
            configJson["fitcheck"]["endpoint"] = endpoint;
            configJson["fitcheck"]["model"]    = model;
            {
                std::ofstream f(appState.config_path);
                if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
                f << configJson.dump(2);
            }
            {
                std::unique_lock<std::shared_mutex> cfglock(appState.config_v2_mutex);
                appState.config_v2 = loadConfigV2(appState.config_path);
            }
            if (provider == "ollama_local" || !apiKey.empty()) {
                std::lock_guard<std::mutex> keylock(appState.api_key_mutex);
                appState.api_key = apiKey;
            }

            std::cout << "[INFO] AI config updated: provider=" << provider << " model=" << model << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendError(res, 400, e.what());
        }
    });

    // ── V2 API ENDPOINTS ───────────────────────────────────────────────────────

    server.Post("/api/onboarding/complete", [&appState](const httplib::Request& req, httplib::Response& res) {
        generateProfile(appState, req, res);
    });

    server.Get("/api/profile", [&appState](const httplib::Request&, httplib::Response& res) {
        std::ifstream file(appState.profile_path);

        if (!file.is_open()) {
            sendError(res, 404, "No profile found");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();

        res.set_content(content, "text/markdown");
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    });

    server.Post("/api/profile/save", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string content = body.value("content", "");

            if (content.size() > 128 * 1024)
                throw std::runtime_error("Profile too large (max 128 KB)");

            std::ofstream file(appState.profile_path);
            if (!file.is_open())
                throw std::runtime_error("Failed to open profile file");

            file << content;
            file.close();

            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendError(res, 400, e.what());
        }
    });

    server.Post("/api/fitcheck", [&appState](const httplib::Request&, httplib::Response& res) {
        runBatchFitcheck(appState, res);
    });

    server.Get("/api/fitcheck/progress", [&appState](const httplib::Request&, httplib::Response& res) {
        sendJson(res, {
            {"running", appState.fitcheck_progress.running.load()},
            {"done",    appState.fitcheck_progress.done.load()},
            {"total",   appState.fitcheck_progress.total.load()},
            {"failed",  appState.fitcheck_progress.failed.load()}
        });
    });

    server.Post("/api/jobs/:id/fitcheck", [&appState](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Fitcheck triggered for job: " << job_id << std::endl;

        std::string profileContent = loadProfileMarkdown(appState.profile_path);
        if (profileContent.empty()) {
            sendError(res, 400, "No profile found. Complete onboarding first.");
            return;
        }

        std::optional<std::string> template_text;
        {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            template_text = get_job_template_text(appState.db, job_id);
        }

        if (!template_text) {
            sendError(res, 404, "Job not found");
            return;
        }

        std::string cleaned = cleanTemplateText(*template_text);
        if (cleaned.empty()) {
            sendError(res, 400, "Job has no description text");
            return;
        }

        auto ai_opt = requireAi(appState, res);
        if (!ai_opt) return;
        const AiSnapshot& ai = *ai_opt;
        std::string key = readApiKey(appState.api_key, appState.api_key_mutex);

        try {
            auto result = checkAndSave(appState, job_id, cleaned, profileContent, ai, key, "md_profile");
            sendJson(res, {
                {"fit_score",     result.score},
                {"fit_label",     result.label},
                {"fit_summary",   result.summary},
                {"fit_reasoning", result.reasoning}
            });
            std::cout << "[INFO] Fitcheck completed for job: " << job_id << std::endl;

        } catch (const FatalAiError& e) {
            sendJson(res, {{"error", std::string(e.what())}, {"error_code", e.code()}}, 500);
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Fit-check failed: ") + e.what());
        }
    });

    // ── IMPORT JOB FROM TEXT ──────────────────────────────────────────────────

    server.Post("/api/jobs/import-text", [&appState]
    (const httplib::Request& req, httplib::Response& res) {
        std::cout << "[INFO] POST /api/jobs/import-text — request received (" << req.body.size() << " bytes)" << std::endl;

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Import: invalid JSON body" << std::endl;
            sendError(res, 400, "Invalid JSON body");
            return;
        }

        std::string text = body.value("text", "");
        if (text.size() < 50) {
            std::cerr << "[ERROR] Import: text too short (" << text.size() << " chars)" << std::endl;
            sendError(res, 400, "Text too short — paste a full job posting");
            return;
        }

        auto ai_opt = requireAi(appState, res);
        if (!ai_opt) return;
        const AiSnapshot& ai = *ai_opt;
        std::string key = readApiKey(appState.api_key, appState.api_key_mutex);

        importJobFromText(appState, res, ai, key, text);
    });

    // ── ADMIN CONSOLE ENDPOINTS ────────────────────────────────────────────────

    server.Delete("/api/admin/jobs/bulk", [&appState](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string fit_label = body.value("fit_label", "");
            if (fit_label.empty())
                throw std::runtime_error("Missing 'fit_label' field");
            int deleted;
            {
                std::lock_guard<std::mutex> lock(appState.db_mutex);
                deleted = bulk_hard_delete_by_fit_label(appState.db, fit_label);
            }
            std::cout << "[ADMIN] Hard-deleted " << deleted << " jobs with fit_label=" << fit_label << std::endl;
            sendJson(res, {{"ok", true}, {"deleted", deleted}});
        } catch (const std::exception& e) {
            sendError(res, 400, e.what());
        }
    });

    server.Delete("/api/admin/jobs/:id", [&appState](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] DELETE /api/admin/jobs/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            delete_job(appState.db, job_id);
            std::cout << "[ADMIN] Deleted job " << job_id << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Delete job failed: " << e.what() << std::endl;
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/admin/fitcheck/clear/:id", [&appState](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            clear_fit_data(appState.db, job_id);
            std::cout << "[ADMIN] Cleared fit data for job " << job_id << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear fit data failed: " << e.what() << std::endl;
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/admin/fitcheck/clear", [&appState](const httplib::Request&, httplib::Response& res) {
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear (all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            clear_all_fit_data(appState.db);
            std::cout << "[ADMIN] Cleared all fit data" << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear all fit data failed: " << e.what() << std::endl;
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/admin/fitcheck/recheck/:id", [&appState](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Admin recheck triggered for job: " << job_id << std::endl;

        std::string profile = loadProfileMarkdown(appState.profile_path);
        if (profile.empty()) {
            sendError(res, 400, "No profile found");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            clear_fit_data(appState.db, job_id);
        }

        std::optional<std::string> templateText;
        {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            templateText = get_job_template_text(appState.db, job_id);
        }

        if (!templateText) {
            sendError(res, 404, "Job not found");
            return;
        }

        auto ai_opt = requireAi(appState, res);
        if (!ai_opt) return;
        const AiSnapshot& ai = *ai_opt;
        std::string key = readApiKey(appState.api_key, appState.api_key_mutex);

        std::string cleaned = cleanTemplateText(*templateText);
        if (cleaned.empty()) {
            sendError(res, 400, "Job has no description text");
            return;
        }

        try {
            auto result = checkAndSave(appState, job_id, cleaned, profile, ai, key, "admin_recheck");
            sendJson(res, {{"ok", true}, {"fit_score", result.score}, {"fit_label", result.label}});
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Recheck failed: ") + e.what());
        }
    });

    server.Post("/api/admin/fitcheck/recheck",[&appState](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Admin batch recheck triggered (clear all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(appState.db_mutex);
            clear_all_fit_data(appState.db);
            sendJson(res, {{"ok", true}, {"message", "All fit data cleared. Trigger /api/fitcheck to recheck."}});
        } catch (const std::exception& e) {
            sendError(res, 500, e.what());
        }
    });

    // ── END V2 API ─────────────────────────────────────────────────────────────

#ifdef _WIN32
    ShellExecuteA(nullptr, "open", "http://localhost:8080", nullptr, nullptr, SW_SHOWNORMAL);
#else
    system("xdg-open http://localhost:8080 2>/dev/null &");
#endif

    for (int attempt = 1; attempt <= 5; ++attempt) {
        std::cout << "[INFO] Server running on http://localhost:8080" << std::endl;
        if (server.listen("0.0.0.0", 8080)) break;
        std::cerr << "[WARNING] listen() failed (attempt " << attempt << "/5), retrying in 2s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    sqlite3_close(appState.db);

    curl_global_cleanup();

    return 0;
}
