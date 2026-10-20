// Platform abstraction between the shared frame-generation core and the
// OS/window-system specifics. Two implementations:
//   src/win/platform_win.cpp   - WGL + Win32 (injected as version.dll proxy)
//   src/linux/platform_linux.cpp - GLX + SDL (injected via LD_PRELOAD)
#pragma once

#include <cstdint>

namespace plat
{
    // --- timing ---
    uint64_t nowNs(); // monotonic
    void highResTimerBegin(); // timeBeginPeriod(1) on Windows, no-op elsewhere
    void highResTimerEnd();
    void sleepMs(double ms);
    void cpuRelax(); // pause instruction for busy-wait tails

    // --- game context capture (game thread only) ---
    // Remembers the calling thread's current GL context + drawable/window as
    // "the game's". 1 = captured, 0 = retry next frame, -1 = permanent failure.
    int captureGameContext();
    void forgetGameContext();
    // Calling thread's current GL context is the captured game context.
    bool isGameContextCurrent();
    // The swap call being processed targets the captured game context/drawable.
    bool isGameSwapTarget();
    // Client-area / drawable size of the game window (game thread).
    bool getDrawableSize(int& w, int& h);

    // --- presenter context ---
    bool createPresenterContext(); // game thread; shares objects with game ctx
    void destroyPresenterContext(); // game thread, after the presenter exited
    bool presenterMakeCurrent(); // presenter thread
    void presenterDoneCurrent();
    void presenterSwapBuffers();
    bool presenterSetSwapInterval(int interval); // false if unsupported

    // Real, unhooked GL function resolver (wglGetProcAddress / glXGetProcAddress
    // trampolines + module-export fallback). A GL context must be current.
    void* getProcAddress(const char* name);

    // Refresh rate (Hz) of the display the game window is on, 0 if unknown.
    // Game thread only (the Linux implementation touches the game's Display
    // and Xlib is not thread-safe).
    int displayRefreshHz();
    // OpenMW's own [Video] "framerate limit" from the user's settings.cfg;
    // 0 = unlimited / not found.
    double gameFramerateLimit();

    // --- input (readable from any thread) ---
    bool gameWindowFocused();
    // Key codes are VK codes on Windows, SDL scancodes on Linux.
    bool keyDown(int code);
}

#ifdef _WIN32
#include <windows.h>
namespace plat::win
{
    // Records the HDC the current SwapBuffers call targets (before OnSwapShared).
    void setSwapTarget(HDC hdc);
    HWND gameHwnd(); // for the overlay's message hook / ImGui backend
}
#endif
