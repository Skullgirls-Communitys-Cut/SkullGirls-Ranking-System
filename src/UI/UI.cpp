#include <Windows.h>
#include <d3d9.h>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_dx9.h"
#include "imgui/backends/imgui_impl_win32.h"

#include "UI.h"
#include "d3d9Wrapper.h"
#include "../match/match.h"
#include "../../env.h"
#include "../../src/main_thread/main_thread.h"
#include "../utils/logger.h"
#include "../utils/cs_lock.h"
#include "steam/steam_api.h"


namespace imgui_show {
    bool Show_Window = false;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ----------------------------------------------------------------------------
// Реализация MatchHistory
// ----------------------------------------------------------------------------
MatchHistory::MatchHistory() : m_device(nullptr) {
    // Подписка на Steam-колбэк автоматически создаётся через STEAM_CALLBACK
    InitializeCriticalSection(&m_cs);
}

MatchHistory::~MatchHistory() {
    Clear();
    DeleteCriticalSection(&m_cs);
}

void MatchHistory::Init(IDirect3DDevice9* device) {
    m_device = device;
}

void MatchHistory::InvalidateDeviceObjects() {
    CSLock lock(m_cs);
    for (auto& rec : m_history) {
        if (rec.avatarTex) {
            rec.avatarTex->Release();
            rec.avatarTex = nullptr;
        }
        rec.avatarRequested = false;   // разрешаем повторный запрос аватара
    }
}

void MatchHistory::RestoreDeviceObjects(IDirect3DDevice9* device) {
    CSLock lock(m_cs);
    m_device = device;
    for (auto& rec : m_history) {
        RequestAvatarForRecord(rec);   // заново запрашиваем аватар (асинхронно)
    }
}

void MatchHistory::AddMatch(CSteamID oppID, int result, long long timestamp, bool confirmed, int httpStatus, const std::string& serverStatus, const std::string& serverMsg) {
    CSLock lock(m_cs);
    Record rec;
    rec.oppID = oppID;
    rec.matchResult = result;
    rec.avatarTex = nullptr;
    rec.avatarRequested = false;
    rec.timestamp = timestamp;
    rec.confirmed = confirmed;
    rec.httpStatus = httpStatus;
    rec.serverStatus = serverStatus;
    rec.serverMsg = serverMsg;
    // Получаем ник сразу (синхронно)
    const char* name = SteamFriends()->GetFriendPersonaName(oppID);
    rec.nickname = name ? name : "Unknown";

    // Запрашиваем аватар (асинхронно)
    RequestAvatarForRecord(rec);

    m_history.push_back(rec);
    // Опционально: ограничить размер истории, например, 20 записей
    const size_t MAX_HISTORY = 20;
    if (m_history.size() > MAX_HISTORY) {
        // Освобождаем текстуру самой старой записи
        if (m_history.front().avatarTex) {
            m_history.front().avatarTex->Release();
        }
        m_history.erase(m_history.begin());
    }
}

void MatchHistory::RequestAvatarForRecord(Record& record) {
    CSLock lock(m_cs);
    if (record.avatarRequested) return;
    record.avatarRequested = true;

    int avatarHandle = SteamFriends()->GetLargeFriendAvatar(record.oppID);
    if (avatarHandle == -1) {
        // Аватар ещё не загружен, ждём колбэк
        return;
    }
    // Аватар уже загружен в кэш Steam
    uint32 width, height;
    if (SteamUtils()->GetImageSize(avatarHandle, &width, &height)) {
        std::vector<uint8> rgba(width * height * 4);
        if (SteamUtils()->GetImageRGBA(avatarHandle, rgba.data(), rgba.size())) {
            CreateTextureFromRGBA(rgba.data(), width, height, &record.avatarTex);
        }
    }
}

void MatchHistory::CreateTextureFromRGBA(const uint8* rgba, uint32 width, uint32 height, IDirect3DTexture9** outTex) {
    if (!m_device || !outTex || !rgba) return;
    *outTex = nullptr;

    // Создаём текстуру в формате A8R8G8B8 (в памяти: B, G, R, A)
    HRESULT hr = m_device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, outTex, nullptr);
    if (FAILED(hr)) return;

