// Synthetic WER validation only. This is not an OpenMW engine build.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>

static const char marker[] = "OpenMW-passive-capture-probe-stack-reference-v1.4";
__declspec(noinline) static int run(const char* mode, const char* report)
{
    char* volatile stackReference = static_cast<char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4096));
    if (!stackReference) return 20;
    strcpy_s(stackReference, 4096, marker);
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    unsigned handled = 0;
    if (strcmp(mode, "handled") == 0) {
        for (unsigned i = 0; i != 100000; ++i) {
            __try { RaiseException(0xE24C4A02, 0, 0, nullptr); }
            __except (GetExceptionCode() == 0xE24C4A02 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { ++handled; }
        }
    }
    QueryPerformanceCounter(&end);
    char catcher[32] = {};
    GetEnvironmentVariableA("OPENMW_DISABLE_CRASH_CATCHER", catcher, sizeof(catcher));
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation = {};
    DWORD needed = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &needed);
        CloseHandle(token);
    }
    FILE* file = nullptr;
    if (fopen_s(&file, report, "wb") != 0) return 21;
    fprintf(file, "{\"pid\":%lu,\"debugger_present\":%s,\"elevated\":%s,\"catcher_environment\":\"%s\",\"heap_address\":%llu,\"handled_count\":%u,\"loop_seconds\":%.9f}",
        GetCurrentProcessId(), IsDebuggerPresent() ? "true" : "false", elevation.TokenIsElevated ? "true" : "false",
        catcher, static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(stackReference)), handled,
        static_cast<double>(end.QuadPart - start.QuadPart) / frequency.QuadPart);
    fclose(file);
    if (strcmp(mode, "fastfail") == 0) __fastfail(7);
    if (strcmp(mode, "access") == 0) *static_cast<volatile int*>(nullptr) = 1;
    HeapFree(GetProcessHeap(), 0, stackReference);
    return strcmp(mode, "nonzero") == 0 ? 17 : 0;
}
int main(int argc, char** argv) { return argc == 3 ? run(argv[1], argv[2]) : 22; }
