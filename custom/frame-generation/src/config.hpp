#pragma once

#include <cstdio>

// Key codes are Windows VK codes on Windows and SDL scancodes on Linux.
#ifdef _WIN32
#define FG_KEY_TOGGLE_DEFAULT 0x23 // VK_END
#define FG_KEY_MENU_DEFAULT 0x77 // VK_F8
#define FG_KEY_FPS_DEFAULT 0x78 // VK_F9
#else
#define FG_KEY_TOGGLE_DEFAULT 77 // SDL_SCANCODE_END
#define FG_KEY_MENU_DEFAULT 65 // SDL_SCANCODE_F8
#define FG_KEY_FPS_DEFAULT 66 // SDL_SCANCODE_F9
#endif

struct Config
{
    // mode: 1 = async frame generation, 0 = passthrough (hooks installed, no FG)
    int mode = 1;
    // key code for the runtime FG on/off toggle (default: End)
    int hotkey = FG_KEY_TOGGLE_DEFAULT;
    // swap interval on the presenter context (0 = off, 1 = vsync)
    int presentVsync = 0;
    // mean |luma delta| on the coarsest pyramid level above which interpolation
    // is replaced by a duplicate of the real frame (scene cut protection)
    float sceneChangeThreshold = 0.12f;
    // block-match search radius per pyramid level (candidates = (2r+1)^2)
    int searchRadius = 2;
    // upper bound on optical-flow pyramid levels
    int maxLevels = 6;
    // stats cadence in the log, seconds
    int statsIntervalSec = 5;
    // fps readout overlay (original -> generated), toggled with fpsKey
    int fpsOverlay = 1;
    // Optional expanded verification panel inside the FPS overlay.
    // Shows cumulative native/generated/presented/drop counters and live ratio.
    int diagnosticsOverlay = 0;
    // Passive FPS overlay placement. Anchor: 0=top-right, 1=top-left,
    // 2=bottom-right, 3=bottom-left. Offsets are inward from that anchor.
    int fpsOverlayAnchor = 0;
    float fpsOverlayOffsetX = 12.0f;
    float fpsOverlayOffsetY = 12.0f;
    // Suppress the next submitted/presented frame when OpenMW reads back the
    // redirected game framebuffer (save/F12 screenshot auxiliary traversal).
    // This prevents GUI-less screenshot traversals from flashing on screen.
    int suppressReadbackFrames = 1;
    // key codes: config menu (F8), fps overlay toggle (F9)
    int menuKey = FG_KEY_MENU_DEFAULT;
    int fpsKey = FG_KEY_FPS_DEFAULT;
    // imgui font/widget scale
    float menuScale = 1.5f;
    // pause interpolation when the game already runs at the display refresh
    // rate (two display-synced presents per game frame would otherwise force
    // the base fps down to refresh/2)
    int autoPause = 1;
    // display refresh override for auto_pause (0 = auto-detect)
    int displayHz = 0;
};

extern Config g_cfg;

#ifdef _WIN32
void ConfigLoad(const wchar_t* iniPath);
#else
void ConfigLoad(const char* iniPath);
#endif
// Writes the current g_cfg back to the ini it was loaded from.
void ConfigSave();

// Scans an already-open OpenMW settings.cfg for [Video] "framerate limit".
// Returns 0 if absent. Used by plat::gameFramerateLimit().
double OpenMWSettingsFramerateLimit(FILE* f);
