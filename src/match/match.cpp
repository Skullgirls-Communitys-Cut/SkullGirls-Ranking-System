#include "../CurlWrapper.h"

#include "Windows.h"
#include <iostream>
#include <chrono>
#include <mutex>
#include <fstream>

#include "steam/steam_api.h"
#include "json.hpp"
using json = nlohmann::json;

#include "match.h"
#include "../../env.h"
#include "../../SUPER_SECRET_KEY.h"
#include "../memory/memory.hpp"
#include "../process/process.h"
#include "../UI/UI.h"

Match g_CurrentMatch;
std::recursive_mutex Match::sendMutex;

int convertMatchResult(int memoryResult, bool weAreFirstPlayer) {
    switch (memoryResult) {
    case 0: return 0;                     // Ещё играем
    case 1: return weAreFirstPlayer ? 1 : 2; // P1 выиграл через TO -> Отправитель/Соперник
    case 2: return weAreFirstPlayer ? 2 : 1; // P2 выиграл через TO -> Соперник/Отправитель
    case 3: return 3;                     // Ничья через TO
    case 4: return weAreFirstPlayer ? 4 : 5; // P1 выиграл -> Отправитель/Соперник
    case 5: return weAreFirstPlayer ? 5 : 4; // P2 выиграл -> Соперник/Отправитель
    case 6: return 6;                     // Ничья
    default: return 0;                    // Некорректное значение
    }
}

void Match::ReadCharacterNames() {
    for (int n = 0; n < MAX_PLAYABLE_CHARACTERS; n++) {
        char Char_Name[16];
        bool readSuccess = MemoryWorker::ReadProcessMemoryWithOffsets(
            ProcessManager::instance().s_SG_Process,
            ProcessManager::instance().s_BaseAddress,
            {
                static_cast<uintptr_t>(AddressTable::Base_Adress()),
                static_cast<uintptr_t>(AddressTable::Offset_Character() + n * 4),
                static_cast<uintptr_t>(AddressTable::Offset_Name())
            },
            &Char_Name,
            sizeof(Char_Name)
        );

        if (!readSuccess) {
            Character_Names[n] = std::nullopt;
            continue;
        }

        if (strlen(Char_Name) == 0) {

            Character_Names[n] = std::nullopt;
            continue;
        }
        Character_Names[n] = std::string(Char_Name);
    }
}

nlohmann::json Match::GenerateCharacterNames(bool WeAreFirstPlayer) const {
    nlohmann::json result = nlohmann::json::array();

    // Выбираем диапазон индексов в зависимости от того, первый ли мы игрок
    int startIdx = WeAreFirstPlayer ? 0 : 3;
    int endIdx = WeAreFirstPlayer ? 3 : MAX_PLAYABLE_CHARACTERS; // 6

    for (int i = startIdx; i < endIdx; ++i) {
        if (Character_Names[i].has_value()) {
            result.push_back(Character_Names[i].value());
        }
    }

    return result;
}