    D3DLOCKED_RECT lockedRect;
    if (SUCCEEDED((*outTex)->LockRect(0, &lockedRect, nullptr, 0))) {
        uint8* dest = static_cast<uint8*>(lockedRect.pBits);
        const uint8* src = rgba;
        const uint32 srcPitch = width * 4; // байт на строку исходных данных

        for (uint32 y = 0; y < height; ++y) {
            uint8* destRow = dest;
            const uint8* srcRow = src;

            for (uint32 x = 0; x < width; ++x) {
                destRow[0] = srcRow[2]; // B
                destRow[1] = srcRow[1]; // G
                destRow[2] = srcRow[0]; // R
                destRow[3] = srcRow[3]; // A

                destRow += 4;
                srcRow += 4;
            }

            dest += lockedRect.Pitch;   
            src += srcPitch;            
        }

        (*outTex)->UnlockRect(0);
    }
}

void MatchHistory::OnAvatarImageLoaded(AvatarImageLoaded_t* pParam) {
    CSLock lock(m_cs);
    // Ищем запись с таким SteamID
    for (auto& rec : m_history) {
        if (rec.oppID == pParam->m_steamID) {
            // Если текстура уже существует (не должно быть, но на всякий случай)
            if (rec.avatarTex) {
                rec.avatarTex->Release();
                rec.avatarTex = nullptr;
            }
            // Получаем данные аватара
            uint32 width, height;
            if (SteamUtils()->GetImageSize(pParam->m_iImage, &width, &height)) {
                std::vector<uint8> rgba(width * height * 4);
                if (SteamUtils()->GetImageRGBA(pParam->m_iImage, rgba.data(), rgba.size())) {
                    CreateTextureFromRGBA(rgba.data(), width, height, &rec.avatarTex);
                }
            }
            break;
        }
    }
}
void MatchHistory::RenderHistory() const {
    CSLock lock(m_cs);
    if (m_history.empty()) {
        ImGui::Text("No matches yet.");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Match History:");
    ImGui::BeginChild("HistoryList", ImVec2(0, 200), true);
    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& rec = m_history[i];
        // Аватар (32x32)
        if (rec.avatarTex) {
            ImGui::PushID(static_cast<int>(i));
            if(ImGui::ImageButton("##avatar", rec.avatarTex, ImVec2(32, 32))) {
                SteamFriends()->ActivateGameOverlayToUser("steamid", rec.oppID);
            }
            ImGui::PopID();
        }
        else {
            ImGui::Dummy(ImVec2(32, 32));
        }
        ImGui::SameLine();

        // Группа: ник и результат под ним
        ImGui::BeginGroup();
        ImGui::Text("Opponent: %s", rec.nickname.c_str());
        switch (rec.matchResult) {
        case 1: ImGui::TextColored(ImVec4(0, 1, 0, 1), "You win via Timeout"); break;
        case 2: ImGui::TextColored(ImVec4(1, 0, 0, 1), "You lose via Timeout"); break;
        case 3: ImGui::TextColored(ImVec4(1, 1, 0, 1), "Draw via Timeout"); break;
        case 4: ImGui::TextColored(ImVec4(0, 1, 0, 1), "You win"); break;
        case 5: ImGui::TextColored(ImVec4(1, 0, 0, 1), "You lose"); break;
        case 6: ImGui::TextColored(ImVec4(1, 1, 0, 1), "Draw"); break;
        default: ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Unknown result"); break;
        }
        if (!rec.confirmed) {
            ImGui::SameLine();
            if (!rec.serverStatus.empty())
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "(Error %d - %s)", rec.httpStatus, rec.serverStatus.c_str());
            else
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "(Error %d)", rec.httpStatus);
            if (!rec.serverMsg.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.5f, 1), "%s", rec.serverMsg.c_str());
            }
        }
        ImGui::EndGroup();

        // --- Timestamp (прижат к правому краю) ---
        // Форматируем timestamp в локальную строку
        std::time_t t = static_cast<std::time_t>(rec.timestamp); // предполагается, что timestamp совместим с time_t
        std::tm tm = *std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%d.%m.%Y %H:%M");
        std::string timeStr = oss.str();

        // Вычисляем позицию для правого выравнивания
        float availWidth = ImGui::GetContentRegionAvail().x;
        float textWidth = ImGui::CalcTextSize(timeStr.c_str()).x;
        float cursorX = ImGui::GetCursorPosX();
        // Если места недостаточно, переходим на новую строку (но в нашем случае строка обычно широкая)
        if (cursorX + textWidth > availWidth) {
            ImGui::NewLine();
            cursorX = ImGui::GetCursorPosX();
        }
        ImGui::SameLine(availWidth - textWidth); // прижимаем к правому краю
        ImGui::Text("%s", timeStr.c_str());

        ImGui::Separator();
    }
    ImGui::EndChild();
}

