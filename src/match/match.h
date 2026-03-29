#pragma once

#include "steam/steam_api.h"
#include <stddef.h>
#include "Windows.h"
#include <mutex>

#include "json.hpp"

#define MAX_PLAYABLE_CHARACTERS 6

class Match {
public:
    
    bool sendMatchInfo();
    void updateCounter() { matchCount++; }
    void SetCanSendMatch(bool NewValue) { CanSendMatch = NewValue; }
    CSteamID getLobbyID(){ return lobbyID; }

private:
    STEAM_CALLBACK(Match, OnLobbyChatMessage, LobbyChatMsg_t); // коллбек сообщений в чате лобби
    STEAM_CALLBACK(Match, OnLobbyEnter, LobbyEnter_t); //коллбек входа в лобби

    CSteamID lobbyID;
    CSteamID player1SteamID;
    CSteamID player2SteamID;
    uint32_t rng0 = 0;
    void ReadCharacterNames();
    nlohmann::json GenerateCharacterNames(bool WeAreFirstPlayer) const;
    std::optional<std::string> Character_Names[MAX_PLAYABLE_CHARACTERS];

    int matchCount = 0;  

    bool CanSendMatch = true;

    static std::recursive_mutex sendMutex;
};

extern Match g_CurrentMatch;