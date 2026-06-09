#pragma once
#include <string>
#include <fstream>
#include <ctime>
#include <cstdarg>
#include <cstdio>
#include "../../env.h"

#if ENABLE_FILE_LOGGER

// Simple string overload
inline void LogToFile(const std::string& message) {
    std::ofstream file("sg_debug.log", std::ios::app);
    if (!file.is_open()) return;

    // Timestamp
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm);

    file << "[" << timebuf << "] " << message << "\n";
    file.flush();
}

// printf-style overload. Uses a fixed-size stack buffer; enlarge if you need longer messages.
inline void LogToFile(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    LogToFile(std::string(buf));
}
#else

inline void LogToFile(const std::string&) {}
inline void LogToFile(const char*, ...) {}

#endif