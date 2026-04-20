#pragma once
#include <string>
#include <Windows.h>
extern HMODULE g_hModule;

class CurlWrapper {
public:
    struct Response {
        long status = 0;
        std::string body;
        bool success = false;
    };

    static Response Request(const std::string& url,
        const std::string& method,
        const std::string& body = "",
        const std::string& content_type = "application/json");

private:
    // Callback должен быть статическим
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
};
