#include "CurlWrapper.h"
#include <curl.h>

size_t CurlWrapper::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

CurlWrapper::Response CurlWrapper::Request(const std::string& url, const std::string& method,
    const std::string& body, const std::string& content_type) {
    Response response;
    CURL* curl = curl_easy_init();
    if (!curl) return response;

    struct curl_slist* headers = nullptr;
    if (!body.empty() || method == "POST") {
        headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }

    if (method == "POST") curl_easy_setopt(curl, CURLOPT_POST, 1L);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA | CURLSSLOPT_NO_REVOKE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // Важно для многопоточности
    // Полезные настройки для стабильности
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        // Сетевая ошибка (не достучались до сервера)
        std::string err = "[CURL ERROR] " + std::string(curl_easy_strerror(res)) + "\n";
        OutputDebugStringA(err.c_str());
        response.success = false;
        response.status = 0;
    }
    else {
        // Соединение успешно, проверяем HTTP статус
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
        response.success = (response.status == 200);

        if (!response.success) {
            std::string err = "[HTTP ERROR] Status: " + std::to_string(response.status) + "\n";
            OutputDebugStringA(err.c_str());
        }
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}
