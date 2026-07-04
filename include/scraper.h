#ifndef JOB_APP_SCRAPER_H
#define JOB_APP_SCRAPER_H

#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "app_state.h"
#include "config.h"
#include "db.h"
#include "json.hpp"


std::string httpGet(const std::string& url, long* out_status = nullptr);
std::vector<Job> scrapeLinkedIn(const ConfigV2& cfg);
Job jobFromJson(const nlohmann::json& data);
void fetchJobDetails(std::vector<Job> jobs, sqlite3* db, std::mutex& db_mutex, ProgressTracker& progress);
std::optional<std::vector<Job>> tryStartDetailFetch(AppState& state);
int scrapeAllSources(AppState& state);


#endif
