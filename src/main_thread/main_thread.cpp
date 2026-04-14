#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

#include <Windows.h>

#include "json.hpp"
using json = nlohmann::json;


#include "main_thread.h"
#include "../../env.h"
#include "../process/process.h"
#include "../memory/memory.hpp"
#include "../match/match.h"
#include "../UI/d3d9Wrapper.h"
#include "../../SUPER_SECRET_KEY.h"

std::atomic<bool> MainThreadShouldStop = false;
std::atomic<bool> MainThreadMatchReaded = false;

std::atomic<bool> NeedUpdate = false;
#define GAME_STATUS_MATCH_STARTED 0x4

bool InitializeHook();
bool checkVersionAndUpdate(const std::string& url, const std::string& expected_version);

int MainThreadProc(HMODULE hModule) {
	if (!ProcessManager::instance().ReadProcess()) {
		MessageBox(NULL, L"Error! Can't read process", L"Main Thread", MB_ICONERROR);
		return -1;
	};

	int s_GameStatus;
	InitializeHook();
	NeedUpdate = checkVersionAndUpdate(VERSION_CHECK_URL, VERSION);
	if (NeedUpdate) return -1;

	while (!MainThreadShouldStop) {

		MemoryWorker::ReadProcessMemoryWithOffsets(
			ProcessManager::instance().s_SG_Process,
			ProcessManager::instance().s_BaseAddress,
			{
			static_cast<uintptr_t>(AddressTable::Base_Adress()),
			static_cast<uintptr_t>(AddressTable::Offset_GameStatus())  // Приведение к нужному типу
			},
			&s_GameStatus);
		//Мы в матче, но ещё не прочитали его!
		if (!MainThreadMatchReaded && 
			s_GameStatus == GAME_STATUS_MATCH_STARTED) {
			
			g_CurrentMatch.updateCounter();
			MainThreadMatchReaded = true;
		}
		// Если мы НЕ в матче И читали персонажей (значит были в матче!)
		else if (s_GameStatus != GAME_STATUS_MATCH_STARTED && MainThreadMatchReaded) {
			g_CurrentMatch.sendMatchInfo();
			MainThreadMatchReaded = false;
		}
		Sleep(10);
	}
}

bool InitializeHook() {
	bool d3d9Hooked = false;

	HMODULE hModule = GetModuleHandle(nullptr); // Получаем базовый адрес текущего модуля
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
	PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDosHeader->e_lfanew);
	PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hModule +
		pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
	for (; pImportDesc->Name; pImportDesc++) {
		const char* dllName = (const char*)((BYTE*)hModule + pImportDesc->Name);
		if (_stricmp(dllName, "d3d9.dll") == 0) {

			PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((BYTE*)hModule + pImportDesc->FirstThunk);
			for (; pThunk->u1.Function; pThunk++) {
				PROC* ppFunc = (PROC*)&pThunk->u1.Function;
				if (*ppFunc == GetProcAddress(GetModuleHandleA("d3d9.dll"), "Direct3DCreate9")) {
					DWORD oldProtect;
					VirtualProtect(ppFunc, sizeof(PROC), PAGE_READWRITE, &oldProtect);
					originalDirect3DCreate9 = (IDirect3D9 * (WINAPI*)(UINT)) * ppFunc; // Сохраняем оригинал
					*ppFunc = (PROC)HookedDirect3DCreate9; // Заменяем на свою функцию
					VirtualProtect(ppFunc, sizeof(PROC), oldProtect, &oldProtect);
					d3d9Hooked = 1;
					break;
				}
				if (d3d9Hooked) return true;
			}
		}
	}
	return false;
}

bool checkVersionAndUpdate(const std::string& url, const std::string& expected_version) {
	httplib::Client cli(url);

	// Отправляем GET-запрос
	auto res = cli.Get("/");
	if (!res || res->status != 200) {
		std::cerr << "HTTP error: " << (res ? std::to_string(res->status) : "no response") << std::endl;
		return true; // ошибка – считаем, что нужно обновление
	}

	try {
		json data = json::parse(res->body);
		if (!data.contains("version")) {
			return true;
		}
		std::string remote_version = data["version"];
		return remote_version != expected_version;
	}
	catch (const std::exception& e) {
		return true;
	}
}
