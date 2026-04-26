#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <curl/curl.h>
#include "httplib.h"
#include "sqlite3.h"
#include "json.hpp"
#include "db.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string base_dir;
static std::string s_config_path;
static std::string s_system_prompt_path;

struct AiSnapshot {
    std::string provider, model, endpoint;
    int max_tokens, top_k;
    double temperature, top_p;
};
static const std::string& configPath() { return s_config_path; }
static const std::string& systemPromptPath() { return s_system_prompt_path; }

// ── HTTP HELPERS ─────────────────────────────────────────────────────────────

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

std::string urlEncode(const std::string& str) {
    std::string encoded;
    for (unsigned char c : str) {
        // RFC 3986: Unreserved characters: A-Z a-z 0-9 - _ . ~
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '-' || c == '_' || 
            c == '.' || c == '~') {
            encoded += c;
        } else {
            // Percent-encode all other characters
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

void rateLimitSleep() {
    thread_local std::mt19937 rng(std::random_device{}());
    thread_local std::uniform_int_distribution<int> dist(800, 1499);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
}

std::string httpRequest(const std::string& url, const std::string& method,
                        const std::vector<std::string>& headers = {},
                        const std::string& postData = "",
                        long timeoutSeconds = 120L,
                        long* out_status = nullptr) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        struct curl_slist* headerList = nullptr;
        for (const auto& header : headers) {
            headerList = curl_slist_append(headerList, header.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl error for " << url << ": " << curl_easy_strerror(res) << std::endl;
        } else {
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (out_status) *out_status = httpCode;
            if (httpCode >= 400)
                std::cerr << "[HTTP] " << url << " returned " << httpCode
                          << " — body: " << response.substr(0, 500) << std::endl;
        }

        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
    }
    return response;
}

std::string httpGet(const std::string& url) {
    return httpRequest(url, "GET", {
        "Accept: application/json",
        "Origin: https://www.jobs.ch",
        "Referer: https://www.jobs.ch/",
        "X-Node-Request: false",
        "X-Source: jobs_ch_desktop"
    });
}

std::string httpPost(const std::string& url, const std::string& apiKey, const std::string& body) {
    return httpRequest(url, "POST", {
        "Content-Type: application/json",
        "Authorization: Bearer " + apiKey
    }, body);
}

// AI inference calls need a longer timeout — model inference can take several minutes.
// Retries once on empty response (handles cold-start drops from cloud providers).
std::string httpPostAI(const std::string& url, const std::string& apiKey, const std::string& body) {
    const std::vector<std::string> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " + apiKey
    };
    auto hasTopLevelError = [](const std::string& raw) -> bool {
        try {
            json j = json::parse(raw);
            return j.contains("error");
        } catch (...) {
            return true; // Non-JSON (e.g. HTML 5xx) counts as error
        }
    };
    long http_status = 0;
    std::string response = httpRequest(url, "POST", headers, body, 600L, &http_status);
    std::cerr << "[DEBUG] httpPostAI: HTTP " << http_status << " body_len=" << response.size()
              << " url=" << url << std::endl;
    if (!response.empty())
        std::cerr << "[DEBUG] httpPostAI response (first 300): " << response.substr(0, 300) << std::endl;
    const bool is_server_failure = response.empty() || (http_status >= 500 && hasTopLevelError(response));
    if (is_server_failure) {
        std::cerr << "[WARN] httpPostAI: HTTP " << http_status << " empty/error, retrying in 5s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        response = httpRequest(url, "POST", headers, body, 600L, &http_status);
        std::cerr << "[DEBUG] httpPostAI retry: HTTP " << http_status << " body_len=" << response.size() << std::endl;
        if (!response.empty())
            std::cerr << "[DEBUG] httpPostAI retry response (first 300): " << response.substr(0, 300) << std::endl;
    }
    return response;
}

static bool isOllamaLocal(const std::string& provider) {
    return provider == "ollama_local";
}

static bool isOllamaProvider(const std::string& provider) {
    return provider == "ollama_local" || provider == "ollama_cloud";
}

static bool supportsJsonMode(const std::string& provider) {
    return provider == "openrouter" || provider == "mistral" || provider == "ollama_cloud";
}

json buildAiRequest(const std::string& provider, const std::string& model, const std::string& prompt,
                    int max_tokens, double temperature, double top_p, int top_k) {
    json req = {
        {"model",       model},
        {"messages",    json::array({{{"role", "user"}, {"content", prompt}}})},
        {"max_tokens",  max_tokens},
        {"temperature", temperature},
        {"top_p",       top_p},
        {"stream",      false}
    };
    if (isOllamaLocal(provider)) {
        req["format"] = "json";
    } else if (supportsJsonMode(provider)) {
        req["response_format"] = {{"type", "json_object"}};
    }
    if (isOllamaLocal(provider) && top_k > 0) req["top_k"] = top_k;
    return req;
}

// ── CONFIG ───────────────────────────────────────────────────────────────────

struct ConfigV2 {
    // Scraping
    std::vector<std::string> scrape_queries;
    int                      scrape_rows{};

