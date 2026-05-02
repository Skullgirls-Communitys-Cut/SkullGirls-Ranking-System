#include "../utils/CurlWrapper.h"

#include "Windows.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <thread>
#include <algorithm>

#include "steam/steam_api.h"
#include "json.hpp"
using json = nlohmann::json;

#include "match.h"
#include "../../env.h"
#include "../../SUPER_SECRET_KEY.h"
#include "../memory/memory.hpp"
#include "../process/process.h"
#include "../UI/UI.h"
#include "../utils/logger.h"

Match g_CurrentMatch;
//std::recursive_mutex Match::sendMutex;
CRITICAL_SECTION Match::sendCS;

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
    LogToFile("sendMatchInfo called");
    LogToFile("Before mutex lock");
    //std::lock_guard<std::recursive_mutex> lock(sendMutex); // если используется
    // Использование — идентично lock_guard:
    CSLock lock(sendCS); // захватит при создании, освободит автоматически
    LogToFile("After mutex lock");

    // if (!CanSendMatch) return false;
    if (!CanSendMatch) {
        LogToFile("CanSendMatch false");
        return false;
    }
    LogToFile("CanSendMatch OK");

    SUPER_SECRET_KEY.RequestTicket();
    LogToFile("RequestTicket called");

    const char* roomTypeStr = SteamMatchmaking()->GetLobbyData(lobbyID, "RoomType");
    int RoomType = (roomTypeStr && roomTypeStr[0]) ? atoi(roomTypeStr) : 0;
    if (RoomType != LOBBY_TYPE_ALL_PLAY or LOBBY_TYPE_QUICK_MATCH) return false;
    LogToFile("RoomType OK: " + std::to_string(RoomType));

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

    // ищем текущего оппонента из лобби
    auto it = std::find_if(m_lobbyMembers.begin(), m_lobbyMembers.end(),
        [&OppSteamID](const LobbyMember& m) {
            return m.steamID == OppSteamID;
        });

    // проверяем данные оппонента
    if (it == m_lobbyMembers.end() || it->rankedVersion.empty() || !it->rankedEnabled) {
        LogToFile("Opponent has no mod or ranked disabled, skipping");
        return false;
    }
    LogToFile("Opponent ranked check OK");

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
    LogToFile("ReadCharacterNames done");

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
    LogToFile("Starting thread");
    std::thread([request, url, path, OppSteamID, result, timestamp]() mutable {
        try {
            LogToFile("Thread started, waiting for ticket...");
            while (!SUPER_SECRET_KEY.GetHasTicket()) {
                Sleep(10);
            }
            LogToFile("Ticket received");

        request["key"] = SUPER_SECRET_KEY.GetCurrentTicketBase64();
        LogToFile("Key set, length: " + std::to_string(
            request["key"].get<std::string>().size()));
        std::string body = request.dump();
        LogToFile("JSON serialized, body size: " + std::to_string(body.size()));

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
        LogToFile("Request done, status: " + std::to_string(res.status));
        if (!res.success) {
            OutputDebugStringA("[DEBUG] POST request failed!\n");
            OutputDebugStringA(("[DEBUG] Status: " + std::to_string(res.status) + "\n").c_str());
            LogToFile("[DEBUG] POST request failed!\n");
            LogToFile("Status: " + std::to_string(res.status));
            LogToFile("Body: " + res.body);
        }
        
        RankUI::g_MatchHistory.AddMatch(OppSteamID, result, timestamp);
        LogToFile("AddMatch done");
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("[MATCH ERROR] Exception: " + std::string(e.what()) + "\n").c_str());
            LogToFile(("[MATCH ERROR] Exception: " + std::string(e.what()) + "\n").c_str());
        }
        catch (...) {
            OutputDebugStringA("[MATCH ERROR] Unknown exception in send thread\n");
            LogToFile("[MATCH ERROR] Unknown exception in send thread\n");
        }
        }).detach();
    LogToFile("Thread detached");
    
    return true;
}

