#include "config.h"
#include <fstream>
#include "json.hpp"


using json = nlohmann::json;


ConfigV2 loadConfigV2(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Could not open config_v2.json");

    json c = json::parse(file);
    validateConfigV2(c);
    ConfigV2 cfg;

    cfg.scrape_enabled    = c["scrape"].value("enabled", true);
    cfg.scrape_queries    = c["scrape"]["queries"].get<std::vector<std::string>>();
    cfg.scrape_rows       = c["scrape"]["rows"].get<int>();

    if (c.contains("linkedin")) {
        cfg.linkedin_enabled     = c["linkedin"].value("enabled", false);
        if (c["linkedin"].contains("queries"))
            cfg.linkedin_queries = c["linkedin"]["queries"].get<std::vector<std::string>>();
        cfg.linkedin_location    = c["linkedin"].value("location", "Switzerland");
        cfg.linkedin_time_range  = c["linkedin"].value("time_range", "r604800");
        cfg.linkedin_max_results = std::min(50, std::max(1, c["linkedin"].value("max_results", 25)));
    }

    cfg.provider          = c["fitcheck"].value("provider", "ollama_local");
    cfg.fitcheck_limit    = c["fitcheck"]["limit"].get<int>();
    cfg.model             = c["fitcheck"]["model"].get<std::string>();
    cfg.ai_endpoint       = c["fitcheck"]["endpoint"].get<std::string>();
    cfg.max_tokens        = c["fitcheck"].value("max_tokens", 4000);
    cfg.temperature       = c["fitcheck"].value("temperature", 1.0);
    cfg.top_p             = c["fitcheck"].value("top_p", 0.95);
    cfg.top_k             = c["fitcheck"].value("top_k", 64);

    return cfg;
}

void validateConfigV2(const json& c) {
    auto require = [&](const std::string& key) {
        if (!c.contains(key))
            throw std::runtime_error("Missing required config key: " + key);
    };
    require("scrape");
    require("fitcheck");
}

AiSnapshot snapshotAiConfig(const ConfigV2& cfg, std::shared_mutex& mtx) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return { cfg.provider, cfg.model, cfg.ai_endpoint,
             cfg.max_tokens, cfg.top_k,
             cfg.temperature, cfg.top_p };
}

bool apiKeyReady(const std::string& api_key, const AiSnapshot& ai){
    return !api_key.empty() || ai.provider == "ollama_local";
}

std::optional<AiSnapshot> getReadyAi(const std::string& api_key, const ConfigV2& cfg, std::shared_mutex& mtx){
    AiSnapshot ai_data = snapshotAiConfig(cfg, mtx);
    if (apiKeyReady(api_key, ai_data)){
        return ai_data;
    }
    return std::nullopt;
}

std::string readApiKey(const std::string& api_key, std::mutex& mtx) {
    std::lock_guard<std::mutex> lock(mtx);
    return api_key;
}

std::string loadProfileMarkdown(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}
