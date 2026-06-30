#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "json.hpp"
#include "http.h"
#include "ai.h"


using json = nlohmann::json;


static bool isOllamaLocal(const std::string& provider) {
    return provider == "ollama_local";
}

static bool supportsJsonMode(const std::string& provider) {
    return provider == "openrouter" || provider == "mistral" || provider == "ollama_cloud";
}

static std::pair<std::string,std::string> classifyAiError(long http_status, const std::string& body) {
    if (http_status == 429) return {"rate_limit",      "Rate limit reached (HTTP 429)"};
    if (http_status == 402) return {"no_credits",      "Insufficient API credits (HTTP 402)"};
    if (http_status == 401 || http_status == 403)
        return {"invalid_api_key", "Invalid API key (HTTP " + std::to_string(http_status) + ")"};

    if ((http_status == 400 || http_status == 0) && !body.empty()) {
        std::string lower = body;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const auto& kw : {"invalid_api_key", "invalid api key", "incorrect api key",
                                "authentication_error", "authentication failed", "unauthorized"}) {
            if (lower.find(kw) != std::string::npos)
                return {"invalid_api_key", "Invalid API key (HTTP " + std::to_string(http_status) + ")"};
        }
        if (lower.find("credit") != std::string::npos || lower.find("billing") != std::string::npos)
            return {"no_credits", "Insufficient API credits (HTTP " + std::to_string(http_status) + ")"};
        if (lower.find("rate limit") != std::string::npos || lower.find("too many requests") != std::string::npos)
            return {"rate_limit", "Rate limit reached (HTTP " + std::to_string(http_status) + ")"};
    }
    return {"", ""};
}


json extractJsonFromResponse(const std::string& raw) {
    std::string content = extractBlock(raw, "json");
    try {
        return json::parse(content);
    } catch (...) {
        size_t obj_start = content.find("{");
        size_t obj_end = content.rfind("}");
        if (obj_start != std::string::npos && obj_end != std::string::npos && obj_end > obj_start)
            return json::parse(content.substr(obj_start, obj_end - obj_start + 1));
        throw std::runtime_error("No valid JSON found in response");
    }
}

std::string extractBlock(const std::string& raw, const std::string& lang) {
    std::string content = raw;
    std::string open = "```" + lang;
    size_t start = content.find(open);
    if (start != std::string::npos) {
        content = content.substr(start + open.size());
        size_t end = content.find("```");
        if (end != std::string::npos) content = content.substr(0, end);
    } else {
        start = content.find("```");
        if (start != std::string::npos) {
            content = content.substr(start + 3);
            size_t end = content.find("```");
            if (end != std::string::npos) content = content.substr(0, end);
        }
    }
    while (!content.empty() && std::isspace(content.front())) content = content.substr(1);
    while (!content.empty() && std::isspace(content.back())) content.pop_back();
    return content;
}

json buildAiRequest(const std::string& provider, const std::string& model, const std::string& prompt,
                    int max_tokens, double temperature, double top_p, int top_k) {
    json req = {
        {"model",       model},
        {"messages",    json::array({{{"role", "user"}, {"content", prompt}}})},
        {"max_tokens",  max_tokens},
        {"temperature", temperature},
        {"top_p",       top_p},
        {"stream",      false}
    };
    if (isOllamaLocal(provider)) {
        req["format"] = "json";
    } else if (supportsJsonMode(provider)) {
        req["response_format"] = {{"type", "json_object"}};
    }
    if (isOllamaLocal(provider) && top_k > 0) req["top_k"] = top_k;
    return req;
}

// AI inference calls need a longer timeout — model inference can take several minutes.
// Retries once on empty response (handles cold-start drops from cloud providers).
std::string httpPostAI(const std::string& url, const std::string& apiKey, const std::string& body) {
    const std::vector<std::string> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " + apiKey
    };
    auto hasTopLevelError = [](const std::string& raw) -> bool {
        try {
            json j = json::parse(raw);
            return j.contains("error");
        } catch (...) {
            return true; // Non-JSON (e.g. HTML 5xx) counts as error
        }
    };
    long http_status = 0;
    std::string response = httpRequest(url, "POST", headers, body, 600L, &http_status);
    const bool is_server_failure = response.empty() || (http_status >= 500 && hasTopLevelError(response));
    if (is_server_failure) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        response = httpRequest(url, "POST", headers, body, 600L, &http_status);
    }
    if (http_status == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        response = httpRequest(url, "POST", headers, body, 600L, &http_status);
    }
    // Check fatal status codes first — before body inspection, since some
    // providers use non-standard body formats (e.g. "detail" instead of "error").
    if (http_status == 0)
        throw FatalAiError("unreachable", "AI endpoint unreachable — check provider is running (" + url + ")");
    if (http_status == 401 || http_status == 403)
        throw FatalAiError("invalid_api_key", "Invalid API key (HTTP " + std::to_string(http_status) + ")");
    if (http_status == 402)
        throw FatalAiError("no_credits", "Insufficient API credits (HTTP 402)");
    if (http_status == 429)
        throw FatalAiError("rate_limit", "Rate limit reached (HTTP 429)");

    if (response.empty() || hasTopLevelError(response)) {
        auto [err_code, err_msg] = classifyAiError(http_status, response);
        if (!err_code.empty()) throw FatalAiError(err_code, err_msg);
        std::string msg = "HTTP " + std::to_string(http_status) + " from " + url;
        if (!response.empty()) msg += ": " + response.substr(0, 500);
        throw std::runtime_error(msg);
    }
    return response;
}

std::string parseStreamingResponse(const std::string& raw) {
    std::istringstream stream(raw);
    std::string line, accumulated;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        // Strip SSE prefix if present ("data: {...}")
        if (line.rfind("data: ", 0) == 0) line = line.substr(6);
        if (line == "[DONE]") break;
        try {
            json chunk = json::parse(line);
            // Ollama native NDJSON: {"message": {"content": "..."}}
            if (chunk.contains("message") && chunk["message"].contains("content"))
                accumulated += chunk["message"]["content"].get<std::string>();
            // OpenAI-compatible SSE: {"choices": [{"delta": {"content": "..."}}]}
            else if (chunk.contains("choices") && chunk["choices"].is_array() && !chunk["choices"].empty()) {
                const auto& delta = chunk["choices"][0];
                if (delta.contains("delta") && delta["delta"].contains("content"))
                    accumulated += delta["delta"]["content"].get<std::string>();
                else if (delta.contains("message") && delta["message"].contains("content"))
                    accumulated += delta["message"]["content"].get<std::string>();
            }
            if (chunk.contains("done") && chunk["done"].get<bool>()) break;
        } catch (...) {}
    }
    if (accumulated.empty() && !raw.empty()) {
        std::cerr << "[WARN] parseStreamingResponse: no content accumulated from non-empty raw response. Raw (first 500 chars):\n"
                  << raw.substr(0, std::min(raw.size(), size_t(500))) << std::endl;
    }
    return accumulated;
}
