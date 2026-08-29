#include "curl.h"
#include "../utils/CurlWrapper.h"
#include <sstream>

#include <Windows.h>

#include "json.hpp"
using json = nlohmann::json;

#include <iostream>
#include <string>
#include <cstring>


#include "main_thread.h"
#include "../../env.h"
#include "../process/process.h"
#include "../memory/memory.hpp"
#include "../match/match.h"
#include "../UI/d3d9Wrapper.h"
#include "../../SUPER_SECRET_KEY.h"
#include "../utils/logger.h"

std::atomic<bool> MainThreadShouldStop = false;
std::atomic<bool> MainThreadMatchReaded = false;

std::atomic<bool> NeedUpdate = false;
#define GAME_STATUS_MATCH_STARTED 0x4

bool InitializeHook();
bool checkVersionAndUpdate(const std::string& url, const std::string& expected_version);

#ifdef _DEBUG
// Наш перехватчик Get
typedef const char* (__fastcall* GetLobbyMemberData_t)(
	ISteamMatchmaking*, void*, CSteamID, CSteamID, const char*);

GetLobbyMemberData_t OriginalGetLobbyMemberData = nullptr;

const char* __fastcall HookedGetLobbyMemberData(
	ISteamMatchmaking* self, void* edx_unused,
	CSteamID lobbyID, CSteamID steamID, const char* key) {

	const char* result = OriginalGetLobbyMemberData(self, edx_unused, lobbyID, steamID, key);
	LogToFile(std::string("GET LobbyMemberData key=") + key +
		" result=" + (result ? result : "null"));
	return result;
}

// Set
typedef void(__fastcall* SetLobbyMemberData_t)(
	ISteamMatchmaking*, void*, CSteamID, const char*, const char*);

SetLobbyMemberData_t OriginalSetLobbyMemberData = nullptr;

void __fastcall HookedSetLobbyMemberData(
	ISteamMatchmaking* self, void* edx_unused,
	CSteamID lobbyID, const char* key, const char* value) {

	LogToFile(std::string("SET LobbyMemberData key=") + key +
		" value=" + (value ? value : "null"));
	OriginalSetLobbyMemberData(self, edx_unused, lobbyID, key, value);
}

// GetLobbyData
typedef const char* (__fastcall* GetLobbyData_t)(
	ISteamMatchmaking*, void*, CSteamID, const char*);
GetLobbyData_t OriginalGetLobbyData = nullptr;

const char* __fastcall HookedGetLobbyData(
	ISteamMatchmaking* self, void* edx_unused,
	CSteamID lobbyID, const char* key) {
	const char* result = OriginalGetLobbyData(self, edx_unused, lobbyID, key);
	LogToFile(std::string("GET LobbyData key=") + key +
		" result=" + (result ? result : "null"));
	return result;
}

// SetLobbyData
typedef bool(__fastcall* SetLobbyData_t)(
	ISteamMatchmaking*, void*, CSteamID, const char*, const char*);
SetLobbyData_t OriginalSetLobbyData = nullptr;

bool __fastcall HookedSetLobbyData(
	ISteamMatchmaking* self, void* edx_unused,
	CSteamID lobbyID, const char* key, const char* value) {
	LogToFile(std::string("SET LobbyData key=") + key +
		" value=" + (value ? value : "null"));
	return OriginalSetLobbyData(self, edx_unused, lobbyID, key, value);
}

// Установка хука
void HookSteamMatchmaking() {
	LogToFile("HookSteamMatchmaking called");

	ISteamMatchmaking* mm = SteamMatchmaking();
	if (!mm) {
		LogToFile("SteamMatchmaking() returned null!");
		return;
	}
	LogToFile("SteamMatchmaking OK, patching vtable...");

	void** vtable = *(void***)mm;
	LogToFile("vtable address: " + std::to_string((uintptr_t)vtable));

	DWORD oldProtect;

	// GetLobbyData (index 19)
	VirtualProtect(&vtable[19], sizeof(void*), PAGE_READWRITE, &oldProtect);
	OriginalGetLobbyData = (GetLobbyData_t)vtable[19];
	vtable[19] = (void*)HookedGetLobbyData;
	VirtualProtect(&vtable[19], sizeof(void*), oldProtect, &oldProtect);
	LogToFile("GetLobbyData hooked");

	// SetLobbyData (index 20)
	VirtualProtect(&vtable[20], sizeof(void*), PAGE_READWRITE, &oldProtect);
	OriginalSetLobbyData = (SetLobbyData_t)vtable[20];
	vtable[20] = (void*)HookedSetLobbyData;
	VirtualProtect(&vtable[20], sizeof(void*), oldProtect, &oldProtect);
	LogToFile("SetLobbyData hooked");

	VirtualProtect(&vtable[24], sizeof(void*), PAGE_READWRITE, &oldProtect);
	OriginalGetLobbyMemberData = (GetLobbyMemberData_t)vtable[24];
	vtable[24] = (void*)HookedGetLobbyMemberData;
	VirtualProtect(&vtable[24], sizeof(void*), oldProtect, &oldProtect);
	LogToFile("GetLobbyMemberData hooked");

	VirtualProtect(&vtable[25], sizeof(void*), PAGE_READWRITE, &oldProtect);
	OriginalSetLobbyMemberData = (SetLobbyMemberData_t)vtable[25];
	vtable[25] = (void*)HookedSetLobbyMemberData;
	VirtualProtect(&vtable[25], sizeof(void*), oldProtect, &oldProtect);
	LogToFile("SetLobbyMemberData hooked");
}
#endif

