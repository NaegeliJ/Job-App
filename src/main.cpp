#define _WIN32_WINNT 0x0A00
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <curl/curl.h>
#include "httplib.h"
#include "json.hpp"
#include "app_state.h"
#include "config.h"
#include "db.h"
#include "routes.h"
#include "scheduler.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    curl_global_init(CURL_GLOBAL_ALL);

    AppState appState;
    Scheduler AutoScheduler(appState);

    fs::path root;
    try {
        root = fs::canonical(argv[0]).parent_path();
    } catch (...) {
        root = fs::current_path();
    }
    if (root.filename().string().rfind("cmake-build-", 0) == 0) { // CLion output dir, step up
        root = root.parent_path();
    }
    appState.base_dir               = root.string();
    appState.profile_path           = appState.base_dir + "/config/user_profile.md";
    appState.config_path            = appState.base_dir + "/config/config_v2.json";
    appState.system_prompt_path     = appState.base_dir + "/config/system_prompt.txt";
    appState.onboarding_prompt_path = appState.base_dir + "/config/onboarding_prompt.txt";
    appState.import_prompt_path     = appState.base_dir + "/config/import_prompt.txt";

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

    try {
        appState.system_prompt_template = readFileOrThrow(appState.system_prompt_path);
        appState.onboarding_prompt      = readFileOrThrow(appState.onboarding_prompt_path);
        appState.import_prompt          = readFileOrThrow(appState.import_prompt_path);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }
    if (appState.system_prompt_template.find("{{profile}}") == std::string::npos ||
        appState.system_prompt_template.find("{{jobText}}") == std::string::npos) {
        std::cerr << "[ERROR] " << appState.system_prompt_path << " missing {{profile}} or {{jobText}} placeholders" << std::endl;
        return 1;
    }
    std::cout << "[INFO] Prompts loaded" << std::endl;

    if (appState.config_v2.automode_enabled) {
        AutoScheduler.start();
    }

    httplib::Server server;
    registerRoutes(server, appState, AutoScheduler);

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
