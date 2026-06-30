#ifndef JOB_APP_HTML_H
#define JOB_APP_HTML_H
#include <string>
#include <vector>


std::string decodeHtmlEntities(const std::string& s);
std::string stripHtmlTags(const std::string& s);
std::string collapseWhitespace(const std::string& s);
std::string cleanHtmlField(const std::string& raw);
std::string extractTagContent(const std::string& html, const std::string& classMarker, const std::string& endTag);
std::string cleanTemplateText(const std::string& raw);
std::vector<std::string> findAllCaptures(const std::string& text, const std::string& pattern);


#endif
