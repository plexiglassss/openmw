#ifndef MWLUA_MAGICBINDINGS_H
#define MWLUA_MAGICBINDINGS_H

#include <cstdint>

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    struct ActiveSpellReadStats
    {
        std::uint64_t mScans = 0;
        std::uint64_t mEmptyScans = 0;
        std::uint64_t mYielded = 0;
        std::uint64_t mLookups = 0;
        std::uint64_t mLookupHits = 0;
        std::uint64_t mCopies = 0;
        std::uint64_t mEffectsCopied = 0;
        std::uint64_t mReuses = 0;
        std::uint64_t mEffectsTables = 0;
        std::uint64_t mEffectsExported = 0;
    };

    sol::table initCoreMagicBindings(const Context& context);
    void addActorMagicBindings(sol::table& actor, const Context& context, ActiveSpellReadStats* readStats = nullptr);
}

#endif // MWLUA_MAGICBINDINGS_H