int MainThreadProc(HMODULE hModule) {
	if (!ProcessManager::instance().ReadProcess()) {
		curl_global_cleanup();
		MessageBox(NULL, L"Error! Can't read process", L"Main Thread", MB_ICONERROR);
		return -1;
	};

	int s_GameStatus;
	InitializeHook();
	curl_global_init(CURL_GLOBAL_DEFAULT);
	g_CurrentMatch.Init();
	InitializeCriticalSection(&MemoryWorker::Detail::cacheMutex);
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
			LogToFile("Before sendMatchInfo");
			g_CurrentMatch.sendMatchInfo();
			LogToFile("After sendMatchInfo");
			MainThreadMatchReaded = false;
		}
		Sleep(10);
	}
	return 0;
}

// Слот ищем по имени из таблицы имён импорта, а не по лежащему в нём адресу:
// адрес мог уже подменить другой мод, и тогда сравнение не совпадёт.
static PROC* FindImportSlot(BYTE* base, PIMAGE_IMPORT_DESCRIPTOR desc, const char* name)
{
	if (!desc->OriginalFirstThunk) return nullptr;
	PIMAGE_THUNK_DATA nameThunk = (PIMAGE_THUNK_DATA)(base + desc->OriginalFirstThunk);
	PIMAGE_THUNK_DATA addrThunk = (PIMAGE_THUNK_DATA)(base + desc->FirstThunk);
	for (; nameThunk->u1.AddressOfData; nameThunk++, addrThunk++) {
		if (nameThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
		PIMAGE_IMPORT_BY_NAME imported = (PIMAGE_IMPORT_BY_NAME)(base + nameThunk->u1.AddressOfData);
		if (strcmp((const char*)imported->Name, name) == 0) return (PROC*)&addrThunk->u1.Function;
	}
	return nullptr;
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

			PROC* ppFunc = FindImportSlot((BYTE*)hModule, pImportDesc, "Direct3DCreate9");
			if (ppFunc) {
				DWORD oldProtect;
				VirtualProtect(ppFunc, sizeof(PROC), PAGE_READWRITE, &oldProtect);
				originalDirect3DCreate9 = (IDirect3D9 * (WINAPI*)(UINT)) * ppFunc; // Сохраняем оригинал
				*ppFunc = (PROC)HookedDirect3DCreate9; // Заменяем на свою функцию
				VirtualProtect(ppFunc, sizeof(PROC), oldProtect, &oldProtect);
				d3d9Hooked = 1;
				return true;
			}
		}
	}
	return false;
}

bool checkVersionAndUpdate(const std::string& url, const std::string& expected_version) {
	// Отправляем GET-запрос. В libcurl лучше передавать полный URL сразу
	auto res = CurlWrapper::Request(VERSION_CHECK_URL VERSION_CHECK_PATH, "GET");

	std::stringstream ss;
	//ss << "[DEBUG] Raw Body: |" << res.body << "|\n";
	//ss << "[DEBUG] Body size: " << res.body.size() << "\n";

	if (!res.success) {
		std::cerr << "HTTP error: " << (res.status != 0 ? std::to_string(res.status) : "no response") << std::endl;
		return true; // ошибка – считаем, что нужно обновление
	}

	try {
		json data = json::parse(res.body);
		//ss << "[DEBUG] JSON 'version' field: " << data["version"].dump() << "\n";

		std::string remote_version = data.value("version", "");
		//ss << "[DEBUG] remote_version: '" << remote_version << "' Len: " << remote_version.length() << "\n";
		//ss << "[DEBUG] expected_version: '" << expected_version << "' Len: " << expected_version.length() << "\n";
		//OutputDebugStringA(ss.str().c_str());

		return remote_version != expected_version;
	}
	catch (const std::exception&) {
		//std::string err = "[DEBUG] JSON Error: ";
		//err += e.what();
		//OutputDebugStringA(err.c_str());
		return true;
	}
}
