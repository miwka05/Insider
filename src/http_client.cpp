#include "http_client.h"
#include <curl/curl.h>
#include <utility>

static size_t discardResponse(char*, size_t size, size_t count, void*) {
    return size * count;
}

HttpClient::HttpClient(std::string url) : url(std::move(url)) {}

bool HttpClient::postJson(const std::string& body, long& http_status, std::string& error_msg) const {
    http_status = 0;
    error_msg.clear();

    CURL* curl = curl_easy_init();
    if (!curl) {
        error_msg = "curl_easy_init() failed";
        return false;
    }

    // Заголовки
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Таймауты, чтобы не ожидать вечно, если сервер не отвечает
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardResponse);

    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        error_msg = curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // Проверяем, что сервер вернул 2xx статус
    if (http_status < 200 || http_status >= 300) {
        error_msg = "HTTP status " + std::to_string(http_status);
        return false;
    }

    return true;
}