#include "log.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#ifdef _WIN32
#include <share.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{
    FILE* g_file = nullptr;
    std::mutex g_mtx;
    std::chrono::steady_clock::time_point g_start;

    unsigned long threadId()
    {
#ifdef _WIN32
        return GetCurrentThreadId();
#else
        return (unsigned long)syscall(SYS_gettid);
#endif
    }
}

#ifdef _WIN32
void LogInit(const wchar_t* path)
{
    g_start = std::chrono::steady_clock::now();
    // Allow other processes to open/read the log while the game is running,
    // but deny a second writer. Log() flushes every line, so tailing/viewing it
    // live is safe.
    g_file = _wfsopen(path, L"w", _SH_DENYWR);
    if (g_file)
        return;

    // OpenMW is commonly installed under Program Files, where a normal user
    // cannot create openmw_fg.log next to version.dll. Fall back to OpenMW's
    // writable user-data directory so logging is still available.
    wchar_t profile[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
    {
        wchar_t fallback[MAX_PATH]{};
        _snwprintf_s(fallback, _TRUNCATE,
            L"%s\\Documents\\My Games\\OpenMW\\openmw_fg.log", profile);
        g_file = _wfsopen(fallback, L"w", _SH_DENYWR);
    }
}
#else
void LogInit(const char* path)
{
    g_start = std::chrono::steady_clock::now();
    g_file = fopen(path, "w");
}
#endif

void Log(const char* fmt, ...)
{
    if (!g_file)
        return;

    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();

    std::lock_guard<std::mutex> lock(g_mtx);
    fprintf(g_file, "[%9.3f] [%5lu] ", t, threadId());
    va_list args;
    va_start(args, fmt);
    vfprintf(g_file, fmt, args);
    va_end(args);
    fputc('\n', g_file);
    fflush(g_file);
}
