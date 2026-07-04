#ifndef JOB_APP_FITCHECK_H
#define JOB_APP_FITCHECK_H

#include <optional>
#include <string>
#include "ai.h"
#include "httplib.h"
#include "app_state.h"
#include "config.h"

FitcheckResult runFitcheck(const std::string& cleaned_text, const std::string& profile, const std::string& system_prompt_template,
                            const AiSnapshot& ai,const std::string& api_key);

FitcheckResult checkAndSave(AppState& state, const std::string& job_id, const std::string& cleaned_text,
                            const std::string& profile, const AiSnapshot& ai, const std::string& api_key,
                            const std::string& source);

// Shared single-job flow: sends the error response and returns nullopt on
// precondition failures; lets checkAndSave exceptions propagate to the caller.
std::optional<FitcheckResult> fitcheckSingleJob(AppState& state, httplib::Response& res,
                                                const std::string& job_id, bool clear_first,
                                                const std::string& source);

struct BatchFitcheckResult {
    int checked{0};
    int failed{0};
    bool already_running{false};
    std::string fatal_code;    // non-empty when a fatal AI error aborted the batch
    std::string fatal_error;
};

// Batch loop without HTTP coupling, so callers besides the route (e.g. the
// scheduler) can run it. Preconditions (profile loaded, AI ready) are the
// caller's job; the fitcheck_progress guard is taken and released here.
BatchFitcheckResult runBatchFitcheckCore(AppState& state, const std::string& profile,
                                         const AiSnapshot& ai, const std::string& api_key);

void runBatchFitcheck(AppState& state, httplib::Response& res);

std::optional<AiSnapshot> requireAi(AppState& state, httplib::Response& res);

void importJobFromText(AppState& state, httplib::Response& res, const AiSnapshot& ai, const std::string& key, const std::string& text);

void generateProfile(AppState& state, const httplib::Request& req, httplib::Response& res);

#endif
