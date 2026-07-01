#include "response.h"

void sendJson(httplib::Response& res, const nlohmann::json& body, int status) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void sendError(httplib::Response& res, int status, const std::string& message) {
    sendJson(res, {{"error", message}}, status);
}
