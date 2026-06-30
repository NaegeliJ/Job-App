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
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <regex>
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

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string base_dir;
static std::string s_config_path;
static std::string s_system_prompt_path;

static const std::string& configPath() { return s_config_path; }
static const std::string& systemPromptPath() { return s_system_prompt_path; }
static std::string profilePath() { return base_dir + "/config/user_profile.md"; }

std::string httpGet(const std::string& url, long* out_status = nullptr) {
    return httpRequest(url, "GET", {
        "Accept: application/json",
        "Origin: https://www.jobs.ch",
        "Referer: https://www.jobs.ch/",
        "X-Node-Request: false",
        "X-Source: jobs_ch_desktop"
    }, "", 120L, out_status);
}

static std::string httpGetLinkedIn(const std::string& url, long* out_status = nullptr) {
    return httpRequest(url, "GET", {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language: en-US,en;q=0.9",
        "Referer: https://www.linkedin.com/jobs/search/"
    }, "", 30L, out_status);
}

static std::string httpGetLinkedInSearch(const std::string& url, long* out_status = nullptr) {
    return httpRequest(url, "GET", {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
        "Accept-Language: en-US,en;q=0.9",
        "Referer: https://www.google.com/",
        "Upgrade-Insecure-Requests: 1",
        "DNT: 1"
    }, "", 30L, out_status);
}


static std::string parseLinkedInPubDate(const std::string& html) {
    std::time_t now = std::time(nullptr);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::regex re(R"((\d+)\s+(day|days|week|weeks|month|months|hour|hours|minute|minutes)\s+ago)");
    std::smatch m;
    if (std::regex_search(html, m, re)) {
        int n = std::stoi(m[1].str());
        std::string unit = m[2].str();
        if (unit == "day" || unit == "days")         tm.tm_mday -= n;
        else if (unit == "week" || unit == "weeks")  tm.tm_mday -= 7 * n;
        else if (unit == "month" || unit == "months") tm.tm_mon -= n;
        // hours/minutes → today, no change
        std::mktime(&tm);
        char buf[11];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return buf;
    }
    return "";
}

static int parseLinkedInEmploymentGrade(const std::string& html) {
    if (html.find("Part-time") != std::string::npos) return 50;
    return 100;
}



static std::vector<Job> scrapeLinkedIn(const ConfigV2& cfg) {
    std::vector<Job> result;
    if (!cfg.linkedin_enabled || cfg.linkedin_queries.empty()) return result;

    for (const auto& q : cfg.linkedin_queries) {
        std::string url =
            "https://www.linkedin.com/jobs/search/"
            "?keywords=" + urlEncode(q)
            + "&location=" + urlEncode(cfg.linkedin_location)
            + "&f_TPR=" + urlEncode(cfg.linkedin_time_range)
            + "&position=1&pageNum=0";

        rateLimitSleep(1500, 3000);
        long status = 0;
        std::string html = httpGetLinkedInSearch(url, &status);

        if (status == 429) {
            std::cerr << "[LI] 429 on search for '" << q << "', sleeping 30s" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(30));
            html = httpGetLinkedInSearch(url, &status);
        }
        if (status != 200 || html.empty()) {
            std::cerr << "[LI] Search failed (HTTP " << status << ") for '" << q << "'" << std::endl;
            continue;
        }

        // Use named delimiter LI to avoid )" collision with regex patterns
        auto ids       = findAllCaptures(html, R"LI(data-entity-urn="urn:li:jobPosting:(\d+)")LI");
        auto titles    = findAllCaptures(html, R"(class="base-search-card__title"[^>]*>([\s\S]*?)</h3>)");
        auto companies = findAllCaptures(html, R"(class="base-search-card__subtitle"[^>]*>[\s\S]*?<a[^>]*>([\s\S]*?)</a>)");
        auto locations = findAllCaptures(html, R"(class="job-search-card__location"[^>]*>([\s\S]*?)</span>)");

        int count = 0;
        for (size_t i = 0; i < ids.size() && count < cfg.linkedin_max_results; i++, count++) {
            Job job;
            job.job_id           = "li_" + ids[i];
            job.source           = "linkedin";
            job.title            = i < titles.size()    ? cleanHtmlField(titles[i])    : "";
            job.company_name     = i < companies.size() ? cleanHtmlField(companies[i]) : "";
            job.place            = i < locations.size() ? cleanHtmlField(locations[i]) : "";
            job.detail_url       = "https://www.linkedin.com/jobs/view/" + ids[i];
            job.application_url  = job.detail_url;
            job.canton_code      = "N/A";
            job.zipcode          = "";
            job.employment_grade = 100;
            result.push_back(std::move(job));
        }
        std::cout << "[LI] Query '" << q << "': " << count << " jobs" << std::endl;
    }
    return result;
}

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

