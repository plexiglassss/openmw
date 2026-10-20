// Win32 overlay input: WH_GETMESSAGE hook on the game window's thread feeds a
// locked event queue; keys are polled with GetAsyncKeyState. Drawing is shared
// with the Linux build via overlay_draw.inc.
#include "overlay.hpp"

#include "../config.hpp"
#include "../hooks.hpp"
#include "../log.hpp"
#include "../platform.hpp"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_win32.h>

#include <cfloat>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fg::overlay
{
    namespace
    {
        HWND g_hwnd = nullptr;
        HHOOK g_msgHook = nullptr;
        bool g_imguiReady = false;

        struct Ev
        {
            UINT msg;
            WPARAM w;
            LPARAM l;
        };
        constexpr int kQueueCap = 512;
        CRITICAL_SECTION g_qcs;
        bool g_qcsInit = false;
        Ev g_queue[kQueueCap];
        int g_qn = 0;

        // cursor-clip bookkeeping around menu open/close
        bool g_hadClipAtOpen = false;
        RECT g_clipAtOpen{};
        std::atomic<bool> g_clipNoted{ false };
        RECT g_notedClip{};

        // OpenMW receives gameplay camera movement from SDL_MOUSEMOTION.
        // The upstream WH_GETMESSAGE filter handles ordinary Win32 input, but
        // SDL2 relative-mouse motion can still reach OpenMW on this runtime.
        // Do not hook SDL_PollEvent or alter cursor ownership: while F8 is open
        // simply tell SDL2 to ignore mouse-motion events, then restore the exact
        // previous event state when the menu closes.  ImGui gets its cursor
        // position/buttons through the existing Win32 polling path.
        using SdlEventStateFn = unsigned char(__cdecl*)(unsigned int, int);
        using SdlFlushEventFn = void(__cdecl*)(unsigned int);
        SdlEventStateFn g_sdlEventState = nullptr;
        SdlFlushEventFn g_sdlFlushEvent = nullptr;
        bool g_sdlMouseGateResolved = false;
        bool g_sdlMouseGateActive = false;
        unsigned char g_sdlMouseMotionPrev = 1;

        constexpr unsigned int kSdlMouseMotion = 0x400u; // SDL_MOUSEMOTION
        constexpr int kSdlQuery = -1;
        constexpr int kSdlIgnore = 0;

        bool resolveSdlMouseGate()
        {
            if (g_sdlMouseGateResolved)
                return g_sdlEventState && g_sdlFlushEvent;
            g_sdlMouseGateResolved = true;

            HMODULE sdl = GetModuleHandleW(L"SDL2.dll");
            if (!sdl)
                sdl = GetModuleHandleW(L"SDL2-2.0.dll");
            if (sdl)
            {
                g_sdlEventState = reinterpret_cast<SdlEventStateFn>(GetProcAddress(sdl, "SDL_EventState"));
                g_sdlFlushEvent = reinterpret_cast<SdlFlushEventFn>(GetProcAddress(sdl, "SDL_FlushEvent"));
            }

            if (g_sdlEventState && g_sdlFlushEvent)
                Log("overlay: SDL mouse-motion gate ready");
            else
                Log("overlay: SDL mouse-motion gate unavailable; using upstream Win32 suppression only");
            return g_sdlEventState && g_sdlFlushEvent;
        }

        void suspendSdlMouseEvents()
        {
            if (g_sdlMouseGateActive || !resolveSdlMouseGate())
                return;

            g_sdlMouseMotionPrev = g_sdlEventState(kSdlMouseMotion, kSdlQuery);
            g_sdlEventState(kSdlMouseMotion, kSdlIgnore);
            g_sdlFlushEvent(kSdlMouseMotion);
            g_sdlMouseGateActive = true;
            Log("overlay: F8 SDL mouse motion suspended");
        }

        void restoreSdlMouseEvents()
        {
            if (!g_sdlMouseGateActive || !g_sdlEventState || !g_sdlFlushEvent)
                return;

            // Remove boundary motion before re-enabling the state OpenMW had.
            g_sdlFlushEvent(kSdlMouseMotion);
            g_sdlEventState(kSdlMouseMotion, g_sdlMouseMotionPrev);
            g_sdlMouseGateActive = false;
            Log("overlay: F8 SDL mouse motion restored");
        }

        bool g_menuKeyWas = false, g_fpsKeyWas = false, g_escKeyWas = false;
        bool g_mouseWas[3] = {};
        bool g_bindSnapshot[256] = {};

#include "overlay_draw.inc"

        const char* keyName(int vk)
        {
            static char buf[64];
            UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
            LONG lp = (LONG)(sc << 16);
            switch (vk)
            {
                // extended keys, else e.g. VK_END reads back as "Num 1"
                case VK_END: case VK_HOME: case VK_PRIOR: case VK_NEXT:
                case VK_INSERT: case VK_DELETE:
                case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
                case VK_DIVIDE: case VK_NUMLOCK: case VK_RCONTROL: case VK_RMENU:
                    lp |= 1 << 24;
            }
            if (!GetKeyNameTextA(lp, buf, sizeof(buf)))
                snprintf(buf, sizeof(buf), "0x%X", vk);
            return buf;
        }

        void armBinding(int row)
        {
            for (int vk = 0; vk < 256; ++vk)
                g_bindSnapshot[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
            g_binding = row;
        }

        void pollBinding()
        {
            // first newly-pressed key binds; Esc cancels; mouse VKs (< 0x08)
            // stay usable for clicking the menu
            for (int vk = 0x08; vk < 0xFF; ++vk)
            {
                bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (!down)
                {
                    g_bindSnapshot[vk] = false;
                    continue;
                }
                if (g_bindSnapshot[vk])
                    continue;
                if (vk != VK_ESCAPE)
                {
                    *g_bindTargets[g_binding] = vk;
                    Log("overlay: '%s' bound to 0x%X (%s)", g_bindLabels[g_binding], vk, keyName(vk));
                }
                g_binding = -1;
                // suppress the edge this press would otherwise trigger
                g_menuKeyWas = g_fpsKeyWas = g_escKeyWas = true;
                return;
            }
        }

        bool isInputMessage(UINT m)
        {
            return (m >= WM_KEYFIRST && m <= WM_KEYLAST)
                || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST)
                || m == WM_INPUT;
        }

        LRESULT CALLBACK getMsgProc(int code, WPARAM wParam, LPARAM lParam)
        {
            if (code == HC_ACTION && wParam == PM_REMOVE)
            {
                MSG* m = (MSG*)lParam;
                if (g_menuOpen.load(std::memory_order_relaxed) && isInputMessage(m->message))
                {
                    EnterCriticalSection(&g_qcs);
                    if (g_qn < kQueueCap)
                        g_queue[g_qn++] = Ev{ m->message, m->wParam, m->lParam };
                    LeaveCriticalSection(&g_qcs);
                    // The game must not see input while the menu is open.
                    m->message = WM_NULL;
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        void openMenu()
        {
            GetClipCursor(&g_clipAtOpen);
            RECT screen{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0 };
            screen.right = screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            screen.bottom = screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
            g_hadClipAtOpen = !EqualRect(&g_clipAtOpen, &screen);
            g_clipNoted = false;
            if (real_ClipCursor)
                real_ClipCursor(nullptr);
            g_menuOpen = true;
            suspendSdlMouseEvents();
        }

        void closeMenu()
        {
            g_menuOpen = false;
            if (real_ClipCursor)
            {
                if (g_clipNoted.load())
                    real_ClipCursor(&g_notedClip);
                else if (g_hadClipAtOpen)
                    real_ClipCursor(&g_clipAtOpen);
            }
            restoreSdlMouseEvents();
        }
    }

    void InstallInputHook()
    {
        g_hwnd = plat::win::gameHwnd();
        if (!g_qcsInit)
        {
            InitializeCriticalSection(&g_qcs);
            g_qcsInit = true;
        }
        DWORD tid = GetWindowThreadProcessId(g_hwnd, nullptr);
        g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, getMsgProc, nullptr, tid);
        Log("overlay: message hook %s (thread %lu)", g_msgHook ? "installed" : "FAILED", tid);
    }

    void RemoveInputHook()
    {
        if (g_msgHook)
        {
            UnhookWindowsHookEx(g_msgHook);
            g_msgHook = nullptr;
        }
        if (g_menuOpen.load())
            closeMenu();
    }

    bool InitGL()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // no imgui.ini litter next to the game
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Constantia (fallback Georgia) from the system font dir; sizes baked
        // at menu_scale so glyphs stay crisp instead of bitmap-scaled.
        char winDir[MAX_PATH];
        GetWindowsDirectoryA(winDir, MAX_PATH);
        char p1[MAX_PATH], p2[MAX_PATH];
        snprintf(p1, sizeof(p1), "%s\\Fonts\\constan.ttf", winDir);
        snprintf(p2, sizeof(p2), "%s\\Fonts\\georgia.ttf", winDir);
        const char* faces[] = { p1, p2 };
        loadFonts(faces, 2);

        applyTheme();
        if (!ImGui_ImplWin32_Init(g_hwnd))
        {
            Log("overlay: ImGui_ImplWin32_Init failed");
            return false;
        }
        if (!ImGui_ImplOpenGL3_Init("#version 430"))
        {
            Log("overlay: ImGui_ImplOpenGL3_Init failed");
            return false;
        }
        g_imguiReady = true;
        Log("overlay: ImGui ready (menu key 0x%X, fps key 0x%X)", g_cfg.menuKey, g_cfg.fpsKey);
        return true;
    }

    void ShutdownGL()
    {
        if (!g_imguiReady)
            return;
        g_imguiReady = false;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void PollHotkeys()
    {
        if (GetForegroundWindow() != g_hwnd)
        {
            g_menuKeyWas = g_fpsKeyWas = g_escKeyWas = false;
            return;
        }
        if (g_binding >= 0)
        {
            pollBinding();
            return;
        }
        // Escape closes the menu; polled here because window messages are
        // unreliable while the game runs in relative-mouse mode.
        bool ek = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (ek && !g_escKeyWas && g_menuOpen.load())
            closeMenu();
        g_escKeyWas = ek;

        bool mk = (GetAsyncKeyState(g_cfg.menuKey) & 0x8000) != 0;
        if (mk && !g_menuKeyWas)
        {
            if (g_menuOpen.load())
                closeMenu();
            else
                openMenu();
        }
        g_menuKeyWas = mk;

        bool fk = (GetAsyncKeyState(g_cfg.fpsKey) & 0x8000) != 0;
        if (fk && !g_fpsKeyWas)
            g_cfg.fpsOverlay = g_cfg.fpsOverlay ? 0 : 1;
        g_fpsKeyWas = fk;
    }

    void Render(int w, int h, float inFps, float outFps, const Diagnostics& diagnostics,
        std::atomic<bool>& fgEnabled, bool interpPaused)
    {
        (void)w;
        (void)h;
        if (!g_imguiReady)
            return;
        bool menu = g_menuOpen.load();
        if (!menu && !g_cfg.fpsOverlay)
            return;

        // feed queued window messages to imgui
        Ev local[kQueueCap];
        int n = 0;
        EnterCriticalSection(&g_qcs);
        n = g_qn;
        if (n)
            memcpy(local, g_queue, sizeof(Ev) * n);
        g_qn = 0;
        LeaveCriticalSection(&g_qcs);
        for (int i = 0; i < n; ++i)
        {
            // Mouse position/buttons are polled below, not fed from messages:
            // ingame SDL relative-mouse mode makes legacy mouse messages
            // unreliable, and the Win32 backend would SetCapture/TrackMouseEvent
            // from this (non-window-owning) thread. Wheel has no polled
            // equivalent, so it still goes through.
            UINT m = local[i].msg;
            if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST && m != WM_MOUSEWHEEL && m != WM_MOUSEHWHEEL)
                continue;
            ImGui_ImplWin32_WndProcHandler(g_hwnd, m, local[i].w, local[i].l);
        }

        // Polled mouse buttons; position comes from the backend's GetCursorPos
        // fallback in ImGui_ImplWin32_NewFrame (active because no WM_MOUSEMOVE
        // is ever fed, so MouseTrackedArea stays 0).
        {
            ImGuiIO& io = ImGui::GetIO();
            const bool focused = GetForegroundWindow() == g_hwnd;
            const bool swap = GetSystemMetrics(SM_SWAPBUTTON) != 0;
            const int vk[3] = { swap ? VK_RBUTTON : VK_LBUTTON, swap ? VK_LBUTTON : VK_RBUTTON, VK_MBUTTON };
            for (int b = 0; b < 3; ++b)
            {
                bool down = menu && focused && (GetAsyncKeyState(vk[b]) & 0x8000) != 0;
                if (down != g_mouseWas[b])
                {
                    io.AddMouseButtonEvent(b, down);
                    g_mouseWas[b] = down;
                }
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::GetIO().MouseDrawCursor = menu;
        ImGui::NewFrame();

        if (g_cfg.fpsOverlay)
            drawFpsOverlay(inFps, outFps, diagnostics, fgEnabled.load(), interpPaused);
        if (menu)
            drawMenu(inFps, outFps, diagnostics, fgEnabled, interpPaused);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    bool MenuOpen()
    {
        return g_menuOpen.load(std::memory_order_relaxed);
    }

    void NoteClipRequest(const RECT* rect)
    {
        if (rect)
        {
            g_notedClip = *rect;
            g_clipNoted = true;
        }
    }
}
