#include "routes.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"
#include "json.hpp"
#include "app_state.h"
#include "ai.h"
#include "config.h"
#include "db.h"
#include "fitcheck.h"
#include "response.h"
#include "scheduler.h"
#include "scraper.h"

using json = nlohmann::json;

static const int LAST_REACTION_CHAR_LIMIT = 500;
static const int PLACE_CHAR_LIMIT = 200;
static const std::vector<std::string> cApplicationStatuses = {
    "waiting", "first_interview", "next_round", "assessment", "offer", "declined", "withdrawn", "ghosted"
};

static json jobRecordToJson(const JobRecord& job) {
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

    jobJson["application_status"]  = job.application_status;
    jobJson["applied_at"]          = job.applied_at;
    jobJson["last_reaction"]       = job.last_reaction;
    jobJson["last_reaction_at"]    = job.last_reaction_at;

    return jobJson;
}

static json progressToJson(const ProgressTracker& progress) {
    return {
        {"running", progress.running.load()},
        {"done",    progress.done.load()},
        {"total",   progress.total.load()},
        {"failed",  progress.failed.load()}
    };
}

void registerRoutes(httplib::Server& server, AppState& state, Scheduler& scheduler) {
    server.set_mount_point("/", (state.base_dir + "/frontend").c_str());
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/index.html");
    });

    server.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, {{"version", APP_VERSION}});
    });

    // ── JOBS ─────────────────────────────────────────────────────────────────

    server.Get("/api/jobs", [&state](const httplib::Request&, httplib::Response& res) {
        std::vector<JobRecord> jobs;
        {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            jobs = get_all_jobs(state.db);
        }
        json result = json::array();
        for (const auto& job : jobs)
            result.push_back(jobRecordToJson(job));
        sendJson(res, result);
    });

    server.Post("/api/jobs/update", [&state](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string job_id = body["job_id"];

            std::lock_guard<std::mutex> lock(state.db_mutex);
            if (body.contains("user_status")) {
                std::string status = body["user_status"];
                if (status != "unseen" && status != "interested" && status != "applied" && status != "skipped" && status != "deleted")
                    throw std::runtime_error("Invalid user_status: " + status);
                update_job_field(state.db, job_id, "user_status", status);
            }
            if (body.contains("rating")) {
                int rating = body["rating"].get<int>();
                if (rating < 0 || rating > 5)
                    throw std::runtime_error("Rating must be 0-5, got: " + std::to_string(rating));
                update_job_field(state.db, job_id, "rating", std::to_string(rating));
            }
            if (body.contains("notes")) {
                std::string notes = body["notes"];
                if (notes.size() > 10000)
                    throw std::runtime_error("Notes too long (max 10000 chars)");
                update_job_field(state.db, job_id, "notes", notes);
            }
            if (body.contains("application_url")) {
                std::string url = body["application_url"];
                if (!url.empty() && url.rfind("http", 0) != 0)
                    throw std::runtime_error("Invalid URL");
                if (url.size() > 2048)
                    throw std::runtime_error("URL too long");
                update_job_field(state.db, job_id, "application_url", url);
            }
            if (body.contains("fit_label")) {
                std::string label = body["fit_label"];
                update_job_field(state.db, job_id, "fit_label", label);
            }
            if (body.contains("fit_score")) {
                int score = body["fit_score"].get<int>();
                if (score < 0 || score > 100)
                    throw std::runtime_error("fit_score must be 0-100");
                update_job_field(state.db, job_id, "fit_score", std::to_string(score));
            }
            if (body.contains("application_status")){
                std::string status = body["application_status"];
                if (!std::ranges::contains(cApplicationStatuses, status) && !status.empty()) {
                    throw std::runtime_error("Application status string not accepted");
                }
                update_job_field(state.db, job_id, "application_status", status);
            }
            if (body.contains("applied_at")){
                std::string applied_at = body["applied_at"];
                update_job_field(state.db, job_id, "applied_at", applied_at); // single source of truth is frontend, no checks needed here
            }
            if (body.contains("last_reaction_at")){
                std::string last_reaction_at = body["last_reaction_at"];
                update_job_field(state.db, job_id, "last_reaction_at", last_reaction_at);
            }
            if (body.contains("place")){
                std::string place = body["place"];
                if (place.length() > PLACE_CHAR_LIMIT) {
                    throw std::runtime_error("place string must not exceed " + std::to_string(PLACE_CHAR_LIMIT) + " chars");
                }
                update_job_field(state.db, job_id, "place", place);
            }
            if (body.contains("last_reaction")){
                std::string last_reaction = body["last_reaction"];
                if (last_reaction.length() > LAST_REACTION_CHAR_LIMIT) {
                    throw std::runtime_error("last reaction string must not exceed " + std::to_string(LAST_REACTION_CHAR_LIMIT) + " chars");
                }
                update_job_field(state.db, job_id, "last_reaction", last_reaction);
            }

            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "bad request"}, {"detail", e.what()}}, 400);
        }
    });

    server.Delete("/api/jobs/bulk", [&state](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            int deleted = 0;

            if (body.contains("fit_label")) {
                std::string fit_label = body["fit_label"];
                if (fit_label.empty())
                    throw std::runtime_error("Missing fit_label value");
                std::lock_guard<std::mutex> lock(state.db_mutex);
                deleted = bulk_soft_delete_by_fit_label(state.db, fit_label);
            } else {
                std::string status = body.value("status", "");
                int older_than_days = body.value("older_than_days", 0);

                if (status.empty())
                    throw std::runtime_error("Missing 'status' or 'fit_label' field");

                std::lock_guard<std::mutex> lock(state.db_mutex);
                deleted = bulk_soft_delete_by_status(state.db, status, older_than_days);
            }

            sendJson(res, {{"ok", true}, {"deleted", deleted}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "bad request"}, {"detail", e.what()}}, 400);
        }
    });

    server.Delete("/api/jobs/:id", [&state](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            delete_job(state.db, req.path_params.at("id"));
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "database error"}, {"detail", e.what()}}, 500);
        }
    });

    server.Post("/api/jobs/:id/soft-delete", [&state](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            update_job_field(state.db, req.path_params.at("id"), "user_status", "deleted");
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "database error"}, {"detail", e.what()}}, 500);
        }
    });

    server.Post("/api/jobs/restore-all", [&state](const httplib::Request&, httplib::Response& res) {
        try {
            int restored;
            {
                std::lock_guard<std::mutex> lock(state.db_mutex);
                restored = restore_all_deleted(state.db);
            }
            sendJson(res, {{"ok", true}, {"restored", restored}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", "database error"}, {"detail", e.what()}}, 500);
        }
    });

    server.Post("/api/jobs/import-text", [&state](const httplib::Request& req, httplib::Response& res) {
        std::cout << "[INFO] POST /api/jobs/import-text — request received (" << req.body.size() << " bytes)" << std::endl;

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
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

        auto ai_opt = requireAi(state, res);
        if (!ai_opt) return;
        std::string key = readApiKey(state.api_key, state.api_key_mutex);

        importJobFromText(state, res, *ai_opt, key, text);
    });

    // ── SCRAPE ───────────────────────────────────────────────────────────────

    server.Post("/api/scrape/jobs", [&state](const httplib::Request&, httplib::Response& res) {
        int inserted = scrapeAllSources(state);
        sendJson(res, {{"ok", true}, {"count", inserted}});
    });

    server.Post("/api/scrape/details", [&state](const httplib::Request&, httplib::Response& res) {

        auto jobs = tryStartDetailFetch(state);
        if (!jobs){
            sendJson(res, {{"ok", false}, {"error", "detail fetch already running"}}, 409);
            return;
        }
        int total = static_cast<int>(jobs->size());
        std::cout << "[INFO] Launching background detail fetch for " << total << " jobs" << std::endl;

        std::thread(fetchJobDetails, std::move(*jobs), state.db, std::ref(state.db_mutex), std::ref(state.detail_progress)).detach();

        sendJson(res, {{"ok", true}, {"status", "background"}, {"count", total}});
    });

    server.Get("/api/scrape/details/progress", [&state](const httplib::Request&, httplib::Response& res) {
        sendJson(res, progressToJson(state.detail_progress));
    });

    // ── CONFIG ───────────────────────────────────────────────────────────────

    server.Get("/api/config", [&state](const httplib::Request&, httplib::Response& res) {
        try {
            res.set_content(readFileOrThrow(state.config_path), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/config", [&state, &scheduler](const httplib::Request& req, httplib::Response& res) {

        bool automode;

        try {
            json incoming = json::parse(req.body);
            validateConfigV2(incoming);

            std::ofstream f(state.config_path);
            if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
            f << incoming.dump(2);
            f.close();

            {
                std::unique_lock<std::shared_mutex> lock(state.config_v2_mutex);
                state.config_v2 = loadConfigV2(state.config_path);
                if (state.config_v2.automode_enabled) automode = true; else automode = false;

            }
            std::cout << "[INFO] Config reloaded" << std::endl;
            if (automode) scheduler.start(); else scheduler.stop();
            sendJson(res, {{"ok", true}});

        } catch (const std::exception& e) {
            sendJson(res, {{"error", "config error"}, {"detail", e.what()}}, 400);
        }
    });

    server.Get("/api/config/ai", [&state](const httplib::Request&, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> cfglock(state.config_v2_mutex);
        std::lock_guard<std::mutex> keylock(state.api_key_mutex);
        sendJson(res, {
            {"provider", state.config_v2.provider},
            {"endpoint", state.config_v2.ai_endpoint},
            {"model",    state.config_v2.model},
            {"key_set",  !state.api_key.empty()}
        });
    });

    server.Post("/api/config/ai", [&state](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);

            std::string provider = body.value("provider", "");
            std::string endpoint = body.value("endpoint", "");
            std::string model    = body.value("model", "");
            std::string apiKey   = body.value("api_key", "");

            if (provider.empty()) throw std::runtime_error("provider required");
            if (endpoint.empty()) throw std::runtime_error("endpoint required");
            if (model.empty())    throw std::runtime_error("model required");

            saveAiConfig(state, provider, endpoint, model, apiKey);
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendError(res, 400, e.what());
        }
    });

    server.Post("/api/config/ai/test", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string provider = body.value("provider", "");
            std::string endpoint = body.value("endpoint", "");
            std::string model    = body.value("model", "");
            std::string apiKey   = body.value("api_key", "");

            if (provider.empty()) throw std::runtime_error("provider required");
            if (endpoint.empty()) throw std::runtime_error("endpoint required");
            if (model.empty())    throw std::runtime_error("model required");

            json request = buildAiRequest(provider, model, "Reply with the single word: ok",
                                           5, 0.0, 1.0, 0);
            httpPostAI(endpoint, apiKey, request.dump(), 60L);
            sendJson(res, {{"ok", true}});
        } catch (const FatalAiError& e) {
            sendJson(res, {{"ok", false}, {"error", e.code()}, {"detail", e.what()}});
        } catch (const std::exception& e) {
            sendError(res, 400, e.what());
        }
    });

    // ── PROFILE / ONBOARDING ─────────────────────────────────────────────────

    server.Post("/api/onboarding/complete", [&state](const httplib::Request& req, httplib::Response& res) {
        generateProfile(state, req, res);
    });

    server.Get("/api/profile", [&state](const httplib::Request&, httplib::Response& res) {
        std::string content = loadProfileMarkdown(state.profile_path);
        if (content.empty()) {
            sendError(res, 404, "No profile found");
            return;
        }
        res.set_content(content, "text/markdown");
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    });

    server.Post("/api/profile/save", [&state](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string content = body.value("content", "");

            if (content.size() > 128 * 1024)
                throw std::runtime_error("Profile too large (max 128 KB)");

            std::ofstream file(state.profile_path);
            if (!file.is_open())
                throw std::runtime_error("Failed to open profile file");

            file << content;
            file.close();

            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            sendError(res, 400, e.what());
        }
    });

    // ── FITCHECK ─────────────────────────────────────────────────────────────

    server.Post("/api/fitcheck", [&state](const httplib::Request&, httplib::Response& res) {
        runBatchFitcheck(state, res);
    });

    server.Get("/api/fitcheck/progress", [&state](const httplib::Request&, httplib::Response& res) {
        sendJson(res, progressToJson(state.fitcheck_progress));
    });

    server.Post("/api/jobs/:id/fitcheck", [&state](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Fitcheck triggered for job: " << job_id << std::endl;

        try {
            auto result = fitcheckSingleJob(state, res, job_id, false, "md_profile");
            if (!result) return;
            sendJson(res, {
                {"fit_score",     result->score},
                {"fit_label",     result->label},
                {"fit_summary",   result->summary},
                {"fit_reasoning", result->reasoning}
            });
            std::cout << "[INFO] Fitcheck completed for job: " << job_id << std::endl;

        } catch (const FatalAiError& e) {
            sendJson(res, {{"error", std::string(e.what())}, {"error_code", e.code()}}, 500);
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Fit-check failed: ") + e.what());
        }
    });

    // ── ADMIN ────────────────────────────────────────────────────────────────

    server.Delete("/api/admin/jobs/bulk", [&state](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string fit_label = body.value("fit_label", "");
            if (fit_label.empty())
                throw std::runtime_error("Missing 'fit_label' field");
            int deleted;
            {
                std::lock_guard<std::mutex> lock(state.db_mutex);
                deleted = bulk_hard_delete_by_fit_label(state.db, fit_label);
            }
            std::cout << "[ADMIN] Hard-deleted " << deleted << " jobs with fit_label=" << fit_label << std::endl;
            sendJson(res, {{"ok", true}, {"deleted", deleted}});
        } catch (const std::exception& e) {
            sendError(res, 400, e.what());
        }
    });

    server.Delete("/api/admin/jobs/:id", [&state](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] DELETE /api/admin/jobs/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            delete_job(state.db, job_id);
            std::cout << "[ADMIN] Deleted job " << job_id << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Delete job failed: " << e.what() << std::endl;
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/admin/fitcheck/clear/:id", [&state](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            clear_fit_data(state.db, job_id);
            std::cout << "[ADMIN] Cleared fit data for job " << job_id << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear fit data failed: " << e.what() << std::endl;
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/admin/fitcheck/clear", [&state](const httplib::Request&, httplib::Response& res) {
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear (all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            clear_all_fit_data(state.db);
            std::cout << "[ADMIN] Cleared all fit data" << std::endl;
            sendJson(res, {{"ok", true}});
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear all fit data failed: " << e.what() << std::endl;
            sendError(res, 500, e.what());
        }
    });

    server.Post("/api/admin/fitcheck/recheck/:id", [&state](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Admin recheck triggered for job: " << job_id << std::endl;

        try {
            auto result = fitcheckSingleJob(state, res, job_id, true, "admin_recheck");
            if (!result) return;
            sendJson(res, {{"ok", true}, {"fit_score", result->score}, {"fit_label", result->label}});
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Recheck failed: ") + e.what());
        }
    });

    server.Post("/api/admin/fitcheck/recheck", [&state](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Admin batch recheck triggered (clear all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(state.db_mutex);
            clear_all_fit_data(state.db);
            sendJson(res, {{"ok", true}, {"message", "All fit data cleared. Trigger /api/fitcheck to recheck."}});
        } catch (const std::exception& e) {
            sendError(res, 500, e.what());
        }
    });
}
