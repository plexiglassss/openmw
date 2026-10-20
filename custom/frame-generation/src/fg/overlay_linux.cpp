// Linux overlay input: SDL events captured by the SDL_PollEvent interposer
// (game thread) arrive through a locked queue; hotkeys use the scancode state
// table maintained there. No ImGui platform backend — the io is fed manually,
// since SDL must never be called from the presenter thread. Drawing is shared
// with the Windows build via overlay_draw.inc.
#include "overlay.hpp"

#include "../config.hpp"
#include "../log.hpp"
#include "../platform.hpp"
#include "../linux/linux_shared.hpp"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include <cfloat>
#include <cstdio>
#include <cstring>

namespace fg::overlay
{
    namespace
    {
        bool g_imguiReady = false;
        bool g_menuKeyWas = false, g_fpsKeyWas = false, g_escKeyWas = false;
        bool g_bindSnapshot[SDL_NUM_SCANCODES] = {};
        uint64_t g_lastFrameNs = 0;

#include "overlay_draw.inc"

        const char* keyName(int sc)
        {
            // table lookup inside SDL, safe from any thread
            static auto getName = (const char* (*)(SDL_Scancode))lin::realSdl("SDL_GetScancodeName");
            static char buf[64];
            const char* n = getName ? getName((SDL_Scancode)sc) : nullptr;
            if (n && *n)
                snprintf(buf, sizeof(buf), "%s", n);
            else
                snprintf(buf, sizeof(buf), "sc %d", sc);
            return buf;
        }

        void armBinding(int row)
        {
            for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc)
                g_bindSnapshot[sc] = lin::keyDown(sc);
            g_binding = row;
        }

        void pollBinding()
        {
            // first newly-pressed key binds; Esc cancels (scancodes 0-3 are
            // reserved/error values, mouse stays usable for clicking the menu)
            for (int sc = 4; sc < SDL_NUM_SCANCODES; ++sc)
            {
                bool down = lin::keyDown(sc);
                if (!down)
                {
                    g_bindSnapshot[sc] = false;
                    continue;
                }
                if (g_bindSnapshot[sc])
                    continue;
                if (sc != SDL_SCANCODE_ESCAPE)
                {
                    *g_bindTargets[g_binding] = sc;
                    Log("overlay: '%s' bound to %d (%s)", g_bindLabels[g_binding], sc, keyName(sc));
                }
                g_binding = -1;
                // suppress the edge this press would otherwise trigger
                g_menuKeyWas = g_fpsKeyWas = g_escKeyWas = true;
                return;
            }
        }

        void openMenu()
        {
            // cursor release happens on the game thread (applyMenuCursorState)
            g_menuOpen = true;
        }

        void closeMenu()
        {
            g_menuOpen = false;
        }

        void feedSdlEvents()
        {
            ImGuiIO& io = ImGui::GetIO();
            SDL_Event evs[512];
            int n = lin::drainOverlayEvents(evs, 512);
            for (int i = 0; i < n; ++i)
            {
                const SDL_Event& e = evs[i];
                switch (e.type)
                {
                    case SDL_MOUSEMOTION:
                        io.AddMousePosEvent((float)e.motion.x, (float)e.motion.y);
                        break;
                    case SDL_MOUSEBUTTONDOWN:
                    case SDL_MOUSEBUTTONUP:
                    {
                        int b = e.button.button == SDL_BUTTON_LEFT ? 0
                            : e.button.button == SDL_BUTTON_RIGHT ? 1
                            : e.button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
                        if (b >= 0)
                            io.AddMouseButtonEvent(b, e.type == SDL_MOUSEBUTTONDOWN);
                        break;
                    }
                    case SDL_MOUSEWHEEL:
                        io.AddMouseWheelEvent((float)e.wheel.x, (float)e.wheel.y);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    void InstallInputHook()
    {
        Log("overlay: SDL input capture armed");
    }

    void RemoveInputHook()
    {
        if (g_menuOpen.load())
            closeMenu();
    }

    bool InitGL()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // no imgui.ini litter next to the game

        const char* faces[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf",
            "/usr/share/fonts/liberation/LiberationSerif-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSerif.ttf",
            "/usr/share/fonts/TTF/DejaVuSerif.ttf",
        };
        loadFonts(faces, (int)(sizeof(faces) / sizeof(faces[0])));

        applyTheme();
        if (!ImGui_ImplOpenGL3_Init("#version 430"))
        {
            Log("overlay: ImGui_ImplOpenGL3_Init failed");
            return false;
        }
        g_imguiReady = true;
        Log("overlay: ImGui ready (menu key %d, fps key %d, SDL scancodes)", g_cfg.menuKey, g_cfg.fpsKey);
        return true;
    }

    void ShutdownGL()
    {
        if (!g_imguiReady)
            return;
        g_imguiReady = false;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }

    void PollHotkeys()
    {
        if (!lin::focused())
        {
            g_menuKeyWas = g_fpsKeyWas = g_escKeyWas = false;
            return;
        }
        if (g_binding >= 0)
        {
            pollBinding();
            return;
        }
        bool ek = lin::keyDown(SDL_SCANCODE_ESCAPE);
        if (ek && !g_escKeyWas && g_menuOpen.load())
            closeMenu();
        g_escKeyWas = ek;

        bool mk = lin::keyDown(g_cfg.menuKey);
        if (mk && !g_menuKeyWas)
        {
            if (g_menuOpen.load())
                closeMenu();
            else
                openMenu();
        }
        g_menuKeyWas = mk;

        bool fk = lin::keyDown(g_cfg.fpsKey);
        if (fk && !g_fpsKeyWas)
            g_cfg.fpsOverlay = g_cfg.fpsOverlay ? 0 : 1;
        g_fpsKeyWas = fk;
    }

    void Render(int w, int h, float inFps, float outFps, const Diagnostics& diagnostics,
        std::atomic<bool>& fgEnabled, bool interpPaused)
    {
        if (!g_imguiReady)
            return;
        bool menu = g_menuOpen.load();
        if (!menu && !g_cfg.fpsOverlay)
            return;

        feedSdlEvents();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);
        uint64_t now = plat::nowNs();
        double dt = g_lastFrameNs ? double(now - g_lastFrameNs) / 1e9 : 1.0 / 60.0;
        g_lastFrameNs = now;
        io.DeltaTime = (float)(dt < 0.0001 ? 0.0001 : dt > 0.1 ? 0.1 : dt);

        ImGui_ImplOpenGL3_NewFrame();
        io.MouseDrawCursor = menu;
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
}
