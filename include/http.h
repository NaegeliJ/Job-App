#ifndef JOB_APP_HTTP_H
#define JOB_APP_HTTP_H
#include <string>
#include <vector>

void rateLimitSleep(int min_ms = 750, int max_ms = 1500);

std::string httpRequest(const std::string& url,
                        const std::string& method,
                        const std::vector<std::string>& headers = {},
                        const std::string& postData = "",
                        long timeoutSeconds = 120L,
                        long* out_status = nullptr);

std::string urlEncode(const std::string& str);

#endif
