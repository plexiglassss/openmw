// Cross-file plumbing for the Linux (LD_PRELOAD) build.
#pragma once

#include <SDL2/SDL.h>

namespace lin
{
    // True once the constructor decided this process is openmw and set up
    // logging/config. Interposers stay passthrough while false.
    bool armed();

    // Handle to the real libGL (dlopened lazily, never unloaded).
    void* glHandle();
    // Real (non-interposed) symbol: dlsym(RTLD_NEXT) with a libGL fallback.
    void* realSym(const char* name);
    // Real SDL symbol via dlsym(RTLD_NEXT); the game provides libSDL2.
    void* realSdl(const char* name);
    // Eagerly resolves the real_gl* trampolines (the presenter uses them
    // before the game may ever hit the lazy interposer paths).
    void resolveRealGl();

    // --- input state maintained by the SDL_PollEvent interposer ---
    bool keyDown(int scancode);
    bool focused();
    // Presenter thread: moves queued (menu-open) SDL input events to out.
    int drainOverlayEvents(SDL_Event* out, int cap);

    // --- swap bookkeeping (game thread) ---
    void setSwapWindow(SDL_Window* w);
    SDL_Window* swapWindow();
}
