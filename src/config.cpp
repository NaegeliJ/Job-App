#include "config.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
#include "app_state.h"


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
    cfg.automode_enabled  = c.value("automode_enabled", false);
    cfg.interval_hours    = c.value("interval_hours", 1);


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

    if (c.value("interval_hours", 1) < 1) {
        throw std::runtime_error("Interval hours must be positive");
    }

    if (c["fitcheck"].value("temperature", 1.0) < 0 || c["fitcheck"].value("temperature", 1.0) > 1){
        throw std::runtime_error("Temperature must be between 0 and 1");

    }
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

std::string readFileOrThrow(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open " + path);
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

void saveAiConfig(AppState& state, const std::string& provider, const std::string& endpoint,
                  const std::string& model, const std::string& apiKey) {
    // Always persist for ollama_local so we can clear a previously saved key.
    if (provider == "ollama_local" || !apiKey.empty()) {
        std::ofstream keyFile(state.base_dir + "/config/api_keys.json");
        if (!keyFile.is_open()) throw std::runtime_error("Could not write api_keys.json");
        keyFile << json{{"api_key", apiKey}}.dump(2);
    }

    json configJson;
    {
        std::ifstream f(state.config_path);
        if (!f.is_open()) throw std::runtime_error("Could not read config_v2.json");
        configJson = json::parse(f);
    }
    configJson["fitcheck"]["provider"] = provider;
    configJson["fitcheck"]["endpoint"] = endpoint;
    configJson["fitcheck"]["model"]    = model;
    {
        std::ofstream f(state.config_path);
        if (!f.is_open()) throw std::runtime_error("Could not write config_v2.json");
        f << configJson.dump(2);
    }
    {
        std::unique_lock<std::shared_mutex> cfglock(state.config_v2_mutex);
        state.config_v2 = loadConfigV2(state.config_path);
    }
    if (provider == "ollama_local" || !apiKey.empty()) {
        std::lock_guard<std::mutex> keylock(state.api_key_mutex);
        state.api_key = apiKey;
    }

    std::cout << "[INFO] AI config updated: provider=" << provider << " model=" << model << std::endl;
}
