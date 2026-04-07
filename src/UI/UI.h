#pragma once

#include "steam/steam_api.h"
#include <vector>
#include <string>
#include <d3d9.h>
// Предварительное объявление для IDirect3DTexture9
struct IDirect3DTexture9;



class MatchHistory {
public:
    struct Record {
        CSteamID oppID;                 // SteamID оппонента
        int matchResult;                // 1 = победа, 0 = поражение
        std::string nickname;           // ник оппонента
        IDirect3DTexture9* avatarTex;   // текстура аватара (DX9)
        bool avatarRequested;           // флаг, что аватар уже запрошен
        long long timestamp;
    };

    MatchHistory();
    ~MatchHistory();
    void InvalidateDeviceObjects();   // освободить все текстуры
    void RestoreDeviceObjects(IDirect3DDevice9* device); // пересоздать их заново
    // Инициализация с указателем на устройство DX9 (для создания текстур)
    void Init(IDirect3DDevice9* device);

    // Добавить результат матча (вызывать, когда сервер подтвердил запись)
    void AddMatch(CSteamID oppID, int result, long long timestamp);

    // Отрисовать историю в текущем окне ImGui
    void RenderHistory() const;

    // Очистить историю (освобождая текстуры)
    void Clear();

private:
    // Steam callback при загрузке аватара
    STEAM_CALLBACK(MatchHistory, OnAvatarImageLoaded, AvatarImageLoaded_t);

    // Вспомогательные методы
    void RequestAvatarForRecord(Record& record);
    void CreateTextureFromRGBA(const uint8* rgba, uint32 width, uint32 height, IDirect3DTexture9** outTex);

    IDirect3DDevice9* m_device;         // устройство DX9
    std::vector<Record> m_history;      // список матчей
};

namespace RankUI {
    // Класс для управления историей матчей
    // Глобальные функции окна (оставляем как есть)
    LRESULT CALLBACK Input(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void Render();

    // Глобальный экземпляр менеджера истории (extern, определён в .cpp)
    extern MatchHistory g_MatchHistory;
}