    // Fit-check
    std::string              provider{"ollama_local"};
    int                      fitcheck_limit{};
    std::string              model{};
    std::string              ai_endpoint{};
    int                      max_tokens{};
    double                   temperature{};
    double                   top_p{};
    int                      top_k{};
};

void validateConfigV2(const json& c) {
    auto require = [&](const std::string& key) {
        if (!c.contains(key))
            throw std::runtime_error("Missing required config key: " + key);
    };
    require("scrape");
    require("fitcheck");
}

ConfigV2 loadConfigV2() {
    std::ifstream file(configPath());
    if (!file.is_open())
        throw std::runtime_error("Could not open config_v2.json");

    json c = json::parse(file);
    validateConfigV2(c);
    ConfigV2 cfg;

    cfg.scrape_queries    = c["scrape"]["queries"].get<std::vector<std::string>>();
    cfg.scrape_rows       = c["scrape"]["rows"].get<int>();

    cfg.provider          = c["fitcheck"].value("provider", "ollama_local");
    cfg.fitcheck_limit    = c["fitcheck"]["limit"].get<int>();
    cfg.model             = c["fitcheck"]["model"].get<std::string>();
    cfg.ai_endpoint       = c["fitcheck"]["endpoint"].get<std::string>();
    cfg.max_tokens        = c["fitcheck"].value("max_tokens", 4000);
    cfg.temperature       = c["fitcheck"].value("temperature", 1.0);
    cfg.top_p             = c["fitcheck"].value("top_p", 0.95);
    cfg.top_k             = c["fitcheck"].value("top_k", 64);

    return cfg;
}


// ── JSON / JOB HELPERS ───────────────────────────────────────────────────────

json job_record_to_json(const JobRecord& job) {
    json job_json;
    job_json["job_id"]              = job.job_id;
    job_json["title"]               = job.title;
    job_json["company_name"]        = job.company_name;
    job_json["place"]               = job.place;
    job_json["zipcode"]             = job.zipcode;
    job_json["canton_code"]         = job.canton_code;
    job_json["employment_grade"]    = job.employment_grade;
    job_json["application_url"]     = job.application_url;
    job_json["user_status"]         = job.user_status;
    job_json["rating"]              = job.rating;
    job_json["notes"]               = job.notes;
    job_json["availability_status"] = job.availability_status;
    job_json["detail_url"]          = job.detail_url;
    job_json["pub_date"]            = job.pub_date;
    job_json["end_date"]            = job.end_date;
    job_json["template_text"]       = job.template_text;

    job_json["fit_score"]           = job.fit_score;
    job_json["fit_label"]           = job.fit_label;
    job_json["fit_summary"]         = job.fit_summary;
    job_json["fit_reasoning"]       = job.fit_reasoning;
    job_json["fit_checked_at"]      = job.fit_checked_at;
    job_json["fit_profile_hash"]    = job.fit_profile_hash;

    return job_json;
}

Job job_from_json(const json& data) {
    Job job;
    job.job_id           = data.value("id", "");
    job.title            = data.value("title", "");
    job.company_name     = data.contains("company") ? data["company"].value("name", "") : "";
    job.place            = data.value("place", "");
    job.zipcode          = data.value("zipcode", "");
    job.canton_code      = (data.contains("locations") && !data["locations"].empty())
                           ? data["locations"][0].value("cantonCode", "N/A") : "N/A";
    job.employment_grade = data.value("employment_grade", 100);
    job.application_url  = data.value("application_url", "");
    job.detail_url       = (data.contains("_links") && data["_links"].contains("detail_de"))
                           ? data["_links"]["detail_de"].value("href", "") : "";
    job.pub_date         = data.value("publication_date", "");
    job.end_date         = data.value("publication_end_date", "");
    job.template_text    = data.value("template_text", "");
    return job;
}


// ── TEMPLATE TEXT CLEANER ────────────────────────────────────────────────────

std::string cleanTemplateText(const std::string& raw) {
    // Step 1: Handle JSON-encoded string (unwrap if needed)
    std::string html;
    try {
        json parsed = json::parse(raw);
        html = parsed.is_string() ? parsed.get<std::string>() : parsed.dump();
        // Strip extra quotes if present
        if (html.size() > 2 && html.front() == '"' && html.back() == '"') {
            html = html.substr(1, html.size() - 2);
        }
    } catch (...) {
        html = raw;
    }

    // Step 2: Strip HTML tags
    std::string text;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) text += c;
    }

    // Step 3: Decode HTML entities
    size_t pos = 0;
    while ((pos = text.find("&amp;", pos)) != std::string::npos) {
        text.replace(pos, 5, "&");
    }
    pos = 0;
    while ((pos = text.find("&lt;", pos)) != std::string::npos) {
        text.replace(pos, 4, "<");
    }
    pos = 0;
    while ((pos = text.find("&gt;", pos)) != std::string::npos) {
        text.replace(pos, 4, ">");
    }
    pos = 0;
    while ((pos = text.find("&quot;", pos)) != std::string::npos) {
        text.replace(pos, 6, "\"");
    }

    // Step 4: Collapse whitespace
    std::string collapsed;
    bool lastWasSpace = false;
    for (char c : text) {
        if (std::isspace(c)) {
            if (!lastWasSpace) {
                collapsed += ' ';
                lastWasSpace = true;
            }
        } else {
            collapsed += c;
            lastWasSpace = false;
        }
    }
    while (!collapsed.empty() && std::isspace(collapsed.back())) {
        collapsed.pop_back();
    }

    // Step 5: Truncate to 8000 chars
    if (collapsed.size() > 8000) {
        collapsed = collapsed.substr(0, 8000);
        while (!collapsed.empty() && (collapsed.back() & 0xC0) == 0x80) {
            collapsed.pop_back();
        }
    }

    return collapsed;
}

