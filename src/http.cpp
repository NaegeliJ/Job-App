#include <iostream>
#include <random>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include "http.h"



void rateLimitSleep(int min_ms, int max_ms) {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(min_ms, max_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
}

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

std::string httpRequest(const std::string& url, const std::string& method,
                        const std::vector<std::string>& headers,
                        const std::string& postData,
                        long timeoutSeconds,
                        long* out_status) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        struct curl_slist* headerList = nullptr;
        for (const auto& header : headers) {
            headerList = curl_slist_append(headerList, header.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl error for " << url << ": " << curl_easy_strerror(res) << std::endl;
        } else {
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (out_status) *out_status = httpCode;
            if (httpCode >= 400)
                std::cerr << "[HTTP] " << url << " returned " << httpCode
                          << " — body: " << response.substr(0, 500) << std::endl;
        }

        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
    }
    return response;
}


std::string urlEncode(const std::string& str) {
    std::string encoded;
    for (unsigned char c : str) {
        // RFC 3986: Unreserved characters: A-Z a-z 0-9 - _ . ~
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            encoded += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}
