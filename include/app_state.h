#ifndef JOB_APP_APP_STATE_H
#define JOB_APP_APP_STATE_H

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "sqlite3.h"
#include "config.h"

struct ProgressTracker {
    std::atomic<bool> running{false};
    std::atomic<int>  done{0};
    std::atomic<int>  total{0};
    std::atomic<int>  failed{0};
};

struct AppState {
    sqlite3* db{};
    std::mutex db_mutex;

    std::string api_key;
    std::mutex api_key_mutex;

    ConfigV2 config_v2;
    std::shared_mutex config_v2_mutex;

    ProgressTracker fitcheck_progress;
    ProgressTracker detail_progress;

    std::string system_prompt_template;

    std::string base_dir;
    std::string config_path;
    std::string system_prompt_path;
    std::string profile_path;
};

#endif //JOB_APP_APP_STATE_H
