#ifndef JOB_APP_RESPONSE_H
#define JOB_APP_RESPONSE_H

#include <string>
#include "httplib.h"
#include "json.hpp"

void sendJson(httplib::Response& res, const nlohmann::json& body, int status = 200);
void sendError(httplib::Response& res, int status, const std::string& message);

#endif
