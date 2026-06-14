#include <Windows.h>
#include <d3d9.h>
#include <math.h>
#include <iostream>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_dx9.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "UI.h"
#include "lvgl.h"

#include "d3d9Wrapper.h"
#include "../match/match.h"
#include "../main_thread/main_thread.h"
#include "../../env.h"

#pragma comment(lib, "d3d9.lib")

// Указатели на оригинальные функции
static HMODULE originalD3D9 = nullptr;

// Типы для оригинальных функций
typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

// Оригинальные функции
Present_t OriginalPresent = nullptr;
Reset_t OriginalReset = nullptr;
CreateDevice_t OriginalCreateDevice = nullptr;

// Для перехвата оконных сообщений
WNDPROC OriginalWndProc = nullptr;
HWND g_hWindow = nullptr;

// LVGL state
static lv_display_t* g_lvgl_display = nullptr;
static IDirect3DTexture9* g_lvgl_texture = nullptr;
static uint8_t* g_lvgl_buf1 = nullptr;
static bool g_lvgl_initialized = false;
static uint32_t g_lvgl_width = 0;
static uint32_t g_lvgl_height = 0;
static uint64_t g_last_tick = 0;
bool g_ui_visible = false; // F3 toggle (extern in .h) — начинаем скрытым, как и ImGui
static bool g_lvgl_render_ready = false; // LVGL рендерится только после первого успешного Present

// LVGL mouse state
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static bool g_mouse_pressed = false;
static lv_indev_t* g_lvgl_indev = nullptr;

// LVGL indev read callback — вызывается каждый кадр для опроса состояния мыши
static void mouse_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    data->point.x = g_mouse_x;
    data->point.y = g_mouse_y;
    data->state = g_mouse_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// --- LVGL UI ---
static lv_obj_t* g_panel = nullptr;
static lv_obj_t* g_checkbox = nullptr;
static lv_obj_t* g_warning = nullptr;
static lv_obj_t* g_version = nullptr;
static lv_obj_t* g_update_label = nullptr;

static void checkbox_event_cb(lv_event_t* e) {
    lv_obj_t* cb = (lv_obj_t*)lv_event_get_target(e);
    bool checked = lv_obj_get_state(cb) & LV_STATE_CHECKED;
    g_CurrentMatch.SetCanSendMatch(checked);
}

// Drag panel by pointer movement (LVGL 9.5 не имеет встроенного DRAGGABLE-флага)
// Работает через глобальные координаты мыши, не зависит от событий LVGL
static bool g_drag_active = false;
static int g_drag_off_x = 0, g_drag_off_y = 0; // смещение курсора относительно угла панели
static bool g_mouse_prev = false; // предыдущее состояние кнопки для детекта фронта

