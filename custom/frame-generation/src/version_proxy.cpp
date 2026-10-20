// Forwards the real version.dll API. This DLL is named version.dll and sits in
// the game directory; SDL2.dll's static VERSION.dll import loads us instead of
// the system copy, which is our injection vector.
#include <windows.h>

namespace
{
    HMODULE realVersion()
    {
        static HMODULE handle = [] {
            wchar_t path[MAX_PATH];
            UINT len = GetSystemDirectoryW(path, MAX_PATH);
            wcscpy_s(path + len, MAX_PATH - len, L"\\version.dll");
            return LoadLibraryW(path);
        }();
        return handle;
    }

    template <typename Fn>
    Fn realFn(const char* name)
    {
        return reinterpret_cast<Fn>(GetProcAddress(realVersion(), name));
    }
}

extern "C"
{
    BOOL WINAPI Proxy_GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD len, LPVOID data)
    {
        static auto fn = realFn<BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID)>("GetFileVersionInfoA");
        return fn(file, handle, len, data);
    }

    BOOL WINAPI Proxy_GetFileVersionInfoW(LPCWSTR file, DWORD handle, DWORD len, LPVOID data)
    {
        static auto fn = realFn<BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID)>("GetFileVersionInfoW");
        return fn(file, handle, len, data);
    }

    // Undocumented; forward with generic register-width args.
    BOOL WINAPI Proxy_GetFileVersionInfoByHandle(UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d)
    {
        static auto fn = realFn<BOOL(WINAPI*)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR)>("GetFileVersionInfoByHandle");
        return fn ? fn(a, b, c, d) : FALSE;
    }

    BOOL WINAPI Proxy_GetFileVersionInfoExA(DWORD flags, LPCSTR file, DWORD handle, DWORD len, LPVOID data)
    {
        static auto fn = realFn<BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID)>("GetFileVersionInfoExA");
        return fn(flags, file, handle, len, data);
    }

    BOOL WINAPI Proxy_GetFileVersionInfoExW(DWORD flags, LPCWSTR file, DWORD handle, DWORD len, LPVOID data)
    {
        static auto fn = realFn<BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID)>("GetFileVersionInfoExW");
        return fn(flags, file, handle, len, data);
    }

    DWORD WINAPI Proxy_GetFileVersionInfoSizeA(LPCSTR file, LPDWORD handle)
    {
        static auto fn = realFn<DWORD(WINAPI*)(LPCSTR, LPDWORD)>("GetFileVersionInfoSizeA");
        return fn(file, handle);
    }

    DWORD WINAPI Proxy_GetFileVersionInfoSizeW(LPCWSTR file, LPDWORD handle)
    {
        static auto fn = realFn<DWORD(WINAPI*)(LPCWSTR, LPDWORD)>("GetFileVersionInfoSizeW");
        return fn(file, handle);
    }

    DWORD WINAPI Proxy_GetFileVersionInfoSizeExA(DWORD flags, LPCSTR file, LPDWORD handle)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD)>("GetFileVersionInfoSizeExA");
        return fn(flags, file, handle);
    }

    DWORD WINAPI Proxy_GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR file, LPDWORD handle)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD)>("GetFileVersionInfoSizeExW");
        return fn(flags, file, handle);
    }

    DWORD WINAPI Proxy_VerFindFileA(DWORD flags, LPCSTR file, LPCSTR winDir, LPCSTR appDir, LPSTR curDir,
        PUINT curDirLen, LPSTR destDir, PUINT destDirLen)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT)>("VerFindFileA");
        return fn(flags, file, winDir, appDir, curDir, curDirLen, destDir, destDirLen);
    }

    DWORD WINAPI Proxy_VerFindFileW(DWORD flags, LPCWSTR file, LPCWSTR winDir, LPCWSTR appDir, LPWSTR curDir,
        PUINT curDirLen, LPWSTR destDir, PUINT destDirLen)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT)>("VerFindFileW");
        return fn(flags, file, winDir, appDir, curDir, curDirLen, destDir, destDirLen);
    }

    DWORD WINAPI Proxy_VerInstallFileA(DWORD flags, LPCSTR srcFile, LPCSTR destFile, LPCSTR srcDir, LPCSTR destDir,
        LPCSTR curDir, LPSTR tmpFile, PUINT tmpFileLen)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT)>("VerInstallFileA");
        return fn(flags, srcFile, destFile, srcDir, destDir, curDir, tmpFile, tmpFileLen);
    }

    DWORD WINAPI Proxy_VerInstallFileW(DWORD flags, LPCWSTR srcFile, LPCWSTR destFile, LPCWSTR srcDir, LPCWSTR destDir,
        LPCWSTR curDir, LPWSTR tmpFile, PUINT tmpFileLen)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT)>("VerInstallFileW");
        return fn(flags, srcFile, destFile, srcDir, destDir, curDir, tmpFile, tmpFileLen);
    }

    DWORD WINAPI Proxy_VerLanguageNameA(DWORD lang, LPSTR name, DWORD size)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPSTR, DWORD)>("VerLanguageNameA");
        return fn(lang, name, size);
    }

    DWORD WINAPI Proxy_VerLanguageNameW(DWORD lang, LPWSTR name, DWORD size)
    {
        static auto fn = realFn<DWORD(WINAPI*)(DWORD, LPWSTR, DWORD)>("VerLanguageNameW");
        return fn(lang, name, size);
    }

    BOOL WINAPI Proxy_VerQueryValueA(LPCVOID block, LPCSTR subBlock, LPVOID* buffer, PUINT len)
    {
        static auto fn = realFn<BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT)>("VerQueryValueA");
        return fn(block, subBlock, buffer, len);
    }

    BOOL WINAPI Proxy_VerQueryValueW(LPCVOID block, LPCWSTR subBlock, LPVOID* buffer, PUINT len)
    {
        static auto fn = realFn<BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT)>("VerQueryValueW");
        return fn(block, subBlock, buffer, len);
    }
}
