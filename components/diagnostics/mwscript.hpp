#ifndef OPENMW_DIAGNOSTICS_MWSCRIPT_HPP
#define OPENMW_DIAGNOSTICS_MWSCRIPT_HPP

#include "timing.hpp"

#include <optional>
#include <string_view>

namespace Diagnostics
{
    inline thread_local std::string_view activeMWScript;

    // The caller owns the name for this scope's lifetime. Nested scripts restore
    // the caller, including when compilation or an opcode throws.
    class MWScriptContext
    {
        std::string_view mPrevious;

    public:
        explicit MWScriptContext(std::string_view name) noexcept
            : mPrevious(activeMWScript)
        {
            activeMWScript = name;
        }

        MWScriptContext(const MWScriptContext&) = delete;
        MWScriptContext& operator=(const MWScriptContext&) = delete;

        ~MWScriptContext() { activeMWScript = mPrevious; }
    };

    // Instrument shared engine operations only while a timed MWScript is active.
    // In particular, explicit Disable bypasses InterpreterContext's resolver.
    // Do not put a clock or a CSV row around every opcode or reference comparison.
    class MWScriptNativeScope
    {
        std::optional<Scope> mScope;

    public:
        explicit MWScriptNativeScope(std::string_view operation)
        {
            if (!activeMWScript.empty())
                mScope.emplace("mwscript_native", activeMWScript, operation);
        }
    };
}

#endif