void MatchHistory::Clear() {
    CSLock lock(m_cs);
    for (auto& rec : m_history) {
        if (rec.avatarTex) {
            rec.avatarTex->Release();
        }
    }
    m_history.clear();

}


namespace RankUI {

    MatchHistory g_MatchHistory;
    LRESULT CALLBACK Input(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // Контекст у ImGui глобальный, а модов с оверлеем может быть несколько.
        if (!g_ImGuiContext) return CallWindowProc(OriginalWndProc, hWnd, uMsg, wParam, lParam);
        ImGui::SetCurrentContext(g_ImGuiContext);

        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

        ImGuiIO& io = ImGui::GetIO();
        if (uMsg == WM_KEYDOWN && wParam == VK_F3) {
            imgui_show::Show_Window = !imgui_show::Show_Window;
            return 0;
        }

        if (((uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) && io.WantCaptureMouse))
            return true;

        if (((uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) && io.WantCaptureKeyboard))
            return true;

        return CallWindowProc(OriginalWndProc, hWnd, uMsg, wParam, lParam);
    }

    void Render() {
        if (!g_ImGuiContext) return;
        ImGui::SetCurrentContext(g_ImGuiContext);

        static bool checked = true;
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();
        if (imgui_show::Show_Window) {

            

            ImGui::Begin("Skullgirls Ranking System", &imgui_show::Show_Window);
        
            if (NeedUpdate) {
                ImGui::Text("Please update Ranked Mod from");
                ImGui::SameLine();
                ImGui::TextLinkOpenURL("github page", "https://github.com/Skullgirls-Communitys-Cut/SkullGirls-Ranking-System/releases/latest");
                ImGui::TextDisabled("Current version: %s", VERSION);
                ImGui::End();
            }
            else {
                if (ImGui::Checkbox("Ranked Mode", &checked)) {
                    g_CurrentMatch.SetCanSendMatch(checked);
                }
                ImGui::Text("Turn on or off ranking matches.");

                CSteamID lobbyID = g_CurrentMatch.getLobbyID();
                int RoomType = g_CurrentMatch.GetRoomType();
				//LogToFile("[UI] Current RoomType: %d", RoomType);
                if (!lobbyID.IsValid() ||
                    (RoomType != LOBBY_TYPE_ALL_PLAY && RoomType != LOBBY_TYPE_QUICK_MATCH)) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "You are playing in the wrong lobby type!"); // -1
                }

                ImGui::Separator();

                // Отображаем историю матчей
                g_MatchHistory.RenderHistory();

                // --- Simple Lobby Test Widget ---
                if (ImGui::Button("Create Lobby Test")) {
                    LogToFile("Create Lobby Test button clicked");
                    g_CurrentMatch.CreateLobby();
                }
                ImGui::SameLine();
                
                CSteamID currentLobbyID = g_CurrentMatch.getLobbyID();
                if (currentLobbyID.IsValid()) {
                    ImGui::TextDisabled("Lobby: %s", std::to_string(currentLobbyID.ConvertToUint64()).c_str());
                    if (ImGui::Button("Join Test Lobby")) {
                        LogToFile("Join Test Lobby button clicked");
                        // Quick test: join our own lobby (just for testing)
                        g_CurrentMatch.JoinLobby(currentLobbyID);
                    }
                }
                ImGui::Text("");

                // --- Тест POST-запроса ---
#ifdef _DEBUG
                if (ImGui::Button("Test POST Request")) {
                    LogToFile("Test POST button clicked");
                    TestPostRequest();
                }
#endif
                // -------------------------

                //for (const auto& member : g_CurrentMatch.GetLobbyMembers()) {
                //    // отображаем member.steamID и member.rankedEnabled
                //}

                ImGui::TextDisabled("Version: %s", VERSION);

                ImGui::End();
            }
        }
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
}