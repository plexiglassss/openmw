#ifndef GAME_MWMECHANICS_MAGICEFFECTACCOUNTING_H
#define GAME_MWMECHANICS_MAGICEFFECTACCOUNTING_H

#include <algorithm>
#include <array>
#include <cstdint>

#include <components/esm3/activespells.hpp>
#include <components/esm3/loadmgef.hpp>

namespace MWMechanics::MagicEffectAccounting
{
    constexpr bool isApplied(std::int32_t flags) noexcept
    {
        return (flags & ESM::ActiveEffect::Flag_Applied) != 0;
    }

    // A magnitude belongs in MagicEffects when it was already accounted for, or when this application actually
    // succeeded. Rejected effects (for example Drain/Absorb while TGM is active) must never enter the aggregate.
    constexpr bool shouldTrackMagnitude(std::int32_t previousFlags, std::int32_t applicationFlags) noexcept
    {
        return isApplied(previousFlags) || isApplied(applicationFlags);
    }

    template <class EffectRange>
    float getAppliedMagnitude(const EffectRange& effects, const ESM::RefId& effectId)
    {
        float magnitude = 0.f;
        for (const auto& effect : effects)
        {
            if (effect.mEffectId == effectId && isApplied(effect.mFlags))
                magnitude += effect.mMagnitude;
        }
        return magnitude;
    }

    inline const std::array<ESM::RefId, 10>& getGodModeSuppressedEffects()
    {
        static const std::array<ESM::RefId, 10> effects{
            ESM::MagicEffect::DrainHealth,
            ESM::MagicEffect::DrainMagicka,
            ESM::MagicEffect::DrainFatigue,
            ESM::MagicEffect::DrainAttribute,
            ESM::MagicEffect::DrainSkill,
            ESM::MagicEffect::AbsorbHealth,
            ESM::MagicEffect::AbsorbMagicka,
            ESM::MagicEffect::AbsorbFatigue,
            ESM::MagicEffect::AbsorbAttribute,
            ESM::MagicEffect::AbsorbSkill,
        };
        return effects;
    }

    inline bool isGodModeSuppressedEffect(const ESM::RefId& effectId)
    {
        const auto& effects = getGodModeSuppressedEffects();
        return std::find(effects.begin(), effects.end(), effectId) != effects.end();
    }
}

#endif
