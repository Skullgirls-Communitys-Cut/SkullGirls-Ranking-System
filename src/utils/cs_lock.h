#pragma once
#include <Windows.h>

// Обёртка для автоматического освобождения CRITICAL_SECTION
struct CSLock {
    CRITICAL_SECTION& cs;
    CSLock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
    ~CSLock() { LeaveCriticalSection(&cs); }
    CSLock(const CSLock&) = delete;
    CSLock& operator=(const CSLock&) = delete;
};
