#include <Windows.h>
#include <TlHelp32.h>
#include "process.h"
#include "../../env.h"

// Инициализация статического указателя
ProcessManager* ProcessManager::s_instance = nullptr;

ProcessManager::ProcessManager() {
    // Инициализация членов данных
    s_ProcessId = 0;
    s_BaseAddress = 0;
    s_SG_Process = nullptr;
}

std::wstring to_lower(const std::wstring& str) {
    std::wstring result = str;
    CharLowerW(&result[0]);
    return result;
}

DWORD ProcessManager::GetModuleBaseAddress(DWORD dwProcessId, std::wstring ModuleName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, dwProcessId);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32 ModuleEntry32 = { 0 };
    ModuleEntry32.dwSize = sizeof(MODULEENTRY32);
    DWORD dwModuleBaseAddress = 0;

    if (Module32First(hSnapshot, &ModuleEntry32)) {
        do {
            if (to_lower(ModuleEntry32.szModule) == to_lower(ModuleName)) {
                dwModuleBaseAddress = (DWORD)ModuleEntry32.modBaseAddr;
                break;
            }
        } while (Module32Next(hSnapshot, &ModuleEntry32));
    }

    CloseHandle(hSnapshot);
    return dwModuleBaseAddress;
}

bool ProcessManager::ReadProcess() {
    // Теперь это нестатический метод, работает с членами экземпляра
    s_ProcessId = GetCurrentProcessId();
    if (s_ProcessId == 0) {
        return false;
    }
    s_BaseAddress = GetModuleBaseAddress(s_ProcessId, SG_NAME);
    if (s_BaseAddress == 0) {
        return false;
    }
    s_SG_Process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, s_ProcessId);
    if (s_SG_Process == 0) {
        return false;
    }
    return true;
}