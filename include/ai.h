#ifndef JOB_APP_AI_H
#define JOB_APP_AI_H

#include <stdexcept>
#include <string>
#include "json.hpp"


struct FitcheckResult {
    int score;
    std::string label, summary, reasoning;
};


class FatalAiError : public std::runtime_error {
public:
    FatalAiError(const std::string& code, const std::string& message)
        : std::runtime_error(message), error_code_(code) {}
    const std::string& code() const { return error_code_; }
private:
    std::string error_code_;
};


std::string parseStreamingResponse(const std::string& raw);
std::string extractBlock(const std::string& raw, const std::string& lang);
std::string httpPostAI(const std::string& url, const std::string& apiKey, const std::string& body);
nlohmann::json extractJsonFromResponse(const std::string& raw);
nlohmann::json buildAiRequest(const std::string& provider, const std::string& model, const std::string& prompt,
                    int max_tokens, double temperature, double top_p, int top_k);


#endif
