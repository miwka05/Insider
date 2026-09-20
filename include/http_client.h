#pragma once

#include <string>

class HttpClient
{
public:
    explicit HttpClient(std::string url);

    bool postJson(const std::string& body, long& http_status, std::string& error_message) const;

private:
    std::string url;
};
