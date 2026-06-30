#include "html.h"
#include <string>
#include <cstring>
#include <regex>
#include <iostream>
#include <cctype>
#include "json.hpp"
using json = nlohmann::json;


std::string decodeHtmlEntities(const std::string& s) {
    struct { const char* entity; char ch; } table[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'},
        {"&#x27;", '\''}, {"&#39;", '\''}, {"&nbsp;", ' '}
    };
    std::string out = s;
    for (auto& e : table) {
        size_t pos = 0;
        while ((pos = out.find(e.entity, pos)) != std::string::npos)
            out.replace(pos, strlen(e.entity), 1, e.ch);
    }
    return out;
}


std::string stripHtmlTags(const std::string& s) {
    std::string out;
    bool inTag = false;
    for (char c : s) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out += c;
    }
    return out;
}


std::string collapseWhitespace(const std::string& s) {
    std::string out;
    bool inSpace = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            if (!inSpace) { out += ' '; inSpace = true; }
        } else {
            out += (char)c; inSpace = false;
        }
    }
    while (!out.empty() && std::isspace((unsigned char)out.front())) out = out.substr(1);
    while (!out.empty() && std::isspace((unsigned char)out.back()))  out.pop_back();
    return out;
}


std::string cleanHtmlField(const std::string& raw) {
    return collapseWhitespace(decodeHtmlEntities(stripHtmlTags(raw)));
}


// Avoids regex to prevent catastrophic backtracking on large HTML blobs.
std::string extractTagContent(const std::string& html, const std::string& classMarker, const std::string& endTag) {
    size_t pos = html.find(classMarker);
    if (pos == std::string::npos) return "";
    size_t gt = html.find('>', pos);
    if (gt == std::string::npos) return "";
    size_t end = html.find(endTag, gt + 1);
    if (end == std::string::npos) return "";
    return html.substr(gt + 1, end - gt - 1);
}


std::vector<std::string> findAllCaptures(const std::string& text, const std::string& pattern) {
    std::vector<std::string> results;
    try {
        std::regex re(pattern);
        auto it  = std::sregex_iterator(text.begin(), text.end(), re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it)
            results.push_back((*it)[1].str());
    } catch (const std::regex_error& e) {
        std::cerr << "[LI] regex error: " << e.what() << std::endl;
    }
    return results;
}


std::string cleanTemplateText(const std::string& raw) {
    std::string html;
    try {
        json parsed = json::parse(raw);
        html = parsed.is_string() ? parsed.get<std::string>() : parsed.dump();
        if (html.size() > 2 && html.front() == '"' && html.back() == '"')
            html = html.substr(1, html.size() - 2);
    } catch (...) {
        html = raw;
    }

    std::string collapsed = collapseWhitespace(decodeHtmlEntities(stripHtmlTags(html)));

    if (collapsed.size() > 8000) {
        collapsed = collapsed.substr(0, 8000);
        // Roll back to a UTF-8 character boundary — continuation bytes have the form 10xxxxxx.
        while (!collapsed.empty() && (collapsed.back() & 0xC0) == 0x80)
            collapsed.pop_back();
    }

    return collapsed;
}