bool Match::sendMatchInfo() {
    std::lock_guard<std::recursive_mutex> lock(sendMutex); // если используется

    if (!CanSendMatch) return false;

    SUPER_SECRET_KEY.RequestTicket();

    const char* roomTypeStr = SteamMatchmaking()->GetLobbyData(lobbyID, "RoomType");
    int RoomType = (roomTypeStr && roomTypeStr[0]) ? atoi(roomTypeStr) : 0;
    if (RoomType != LOBBY_TYPE_ALL_PLAY) return false;

    // Формируем все данные под мьютексом, чтобы гарантировать консистентность
    long long timestamp = std::time(nullptr);
    long long mySteamIDuint64 = SteamUser()->GetSteamID().ConvertToUint64();

    const char* oppStr = SteamMatchmaking()->GetLobbyMemberData(lobbyID, SteamUser()->GetSteamID(), "Opp");
    CSteamID OppSteamID = (oppStr && oppStr[0]) ? CSteamID(strtoull(oppStr, nullptr, 10)) : k_steamIDNil;
    long long OppSteamIDuint64 = OppSteamID.ConvertToUint64();

    const char* MylocStr = SteamMatchmaking()->GetLobbyMemberData(lobbyID, SteamUser()->GetSteamID(), "Loc");
    int MyLoc = (MylocStr && MylocStr[0]) ? atoi(MylocStr) : 0;

    const char* OpplocStr = SteamMatchmaking()->GetLobbyMemberData(lobbyID, OppSteamID, "Loc");
    int OppLoc = (OpplocStr && OpplocStr[0]) ? atoi(OpplocStr) : 0;

    bool WeAreFirstPlayer = (OppSteamID == player2SteamID) ? true : false;

    int resultMemory;
    MemoryWorker::ReadProcessMemoryWithOffsets(
        ProcessManager::instance().s_SG_Process,
        ProcessManager::instance().s_BaseAddress,
        {
            static_cast<uintptr_t>(AddressTable::Base_Adress()),
            static_cast<uintptr_t>(AddressTable::Offset_ResultMatch())
        },
        &resultMemory
    );

    int result = convertMatchResult(resultMemory, WeAreFirstPlayer);

    ReadCharacterNames();

    json request;
    request["version"] = VERSION;

    request["matchId"] = (int)rng0 + matchCount;
    request["matchResult"] = result;
    request["timeStamp"] = timestamp;

    request["myInfo"]["steamId"] = mySteamIDuint64;
    request["myInfo"]["characters"] = GenerateCharacterNames(WeAreFirstPlayer);
    request["myInfo"]["region"] = MyLoc;

    request["opponentInfo"]["steamId"] = OppSteamIDuint64;
    request["opponentInfo"]["characters"] = GenerateCharacterNames(!WeAreFirstPlayer);
    request["opponentInfo"]["region"] = OppLoc;

    std::string url = API_URL;
    std::string path = API_PATH;

    // Запускаем поток для отправки, передавая копии данных
    
    std::thread([request, url, path, OppSteamID, result, timestamp]() mutable {
        while (!SUPER_SECRET_KEY.GetHasTicket()) {
            Sleep(10);
        };

        request["key"] = SUPER_SECRET_KEY.GetCurrentTicketBase64();

        std::string body = request.dump();


#ifdef _DEBUG
        std::string debug_body = request.dump(4);
        OutputDebugStringA(("[DEBUG] Sending JSON: " + debug_body + "\n").c_str());

        std::ofstream file("output.json");
        if (file.is_open()) {
            file << debug_body;
            file.close();
        }
#endif

        auto res = CurlWrapper::Request(url + path, "POST", body, "application/json");
        if (!res.success) {
            OutputDebugStringA("[DEBUG] POST request failed!\n");
        }

        RankUI::g_MatchHistory.AddMatch(OppSteamID, result, timestamp);
        }).detach();

    
    return true;
}

#define MAX_MESSAGE_SIZE 256
void Match::OnLobbyChatMessage(LobbyChatMsg_t* pCallback) {
    char Message[MAX_MESSAGE_SIZE];
    int messageSize = SteamMatchmaking()->GetLobbyChatEntry(
        pCallback->m_ulSteamIDLobby,
        pCallback->m_iChatID,
        NULL,
        Message,
        sizeof(Message),
        NULL
    );

    if (memcmp(Message, "MINF", 4) == 0) {
        uint64_t steamID64P1 = 0;
        uint64_t steamID64P2 = 0;
        uint32_t matchIdRng0 = 0;

        // Извлекаем SteamID1 (little-endian)
        memcpy(&steamID64P1, Message + 4, 8);
        // Извлекаем SteamID2 (little-endian)
        memcpy(&steamID64P2, Message + 12, 8);
        // Извлекаем MatchID/RNG0 (little-endian)
        memcpy(&matchIdRng0, Message + 20, 4);

        CSteamID steamIDP1(steamID64P1);
        CSteamID steamIDP2(steamID64P2);

        if (steamIDP1 == SteamUser()->GetSteamID() or
            steamIDP2 == SteamUser()->GetSteamID()) {

            player1SteamID = steamIDP1;
            player2SteamID = steamIDP2;
            rng0 = matchIdRng0;
            matchCount = 0;
            //We are playing this match!

            CSteamID opponentSteamID = (SteamUser()->GetSteamID() == steamIDP1) ? steamIDP2 : steamIDP1;
        }
    }
}


void Match::OnLobbyEnter(LobbyEnter_t* pCallback) {
    if (pCallback->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess) {
        lobbyID = CSteamID(pCallback->m_ulSteamIDLobby);
    }
    else {
        lobbyID = k_steamIDNil;
    }
}