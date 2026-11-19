#include "config.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

Config g_cfg;

namespace
{
    void applyKey(const char* key, const char* value)
    {
        if (!strcmp(key, "mode"))
            g_cfg.mode = !strcmp(value, "off") ? 0 : 1;
        else if (!strcmp(key, "hotkey"))
            g_cfg.hotkey = (int)strtol(value, nullptr, 0);
        else if (!strcmp(key, "present_vsync"))
            g_cfg.presentVsync = atoi(value);
        else if (!strcmp(key, "overlay"))
            ; // removed green-square debug setting; ignore in old inis
        else if (!strcmp(key, "scene_change_threshold"))
            g_cfg.sceneChangeThreshold = (float)atof(value);
        else if (!strcmp(key, "search_radius"))
            g_cfg.searchRadius = atoi(value);
        else if (!strcmp(key, "max_levels"))
            g_cfg.maxLevels = atoi(value);
        else if (!strcmp(key, "stats_interval_sec"))
            g_cfg.statsIntervalSec = atoi(value);
        else if (!strcmp(key, "fps_overlay"))
            g_cfg.fpsOverlay = atoi(value);
        else if (!strcmp(key, "diagnostics_overlay"))
            g_cfg.diagnosticsOverlay = atoi(value);
        else if (!strcmp(key, "fps_overlay_anchor"))
        {
            if (!strcmp(value, "top_right")) g_cfg.fpsOverlayAnchor = 0;
            else if (!strcmp(value, "top_left")) g_cfg.fpsOverlayAnchor = 1;
            else if (!strcmp(value, "bottom_right")) g_cfg.fpsOverlayAnchor = 2;
            else if (!strcmp(value, "bottom_left")) g_cfg.fpsOverlayAnchor = 3;
            else g_cfg.fpsOverlayAnchor = atoi(value);
            if (g_cfg.fpsOverlayAnchor < 0 || g_cfg.fpsOverlayAnchor > 3)
                g_cfg.fpsOverlayAnchor = 0;
        }
        else if (!strcmp(key, "fps_overlay_offset_x"))
            g_cfg.fpsOverlayOffsetX = (float)atof(value);
        else if (!strcmp(key, "fps_overlay_offset_y"))
            g_cfg.fpsOverlayOffsetY = (float)atof(value);
        else if (!strcmp(key, "fps_overlay_x") || !strcmp(key, "fps_overlay_y"))
            ; // legacy draggable-overlay keys from test builds; intentionally ignored
        else if (!strcmp(key, "suppress_readback_frames"))
            g_cfg.suppressReadbackFrames = atoi(value);
        else if (!strcmp(key, "menu_key"))
            g_cfg.menuKey = (int)strtol(value, nullptr, 0);
        else if (!strcmp(key, "fps_key"))
            g_cfg.fpsKey = (int)strtol(value, nullptr, 0);
        else if (!strcmp(key, "menu_scale"))
            g_cfg.menuScale = (float)atof(value);
        else if (!strcmp(key, "auto_pause"))
            g_cfg.autoPause = atoi(value);
        else if (!strcmp(key, "display_hz"))
            g_cfg.displayHz = atoi(value);
        else
            Log("config: unknown key '%s'", key);
    }

#ifdef _WIN32
    wchar_t g_iniPath[260];
    wchar_t g_userIniPath[260];

    void initUserIniPath()
    {
        wchar_t profile[260]{};
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, (DWORD)(sizeof(profile) / sizeof(profile[0])));
        if (n > 0 && n < sizeof(profile) / sizeof(profile[0]))
            _snwprintf_s(g_userIniPath, _TRUNCATE, L"%s\\Documents\\My Games\\OpenMW\\openmw_fg.ini", profile);
    }

    FILE* openPath(const wchar_t* path, const char* mode)
    {
        if (!path || !*path)
            return nullptr;
        FILE* f = nullptr;
        wchar_t wmode[8] = { (wchar_t)mode[0], 0 };
        return _wfopen_s(&f, path, wmode) ? nullptr : f;
    }

    FILE* openIni(const char* mode)
    {
        return openPath(g_iniPath, mode);
    }

    FILE* openUserIni(const char* mode)
    {
        return openPath(g_userIniPath, mode);
    }

    void logIniPath(const char* what)
    {
        Log("config: %s %ls", what, g_iniPath);
    }
