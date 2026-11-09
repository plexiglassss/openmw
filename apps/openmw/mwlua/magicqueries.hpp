#ifndef MWLUA_MAGICQUERIES_H
#define MWLUA_MAGICQUERIES_H

#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <components/esm3/activespells.hpp>

#include "../mwmechanics/activespells.hpp"
#include "magicbindingshelper.hpp"

namespace MWLua::MagicBindings
{
    // These queries always read the current native stores. Only the compact
    // result is compared with the previous result; no frame/world-state cache
    // or borrowed native iterator survives a read.
    struct EffectSource
    {
        ESM::RefId mEffectId;
        ESM::RefNum mCaster;

        bool operator==(const EffectSource&) const = default;
    };

    struct EffectSourceFilter
    {
        std::optional<bool> mTemporary;
        std::optional<ESM::RefNum> mCaster;
        std::optional<unsigned int> mCasterType;
        // An absent filter accepts every applied effect. A present, empty
        // filter accepts none. Sorting this selection never reorders results.
        std::optional<std::vector<ESM::RefId>> mEffectIds;
    };

    struct EffectCaster
    {
        ESM::RefNum mId;
        unsigned int mType = 0;
    };

    class EffectSourceQueryData
    {
        EffectSourceFilter mFilter;
        std::vector<EffectSource> mValues;
        std::vector<EffectSource> mPending;
        bool mInitialized = false;

    public:
        explicit EffectSourceQueryData(EffectSourceFilter filter)
            : mFilter(std::move(filter))
        {
            if (mFilter.mEffectIds)
            {
                auto& ids = *mFilter.mEffectIds;
                std::sort(ids.begin(), ids.end());
                ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            }
        }

        const std::vector<EffectSource>& values() const { return mValues; }

        template <class Spells, class ResolveCaster>
        bool update(const Spells& spells, ResolveCaster&& resolveCaster)
        {
            mPending.clear();
            std::optional<ESM::RefNum> lastCaster;
            EffectCaster caster;
            for (const auto& spell : spells)
            {
                if (mFilter.mTemporary && spell.hasFlag(ESM::ActiveSpells::Flag_Temporary) != *mFilter.mTemporary)
                    continue;
                if (mFilter.mCaster && spell.getCaster() != *mFilter.mCaster)
                    continue;
                if (!lastCaster || *lastCaster != spell.getCaster())
                {
                    lastCaster = spell.getCaster();
                    caster = resolveCaster(*lastCaster);
                }
                if (mFilter.mCasterType && caster.mType != *mFilter.mCasterType)
                    continue;
                for (const auto& effect : spell.getEffects())
                {
                    if (!(effect.mFlags & ESM::ActiveEffect::Flag_Applied))
                        continue;
                    if (mFilter.mEffectIds
                        && !std::binary_search(
                            mFilter.mEffectIds->begin(), mFilter.mEffectIds->end(), effect.mEffectId))
                        continue;
                    mPending.push_back({ effect.mEffectId, caster.mId });
                }
            }
            const bool changed = !mInitialized || mPending != mValues;
            if (changed)
                mValues.swap(mPending);
            mInitialized = true;
            return changed;
        }
    };

    template <class ResolveCaster>
    bool readEffectSources(const ObjectVariant& actor, EffectSourceQueryData& query, ResolveCaster&& resolveCaster)
    {
        if (auto* stats = getActorStats(actor))
            return query.update(stats->getActiveSpells(), std::forward<ResolveCaster>(resolveCaster));
        return query.update(std::span<const MWMechanics::ActiveSpells::ActiveSpellParams>{},
            std::forward<ResolveCaster>(resolveCaster));
    }

    class EffectMagnitudeQueryData
    {
        std::vector<MWMechanics::EffectKey> mKeys;
        std::vector<float> mValues;
        std::vector<float> mPending;
        bool mInitialized = false;

    public:
        explicit EffectMagnitudeQueryData(std::vector<MWMechanics::EffectKey> keys)
            : mKeys(std::move(keys))
            , mPending(mKeys.size())
        {
        }

        const std::vector<float>& values() const { return mValues; }
        std::size_t size() const { return mKeys.size(); }

        bool update(const MWMechanics::MagicEffects* effects)
        {
            mPending.resize(mKeys.size());
            for (std::size_t i = 0; i < mKeys.size(); ++i)
                mPending[i] = effects ? effects->getOrDefault(mKeys[i]).getMagnitude() : 0.f;
            // Bit comparison also preserves signed zero and NaN payloads in
            // this value API instead of silently retaining a different value.
            const bool changed = !mInitialized
                || !std::equal(
                    mPending.begin(), mPending.end(), mValues.begin(), mValues.end(), [](float lhs, float rhs) {
                        return std::bit_cast<std::uint32_t>(lhs) == std::bit_cast<std::uint32_t>(rhs);
                    });
            if (changed)
                mValues.swap(mPending);
            mInitialized = true;
            return changed;
        }
    };

    inline bool readEffectMagnitudes(const ObjectVariant& actor, EffectMagnitudeQueryData& query)
    {
        auto* stats = getActorStats(actor);
        return query.update(stats ? &stats->getMagicEffects() : nullptr);
    }

    inline bool hasActiveEffects(const ObjectVariant& actor)
    {
        auto* stats = getActorStats(actor);
        if (!stats)
            return false;
        const auto& effects = stats->getMagicEffects();
        return std::any_of(effects.begin(), effects.end(),
            [](const auto& entry) { return entry.second.getBase() != 0 || entry.second.getModifier() != 0.f; });
    }
}

namespace MWLua
{
    // Public here so the real Lua snapshot boundary can be covered by the
    // headless engine tests, as well as the native query kernels above.
    struct EffectSourceQuery
    {
        ObjectVariant mActor;
        MagicBindings::EffectSourceQueryData mData;
        std::optional<sol::main_table> mEffectIds;
        std::optional<sol::main_table> mCasters;
        bool mSnapshotCurrent = false;

        std::tuple<sol::table, sol::table, bool> read(sol::this_state state);
    };

    struct EffectMagnitudeQuery
    {
        ObjectVariant mActor;
        MagicBindings::EffectMagnitudeQueryData mData;
        std::optional<sol::main_table> mMagnitudes;
        bool mSnapshotCurrent = false;

        std::tuple<sol::table, bool> read(sol::this_state state);
    };
}

#endif