static void lvgl_build_ui() {
    if (g_panel) return;

    // Делаем фон экрана прозрачным, чтобы не перекрывать игру
    lv_obj_set_style_bg_opa((lv_obj_t*)lv_screen_active(), LV_OPA_TRANSP, 0);

    g_panel = (lv_obj_t*)lv_obj_create((lv_obj_t*)lv_screen_active());
    lv_obj_set_size(g_panel, 350, 420);
    lv_obj_align(g_panel, LV_ALIGN_TOP_LEFT, 10, 10);
    // Менее прозрачный фон (90% вместо 80%), чтобы текст читался
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(g_panel, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(g_panel, 8, 0);
    lv_obj_set_style_pad_all(g_panel, 10, 0);
    lv_obj_set_style_border_width(g_panel, 1, 0);
    lv_obj_set_style_border_color(g_panel, lv_color_hex(0x444444), 0);
    // Светлый текст для всех дочерних элементов по умолчанию
    lv_obj_set_style_text_color(g_panel, lv_color_hex(0xCCCCCC), 0);

    int y = 10;

    // Update notice (hidden by default)
    g_update_label = (lv_obj_t*)lv_label_create(g_panel);
    lv_label_set_text(g_update_label, "Please update Ranked Mod!");
    lv_obj_set_style_text_color(g_update_label, lv_color_hex(0xFF4444), 0);
    lv_obj_set_pos(g_update_label, 10, y);
    lv_obj_add_flag(g_update_label, LV_OBJ_FLAG_HIDDEN);
    y += 25;

    // Checkbox
    g_checkbox = (lv_obj_t*)lv_checkbox_create(g_panel);
    lv_checkbox_set_text(g_checkbox, "Ranked Mode");
    lv_obj_set_pos(g_checkbox, 10, y);
    lv_obj_add_state(g_checkbox, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_checkbox, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    y += 35;

    // Info label
    lv_obj_t* info = (lv_obj_t*)lv_label_create(g_panel);
    lv_label_set_text(info, "Turn on or off ranking matches.");
    lv_obj_set_pos(info, 10, y);
    y += 25;

    // Wrong lobby warning (hidden by default)
    g_warning = (lv_obj_t*)lv_label_create(g_panel);
    lv_label_set_text(g_warning, "You are playing in the wrong lobby type!");
    lv_obj_set_style_text_color(g_warning, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_pos(g_warning, 10, y);
    lv_obj_add_flag(g_warning, LV_OBJ_FLAG_HIDDEN);
    y += 30;

    // Match history will be added here in step 8

    // Version (bottom-left)
    g_version = (lv_obj_t*)lv_label_create(g_panel);
    char ver[64];
    snprintf(ver, sizeof(ver), "Version: %s", VERSION);
    lv_label_set_text(g_version, ver);
    lv_obj_set_style_text_opa(g_version, LV_OPA_50, 0);
    lv_obj_align(g_version, LV_ALIGN_BOTTOM_LEFT, 10, -5);
}

static void lvgl_update_ui() {
    if (!g_panel) return;

    // Update notice
    if (NeedUpdate)
        lv_obj_remove_flag(g_update_label, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_update_label, LV_OBJ_FLAG_HIDDEN);

    // Wrong lobby type warning
    CSteamID lobbyID = g_CurrentMatch.getLobbyID();
    int roomType = g_CurrentMatch.GetRoomType();
    bool wrongLobby = !lobbyID.IsValid() ||
        (roomType != LOBBY_TYPE_ALL_PLAY && roomType != LOBBY_TYPE_QUICK_MATCH);
    if (wrongLobby && g_ui_visible)
        lv_obj_remove_flag(g_warning, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_warning, LV_OBJ_FLAG_HIDDEN);
}

// Наша обработка оконных сообщений
LRESULT CALLBACK Hooked_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // Отслеживаем состояние мыши для LVGL
    switch (uMsg) {
    case WM_MOUSEMOVE:
        g_mouse_x = LOWORD(lParam);
        g_mouse_y = HIWORD(lParam);
        break;
    case WM_LBUTTONDOWN:
        g_mouse_pressed = true;
        break;
    case WM_LBUTTONUP:
        g_mouse_pressed = false;
        break;
    }

    return RankUI::Input(hWnd, uMsg, wParam, lParam);
}


// LVGL flush callback — вызывается LVGL после полной отрисовки кадра
static void lvgld9_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (g_lvgl_texture && g_lvgl_width > 0 && g_lvgl_height > 0) {
        D3DLOCKED_RECT lr;
        if (g_lvgl_texture->LockRect(0, &lr, NULL, D3DLOCK_DISCARD) == D3D_OK) {
            // lv_color32_t = {B, G, R, A} = D3DFMT_A8R8G8B8 на little-endian x86
            // Копируем построчно — Pitch текстуры может отличаться от width*4
            uint8_t* dst = (uint8_t*)lr.pBits;
            uint8_t* src = px_map;
            for (uint32_t y = 0; y < g_lvgl_height; ++y) {
                memcpy(dst, src, g_lvgl_width * 4);
                dst += lr.Pitch;
                src += g_lvgl_width * 4;
            }
            g_lvgl_texture->UnlockRect(0);
        }
    }
    lv_display_flush_ready(disp);
}

// Наши функции-перехватчики
HRESULT STDMETHODCALLTYPE Hooked_Present(IDirect3DDevice9* pDevice, CONST RECT* pSourceRect,
    CONST RECT* pDestRect, HWND hDestWindowOverride,
    CONST RGNDATA* pDirtyRegion)
{
    // Проверяем, не потеряно ли устройство
    HRESULT hr = pDevice->TestCooperativeLevel();
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) {
        return OriginalPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    // Первый Present после старта/Reset — ничего не рендерим, 
    // даём D3D9-устройству стабилизироваться
    if (!g_lvgl_render_ready) {
        g_lvgl_render_ready = true;
        return OriginalPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    // --- LVGL: tick + timer handler + quad ---
    if (g_lvgl_initialized && g_ui_visible) {
        // Delta time
        uint64_t now = GetTickCount64();
        if (g_last_tick == 0) g_last_tick = now;
        uint32_t elapsed = (uint32_t)(now - g_last_tick);
        g_last_tick = now;
        if (elapsed > 100) elapsed = 33; // защита от рывков при сворачивании

        lvgl_update_ui();
        lv_tick_inc(elapsed);
        lv_timer_handler();

        // --- Drag panel мышью (глобальные координаты, не зависит от событий LVGL) ---
        {
            bool pressed = g_mouse_pressed;
            if (pressed && !g_mouse_prev && g_panel) {
                // Только что нажали — проверяем, попадает ли курсор на панель
                lv_coord_t px = lv_obj_get_x(g_panel);
                lv_coord_t py = lv_obj_get_y(g_panel);
                lv_coord_t pw = lv_obj_get_width(g_panel);
                lv_coord_t ph = lv_obj_get_height(g_panel);
                if (g_mouse_x >= px && g_mouse_x < px + pw &&
                    g_mouse_y >= py && g_mouse_y < py + ph) {
                    g_drag_active = true;
                    g_drag_off_x = g_mouse_x - px;
                    g_drag_off_y = g_mouse_y - py;
                }
            }
            else if (!pressed && g_mouse_prev) {
                g_drag_active = false;
            }

            if (g_drag_active && g_panel) {
                lv_obj_set_pos(g_panel,
                    g_mouse_x - g_drag_off_x,
                    g_mouse_y - g_drag_off_y);
            }

            g_mouse_prev = pressed;
        }

        // Рендерим LVGL фреймбуфер как текстурированный quad поверх игры
        if (g_lvgl_texture) {
            // Настраиваем alpha blending
            pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

            // Отключаем Z для overlay
            pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

            pDevice->SetTexture(0, g_lvgl_texture);
            pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

            float w = (float)g_lvgl_width;
            float h = (float)g_lvgl_height;

            struct QuadVertex { float x, y, z, rhw; float u, v; };
            QuadVertex verts[4] = {
                { 0,   0,   0, 1, 0, 0 },
                { w,   0,   0, 1, 1, 0 },
                { 0,   h,   0, 1, 0, 1 },
                { w,   h,   0, 1, 1, 1 },
            };

            pDevice->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
            pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
            pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

            pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(QuadVertex));
        }
    }

    // --- ImGui (параллельно, пока миграция не завершена) ---
    RankUI::Render();
    return OriginalPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}


HRESULT STDMETHODCALLTYPE Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    g_lvgl_render_ready = false; // сброс — ждём первый Present после Reset

    // Освобождаем LVGL D3D9-текстуру и буфер фреймбуфера
    if (g_lvgl_texture) {
        g_lvgl_texture->Release();
        g_lvgl_texture = nullptr;
    }
    free(g_lvgl_buf1);
    g_lvgl_buf1 = nullptr;

    ImGui_ImplDX9_InvalidateDeviceObjects();
    RankUI::g_MatchHistory.InvalidateDeviceObjects();   // ОСВОБОДИТЬ СТАРЫЕ ТЕКСТУРЫ

    HRESULT hr = OriginalReset(pDevice, pPresentationParameters);
    if (SUCCEEDED(hr))
    {
        // Обновляем размеры LVGL
        g_lvgl_width = pPresentationParameters->BackBufferWidth;
        g_lvgl_height = pPresentationParameters->BackBufferHeight;

        // Пересоздаём буфер фреймбуфера под новый размер
        size_t buf_size = g_lvgl_width * g_lvgl_height * sizeof(lv_color32_t);
        g_lvgl_buf1 = (uint8_t*)calloc(1, buf_size);

        // Обновляем LVGL display: размер + буфер
        if (g_lvgl_display) {
            lv_display_set_resolution(g_lvgl_display, g_lvgl_width, g_lvgl_height);
            lv_display_set_buffers(g_lvgl_display, g_lvgl_buf1, nullptr, (uint32_t)buf_size,
                LV_DISPLAY_RENDER_MODE_FULL);
        }

        // Пересоздаём LVGL D3D9-текстуру под новый размер
        if (FAILED(pDevice->CreateTexture(g_lvgl_width, g_lvgl_height, 1, D3DUSAGE_DYNAMIC,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_lvgl_texture, nullptr)))
        {
            g_lvgl_texture = nullptr;
        }

        ImGui_ImplDX9_CreateDeviceObjects();
        RankUI::g_MatchHistory.RestoreDeviceObjects(pDevice);   // ПЕРЕСОЗДАТЬ ТЕКСТУРЫ
    }
    return hr;
}

