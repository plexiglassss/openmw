#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 1;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { std::cerr << "LoadLibrary failed: " << GetLastError() << "\n"; return 2; }
    using SizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    const auto size = reinterpret_cast<SizeFn>(GetProcAddress(dll, "GetFileVersionInfoSizeW"));
    if (!size) return 3;
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    std::wstring file = std::wstring(system) + L"\\kernel32.dll";
    DWORD ignored = 0;
    if (!size(file.c_str(), &ignored)) { std::cerr << "Version forwarding failed\n"; return 4; }
    FreeLibrary(dll);
    std::cout << "PASS: x64 DLL loads and forwards version information\n";
}
