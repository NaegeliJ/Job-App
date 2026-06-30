#ifndef JOB_APP_CONFIG_H
#define JOB_APP_CONFIG_H
#include <string>
#include <vector>
#include <shared_mutex>
#include "json.hpp"


struct ConfigV2 {
    bool                     scrape_enabled{true};
    std::vector<std::string> scrape_queries;
    int                      scrape_rows{};

    bool                     linkedin_enabled{false};
    std::vector<std::string> linkedin_queries;
    std::string              linkedin_location{"Switzerland"};
    std::string              linkedin_time_range{"r604800"};
    int                      linkedin_max_results{25};

    std::string              provider{"ollama_local"};
    int                      fitcheck_limit{};
    std::string              model{};
    std::string              ai_endpoint{};
    int                      max_tokens{};
    double                   temperature{};
    double                   top_p{};
    int                      top_k{};
};

struct AiSnapshot {
    std::string provider, model, endpoint;
    int max_tokens, top_k;
    double temperature, top_p;
};

ConfigV2 loadConfigV2(const std::string& path);
void validateConfigV2(const nlohmann::json& c);
AiSnapshot snapshotAiConfig(const ConfigV2& cfg, std::shared_mutex& mtx);
bool apiKeyReady(const std::string& api_key, const AiSnapshot& ai);
std::optional<AiSnapshot> getReadyAi(const std::string& api_key, const ConfigV2& cfg, std::shared_mutex& mtx);
std::string readApiKey(const std::string& api_key, std::mutex& mtx);

#endif