Job jobFromJson(const json& data) {
    auto str = [](const json& j, const std::string& key, const std::string& def = "") -> std::string {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return def;
        return it->get<std::string>();
    };

    Job job;
    job.job_id           = str(data, "id");
    job.title            = str(data, "title");
    job.company_name     = (data.contains("company") && !data["company"].is_null())
                           ? str(data["company"], "name") : "";
    job.place            = str(data, "place");
    job.zipcode          = str(data, "zipcode");
    job.canton_code      = (data.contains("locations") && !data["locations"].is_null() && !data["locations"].empty())
                           ? str(data["locations"][0], "cantonCode", "N/A") : "N/A";
    job.employment_grade = data.value("employment_grade", 100);
    job.application_url  = str(data, "application_url");
    job.detail_url       = (data.contains("_links") && !data["_links"].is_null() && data["_links"].contains("detail_de") && !data["_links"]["detail_de"].is_null())
                           ? str(data["_links"]["detail_de"], "href") : "";
    job.pub_date         = str(data, "publication_date");
    job.end_date         = str(data, "publication_end_date");
    job.template_text    = str(data, "template_text");
    return job;
}

static std::string generateManualJobId(const std::string& text) {
    size_t hash = std::hash<std::string>{}(text.substr(0, 500));
    std::stringstream ss;
    ss << "m" << std::hex << hash;
    return ss.str();
}

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

