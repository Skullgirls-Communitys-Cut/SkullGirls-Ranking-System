
#include <atomic>

int MainThreadProc(HMODULE hModule);

extern std::atomic<bool> MainThreadShouldStop;