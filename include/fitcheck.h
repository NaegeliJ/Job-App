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

void runBatchFitcheck(AppState& state, httplib::Response& res);

std::optional<AiSnapshot> requireAi(AppState& state, httplib::Response& res);

void importJobFromText(AppState& state, httplib::Response& res, const AiSnapshot& ai, const std::string& key, const std::string& text);

void generateProfile(AppState& state, const httplib::Request& req, httplib::Response& res);

#endif
