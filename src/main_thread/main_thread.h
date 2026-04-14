
#include <atomic>

int MainThreadProc(HMODULE hModule);

extern std::atomic<bool> MainThreadShouldStop;

extern std::atomic<bool> NeedUpdate;