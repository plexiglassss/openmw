// Dear ImGui overlay: config menu (F8) + fps readout (F9).
// ImGui lives entirely on the presenter thread/context; input arrives from the
// game thread through a locked event queue (Win32 WH_GETMESSAGE hook on
// Windows, the SDL_PollEvent interposer on Linux).
#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <atomic>
#include <cstdint>

namespace fg::overlay
{
    struct Diagnostics
    {
        uint64_t nativeFrames = 0;
        uint64_t generatedFrames = 0;
        uint64_t presentedFrames = 0;
        uint64_t droppedFrames = 0;
    };

    // Game thread (init/teardown of the input capture).
    void InstallInputHook();
    void RemoveInputHook();

    // Presenter thread, GL context current.
    bool InitGL();
    void ShutdownGL();
    // Hotkey edge detection; call once per presenter iteration.
    void PollHotkeys();
    // Draw menu/fps overlay into the currently bound framebuffer, pre-swap.
    void Render(int w, int h, float inFps, float outFps, const Diagnostics& diagnostics,
        std::atomic<bool>& fgEnabled, bool interpPaused);

    // True while the menu is open (game input suppressed, cursor freed).
    bool MenuOpen();

#ifdef _WIN32
    // Original upstream F8 behavior: remember ClipCursor requests made while
    // the menu owns the cursor, then restore them when the menu closes.
    void NoteClipRequest(const RECT* rect);
#endif
}
