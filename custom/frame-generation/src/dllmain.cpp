#include <windows.h>

#include <cwchar>

#include "config.hpp"
#include "hooks.hpp"
#include "log.hpp"

namespace
{
    HMODULE g_self = nullptr;

    bool isOpenMW()
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* base = wcsrchr(exe, L'\\');
        base = base ? base + 1 : exe;
        return _wcsicmp(base, L"openmw.exe") == 0;
    }

    DWORD WINAPI initThread(LPVOID)
    {
        wchar_t dir[MAX_PATH];
        GetModuleFileNameW(g_self, dir, MAX_PATH);
        wchar_t* slash = wcsrchr(dir, L'\\');
        if (slash)
            *slash = 0;

        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\openmw_fg.log", dir);
        LogInit(path);
        Log("openmw-fg: init (pid %lu)", GetCurrentProcessId());

        swprintf_s(path, L"%s\\openmw_fg.ini", dir);
        ConfigLoad(path);

        if (!Hooks_Install())
            Log("openmw-fg: hook install FAILED, mod inactive");
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        // Only arm inside the game itself; the proxy exports still work
        // everywhere (launcher, wizard, etc. also load SDL2 -> version.dll).
        if (isOpenMW())
        {
            HANDLE h = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
            if (h)
                CloseHandle(h);
        }
    }
    return TRUE;
}
