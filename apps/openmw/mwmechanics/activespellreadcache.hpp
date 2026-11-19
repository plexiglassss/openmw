#ifndef OPENMW_MWMECHANICS_ACTIVESPELLREADCACHE_H
#define OPENMW_MWMECHANICS_ACTIVESPELLREADCACHE_H

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <components/esm3/activespells.hpp>

namespace MWMechanics
{
    // The fields that require allocation when a caller retains a value from an active-spell read.
    struct ActiveSpellReadDetails
    {
        std::string mDisplayName;
        std::vector<ESM::ActiveEffect> mEffects;
    };

    class ActiveSpellReadCache
    {
    public:
        using Snapshot = std::shared_ptr<const ActiveSpellReadDetails>;
        struct Result
        {
            Snapshot mSnapshot;
            bool mReused;
        };

        ActiveSpellReadCache() = default;
        // A native copy is independent and need not read the original spell's reader cache.
        ActiveSpellReadCache(const ActiveSpellReadCache&) noexcept {}
        ActiveSpellReadCache& operator=(const ActiveSpellReadCache&) noexcept
        {
            mLast.reset();
            return *this;
        }
        ActiveSpellReadCache(ActiveSpellReadCache&&) noexcept = default;
        ActiveSpellReadCache& operator=(ActiveSpellReadCache&&) noexcept = default;

        Result read(const std::string& name, const std::vector<ESM::ActiveEffect>& effects, std::uint64_t epoch) const
        {
            if (mLast && epoch != 0 && mEpoch == epoch)
                return { mLast, true };
            if (mLast && mLast->mDisplayName == name && mLast->mEffects.size() == effects.size()
                && std::equal(effects.begin(), effects.end(), mLast->mEffects.begin(), sameEffect))
            {
                mEpoch = epoch;
                return { mLast, true };
            }
            // Construct before replacing the old version. Lua may keep that version past removal of this spell.
            mLast = std::make_shared<const ActiveSpellReadDetails>(name, effects);
            mEpoch = epoch;
            return { mLast, false };
        }

    private:
        // A structural spell-list revision does not track ticking durations, magnitudes or effect arguments.
        static bool sameEffect(const ESM::ActiveEffect& a, const ESM::ActiveEffect& b)
        {
            const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
            return bits(a.mTimeLeft) == bits(b.mTimeLeft) && bits(a.mMagnitude) == bits(b.mMagnitude)
                && a.mFlags == b.mFlags && a.mEffectIndex == b.mEffectIndex && a.mEffectId == b.mEffectId
                && a.mArg == b.mArg && bits(a.mDuration) == bits(b.mDuration)
                && bits(a.mMinMagnitude) == bits(b.mMinMagnitude) && bits(a.mMaxMagnitude) == bits(b.mMaxMagnitude);
        }

        mutable Snapshot mLast;
        mutable std::uint64_t mEpoch = 0;
    };
}

#endif
