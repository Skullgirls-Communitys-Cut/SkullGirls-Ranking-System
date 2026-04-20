#pragma once
#include <string>
#include <fstream>
#include <ctime>

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