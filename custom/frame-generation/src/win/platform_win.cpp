// WGL/Win32 implementation of the plat:: abstraction. The injection vector
// (version.dll proxy + MinHook) lives in dllmain.cpp / hooks.cpp; this file
// only covers contexts, timing and input state.
#include "../platform.hpp"

#include "../config.hpp"
#include "../gl_min.hpp"
#include "../hooks.hpp"
#include "../log.hpp"

#include <shlobj.h>
#include <timeapi.h>

#include <cstdio>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x0002

namespace
{
    HWND g_hwnd = nullptr;
    HDC g_gameHdc = nullptr;
    HGLRC g_gameCtx = nullptr;
    HGLRC g_fgCtx = nullptr;
    HDC g_swapHdc = nullptr; // target of the SwapBuffers call being processed
    HDC g_presHdc = nullptr; // presenter thread's DC on the game window

    HGLRC(WINAPI* g_wglCreateContextAttribsARB)(HDC, HGLRC, const int*) = nullptr;
    BOOL(WINAPI* g_wglSwapIntervalEXT)(int) = nullptr;

    LARGE_INTEGER g_qpf{};

    uint64_t qpcFreq()
    {
        if (!g_qpf.QuadPart)
            QueryPerformanceFrequency(&g_qpf);
        return (uint64_t)g_qpf.QuadPart;
    }
}

namespace plat
{
    uint64_t nowNs()
    {
        LARGE_INTEGER v;
        QueryPerformanceCounter(&v);
        // split to avoid 64-bit overflow of ticks * 1e9
        uint64_t f = qpcFreq();
        uint64_t sec = (uint64_t)v.QuadPart / f;
        uint64_t rem = (uint64_t)v.QuadPart % f;
        return sec * 1000000000ull + rem * 1000000000ull / f;
    }

    void highResTimerBegin()
    {
        timeBeginPeriod(1);
    }

    void highResTimerEnd()
    {
        timeEndPeriod(1);
    }

    void sleepMs(double ms)
    {
        if (ms > 0)
            Sleep((DWORD)ms);
    }

    void cpuRelax()
    {
        YieldProcessor();
    }

    int captureGameContext()
    {
        g_gameHdc = g_swapHdc;
        g_hwnd = WindowFromDC(g_swapHdc);
        g_gameCtx = wglGetCurrentContext();
        return (g_hwnd && g_gameCtx) ? 1 : 0;
    }

    void forgetGameContext()
    {
        g_hwnd = nullptr;
        g_gameHdc = nullptr;
        g_gameCtx = nullptr;
    }

    bool isGameContextCurrent()
    {
        return g_gameCtx && wglGetCurrentContext() == g_gameCtx;
    }

    bool isGameSwapTarget()
    {
        return g_swapHdc == g_gameHdc && wglGetCurrentContext() == g_gameCtx;
    }

    bool getDrawableSize(int& w, int& h)
    {
        RECT rc{};
        if (!g_hwnd || !GetClientRect(g_hwnd, &rc))
            return false;
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
        return true;
    }

    bool createPresenterContext()
    {
        g_wglCreateContextAttribsARB
            = (HGLRC(WINAPI*)(HDC, HGLRC, const int*))getProcAddress("wglCreateContextAttribsARB");
        if (g_wglCreateContextAttribsARB)
        {
            const int attribsCore[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0
            };
            g_fgCtx = g_wglCreateContextAttribsARB(g_gameHdc, g_gameCtx, attribsCore);
            if (g_fgCtx)
                return true;
            const int attribsCompat[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, 0
            };
            g_fgCtx = g_wglCreateContextAttribsARB(g_gameHdc, g_gameCtx, attribsCompat);
            if (g_fgCtx)
                return true;
        }
        g_fgCtx = wglCreateContext(g_gameHdc);
        if (!g_fgCtx)
        {
            Log("plat: wglCreateContext failed (err %lu)", GetLastError());
            return false;
        }
        if (!wglShareLists(g_gameCtx, g_fgCtx))
        {
            Log("plat: wglShareLists failed (err %lu)", GetLastError());
            wglDeleteContext(g_fgCtx);
            g_fgCtx = nullptr;
            return false;
        }
        return true;
    }

    void destroyPresenterContext()
    {
        if (g_fgCtx)
        {
            wglDeleteContext(g_fgCtx);
            g_fgCtx = nullptr;
        }
    }

    bool presenterMakeCurrent()
    {
        g_presHdc = GetDC(g_hwnd);
        if (!g_presHdc || !wglMakeCurrent(g_presHdc, g_fgCtx))
        {
            Log("plat: presenter wglMakeCurrent failed (err %lu)", GetLastError());
            return false;
        }
        return true;
    }

    void presenterDoneCurrent()
    {
        wglMakeCurrent(nullptr, nullptr);
        if (g_presHdc)
        {
            ReleaseDC(g_hwnd, g_presHdc);
            g_presHdc = nullptr;
        }
    }

    void presenterSwapBuffers()
    {
        real_SwapBuffers(g_presHdc);
    }

    bool presenterSetSwapInterval(int interval)
    {
        if (!g_wglSwapIntervalEXT)
            g_wglSwapIntervalEXT = (BOOL(WINAPI*)(int))getProcAddress("wglSwapIntervalEXT");
        if (!g_wglSwapIntervalEXT)
            return false;
        g_wglSwapIntervalEXT(interval);
        return true;
    }

    void* getProcAddress(const char* name)
    {
        PROC p = real_wglGetProcAddress(name);
        // NVIDIA/AMD return small sentinel values for unsupported functions.
        if (p && (uintptr_t)p > 3 && (intptr_t)p != -1)
            return (void*)p;
        HMODULE gl = GetModuleHandleW(L"opengl32.dll");
        return gl ? (void*)GetProcAddress(gl, name) : nullptr;
    }

    int displayRefreshHz()
    {
        if (g_hwnd)
        {
            HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (mon && GetMonitorInfoW(mon, &mi))
            {
                DEVMODEW dm{};
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)
                    && dm.dmDisplayFrequency > 1)
                    return (int)dm.dmDisplayFrequency;
            }
        }
        if (g_gameHdc)
        {
            int hz = GetDeviceCaps(g_gameHdc, VREFRESH);
            if (hz > 1)
                return hz;
        }
        return 0;
    }

    double gameFramerateLimit()
    {
        PWSTR docs = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
            return 0.0;
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%s\\My Games\\OpenMW\\settings.cfg", docs);
        CoTaskMemFree(docs);
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"r") || !f)
            return 0.0;
        double v = OpenMWSettingsFramerateLimit(f);
        fclose(f);
        return v;
    }

    bool gameWindowFocused()
    {
        return g_hwnd && GetForegroundWindow() == g_hwnd;
    }

    bool keyDown(int code)
    {
        return (GetAsyncKeyState(code) & 0x8000) != 0;
    }
}

namespace plat::win
{
    void setSwapTarget(HDC hdc)
    {
        g_swapHdc = hdc;
    }

    HWND gameHwnd()
    {
        return g_hwnd;
    }
}