void TestPostRequest() {
    std::string url = API_URL;
    std::string path = API_PATH;

    nlohmann::json testRequest;
    testRequest["version"] = "1.3";
    testRequest["matchId"] = 515748;
    testRequest["matchResult"] = 5;
    testRequest["timeStamp"] = std::time(nullptr); // свежий timestamp
    testRequest["key"] = "1400000068fedc32d82e01a6c066f50501001001fbb1e3691800000001000000050000002a4ead7b431752c89fdb370001000000060100008600000004000000c066f50501001001b2bd03006e13ea506501a8c000000000532fe269d3defd6901006f7900000e00b4bd03000000b5bd03000000b6bd03000000b7bd03000000b8bd03000000b9bd0300000016d406000000c889170000002ca317000000dcc517000000ddc517000000a69e1a0000004cd51f000000b694280000000000569a3b74bb14ac79495e5b510bc08c0d812cd0d60e22dc238919d9e334fd74c7a84887cb2dcd06ffddd12524344711cc4d359f589d085dfb2f73de797ae5ce6d95d759336f299aa117ec4d8deba1d3d1b9e1dff5b3905d3219d25b28124dfbe36b4f9908e8a3cc4f16d5bfefa01d1d2cfd26fc3900a7efadd5bd5d7912489f6d016ef97f0000f050b560410100003f03174a00000000d0328389f97f000078c77d89f97f0000704db56041010000cec87d89f97f0000e0eb9e1f0400000020ee9e1f04000000520000000000000000ee9e1f0400000052000000000000000212106df97f000020ee9e1f04000000f01eff6df97f0000f01eff6df97f0000d309bc8cf97f000050ed9e1f04000000138e816df97f000060ed9e1f0400000096d2816df97f000070328389f97f000078c77d89f97f0000b0f19e1f040000004726bc8cf97f0000c0f19e1f040000001bbe816df97f000060ed9e1f0400000060ec9e1f04000000faffffff0000000080ed9e1f0400000020ed9e1f04000000f01eff6df97f0000000000fd2c8e0000fa4b7c5f000000002c0df2d459ca000040a8be8cf97f0000250000000000000080bccc6df97f0000ffffffff000000000000000000000000000000000000000092a7816df97f00000000000000000000b5c87d89f97f0000ffffffff000000003f0000000000000006000000000000008338bc8cf97f00003e000000000000008bdb816df97f00003f00000000000000c0e4e16041010000c0e4e160410100003e000000000000002ef39e1f040000000001000000000000fee4e160410100003f00000000000000c0b9136141010000ff4bfd8300000000f0308389f97f0000e6a67d89f97f0000006f305d41010000d0f3f26df97f0000c00fb05f4101000001a97d89f97f0000000000000000000000ed9e1f040000000071f36041010000f14bdd80000000000000000000000000a62a02000000000090f29e1f040000004a6f556df97f000090f29e1f04000000000000000000000000e2f3604101000090288389f97f000090f29e1f04000000fd7a556df97f00000000000000000000000000000000000000e2f3604101000000000000000000000100000000000000a7b2586df97f0000010000000000000001e2f360410100000000886cf97f000000000000000000000000000000000000000000000000000000e2f36041010000ccf9ba6df97f0000656e74557064617465486f7374733a20000000000000000000608f604101000000000000000000003253586df97f00000000000000000000a62a02000000000080f39e1f040000004a6f556df97f000080f39e1f040000000000000000000000c0b5f3604101000090288389f97f000080f39e1f04000000fd7a556df97f0000000000000000000000000000000000002022b65f410100002500d11f0000000070358389f97f0000e6a67d89f97f0000010000000000000001b5f360410100000000886cf97f0000000000000000000000000000000000000000000000000000c0b5f36041010000ccf9ba6df97f00003da21d6141010000806518614101000000000000000000009ca47d89f97f000070358389f97f00000f00000000000000d00000000000000090288389f97f000080288389f97f00004da97d89f97f0000e0000000000000001e62106df97f00000000886cf97f0000fe0000000000000000000000000000009ca47d89f97f0000f0308389f97f0000e9f09e1f04000000df00000000000000b9f6a06cf97f0000f04b116141010000df03bf2a0000000090338389f97f0000e6a67d89f97f00006051f26041010000f34be88500000000a0abf36041010000004cf38600000000f0308389f97f0000e6a67d89f97f0000e9f09e1f0400000042000000000000001000000000000000100000000000000000000000000000009ca47d89f97f00000000000000000000f8ffffff000000000700000000000000000000000000000000000000000000009ca47d89f97f0000f0308389f97f00000000000000000000d8e4e1604101000090288389f97f000080288389f97f00004da97d89f97f000018000000000000001e62106df97f0000e8f19e1f040000000087456df97f0000d8e7e16041010000c0f19e1f040000000a00000000000000280a666df97f000004000000000000008489456df97f00000000000000000000d8e4e160410100006051f26041010000a47f656df97f0000c0e4e16041010000d8e4e1604101000000000000000000005681456df97f0000f0308389f97f0000fe01000000000000c8ede20d000000006bfc446df97f00000000000000000000f803656df97f00003df29e1f0400000010f29e1f04000000080000000000000000000000000000003c10f2d459ca00000000000000000000bc4fb560410100001b03456df97f00000500000004000000bc4fb56041010000f9f19e1f040000000bf1a76cf97f00003df29e1f04000000c8ede20d040000006c10f2d459ca0000c0e4e1604101000038f29e1f040000001b03456df97f0000c8ede20d1000103a38f29e1f0400000049f29e1f0400000000000000000000003ff29e1f040000003ff29e1f040000000000000000000000ba727e8900000000000000000000000008c8db8b6f1801000000000000000000000000000000000000000000000000000000000000000000000000000000000064000000000000800000000000000000ba727e89f97f0000cc10f2d459ca0000e6a67d89f97f000000f6e1604101000040f49e1f04000000b0f39e1f04000000a6fa576df97f0000c0f3136141010000c0e4e1604101000000f6e16041010000770334dc00000000b0318389f97f0000e6a67d89f97f0000130000000000000088f49e1f0400000000e5e1604101000000e5e1604101000000d81561410100006f6442da0000000000e5e16041010000f7cc576df97f000078f59f1f040000000100000000000000244d6225000000009ca47d89f97f0000b0318389f97f000070f59f1f04000000000000000000000090288389f97f000080288389f97f00004da97d89f97f000040000000000000001e62106df97f00006051f26041010000ff4bf58500000000f0308389f97f000078c77d89f97f0000c0e2f36041010000cec87d89f97f00000000000000000000e3f5656df97f000040f49e1f0400000000000000000000000100000000000000244d62250000000070f59f1f0400000000dc576df97f000040f49e1f0400000078f59f1f"; // пример ключа

    testRequest["myInfo"]["steamId"] = 76561198060234432;
    testRequest["myInfo"]["characters"] = { "Valentine", "RoboFortune" };
    testRequest["myInfo"]["region"] = 12;

    testRequest["opponentInfo"]["steamId"] = 76561198359406570;
    testRequest["opponentInfo"]["characters"] = { "Parasoul", "BigBand" };
    testRequest["opponentInfo"]["region"] = 12;

    std::string body = testRequest.dump();
    OutputDebugStringA(("[TEST] Sending: " + testRequest.dump(4) + "\n").c_str());
    LogToFile(("[TEST] Sending: " + testRequest.dump(4) + "\n").c_str());

    auto res = CurlWrapper::Request(url + path, "POST", body, "application/json");

    OutputDebugStringA(("[TEST] Status: " + std::to_string(res.status) + "\n").c_str());
    OutputDebugStringA(("[TEST] Body: " + res.body + "\n").c_str());
    OutputDebugStringA(res.success ? "[TEST] SUCCESS\n" : "[TEST] FAILED\n");
    LogToFile(("[TEST] Status: " + std::to_string(res.status) + "\n").c_str());
    LogToFile(("[TEST] Body: " + res.body + "\n").c_str());
    LogToFile(res.success ? "[TEST] SUCCESS\n" : "[TEST] FAILED\n");
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

        SteamMatchmaking()->SetLobbyMemberData(lobbyID, "ranked_version", VERSION);
        SteamMatchmaking()->SetLobbyMemberData(
            lobbyID, "ranked_enabled", CanSendMatch ? "1" : "0");

        // читаем тех кто уже есть в лобби используя m_lobbyMembers для сохранения всех игроков
        m_lobbyMembers.clear();
        int memberCount = SteamMatchmaking()->GetNumLobbyMembers(lobbyID);
        for (int i = 0; i < memberCount; i++) {
            CSteamID memberID = SteamMatchmaking()->GetLobbyMemberByIndex(lobbyID, i);

            const char* version = SteamMatchmaking()->GetLobbyMemberData(
                lobbyID, memberID, "ranked_version");
            const char* ranked = SteamMatchmaking()->GetLobbyMemberData(
                lobbyID, memberID, "ranked_enabled");

            LobbyMember member;
            member.steamID = memberID;
            member.rankedVersion = (version && strlen(version) > 0) ? version : "";
            member.rankedEnabled = (ranked && strcmp(ranked, "1") == 0);
            m_lobbyMembers.push_back(member);
        }
    }
    else {
        lobbyID = k_steamIDNil;
        m_lobbyMembers.clear();
    }
}

