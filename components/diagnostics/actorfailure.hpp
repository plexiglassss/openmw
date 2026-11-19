#ifndef OPENMW_DIAGNOSTICS_ACTORFAILURE_HPP
#define OPENMW_DIAGNOSTICS_ACTORFAILURE_HPP

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Diagnostics
{
    // Only called on an existing exception path. Keep normal gameplay untouched.
    // Module-relative PCs can be resolved with the exact build's executable/PDB.
    inline std::string actorFailureDetails(std::string_view operation, std::string_view object)
    {
        std::ostringstream out;
        out << "class does not have creature stats; operation=" << operation << "; " << object;
#ifdef _WIN32
        static std::atomic<unsigned> captures{ 0 };
        if (captures.fetch_add(1, std::memory_order_relaxed) < 8)
        {
            void* frames[24]{};
            const USHORT count = CaptureStackBackTrace(0, 24, frames, nullptr);
            out << "; stack=";
            for (USHORT i = 0; i < count; ++i)
            {
                if (i != 0)
                    out << ',';
                HMODULE module = nullptr;
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<LPCSTR>(frames[i]), &module))
                {
                    char path[MAX_PATH]{};
                    const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
                    std::string_view name(path, length < MAX_PATH ? length : MAX_PATH);
                    const auto separator = name.find_last_of("/\\");
                    if (separator != std::string_view::npos)
                        name.remove_prefix(separator + 1);
                    out << name << "+0x" << std::hex
                        << (reinterpret_cast<std::uintptr_t>(frames[i])
                            - reinterpret_cast<std::uintptr_t>(module)) << std::dec;
                }
                else
                    out << "unknown+0x" << std::hex << reinterpret_cast<std::uintptr_t>(frames[i]) << std::dec;
            }
        }
#endif
        return out.str();
    }
}

#endif
