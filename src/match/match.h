#pragma once

#include "steam/steam_api.h"
#include <stddef.h>
#include "Windows.h"

#include "json.hpp"
#include "../utils/cs_lock.h"

#define MAX_PLAYABLE_CHARACTERS 6
void TestPostRequest();

struct LobbyMember {
    CSteamID steamID;
    bool rankedEnabled;
    std::string rankedVersion;
};

class Match {
public:
    
    bool sendMatchInfo();
    void updateCounter() { matchCount++; }
    void SetCanSendMatch(bool NewValue) { CanSendMatch = NewValue; }
    CSteamID getLobbyID(){ return lobbyID; }
    void Init() { InitializeCriticalSection(&sendCS); }
    const std::vector<LobbyMember>& GetLobbyMembers() const { return m_lobbyMembers; }

private:
    STEAM_CALLBACK(Match, OnLobbyChatMessage, LobbyChatMsg_t); // коллбек сообщений в чате лобби
    STEAM_CALLBACK(Match, OnLobbyEnter, LobbyEnter_t); //коллбек входа в лобби
    STEAM_CALLBACK(Match, OnLobbyChatUpdate, LobbyChatUpdate_t); // коллбек для обновления чата лобби при входе/выходе игроков
    STEAM_CALLBACK(Match, OnLobbyDataUpdate, LobbyDataUpdate_t); // коллбек для обновления данных лобби при изменении метаданных

    CSteamID lobbyID;
    CSteamID player1SteamID;
    CSteamID player2SteamID;
    uint32_t rng0 = 0;
    void ReadCharacterNames();
    nlohmann::json GenerateCharacterNames(bool WeAreFirstPlayer) const;
    std::optional<std::string> Character_Names[MAX_PLAYABLE_CHARACTERS];
    std::vector<LobbyMember> m_lobbyMembers;

    int matchCount = 0;  

    bool CanSendMatch = true;

    static CRITICAL_SECTION sendCS;
};

extern Match g_CurrentMatch;