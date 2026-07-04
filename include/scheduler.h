#ifndef JOB_APP_SCHEDULER_H
#define JOB_APP_SCHEDULER_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "app_state.h"


class Scheduler {

    public:
        explicit Scheduler(AppState& state);
        ~Scheduler();

        // Both idempotent and safe to call from concurrent HTTP handlers.
        void start();
        void stop();

    private:
        void runLoop();
        void tick();

        AppState& state;

        std::mutex lifecycle_mutex;   // guards start()/stop() against racing config POSTs
        std::thread schedulerThread;
        std::atomic<bool> stopRequested{false};

        std::chrono::steady_clock::time_point lastExecution;

};

#endif
