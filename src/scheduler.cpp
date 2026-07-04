#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include "scheduler.h"
#include "config.h"
#include "scraper.h"
#include "fitcheck.h"
#include "app_state.h"


Scheduler::Scheduler(AppState& state) : state(state){}

Scheduler::~Scheduler(){
    stop();
}

void Scheduler::start(){

    std::lock_guard<std::mutex> lock(lifecycle_mutex);

    if (schedulerThread.joinable()){
        return;
    }

    stopRequested = false;
    schedulerThread = std::thread(&Scheduler::runLoop, this);
    std::cout << "[AUTOMODE] ON" << std::endl;


}

void Scheduler::stop(){

    std::lock_guard<std::mutex> lock(lifecycle_mutex);

    if (!schedulerThread.joinable()) {
        return;
    }

    stopRequested = true;
    schedulerThread.join();
    std::cout << "[AUTOMODE] OFF" << std::endl;


}

void Scheduler::runLoop(){

    int interval_hours = 1;
    lastExecution = std::chrono::steady_clock::now();

    while(!stopRequested){

        std::this_thread::sleep_for(std::chrono::seconds(5));

        {
            std::shared_lock<std::shared_mutex> lock(state.config_v2_mutex);
            interval_hours = state.config_v2.interval_hours;
        }

        if (std::chrono::steady_clock::now() - lastExecution >= std::chrono::hours(interval_hours)) {

             tick();
             lastExecution = std::chrono::steady_clock::now();
        }
    }

}

void Scheduler::tick(){

    std::string apiKey = readApiKey(state.api_key, state.api_key_mutex);
    auto ai = getReadyAi(apiKey, state.config_v2, state.config_v2_mutex);
    std::string profile = loadProfileMarkdown(state.profile_path);

    if (!ai || profile.empty()) {
        std::cout << "[AUTOMODE] Failed to run pipeline due to missing ai or profile data" << std::endl;
        return;
    }

    scrapeAllSources(state);

    if (auto jobs = tryStartDetailFetch(state)){
        fetchJobDetails(std::move(*jobs), state.db, state.db_mutex, state.detail_progress);
    }

    BatchFitcheckResult result = runBatchFitcheckCore(state, profile, *ai, apiKey);
    if (!result.fatal_code.empty()) {
        std::cerr << "[AUTOMODE] Batch fit-check aborted (" << result.fatal_code << "): " << result.fatal_error << std::endl;
        return;
    }
    std::cout << "[AUTOMODE] Pipeline done: " << result.checked << " checked, " << result.failed << " failed" << std::endl;


}