#else
    char g_iniPath[1024];

    FILE* openIni(const char* mode)
    {
        return fopen(g_iniPath, mode);
    }

    void logIniPath(const char* what)
    {
        Log("config: %s %s", what, g_iniPath);
    }
#endif

    void parse(FILE* f)
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            char* s = line;
            while (*s == ' ' || *s == '\t')
                ++s;
            if (*s == ';' || *s == '#' || *s == '[' || *s == '\r' || *s == '\n' || !*s)
                continue;
            char* eq = strchr(s, '=');
            if (!eq)
                continue;
            *eq = 0;
            char* key = s;
            char* value = eq + 1;
            // trim
            for (char* e = key + strlen(key) - 1; e >= key && (*e == ' ' || *e == '\t'); --e)
                *e = 0;
            while (*value == ' ' || *value == '\t')
                ++value;
            for (char* e = value + strlen(value) - 1; e >= value && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n'); --e)
                *e = 0;
            applyKey(key, value);
        }
    }
}

#ifdef _WIN32
void ConfigLoad(const wchar_t* iniPath)
{
    wcscpy_s(g_iniPath, iniPath);
    initUserIniPath();
#else
void ConfigLoad(const char* iniPath)
{
    snprintf(g_iniPath, sizeof(g_iniPath), "%s", iniPath);
#endif
    FILE* f = openIni("r");
    if (!f)
        logIniPath("not found, using defaults/user override:");
    else
    {
        parse(f);
        fclose(f);
    }
#ifdef _WIN32
    // If Program Files is not writable, ConfigSave falls back here. Load that
    // file after the install-dir INI so user-saved values take precedence.
    FILE* user = openUserIni("r");
    if (user)
    {
        parse(user);
        fclose(user);
        Log("config: loaded user override %ls", g_userIniPath);
    }
#endif

    if (g_cfg.fpsOverlayOffsetX < 0.f) g_cfg.fpsOverlayOffsetX = 0.f;
    if (g_cfg.fpsOverlayOffsetY < 0.f) g_cfg.fpsOverlayOffsetY = 0.f;
    Log("config: mode=%d hotkey=0x%X present_vsync=%d scene_change_threshold=%.3f search_radius=%d max_levels=%d fps_overlay=%d diagnostics_overlay=%d fps_anchor=%d fps_offset=(%.1f,%.1f) suppress_readback_frames=%d menu_key=0x%X fps_key=0x%X auto_pause=%d display_hz=%d",
        g_cfg.mode, g_cfg.hotkey, g_cfg.presentVsync, (double)g_cfg.sceneChangeThreshold,
        g_cfg.searchRadius, g_cfg.maxLevels, g_cfg.fpsOverlay, g_cfg.diagnosticsOverlay,
        g_cfg.fpsOverlayAnchor, (double)g_cfg.fpsOverlayOffsetX, (double)g_cfg.fpsOverlayOffsetY,
        g_cfg.suppressReadbackFrames, g_cfg.menuKey, g_cfg.fpsKey, g_cfg.autoPause, g_cfg.displayHz);
}

void ConfigSave()
{
    FILE* f = openIni("w");
#ifdef _WIN32
    bool userFallback = false;
    if (!f)
    {
        f = openUserIni("w");
        userFallback = f != nullptr;
        if (userFallback)
            Log("config: install-dir INI is not writable; saving user override to %ls", g_userIniPath);
    }
#else
    if (!f)
    {
        logIniPath("cannot write");
        return;
    }
#endif
    if (!f)
    {
        logIniPath("cannot write install-dir or user override:");
        return;
    }
#ifdef _WIN32
    const char* keyComment1 = "; runtime FG toggle key, VK code (0x23 = End)";
    const char* keyComment2 = "; config menu key (0x77 = F8) and fps overlay key (0x78 = F9)";
#else
    const char* keyComment1 = "; runtime FG toggle key, SDL scancode (77 = End)";
    const char* keyComment2 = "; config menu key (65 = F8) and fps overlay key (66 = F9), SDL scancodes";
#endif
    fprintf(f,
        "; OpenMW frame generation - config\n"
        "; mode: async (frame generation) | off (hooks idle, passthrough)\n"
        "mode=%s\n"
        "%s\n"
        "hotkey=0x%X\n"
        "%s\n"
        "menu_key=0x%X\n"
        "fps_key=0x%X\n"
        "; vsync on the FG presenter\n"
        "present_vsync=%d\n"
        "; fps readout overlay (original -> generated)\n"
        "fps_overlay=%d\n"
        "; optional detailed verification counters in the FPS overlay\n"
        "diagnostics_overlay=%d\n"
        "; passive fps overlay placement: top_right | top_left | bottom_right | bottom_left\n"
        "fps_overlay_anchor=%s\n"
        "; inward pixel offsets from the selected anchor\n"
        "fps_overlay_offset_x=%.1f\n"
        "fps_overlay_offset_y=%.1f\n"
        "; suppress auxiliary framebuffer-readback traversals (fixes save/screenshot UI flashes)\n"
        "suppress_readback_frames=%d\n"
        "menu_scale=%.2f\n"
        "scene_change_threshold=%.3f\n"
        "search_radius=%d\n"
        "max_levels=%d\n"
        "stats_interval_sec=%d\n"
        "; pause interpolation when the game already runs at the display refresh\n"
        "; (two synced presents per frame would halve the base fps, not help it)\n"
        "auto_pause=%d\n"
        "; display refresh rate override for auto_pause, 0 = auto-detect\n"
        "display_hz=%d\n",
        g_cfg.mode ? "async" : "off", keyComment1, g_cfg.hotkey, keyComment2, g_cfg.menuKey, g_cfg.fpsKey,
        g_cfg.presentVsync, g_cfg.fpsOverlay, g_cfg.diagnosticsOverlay,
        g_cfg.fpsOverlayAnchor == 1 ? "top_left" : g_cfg.fpsOverlayAnchor == 2 ? "bottom_right" :
            g_cfg.fpsOverlayAnchor == 3 ? "bottom_left" : "top_right",
        (double)g_cfg.fpsOverlayOffsetX, (double)g_cfg.fpsOverlayOffsetY,
        g_cfg.suppressReadbackFrames, (double)g_cfg.menuScale, (double)g_cfg.sceneChangeThreshold,
        g_cfg.searchRadius, g_cfg.maxLevels, g_cfg.statsIntervalSec,
        g_cfg.autoPause, g_cfg.displayHz);
    fclose(f);
#ifdef _WIN32
    if (userFallback)
        Log("config: saved %ls", g_userIniPath);
    else
    {
        // A writable install-dir INI is authoritative; remove a stale fallback
        // so it cannot unexpectedly override newly saved values next launch.
        if (g_userIniPath[0])
            _wremove(g_userIniPath);
        logIniPath("saved");
    }
#else
    logIniPath("saved");
#endif
}

double OpenMWSettingsFramerateLimit(FILE* f)
{
    char line[512];
    bool inVideo = false;
    while (fgets(line, sizeof(line), f))
    {
        char* s = line;
        while (*s == ' ' || *s == '\t')
            ++s;
        if (*s == '[')
        {
            inVideo = strncmp(s, "[Video]", 7) == 0;
            continue;
        }
        if (!inVideo)
            continue;
        char* eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        for (char* e = s + strlen(s) - 1; e >= s && (*e == ' ' || *e == '\t'); --e)
            *e = 0;
        if (strcmp(s, "framerate limit"))
            continue;
        return atof(eq + 1);
    }
    return 0.0;
}