// Функция для перехвата методов устройства
void HookDevice(IDirect3DDevice9* pDevice)
{
    // Получаем указатель на таблицу виртуальных методов
    void** vTable = *((void***)pDevice);

    // Сохраняем оригинальные указатели
    OriginalPresent = (Present_t)vTable[17]; // Present обычно имеет индекс 17
    OriginalReset = (Reset_t)vTable[16];     // Reset обычно имеет индекс 16
    // Меняем защиту памяти для записи
    DWORD oldProtect;
    VirtualProtect(&vTable[17], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    VirtualProtect(&vTable[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);

    // Заменяем указатели на наши функции
    vTable[17] = (void*)Hooked_Present;
    vTable[16] = (void*)Hooked_Reset;
    // Восстанавливаем защиту
    VirtualProtect(&vTable[17], sizeof(void*), oldProtect, &oldProtect);
    VirtualProtect(&vTable[16], sizeof(void*), oldProtect, &oldProtect);
}

// Перехваченный CreateDevice
HRESULT STDMETHODCALLTYPE Hooked_CreateDevice(IDirect3D9* pD3D, UINT Adapter, D3DDEVTYPE DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface)
{
    // Вызываем оригинальный CreateDevice
    HRESULT hr = OriginalCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
        pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr))
    {
        IDirect3DDevice9* device = *ppReturnedDeviceInterface;

        // Сохраняем handle окна
        g_hWindow = hFocusWindow;
        g_lvgl_width = pPresentationParameters->BackBufferWidth;
        g_lvgl_height = pPresentationParameters->BackBufferHeight;

        // --- Инициализация LVGL ---
        lv_init();

        g_lvgl_display = (lv_display_t*)lv_display_create((int32_t)g_lvgl_width, (int32_t)g_lvgl_height);
        lv_display_set_color_format(g_lvgl_display, LV_COLOR_FORMAT_ARGB8888);

        size_t buf_size = g_lvgl_width * g_lvgl_height * sizeof(lv_color32_t);
        g_lvgl_buf1 = (uint8_t*)calloc(1, buf_size); // calloc — обнуляем, чтобы прозрачный фон не содержал мусора
        lv_display_set_buffers(g_lvgl_display, g_lvgl_buf1, nullptr, (uint32_t)buf_size,
            LV_DISPLAY_RENDER_MODE_FULL);

        lv_display_set_flush_cb(g_lvgl_display, lvgld9_flush_cb);

        // Создаём D3D9 текстуру для LVGL фреймбуфера
        device->CreateTexture(g_lvgl_width, g_lvgl_height, 1, D3DUSAGE_DYNAMIC,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_lvgl_texture, nullptr);

        // Создаём indev для мыши
        g_lvgl_indev = (lv_indev_t*)lv_indev_create();
        lv_indev_set_type(g_lvgl_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(g_lvgl_indev, mouse_read_cb);

        // Строим LVGL UI
        lvgl_build_ui();

        g_lvgl_initialized = true;

        // --- Инициализация ImGui (параллельно, до завершения миграции) ---
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        ImGui_ImplWin32_Init(hFocusWindow);
        ImGui_ImplDX9_Init(device);
        RankUI::g_MatchHistory.Init(device);
        // Перехватываем оконную процедуру
        OriginalWndProc = (WNDPROC)SetWindowLongPtr(hFocusWindow, GWLP_WNDPROC, (LONG_PTR)Hooked_WndProc);

        // Перехватываем методы устройства
        HookDevice(device);
    }

    return hr;
}

// Перехваченная Direct3DCreate9
IDirect3D9* WINAPI HookedDirect3DCreate9(UINT SDKVersion)
{
    std::cout << "Called d3d9!" << "\n";
    if (!originalDirect3DCreate9)
    {
        return nullptr;
    }

    // Создаем оригинальный объект IDirect3D9
    IDirect3D9* pD3D = originalDirect3DCreate9(SDKVersion);
    if (!pD3D)
        return nullptr;

    // Перехватываем виртуальную таблицу IDirect3D9
    void** vTable = *((void***)pD3D);
    OriginalCreateDevice = (CreateDevice_t)vTable[16]; // CreateDevice имеет индекс 16

    // Заменяем CreateDevice на наш перехватчик
    DWORD oldProtect;
    VirtualProtect(&vTable[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    vTable[16] = (void*)Hooked_CreateDevice;
    VirtualProtect(&vTable[16], sizeof(void*), oldProtect, &oldProtect);

    return pD3D;
}

