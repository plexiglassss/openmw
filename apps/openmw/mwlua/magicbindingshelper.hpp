#ifndef MWLUA_MAGICBINDINGSHELPER_H
#define MWLUA_MAGICBINDINGSHELPER_H

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/magiceffects.hpp"
#include "../mwworld/class.hpp"

#include "objectvariant.hpp"

namespace MWLua
{
    struct ActiveEffect
    {
        MWMechanics::EffectKey key;
        MWMechanics::EffectParam param;
    };

    namespace MagicBindings
    {
        // Resolve on every call: ObjectVariant retains the actor's local/global/self
        // context, and SafePtr must still observe changes to the world registry.
        inline MWMechanics::CreatureStats* getActorStats(const MWWorld::Ptr& ptr)
        {
            if (ptr.isEmpty())
                return nullptr;
            const MWWorld::Class& cls = ptr.getClass();
            if (!cls.isActor())
                return nullptr;
            return &cls.getCreatureStats(ptr);
        }

        inline MWMechanics::CreatureStats* getActorStats(const ObjectVariant& actor)
        {
            return getActorStats(actor.ptr());
        }

        template <class GetEffectKey>
        sol::optional<ActiveEffect> getActiveEffect(const ObjectVariant& actor, GetEffectKey&& getEffectKey)
        {
            const MWWorld::Ptr& ptr = actor.ptr();
            if (ptr.isEmpty())
                return sol::nullopt;
            const MWWorld::Class& cls = ptr.getClass();
            if (!cls.isActor())
                return sol::nullopt;

            // The native key lookup does not call Lua or change the actor. Keep it
            // before getCreatureStats, which may initialize custom data or throw.
            const MWMechanics::EffectKey key = getEffectKey();
            const auto& store = cls.getCreatureStats(ptr).getMagicEffects();
            if (const auto effect = store.get(key))
                return ActiveEffect{ key, effect.value() };
            return ActiveEffect{ key, MWMechanics::EffectParam() };
        }
    }
}

#endif