void Match::OnLobbyChatUpdate(LobbyChatUpdate_t* pCallback){
    // Кто изменил состояние
    CSteamID changedUser(pCallback->m_ulSteamIDUserChanged);

    if (pCallback->m_rgfChatMemberStateChange & k_EChatMemberStateChangeEntered) {
        // Читаем данные нового игрока и добавляем в список
        const char* version = SteamMatchmaking()->GetLobbyMemberData(
            lobbyID, changedUser, "ranked_version");
        const char* ranked = SteamMatchmaking()->GetLobbyMemberData(
            lobbyID, changedUser, "ranked_enabled");

        LobbyMember member;
        member.steamID = changedUser;
        member.rankedVersion = (version && strlen(version) > 0) ? version : "";
        member.rankedEnabled = (ranked && strcmp(ranked, "1") == 0);
        m_lobbyMembers.push_back(member);
    }
    else if (pCallback->m_rgfChatMemberStateChange &
        (k_EChatMemberStateChangeLeft | k_EChatMemberStateChangeDisconnected)) {
        // Удаляем вышедшего игрока из списка
        m_lobbyMembers.erase(
            std::remove_if(m_lobbyMembers.begin(), m_lobbyMembers.end(),
                [&changedUser](const LobbyMember& m) {
                    return m.steamID == changedUser;
                }),
            m_lobbyMembers.end()
        );
    }
}

void Match::OnLobbyDataUpdate(LobbyDataUpdate_t* pCallback) {
    // m_bSuccess — данные обновились успешно
    // m_ulSteamIDMember — чьи данные изменились
    // m_ulSteamIDLobby — какое лобби

    if (!pCallback->m_bSuccess) return;

    CSteamID changedMember(pCallback->m_ulSteamIDMember);

    // Находим игрока в списке и обновляем его данные
    for (auto& member : m_lobbyMembers) {
        if (member.steamID == changedMember) {
            const char* ranked = SteamMatchmaking()->GetLobbyMemberData(
                lobbyID, changedMember, "ranked_enabled");
            member.rankedEnabled = (ranked && strcmp(ranked, "1") == 0);
            break;
        }
    }
}