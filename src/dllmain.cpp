// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include <string>
#include "dll_proxy/dll_proxy.h"
#include "main_thread/main_thread.h"
#include "../env.h"

bool IsSGLoadThisDLL();
HANDLE hMainThread;
HMODULE g_hModule = NULL;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        if (!IsSGLoadThisDLL()) {
            MessageBox(NULL, L"This DLL loaded not by Skullgirls!\nPlease, delete d3d9.dll from this folder!", NULL, MB_ICONERROR);
            return FALSE;
        }
        g_hModule = hModule;
        if (!LoadOriginalLibrary()) {
            return FALSE;
        }
        hMainThread = CreateThread(
            nullptr,
            NULL,
            (LPTHREAD_START_ROUTINE)MainThreadProc,
            hModule,
            CREATE_SUSPENDED,
            NULL
        );
        if (hMainThread) {
            // Продолжаем выполнение после полной загрузки DLL
            ResumeThread(hMainThread);
        }
        break;

    case DLL_PROCESS_DETACH:
        WaitForSingleObject(hMainThread, 5000);
        CloseHandle(hMainThread);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

bool IsSGLoadThisDLL() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    // Извлекаем только имя файла из полного пути
    std::wstring fullPath(exePath);
    size_t lastSlash = fullPath.find_last_of(L"\\/");
    std::wstring exeName = (lastSlash == std::wstring::npos) ?
        fullPath : fullPath.substr(lastSlash + 1);

    std::wstring GameName = SG_NAME;
    // Приводим к нижнему регистру
    for (auto& c : exeName) c = towlower(c);
    for (auto& c : GameName) c = towlower(c);

    return exeName.find(GameName) != std::wstring::npos;
}