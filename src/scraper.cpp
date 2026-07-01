#include "scraper.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <regex>
#include <thread>

#include "html.h"
#include "http.h"

using json = nlohmann::json;

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

std::vector<Job> scrapeLinkedIn(const ConfigV2& cfg) {
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


std::string httpGet(const std::string& url, long* out_status) {
    return httpRequest(url, "GET", {
        "Accept: application/json",
        "Origin: https://www.jobs.ch",
        "Referer: https://www.jobs.ch/",
        "X-Node-Request: false",
        "X-Source: jobs_ch_desktop"
    }, "", 120L, out_status);
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

void fetchJobDetails(std::vector<Job> jobs, sqlite3* db, std::mutex& db_mutex, ProgressTracker& progress) {
    int updated = 0, failed = 0;
    bool linkedInBlocked = false;

    for (const auto& job : jobs) {
        const bool is_linkedin = (job.source == "linkedin");

        if (is_linkedin && linkedInBlocked) { progress.done++; continue; }

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
                    progress.failed++;
                    progress.done++;
                    continue;
                }

                if (status != 200) {
                    std::cerr << "[DETAIL] " << job.job_id << " HTTP " << status
                              << " — skipping" << std::endl;
                    failed++;
                    progress.failed++;
                    progress.done++;
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
                    progress.failed++;
                    progress.done++;
                    continue;
                }
                if (status == 999 || status == 429 || (status != 200 && status != 0)) {
                    std::cerr << "[LI] HTTP " << status << " on detail " << job.job_id << " — stopping LinkedIn detail fetches" << std::endl;
                    linkedInBlocked = true;
                    failed++;
                    progress.failed++;
                    progress.done++;
                    continue;
                }
                if (html.empty()) {
                    std::cerr << "[LI] Empty response for detail " << job.job_id << std::endl;
                    failed++;
                    progress.failed++;
                    progress.done++;
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
            progress.done++;
            std::cout << "[DETAIL] " << job.job_id << " updated" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "[DETAIL] Failed for " << job.job_id << ": " << e.what() << std::endl;
            failed++;
            progress.failed++;
            progress.done++;
        }
    }
    progress.running = false;
    std::cout << "[INFO] Background detail fetch done: " << updated << " updated, " << failed << " failed" << std::endl;
}