// ── MAIN ─────────────────────────────────────────────────────────────────────

int main() {
    // Initialize curl globalization
    curl_global_init(CURL_GLOBAL_ALL);

    // Resolve project root directory
    fs::path root = fs::current_path();
    std::string folder_name = root.filename().string();
    if (folder_name.rfind("cmake-build-", 0) == 0) {
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
        std::cout << "API keys loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Could not load API keys: " << e.what() << std::endl;
    }

    sqlite3* db;
    if (sqlite3_open((base_dir + "/data/jobs_v2.db").c_str(), &db) != SQLITE_OK) {
        std::cerr << "Cannot open database v2: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database v2 opened" << std::endl;
    db_init(db);
    db_v2_init(db);
    std::mutex db_write_mutex;

    std::mutex api_key_mutex;

    ConfigV2 config_v2;
    std::shared_mutex config_v2_mutex;
    try {
        config_v2 = loadConfigV2();
        std::cout << "Config v2 loaded" << std::endl;
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
        std::cout << "System prompt loaded" << std::endl;
    }

    // ── SERVER ───────────────────────────────────────────────────────────────

    httplib::Server server;

    // Serve static files (CSS, JS)
    server.set_mount_point("/", (base_dir + "/frontend").c_str());
    
    // Serve index.html for root path
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/index.html");
    });

    server.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"version", APP_VERSION}}.dump(), "application/json");
    });

    server.Get("/api/jobs", [&db](const httplib::Request&, httplib::Response& res) {
        json result = json::array();
        for (const auto& job : get_all_jobs(db))
            result.push_back(job_record_to_json(job));
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/jobs/update", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string job_id = body["job_id"];

            std::lock_guard<std::mutex> lock(db_write_mutex);
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

    server.Delete("/api/jobs/bulk", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            int deleted = 0;

            if (body.contains("fit_label")) {
                std::string fit_label = body["fit_label"];
                if (fit_label.empty())
                    throw std::runtime_error("Missing fit_label value");
                std::lock_guard<std::mutex> lock(db_write_mutex);
                deleted = bulk_soft_delete_by_fit_label(db, fit_label);
            } else {
                std::string status = body.value("status", "");
                int older_than_days = body.value("older_than_days", 0);

                if (status.empty())
                    throw std::runtime_error("Missing 'status' or 'fit_label' field");

                std::lock_guard<std::mutex> lock(db_write_mutex);
                deleted = bulk_soft_delete_by_status(db, status, older_than_days);
            }

            res.set_content(json{{"ok", true}, {"deleted", deleted}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "bad request"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Delete("/api/jobs/:id", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            delete_job(db, req.path_params.at("id"));
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", "database error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/jobs/:id/soft-delete", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            update_job_field(db, req.path_params.at("id"), "user_status", "deleted");
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", "database error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/jobs/restore-all", [&db, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        try {
            int restored;
            {
                std::lock_guard<std::mutex> lock(db_write_mutex);
                restored = restore_all_deleted(db);
            }
            res.set_content(json{{"ok", true}, {"restored", restored}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", "database error"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/scrape/jobs", [&db, &config_v2, &config_v2_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Starting job scrape operation" << std::endl;
        int inserted = 0;

        std::vector<std::string> queries;
        int rows;
        {
            std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
            queries = config_v2.scrape_queries;
            rows = config_v2.scrape_rows;
        }

        for (const auto& q : queries) {
            rateLimitSleep();
            std::string url = "https://job-search-api.jobs.ch/search/semantic?query="
                + urlEncode(q) + "&rows=" + std::to_string(rows) + "&page=1";
            try {
                json searchData = json::parse(httpGet(url));
                auto documents  = searchData["documents"];
                std::cout << "Query: " << q << " - " << documents.size() << " results" << std::endl;

                for (auto& doc : documents) {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                    insert_or_update_job(db, job_from_json(doc));
                    inserted++;
                }
                {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                    delete_expired_jobs(db);
                }

            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to process search results for query '" << q 
                          << "': " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ERROR] Unknown error processing query: " << q << std::endl;
            }
        }

        std::cout << "[INFO] Scrape completed: " << inserted << " jobs processed" << std::endl;
        res.set_content(json{{"ok", true}, {"count", inserted}}.dump(), "application/json");
    });

    server.Post("/api/scrape/details", [&db, &config_v2, &config_v2_mutex, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::vector<Job> jobs_needing_details = get_jobs_needing_details(db);
        std::cout << "[INFO] Fetching details for " << jobs_needing_details.size() << " jobs" << std::endl;

        int updated = 0, failed = 0;
         for (const auto& job : jobs_needing_details) {
            try {
                rateLimitSleep();
                json detail = json::parse(httpGet("https://www.jobs.ch/api/v1/public/search/job/" + urlEncode(job.job_id)));

                Job updated_job = job_from_json(detail);
                updated_job.job_id = job.job_id;

                {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                    update_job_details(db, updated_job);
                }
                updated++;
                std::cout << "[DEBUG] Fetched details for job: " << job.job_id << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "Failed to fetch details for job: " << job.job_id << " - " << e.what() << std::endl;
                failed++;
            }
        }

        std::cout << "[INFO] Details fetch completed: " << updated << " updated, " << failed << " failed" << std::endl;
        res.set_content(json{{"ok", true}, {"updated", updated}, {"failed", failed}}.dump(), "application/json");
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
                config_v2 = loadConfigV2();
            }
            std::cout << "Config reloaded" << std::endl;
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

            std::string new_provider = body.value("provider", "");
            std::string new_endpoint = body.value("endpoint", "");
            std::string new_model    = body.value("model", "");
            std::string new_key      = body.value("api_key", "");

            if (new_provider.empty()) throw std::runtime_error("provider required");
            if (new_endpoint.empty()) throw std::runtime_error("endpoint required");
            if (new_model.empty())    throw std::runtime_error("model required");

            // Write api_keys.json: always for ollama_local (key should be empty), only if non-empty for others
            if (new_provider == "ollama_local" || !new_key.empty()) {
                std::ofstream kf(base_dir + "/config/api_keys.json");
                if (!kf.is_open()) throw std::runtime_error("Could not write api_keys.json");
                kf << json{{"api_key", new_key}}.dump(2);
            }

            // Read, patch, write config_v2.json
            json cfg_json;
            {
                std::ifstream f(configPath());
                if (!f.is_open()) throw std::runtime_error("Could not read config_v2.json");
                cfg_json = json::parse(f);
            }
            cfg_json["fitcheck"]["provider"] = new_provider;
            cfg_json["fitcheck"]["endpoint"] = new_endpoint;
            cfg_json["fitcheck"]["model"]    = new_model;
            {
                std::ofstream f(configPath());
                if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
                f << cfg_json.dump(2);
            }

            // Hot-reload in memory
            {
                std::unique_lock<std::shared_mutex> cfglock(config_v2_mutex);
                config_v2 = loadConfigV2();
            }
            if (new_provider == "ollama_local" || !new_key.empty()) {
                std::lock_guard<std::mutex> keylock(api_key_mutex);
                api_key = new_key;
            }

            std::cout << "[INFO] AI config updated: provider=" << new_provider << " model=" << new_model << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ── V2 SHARED HELPERS ──────────────────────────────────────────────────────

    auto loadProfileMarkdown = []() -> std::string {
        std::string markdownPath = base_dir + "/config/user_profile.md";
        std::ifstream file(markdownPath);
        if (!file.is_open()) return "";
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        return content;
    };

    auto snapshotAiConfig = [&config_v2, &config_v2_mutex]() -> AiSnapshot {
        std::shared_lock<std::shared_mutex> lock(config_v2_mutex);
        return { config_v2.provider, config_v2.model, config_v2.ai_endpoint,
                 config_v2.max_tokens, config_v2.top_k,
                 config_v2.temperature, config_v2.top_p };
    };

    auto buildFitcheckPrompt = [&system_prompt_template](const std::string& profile, const std::string& jobText) -> std::string {
        std::string result = system_prompt_template;
        size_t pos;
        while ((pos = result.find("{{profile}}")) != std::string::npos)
            result.replace(pos, 11, profile);
        while ((pos = result.find("{{jobText}}")) != std::string::npos)
            result.replace(pos, 11, jobText);
        return result;
    };

    auto parseStreamingResponse = [](const std::string& raw) -> std::string {
        std::istringstream stream(raw);
        std::string line, accumulated;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            // Strip SSE prefix if present ("data: {...}")
            if (line.rfind("data: ", 0) == 0) line = line.substr(6);
            if (line == "[DONE]") break;
            try {
                json chunk = json::parse(line);
                // Ollama native NDJSON: {"message": {"content": "..."}}
                if (chunk.contains("message") && chunk["message"].contains("content"))
                    accumulated += chunk["message"]["content"].get<std::string>();
                // OpenAI-compatible SSE: {"choices": [{"delta": {"content": "..."}}]}
                else if (chunk.contains("choices") && chunk["choices"].is_array() && !chunk["choices"].empty()) {
                    const auto& delta = chunk["choices"][0];
                    if (delta.contains("delta") && delta["delta"].contains("content"))
                        accumulated += delta["delta"]["content"].get<std::string>();
                    else if (delta.contains("message") && delta["message"].contains("content"))
                        accumulated += delta["message"]["content"].get<std::string>();
                }
                if (chunk.contains("done") && chunk["done"].get<bool>()) break;
            } catch (...) {}
        }
        if (accumulated.empty() && !raw.empty()) {
            std::cerr << "[WARN] parseStreamingResponse: no content extracted. Raw (first 500 chars):\n"
                      << raw.substr(0, std::min(raw.size(), size_t(500))) << std::endl;
        }
        return accumulated;
    };

    auto extractBlock = [](const std::string& raw, const std::string& lang) -> std::string {
        std::string content = raw;
        std::string open = "```" + lang;
        size_t start = content.find(open);
        if (start != std::string::npos) {
            content = content.substr(start + open.size());
            size_t end = content.find("```");
            if (end != std::string::npos) content = content.substr(0, end);
        } else {
            start = content.find("```");
            if (start != std::string::npos) {
                content = content.substr(start + 3);
                size_t end = content.find("```");
                if (end != std::string::npos) content = content.substr(0, end);
            }
        }
        while (!content.empty() && std::isspace(content.front())) content = content.substr(1);
        while (!content.empty() && std::isspace(content.back())) content.pop_back();
        return content;
    };

    auto extractJsonFromResponse = [&](const std::string& raw) -> json {
        std::string content = extractBlock(raw, "json");
        try {
            return json::parse(content);
        } catch (...) {
            size_t objStart = content.find("{");
            size_t objEnd = content.rfind("}");
            if (objStart != std::string::npos && objEnd != std::string::npos && objEnd > objStart)
                return json::parse(content.substr(objStart, objEnd - objStart + 1));
            throw std::runtime_error("No valid JSON found in response");
        }
    };

    // ── V2 API ENDPOINTS ───────────────────────────────────────────────────────

    server.Post("/api/onboarding/complete", [&api_key, &snapshotAiConfig, &extractBlock, &parseStreamingResponse](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            
            if (!body.contains("answers") || !body["answers"].is_array() || body["answers"].size() != 9) {
                res.status = 400;
                res.set_content(json{{"error", "Expected 9 answers"}}.dump(), "application/json");
                return;
            }
            
            if (api_key.empty() && snapshotAiConfig().provider != "ollama_local") {
                res.status = 500;
                res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
                return;
            }

            const auto& answers = body["answers"];
            
            // Build prompt for LLM to generate markdown profile
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
            
            std::string fullProfile = "Candidate Onboarding Answers:\n\n";
            for (int i = 0; i < 9; i++) {
                fullProfile += "Q" + std::to_string(i+1) + ": " + questions[i] + "\n";
                std::string answerVal = answers[i].is_string() ? answers[i].get<std::string>() : answers[i].dump();
                fullProfile += "A" + std::to_string(i+1) + ": " + answerVal + "\n\n";
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

            prompt += fullProfile;

            auto ai = snapshotAiConfig();

            json request = {
                {"model",       ai.model},
                {"messages",    json::array({{{"role", "user"}, {"content", prompt}}})},
                {"max_tokens",  ai.max_tokens},
                {"temperature", ai.temperature},
                {"top_p",       ai.top_p},
                {"stream",      false}
            };
            if (!isOllamaLocal(ai.provider)) request["response_format"] = {{"type", "text"}};
            if (isOllamaLocal(ai.provider) && ai.top_k > 0) request["top_k"] = ai.top_k;

            std::string response = httpPostAI(ai.endpoint, api_key, request.dump());
            std::string accumulatedResponse = parseStreamingResponse(response);

            if (accumulatedResponse.empty()) {
                throw std::runtime_error("Empty response from API");
            }
            
            std::string markdownContent = extractBlock(accumulatedResponse, "markdown");
            
            // Save to file
            std::string markdownPath = base_dir + "/config/user_profile.md";
            std::ofstream outfile(markdownPath);
            if (!outfile.is_open()) {
                throw std::runtime_error("Failed to open file: " + markdownPath);
            }
            outfile << markdownContent;
            outfile.close();
            
            res.set_content(json{{"ok", true}}.dump(), "application/json");
            
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string(e.what())}}.dump(), "application/json");
        }
    });

    server.Get("/api/profile", [](const httplib::Request&, httplib::Response& res) {
        std::string markdownPath = base_dir + "/config/user_profile.md";
        std::ifstream file(markdownPath);
        
        if (!file.is_open()) {
            res.status = 404;
            res.set_content(json{{"error", "No profile found"}}.dump(), "application/json");
            return;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();
        
        res.set_content(content, "text/markdown");
        res.set_header("Content-Type", "text/markdown");
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    });

    server.Post("/api/profile/save", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string content = body.value("content", "");

            if (content.size() > 128 * 1024)
                throw std::runtime_error("Profile too large (max 128 KB)");

            std::string markdownPath = base_dir + "/config/user_profile.md";
            std::ofstream file(markdownPath);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open file: " + markdownPath);
            }
            
            file << content;
            file.close();
            
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/fitcheck", [&config_v2, &config_v2_mutex, &api_key, &db_write_mutex, &db, &snapshotAiConfig, &buildFitcheckPrompt, &parseStreamingResponse, &extractJsonFromResponse](const httplib::Request&, httplib::Response& res) {
        std::string markdownPath = base_dir + "/config/user_profile.md";
        std::ifstream file(markdownPath);
        
        if (!file.is_open()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found. Complete onboarding first."}}.dump(), "application/json");
            return;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        
        auto ai = snapshotAiConfig();
        int fitcheck_limit;
        { std::shared_lock<std::shared_mutex> lock(config_v2_mutex); fitcheck_limit = config_v2.fitcheck_limit; }

        if (api_key.empty() && ai.provider != "ollama_local") {
            res.status = 500;
            res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
            return;
        }

        std::vector<JobRecord> jobs;
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            jobs = get_jobs_needing_fitcheck_v2(db, fitcheck_limit);
        }

        std::cout << "[INFO] Starting fit-check for " << jobs.size() << " jobs" << std::endl;

        int checked = 0, failed = 0;
        for (auto& job : jobs) {
            try {
                std::string cleaned = cleanTemplateText(job.template_text);
                if (cleaned.empty()) {
                    std::cerr << "[WARN] Empty template for job: " << job.job_id << std::endl;
                    failed++;
                    continue;
                }

                std::string prompt = buildFitcheckPrompt(content, cleaned);

                json request = buildAiRequest(ai.provider, ai.model, prompt, ai.max_tokens,
                                              ai.temperature, ai.top_p, ai.top_k);

                std::string response = httpPostAI(ai.endpoint, api_key, request.dump());

                std::string accumulated = parseStreamingResponse(response);
                if (accumulated.empty()) throw std::runtime_error("Empty response from API");
                json fit_data = extractJsonFromResponse(accumulated);
                {
                    std::lock_guard<std::mutex> lock(db_write_mutex);
                     save_fit_result_v2(db, job.job_id,
                                       fit_data.value("fit_score", 0),
                                       fit_data.value("fit_label", "Unknown"),
                                       fit_data.value("fit_summary", ""),
                                       fit_data.value("fit_reasoning", ""),
                                       "md_file_profile");
                }
                checked++;
                std::cout << "[INFO] Fit-checked: " << job.job_id << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed fit-check for " << job.job_id << ": " << e.what() << std::endl;
                failed++;
            }
        }

        res.set_content(json{{"ok", true}, {"checked", checked}, {"failed", failed}}.dump(), "application/json");
    });

    // POST /api/jobs/:id/fitcheck — Re-check fit for a single job
    server.Post("/api/jobs/:id/fitcheck", [&config_v2, &config_v2_mutex, &api_key, &db_write_mutex, &db, &snapshotAiConfig, &buildFitcheckPrompt, &parseStreamingResponse, &extractJsonFromResponse](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Fitcheck triggered for job: " << job_id << std::endl;
        
        // Read profile from markdown file
        std::string markdownPath = base_dir + "/config/user_profile.md";
        std::ifstream file(markdownPath);
        if (!file.is_open()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found. Complete onboarding first."}}.dump(), "application/json");
            return;
        }
        
        std::string profileContent((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        file.close();
        
        std::optional<std::string> template_text;
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            template_text = get_job_template_text(db, job_id);
        }

        if (!template_text) {
            res.status = 404;
            res.set_content(json{{"error", "Job not found"}}.dump(), "application/json");
            return;
        }

        try {
            std::string cleaned = cleanTemplateText(*template_text);
            if (cleaned.empty()) {
                res.status = 400;
                res.set_content(json{{"error", "Job has no description text"}}.dump(), "application/json");
                return;
            }

            // Build prompt
            std::string prompt = buildFitcheckPrompt(profileContent, cleaned);

            auto ai = snapshotAiConfig();

            if (api_key.empty() && ai.provider != "ollama_local") {
                res.status = 500;
                res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
                return;
            }

            json request = buildAiRequest(ai.provider, ai.model, prompt, ai.max_tokens,
                                          ai.temperature, ai.top_p, ai.top_k);

            std::string api_response = httpPostAI(ai.endpoint,
                                                api_key, request.dump());
            
            std::cout << "[DEBUG] API response length: " << api_response.length() << std::endl;
            
            std::string accumulatedResponse = parseStreamingResponse(api_response);
            
            std::cout << "[DEBUG] Accumulated response length: " << accumulatedResponse.length() << std::endl;
            if (accumulatedResponse.empty()) {
                res.status = 500;
                res.set_content(json{{"error", "Empty response from AI"}}.dump(), "application/json");
                return;
            }
            
            json fit_data;
            try {
                fit_data = extractJsonFromResponse(accumulatedResponse);
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(json{{"error", "Failed to parse AI response", "raw_response", accumulatedResponse}}.dump(), "application/json");
                return;
            }
            
            {
                std::lock_guard<std::mutex> lock(db_write_mutex);
                save_fit_result_v2(db, job_id,
                                   fit_data.value("fit_score", 0),
                                   fit_data.value("fit_label", "Unknown"),
                                   fit_data.value("fit_summary", ""),
                                   fit_data.value("fit_reasoning", ""),
                                   "md_profile");
            }
            
            res.set_content(fit_data.dump(), "application/json");
            
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string("Fit-check failed: ") + e.what()}}.dump(), "application/json");
        }
    });

    // ── IMPORT JOB FROM TEXT ──────────────────────────────────────────────────

    auto generateManualJobId = [](const std::string& text) -> std::string {
        size_t hash = std::hash<std::string>{}(text.substr(0, 500));
        std::stringstream ss;
        ss << "m" << std::hex << hash;
        return ss.str();
    };

    server.Post("/api/jobs/import-text", [&api_key, &db_write_mutex, &db,
        &snapshotAiConfig, &loadProfileMarkdown, &generateManualJobId, &buildFitcheckPrompt, &parseStreamingResponse, &extractJsonFromResponse]
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

        auto ai = snapshotAiConfig();

        if (api_key.empty() && ai.provider != "ollama_local") {
            res.status = 500;
            res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
            return;
        }

        std::string jobId = generateManualJobId(text);
        std::cout << "[INFO] Import: generated job_id=" << jobId << " from " << text.size() << " chars" << std::endl;

        std::string truncated = text.substr(0, 8000);
        std::string extractPrompt =
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
            json extractRequest = buildAiRequest(ai.provider, ai.model, extractPrompt, ai.max_tokens,
                                                  0.3, ai.top_p, ai.top_k);

            std::string extractResponse = httpPostAI(ai.endpoint, api_key, extractRequest.dump());
            std::string accumulated = parseStreamingResponse(extractResponse);
            if (accumulated.empty()) throw std::runtime_error("Empty response from extraction AI");
            std::cout << "[INFO] Import: extraction AI responded (" << accumulated.size() << " chars)" << std::endl;
            std::cout << "[DEBUG] Import: extraction raw (first 500): " << accumulated.substr(0, 500) << std::endl;

            json extracted;
            try {
                extracted = extractJsonFromResponse(accumulated);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Import: extraction JSON parse failed: " << e.what() << std::endl;
                try { extracted = json::parse(accumulated); } catch (...) {
                    throw std::runtime_error(std::string("Extraction parse failed: ") + e.what());
                }
            }

            Job job;
            job.job_id           = jobId;
            job.title            = extracted.value("title", "");
            job.company_name     = extracted.value("company_name", "");
            job.place            = extracted.value("place", "");
            job.zipcode          = extracted.value("zipcode", "");
            job.canton_code      = "N/A";
            {
                auto& eg = extracted["employment_grade"];
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
            job.application_url  = extracted.value("application_url", "");
            job.detail_url       = "";
            job.pub_date         = extracted.value("pub_date", "");
            if (job.pub_date.empty()) {
                std::time_t now = std::time(nullptr);
                std::tm tm_buf{};
#ifdef _MSC_VER
                localtime_s(&tm_buf, &now);
#else
                localtime_r(&now, &tm_buf);
#endif
                char buf[11];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
                job.pub_date = buf;
            }
            job.end_date         = extracted.value("end_date", "");
            std::string description = extracted.value("description", "");
            job.template_text    = description.empty() ? text : description;

            {
                std::lock_guard<std::mutex> lock(db_write_mutex);
                insert_or_update_job(db, job);
            }

            std::cout << "[INFO] Import: job inserted — " << jobId << " — " << job.title << std::endl;

            std::string profileContent = loadProfileMarkdown();
            if (!profileContent.empty()) {
                std::cout << "[INFO] Import: running fit-check for " << jobId << "..." << std::endl;
                std::string cleaned = cleanTemplateText(job.template_text);
                if (!cleaned.empty()) {
                    std::string fitPrompt = buildFitcheckPrompt(profileContent, cleaned);
                    json fitRequest = buildAiRequest(ai.provider, ai.model, fitPrompt, ai.max_tokens,
                                                    ai.temperature, ai.top_p, ai.top_k);

                    std::string fitResponse = httpPostAI(ai.endpoint, api_key, fitRequest.dump());
                    std::string fitAccumulated = parseStreamingResponse(fitResponse);
                    if (!fitAccumulated.empty()) {
                        try {
                            json fitData = extractJsonFromResponse(fitAccumulated);
                            std::lock_guard<std::mutex> lock(db_write_mutex);
                            save_fit_result_v2(db, jobId,
                                fitData.value("fit_score", 0),
                                fitData.value("fit_label", "Unknown"),
                                fitData.value("fit_summary", ""),
                                fitData.value("fit_reasoning", ""),
                                "md_file_profile");
                            std::cout << "[INFO] Import: fit-check complete for " << jobId << " — " << fitData.value("fit_label", "?") << " (" << fitData.value("fit_score", 0) << ")" << std::endl;
                        } catch (const std::exception& e2) {
                            std::cerr << "[WARN] Import: fit-check JSON parse failed for " << jobId << ": " << e2.what() << std::endl;
                            std::cerr << "[DEBUG] Fit-check raw (first 500): " << fitAccumulated.substr(0, 500) << std::endl;
                        }
                    } else {
                        std::cerr << "[WARN] Import: fit-check returned empty for " << jobId << std::endl;
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

    server.Delete("/api/admin/jobs/bulk", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string fit_label = body.value("fit_label", "");
            if (fit_label.empty())
                throw std::runtime_error("Missing 'fit_label' field");
            int deleted;
            {
                std::lock_guard<std::mutex> lock(db_write_mutex);
                deleted = bulk_hard_delete_by_fit_label(db, fit_label);
            }
            std::cout << "[ADMIN] Hard-deleted " << deleted << " jobs with fit_label=" << fit_label << std::endl;
            res.set_content(json{{"ok", true}, {"deleted", deleted}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Delete("/api/admin/jobs/:id", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] DELETE /api/admin/jobs/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            delete_job(db, job_id);
            std::cout << "[ADMIN] Deleted job " << job_id << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Delete job failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/clear/:id", [&db, &db_write_mutex](const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear/" << job_id << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_fit_data(db, job_id);
            std::cout << "[ADMIN] Cleared fit data for job " << job_id << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear fit data failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/clear", [&db, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[ADMIN] POST /api/admin/fitcheck/clear (all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_all_fit_data(db);
            std::cout << "[ADMIN] Cleared all fit data" << std::endl;
            res.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "[ADMIN] Clear all fit data failed: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/recheck/:id", [&api_key, &db_write_mutex, &db,
        &snapshotAiConfig, &loadProfileMarkdown, &buildFitcheckPrompt, &parseStreamingResponse, &extractJsonFromResponse]
    (const httplib::Request& req, httplib::Response& res) {
        std::string job_id = req.path_params.at("id");
        std::cout << "[INFO] Admin recheck triggered for job: " << job_id << std::endl;

        std::string profile = loadProfileMarkdown();
        if (profile.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "No profile found"}}.dump(), "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_fit_data(db, job_id);
        }

        std::optional<std::string> templateText;
        {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            templateText = get_job_template_text(db, job_id);
        }

        if (!templateText) {
            res.status = 404;
            res.set_content(json{{"error", "Job not found"}}.dump(), "application/json");
            return;
        }

        auto ai = snapshotAiConfig();

        if (api_key.empty() && ai.provider != "ollama_local") {
            res.status = 500;
            res.set_content(json{{"error", "AI not configured — set provider and API key in Settings."}}.dump(), "application/json");
            return;
        }

        try {
            std::string cleaned = cleanTemplateText(*templateText);
            std::string prompt = buildFitcheckPrompt(profile, cleaned);

            json request = buildAiRequest(ai.provider, ai.model, prompt, ai.max_tokens,
                                          ai.temperature, ai.top_p, ai.top_k);

            std::string apiResponse = httpPostAI(ai.endpoint, api_key, request.dump());
            std::string accumulated = parseStreamingResponse(apiResponse);

            if (accumulated.empty()) {
                res.status = 500;
                res.set_content(json{{"error", "Empty response from AI"}}.dump(), "application/json");
                return;
            }

            json fitData = extractJsonFromResponse(accumulated);

            {
                std::lock_guard<std::mutex> lock(db_write_mutex);
                save_fit_result_v2(db, job_id,
                                   fitData.value("fit_score", 0),
                                   fitData.value("fit_label", "Unknown"),
                                   fitData.value("fit_summary", ""),
                                   fitData.value("fit_reasoning", ""),
                                   "admin_recheck");
            }

            res.set_content(json{{"ok", true}, {"fit_score", fitData.value("fit_score", 0)}, {"fit_label", fitData.value("fit_label", "Unknown")}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", std::string("Recheck failed: ") + e.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/fitcheck/recheck", [&db, &db_write_mutex](const httplib::Request&, httplib::Response& res) {
        std::cout << "[INFO] Admin batch recheck triggered (clear all)" << std::endl;
        try {
            std::lock_guard<std::mutex> lock(db_write_mutex);
            clear_all_fit_data(db);
            res.set_content(json{{"ok", true}, {"message", "All fit data cleared. Trigger /api/fitcheck to recheck."}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // ── END V2 API ─────────────────────────────────────────────────────────────

    for (int attempt = 1; attempt <= 5; ++attempt) {
        std::cout << "Server running on http://0.0.0.0:8080" << std::endl;
        if (server.listen("0.0.0.0", 8080)) break;
        std::cerr << "listen() failed (attempt " << attempt << "/5), retrying in 2s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    sqlite3_close(db);
    
    // Cleanup curl globalization
    curl_global_cleanup();
    
    return 0;
}