// ── MAIN ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    curl_global_init(CURL_GLOBAL_ALL);

    fs::path root;
    try {
        root = fs::canonical(argv[0]).parent_path();
    } catch (...) {
        root = fs::current_path();
    }
    if (root.filename().string().rfind("cmake-build-", 0) == 0) { // CLion output dir, step up
        root = root.parent_path();
    }
    base_dir = root.string();

    s_config_path = base_dir + "/config/config_v2.json";
    s_system_prompt_path = base_dir + "/config/system_prompt.txt";

    std::string api_key;
    try {
        std::ifstream f(base_dir + "/config/api_keys.json");
        json keys = json::parse(f);
        api_key = keys.value("api_key", "");
        std::cout << "[INFO] API keys loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load API keys: " << e.what() << std::endl;
    }

    sqlite3* db;
    std::error_code ec;
    fs::create_directories(base_dir + "/data", ec);  // sqlite creates the file, not the dir
    if (sqlite3_open((base_dir + "/data/jobs_v2.db").c_str(), &db) != SQLITE_OK) {
        std::cerr << "[Error] Cannot open database v2: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "[INFO] Database v2 opened" << std::endl;
    db_init(db);
    db_v2_init(db);
    std::mutex db_mutex;

    std::mutex api_key_mutex;

    struct ProgressTracker {
        std::atomic<bool> running{false};
        std::atomic<int>  done{0};
        std::atomic<int>  total{0};
        std::atomic<int>  failed{0};
    };
    ProgressTracker fitcheck_progress;
    ProgressTracker detail_progress;

    ConfigV2 config_v2;
    std::shared_mutex config_v2_mutex;
    try {
        config_v2 = loadConfigV2(configPath());
        std::cout << "[INFO] Config v2 loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load config_v2.json: " << e.what() << std::endl;
    }

    std::string system_prompt_template;
    {
        std::ifstream f(systemPromptPath());
        if (!f.is_open()) {
            std::cerr << "[ERROR] Cannot open " << systemPromptPath() << std::endl;
            return 1;
        }
        system_prompt_template.assign((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
        if (system_prompt_template.find("{{profile}}") == std::string::npos ||
            system_prompt_template.find("{{jobText}}") == std::string::npos) {
            std::cerr << "[ERROR] " << systemPromptPath() << " missing {{profile}} or {{jobText}} placeholders" << std::endl;
            return 1;
        }
        std::cout << "[INFO] System prompt loaded" << std::endl;
    }

    // ── SERVER ───────────────────────────────────────────────────────────────

    httplib::Server server;

    server.set_mount_point("/", (base_dir + "/frontend").c_str());
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/index.html");
    });

    server.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"version", APP_VERSION}}.dump(), "application/json");
    });

    server.Get("/api/jobs", [&db](const httplib::Request&, httplib::Response& res) {
        json result = json::array();
        for (const auto& job : get_all_jobs(db))
            result.push_back(jobRecordToJson(job));
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/jobs/update", [&db, &db_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string job_id = body["job_id"];

            std::lock_guard<std::mutex> lock(db_mutex);
            if (body.contains("user_status")) {
                std::string status = body["user_status"];
                if (status != "unseen" && status != "interested" && status != "applied" && status != "skipped" && status != "deleted")
                    throw std::runtime_error("Invalid user_status: " + status);
                update_job_field(db, job_id, "user_status", status);
            }
            if (body.contains("rating")) {
                int rating = body["rating"].get<int>();
                if (rating < 0 || rating > 5)
                    throw std::runtime_error("Rating must be 0-5, got: " + std::to_string(rating));
                update_job_field(db, job_id, "rating", std::to_string(rating));
            }
            if (body.contains("notes")) {
                std::string notes = body["notes"];
                if (notes.size() > 10000)
                    throw std::runtime_error("Notes too long (max 10000 chars)");
                update_job_field(db, job_id, "notes", notes);
            }
            if (body.contains("application_url")) {
                std::string url = body["application_url"];
                if (!url.empty() && url.rfind("http", 0) != 0)
                    throw std::runtime_error("Invalid URL");
                if (url.size() > 2048)
                    throw std::runtime_error("URL too long");
                update_job_field(db, job_id, "application_url", url);
            }

            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "bad request"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Delete("/api/jobs/bulk", [&db, &db_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            int deleted = 0;

            if (body.contains("fit_label")) {
                std::string fit_label = body["fit_label"];
                if (fit_label.empty())
                    throw std::runtime_error("Missing fit_label value");
                std::lock_guard<std::mutex> lock(db_mutex);
                deleted = bulk_soft_delete_by_fit_label(db, fit_label);
            } else {
                std::string status = body.value("status", "");
                int older_than_days = body.value("older_than_days", 0);

                if (status.empty())
                    throw std::runtime_error("Missing 'status' or 'fit_label' field");

                std::lock_guard<std::mutex> lock(db_mutex);
                deleted = bulk_soft_delete_by_status(db, status, older_than_days);
            }

            res.set_content(json{{"ok", true}, {"deleted", deleted}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "bad request"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Delete("/api/jobs/:id", [&db, &db_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            delete_job(db, req.path_params.at("id"));
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", "database error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/jobs/:id/soft-delete", [&db, &db_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            update_job_field(db, req.path_params.at("id"), "user_status", "deleted");
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", "database error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/jobs/restore-all", [&db, &db_mutex](const httplib::Request&, httplib::Response& res) {
        try {
            int restored;
            {
                std::lock_guard<std::mutex> lock(db_mutex);
                restored = restore_all_deleted(db);
            }
            res.set_content(json{{"ok", true}, {"restored", restored}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", "database error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/scrape/jobs", [&db, &config_v2, &config_v2_mutex, &db_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scrape operation" << std::endl;
        int inserted = 0;

        std::vector<std::string> queries;
        int rows;
        bool jobsch_enabled;
        ConfigV2 linkedInConfig;
        {
            std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
            jobsch_enabled = config_v2.scrape_enabled;
            queries        = config_v2.scrape_queries;
            rows           = config_v2.scrape_rows;
            linkedInConfig         = config_v2;
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
                    std::lock_guard<std::mutex> lock(db_mutex);
                    insert_or_update_job(db, jobFromJson(doc));
                    inserted++;
                }
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    delete_expired_jobs(db);
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
                    std::lock_guard<std::mutex> lock(db_mutex);
                    insert_or_update_job(db, job);
                    inserted++;
                }
                std::cout << "[LI] Inserted " << linkedInJobs.size() << " LinkedIn jobs" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[LI] Scrape error: " << e.what() << std::endl;
            }
        }

        std::cout << "[INFO] Scrape completed: " << inserted << " jobs processed" << std::endl;
        res.set_content(json{{"ok", true}, {"count", inserted}}.dump(), "application/json");
    });

    server.Post("/api/scrape/details", [&db, &db_mutex, &detail_progress](const httplib::Request&, httplib::Response& res) {
        std::vector<Job> jobs;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            jobs = get_jobs_needing_details(db);
        }
        bool expected = false;
        if (!detail_progress.running.compare_exchange_strong(expected, true)) {
            res.status = 409;
            res.set_content(json{{"ok", false}, {"error", "detail fetch already running"}}.dump(), "application/json");
            return;
        }

        int total = static_cast<int>(jobs.size());
        std::cout << "[INFO] Launching background detail fetch for " << total << " jobs" << std::endl;

        detail_progress.done   = 0;
        detail_progress.failed = 0;
        detail_progress.total  = total;

        std::thread([jobs = std::move(jobs), &db, &db_mutex, &detail_progress]() {
            int updated = 0, failed = 0;
            bool linkedInBlocked = false;

            for (const auto& job : jobs) {
                const bool is_linkedin = (job.source == "linkedin");

                if (is_linkedin && linkedInBlocked) { detail_progress.done++; continue; }

                try {
                    Job updated_job;

                    if (!is_linkedin) {
                        rateLimitSleep();
                        long status = 0;
                        std::string body = httpGet(
                            "https://www.jobs.ch/api/v1/public/search/job/" + urlEncode(job.job_id), &status);

                        if (status == 404 || status == 410) {
                            std::cerr << "[DETAIL] " << job.job_id << " HTTP " << status
                                      << " — skipping" << std::endl;
                            {
                                std::lock_guard<std::mutex> lock(db_mutex);
                                delete_job(db, job.job_id);
                            }
                            failed++;
                            detail_progress.failed++;
                            detail_progress.done++;
                            continue;
                        }

                        if (status != 200) {
                            std::cerr << "[DETAIL] " << job.job_id << " HTTP " << status
                                      << " — skipping" << std::endl;
                            failed++;
                            detail_progress.failed++;
                            detail_progress.done++;
                            continue;
                        }
                        json detail = json::parse(body);
                        updated_job = jobFromJson(detail);
                        updated_job.job_id = job.job_id;
                        updated_job.source = "jobs_ch";

                    } else {
                        // "li_" is our internal namespace prefix, not part of the LinkedIn job ID.
                        std::string linkedInId = job.job_id.substr(3);
                        std::string url   = "https://www.linkedin.com/jobs-guest/jobs/api/jobPosting/" + linkedInId;

                        rateLimitSleep(1500, 3000);
                        long status = 0;
                        std::string html = httpGetLinkedIn(url, &status);

                        if (status == 429) {
                            std::cerr << "[LI] 429 on detail " << job.job_id << ", sleeping 30s and retrying" << std::endl;
                            std::this_thread::sleep_for(std::chrono::seconds(30));
                            html = httpGetLinkedIn(url, &status);
                        }
                        if (status == 404) {
                            std::cerr << "[LI] 404 on detail " << job.job_id << " — job expired, skipping" << std::endl;
                            {
                                std::lock_guard<std::mutex> lock(db_mutex);
                                delete_job(db, job.job_id);
                            }
                            failed++;
                            detail_progress.failed++;
                            detail_progress.done++;
                            continue;
                        }
                        if (status == 999 || status == 429 || (status != 200 && status != 0)) {
                            std::cerr << "[LI] HTTP " << status << " on detail " << job.job_id << " — stopping LinkedIn detail fetches" << std::endl;
                            linkedInBlocked = true;
                            failed++;
                            detail_progress.failed++;
                            detail_progress.done++;
                            continue;
                        }
                        if (html.empty()) {
                            std::cerr << "[LI] Empty response for detail " << job.job_id << std::endl;
                            failed++;
                            detail_progress.failed++;
                            detail_progress.done++;
                            continue;
                        }

                        updated_job = job;

                        {
                            std::string v = extractTagContent(html, "top-card-layout__title", "</h1>");
                            if (!v.empty()) updated_job.title = cleanHtmlField(v);
                        }
                        {
                            std::string v = extractTagContent(html, "topcard__org-name-link", "</a>");
                            if (!v.empty()) updated_job.company_name = cleanHtmlField(v);
                        }
                        {
                            std::string v = extractTagContent(html, "show-more-less-html__markup", "</div>");
                            if (v.empty())
                                v = extractTagContent(html, "description__text", "</div>");
                            if (!v.empty())
                                updated_job.template_text = v;
                        }
                        updated_job.pub_date = parseLinkedInPubDate(html);
                        updated_job.employment_grade = parseLinkedInEmploymentGrade(html);
                        updated_job.source = "linkedin";
                    }

                    {
                        std::lock_guard<std::mutex> lock(db_mutex);
                        update_job_details(db, updated_job);
                    }
                    updated++;
                    detail_progress.done++;
                    std::cout << "[DETAIL] " << job.job_id << " updated" << std::endl;

                } catch (const std::exception& e) {
                    std::cerr << "[DETAIL] Failed for " << job.job_id << ": " << e.what() << std::endl;
                    failed++;
                    detail_progress.failed++;
                    detail_progress.done++;
                }
            }
            detail_progress.running = false;
            std::cout << "[INFO] Background detail fetch done: " << updated << " updated, " << failed << " failed" << std::endl;
        }).detach();

        res.set_content(json{{"ok", true}, {"status", "background"}, {"count", total}}.dump(), "application/json");
    });

    server.Get("/api/scrape/details/progress", [&detail_progress](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{
            {"running", detail_progress.running.load()},
            {"done",    detail_progress.done.load()},
            {"total",   detail_progress.total.load()},
            {"failed",  detail_progress.failed.load()}
        }.dump(), "application/json");
    });

    server.Get("/api/config", [](const httplib::Request&, httplib::Response& res) {
        try {
            std::ifstream f(configPath());
            if (!f.is_open()) throw std::runtime_error("Could not open config_v2.json");
            std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            res.set_content(body, "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/config", [&config_v2, &config_v2_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json incoming = json::parse(req.body);
            validateConfigV2(incoming);

            std::ofstream f(configPath());
            if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
            f << incoming.dump(2);
            f.close();

            {
                std::unique_lock<std::shared_mutex> lock(config_v2_mutex);
                config_v2 = loadConfigV2(configPath());
            }
            std::cout << "[INFO] Config reloaded" << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "config error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Get("/api/config/ai", [&config_v2, &config_v2_mutex, &api_key, &api_key_mutex](const httplib::Request&, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> cfglock(config_v2_mutex);
        std::lock_guard<std::mutex> keylock(api_key_mutex);
        json result = {
            {"provider", config_v2.provider},
            {"endpoint", config_v2.ai_endpoint},
            {"model",    config_v2.model},
            {"key_set",  !api_key.empty()}
        };
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/config/ai", [&config_v2, &config_v2_mutex, &api_key, &api_key_mutex](const httplib::Request& req, httplib::Response& res) {
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
                std::ofstream keyFile(base_dir + "/config/api_keys.json");
                if (!keyFile.is_open()) throw std::runtime_error("Could not write api_keys.json");
                keyFile << json{{"api_key", apiKey}}.dump(2);
            }

            json configJson;
            {
                std::ifstream f(configPath());
                if (!f.is_open()) throw std::runtime_error("Could not read config_v2.json");
                configJson = json::parse(f);
            }
            configJson["fitcheck"]["provider"] = provider;
            configJson["fitcheck"]["endpoint"] = endpoint;
            configJson["fitcheck"]["model"]    = model;
            {
                std::ofstream f(configPath());
                if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
                f << configJson.dump(2);
            }
            {
                std::unique_lock<std::shared_mutex> cfglock(config_v2_mutex);
                config_v2 = loadConfigV2(configPath());
            }
            if (provider == "ollama_local" || !apiKey.empty()) {
                std::lock_guard<std::mutex> keylock(api_key_mutex);
                api_key = apiKey;
            }

            std::cout << "[INFO] AI config updated: provider=" << provider << " model=" << model << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ── V2 API ENDPOINTS ───────────────────────────────────────────────────────

    server.Post("/api/onboarding/complete", [&api_key, &api_key_mutex, &config_v2, &config_v2_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);

            if (!body.contains("answers") || !body["answers"].is_array() || body["answers"].size() != 9) {
                res.status = 400;
                res.set_content(json{{"error", "Expected 9 answers"}}.dump(), "application/json");
                return;
            }

            std::string key = readApiKey(api_key, api_key_mutex);
            auto ai_opt = getReadyAi(key, config_v2, config_v2_mutex);
            if (!ai_opt) {
                res.status = 500;
                res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
                return;
            }

            const AiSnapshot& ai = *ai_opt;

            const auto& answers = body["answers"];

            std::string questions[] = {
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
            for (int i = 0; i < 9; i++) {
                profileText += "Q" + std::to_string(i+1) + ": " + questions[i] + "\n";
                std::string answer = answers[i].is_string() ? answers[i].get<std::string>() : answers[i].dump();
                profileText += "A" + std::to_string(i+1) + ": " + answer + "\n\n";
            }

            std::string prompt = R"(Generate a comprehensive user profile in markdown format from the candidate answers below.

TEMPLATE STRUCTURE TO FOLLOW:
# User Profile

Generated: [TIMESTAMP]
Last Updated: [TIMESTAMP]
Version: [HASH]

---

## Q1: CV Drop
[Answer]

---

## Q2: Career Goal (3–5 Years)
[Answer]

---

## Q3: Intrinsic Motivation
[Answer]

---

## Q4: No-Gos
[Answer]

---

## Q5: Tech Skills: Build vs. Tolerate
[Answer]

---

## Q6: Company Type & Region
[Answer]

---

## Q7: Hard Constraints
[Answer]

---

## Q8: Work Style
[Answer]

---

## Q9: What Should the LLM Know That's Not in the CV?
[Answer]

---

## Synthesized Narrative
[Auto-generated from all answers above. Combine into cohesive paragraph for job assessment.]

[EXAMPLE NARRATIVE]
[Generated narrative]

---

*This profile is used by the AI to assess job fit. Edit any section above,
then trigger a profile refresh to update the narrative.*
)";

            prompt += profileText;

            json request = buildAiRequest(ai.provider, ai.model, prompt, ai.max_tokens, ai.temperature, ai.top_p, ai.top_k);

            if (ai.provider != "ollama_local") request["response_format"] = {{"type", "text"}};

            std::string response = httpPostAI(ai.endpoint, key, request.dump());
            std::string parsedResponse = parseStreamingResponse(response);

            if (parsedResponse.empty())
                throw std::runtime_error("Empty parsed response from AI (httpPostAI succeeded but parse failed)");

            std::string profileMarkdown = extractBlock(parsedResponse, "markdown");

            std::ofstream file(profilePath());
            if (!file.is_open())
                throw std::runtime_error("Failed to open profile file");
            file << profileMarkdown;

            res.set_content(json{{"ok", true}}.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string(e.what())}}.dump(), "application/json");
        }
    });

    server.Get("/api/profile", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file(profilePath());

        if (!file.is_open()) {
            res.status = 404;
            res.set_content(json{{"error", "No profile found"}}.dump(), "application/json");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();

        res.set_content(content, "text/markdown");
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    });

    server.Post("/api/profile/save", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string content = body.value("content", "");

            if (content.size() > 128 * 1024)
                throw std::runtime_error("Profile too large (max 128 KB)");

            std::ofstream file(profilePath());
            if (!file.is_open())
                throw std::runtime_error("Failed to open profile file");

            file << content;
            file.close();

            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/fitcheck", [&config_v2, &config_v2_mutex, &api_key, &api_key_mutex, &db_mutex, &db, &system_prompt_template, &fitcheck_progress](const httplib::Request&, httplib::Response& res) {
        std::string content = loadProfileMarkdown(profilePath());
        if (content.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found. Complete onboarding first."}}.dump(), "application/json");
            return;
        }

        std::string key = readApiKey(api_key, api_key_mutex);
        auto ai_opt = getReadyAi(key, config_v2, config_v2_mutex);
        if (!ai_opt) {
            res.status = 500;
            res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
            return;
        }
        const AiSnapshot& ai = *ai_opt;

        int fitcheck_limit;
        { std::shared_lock<std::shared_mutex> lock(config_v2_mutex); fitcheck_limit = config_v2.fitcheck_limit; }

        std::vector<JobRecord> jobs;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            jobs = get_jobs_needing_fitcheck_v2(db, fitcheck_limit);
        }

        std::cout << "[INFO] Starting fit-check for " << jobs.size() << " jobs" << std::endl;

        fitcheck_progress.done    = 0;
        fitcheck_progress.failed  = 0;
        fitcheck_progress.total   = static_cast<int>(jobs.size());
        fitcheck_progress.running = true;

        int checked = 0, failed = 0;
        try {
            for (auto& job : jobs) {
                std::string cleaned = cleanTemplateText(job.template_text);
                if (cleaned.empty()) {
                    std::cerr << "[WARN] Empty template for job: " << job.job_id << std::endl;
                    failed++;
                    fitcheck_progress.failed++;
                    fitcheck_progress.done++;
                    continue;
                }
                try {
                    auto result = runFitcheck(cleaned, content, system_prompt_template, ai, key);
                    {
                        std::lock_guard<std::mutex> lock(db_mutex);
                        save_fit_result_v2(db, job.job_id, result.score, result.label, result.summary, result.reasoning, "md_file_profile");
                    }
                    checked++;
                    fitcheck_progress.done++;
                    std::cout << "[INFO] Fit-checked [" << checked << "/" << jobs.size() << "]: " << job.job_id << std::endl;
                } catch (const FatalAiError& e) {
                    if (e.code() == "invalid_api_key" || e.code() == "no_credits")
                        throw;
                    std::cerr << "[WARN] Transient AI error for " << job.job_id << ": " << e.what() << std::endl;
                    failed++;
                    fitcheck_progress.failed++;
                    fitcheck_progress.done++;
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] Failed fit-check for " << job.job_id << ": " << e.what() << std::endl;
                    failed++;
                    fitcheck_progress.failed++;
                    fitcheck_progress.done++;
                }
            }
        } catch (const FatalAiError& e) {
            std::cerr << "[ERROR] Fatal AI error during fit-check: " << e.what() << std::endl;
            fitcheck_progress.running = false;
            res.status = 500;
            res.set_content(json{{"ok", false}, {"error_code", e.code()}, {"error", e.what()}}.dump(), "application/json");
            return;
        }

        fitcheck_progress.running = false;
        res.set_content(json{{"ok", true}, {"checked", checked}, {"failed", failed}}.dump(), "application/json");
    });

    server.Get("/api/fitcheck/progress", [&fitcheck_progress](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{
            {"running", fitcheck_progress.running.load()},
            {"done",    fitcheck_progress.done.load()},
            {"total",   fitcheck_progress.total.load()},
            {"failed",  fitcheck_progress.failed.load()}
        }.dump(), "application/json");
    });

    server.Post("/api/jobs/:id/fitcheck", [&config_v2, &config_v2_mutex, &api_key, &api_key_mutex, &db_mutex, &db, &system_prompt_template](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Fitcheck triggered for job: " << job_id << std::endl;

        std::string profileContent = loadProfileMarkdown(profilePath());
        if (profileContent.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found. Complete onboarding first."}}.dump(), "application/json");
            return;
        }

        std::optional<std::string> template_text;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            template_text = get_job_template_text(db, job_id);
        }

        if (!template_text) {
            res.status = 404;
            res.set_content(json{{"error", "Job not found"}}.dump(), "application/json");
            return;
        }

        std::string cleaned = cleanTemplateText(*template_text);
        if (cleaned.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "Job has no description text"}}.dump(), "application/json");
            return;
        }

        std::string key = readApiKey(api_key, api_key_mutex);
        auto ai_opt = getReadyAi(key, config_v2, config_v2_mutex);
        if (!ai_opt) {
            res.status = 500;
            res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
            return;
        }
        const AiSnapshot& ai = *ai_opt;

        try {
            auto result = runFitcheck(cleaned, profileContent, system_prompt_template, ai, key);
            {
                std::lock_guard<std::mutex> lock(db_mutex);
                save_fit_result_v2(db, job_id, result.score, result.label, result.summary, result.reasoning, "md_profile");
            }
            res.set_content(json{
                {"fit_score",     result.score},
                {"fit_label",     result.label},
                {"fit_summary",   result.summary},
                {"fit_reasoning", result.reasoning}
            }.dump(), "application/json");
            std::cout << "[INFO] Fitcheck completed for job: " << job_id << std::endl;

        } catch (const FatalAiError& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string(e.what())}, {"error_code", e.code()}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string("Fit-check failed: ") + e.what()}}.dump(), "application/json");
        }
    });

    // ── IMPORT JOB FROM TEXT ──────────────────────────────────────────────────

    server.Post("/api/jobs/import-text", [&api_key, &api_key_mutex, &db_mutex, &db, &config_v2, &config_v2_mutex, &system_prompt_template]
    (const httplib::Request& req, httplib::Response& res) {
        std::cout << "[INFO] POST /api/jobs/import-text — request received (" << req.body.size() << " bytes)" << std::endl;

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Import: invalid JSON body" << std::endl;
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON body"}}.dump(), "application/json");
            return;
        }

        std::string text = body.value("text", "");
        if (text.size() < 50) {
            std::cerr << "[ERROR] Import: text too short (" << text.size() << " chars)" << std::endl;
            res.status = 400;
            res.set_content(json{{"error", "Text too short — paste a full job posting"}}.dump(), "application/json");
            return;
        }

        std::string key = readApiKey(api_key, api_key_mutex);
        auto ai_opt = getReadyAi(key, config_v2, config_v2_mutex);
        if (!ai_opt) {
            res.status = 500;
            res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
            return;
        }
        const AiSnapshot& ai = *ai_opt;

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
                std::lock_guard<std::mutex> lock(db_mutex);
                insert_or_update_job(db, job);
            }

            std::cout << "[INFO] Import: job inserted — " << jobId << " — " << job.title << std::endl;

            std::string profileContent = loadProfileMarkdown(profilePath());
            if (!profileContent.empty()) {
                std::cout << "[INFO] Import: running fit-check for " << jobId << "..." << std::endl;
                std::string cleaned = cleanTemplateText(job.template_text);
                if (!cleaned.empty()) {
                    try {
                        auto result = runFitcheck(cleaned, profileContent, system_prompt_template, ai, key);
                        std::lock_guard<std::mutex> lock(db_mutex);
                        save_fit_result_v2(db, jobId, result.score, result.label, result.summary, result.reasoning, "md_file_profile");
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
    });

    // ── ADMIN CONSOLE ENDPOINTS ────────────────────────────────────────────────

    server.Delete("/api/admin/jobs/bulk", [&db, &db_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string fit_label = body.value("fit_label", "");
            if (fit_label.empty())
                throw std::runtime_error("Missing 'fit_label' field");
            int deleted;
            {
                std::lock_guard<std::mutex> lock(db_mutex);
                deleted = bulk_hard_delete_by_fit_label(db, fit_label);
            }
            std::cout << "[ADMIN] Hard-deleted " << deleted << " jobs with fit_label=" << fit_label << std::endl;
            res.set_content(json{{"ok", true}, {"deleted", deleted}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Delete("/api/admin/jobs/:id", [&db, &db_mutex](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] DELETE /api/admin/jobs/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            delete_job(db, job_id);
            std::cout << "[ADMIN] Deleted job " << job_id << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Delete job failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/clear/:id", [&db, &db_mutex](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            clear_fit_data(db, job_id);
            std::cout << "[ADMIN] Cleared fit data for job " << job_id << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear fit data failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/clear", [&db, &db_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear (all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            clear_all_fit_data(db);
            std::cout << "[ADMIN] Cleared all fit data" << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear all fit data failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/recheck/:id", [&api_key, &api_key_mutex, &db_mutex, &db, &config_v2, &config_v2_mutex,
        &system_prompt_template]
    (const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Admin recheck triggered for job: " << job_id << std::endl;

        std::string profile = loadProfileMarkdown(profilePath());
        if (profile.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found"}}.dump(), "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            clear_fit_data(db, job_id);
        }

        std::optional<std::string> templateText;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            templateText = get_job_template_text(db, job_id);
        }

        if (!templateText) {
            res.status = 404;
            res.set_content(json{{"error", "Job not found"}}.dump(), "application/json");
            return;
        }

        std::string key = readApiKey(api_key, api_key_mutex);
        auto ai_opt = getReadyAi(key, config_v2, config_v2_mutex);
        if (!ai_opt) {
            res.status = 500;
            res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
            return;
        }
        const AiSnapshot& ai = *ai_opt;

        std::string cleaned = cleanTemplateText(*templateText);
        if (cleaned.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "Job has no description text"}}.dump(), "application/json");
            return;
        }

        try {
            auto result = runFitcheck(cleaned, profile, system_prompt_template, ai, key);
            {
                std::lock_guard<std::mutex> lock(db_mutex);
                save_fit_result_v2(db, job_id, result.score, result.label, result.summary, result.reasoning, "admin_recheck");
            }
            res.set_content(json{{"ok", true}, {"fit_score", result.score}, {"fit_label", result.label}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string("Recheck failed: ") + e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/recheck",[&db, &db_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Admin batch recheck triggered (clear all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            clear_all_fit_data(db);
            res.set_content(json{{"ok", true}, {"message", "All fit data cleared. Trigger /api/fitcheck to recheck."}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
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
    sqlite3_close(db);

    curl_global_cleanup();

    return 0;
}
