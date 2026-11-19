#ifndef OPENMW_COMPONENTS_ESM3_DUNGEONSTATES_H
#define OPENMW_COMPONENTS_ESM3_DUNGEONSTATES_H

#include <cstdint>
#include <map>

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // Matches Been There, Done That's per-interior semantics. A peaceful room
    // first visited without hostiles is not a cleared dungeon.
    enum class DungeonStatus : std::uint8_t
    {
        Invalid = 0,
        Uncleared = 1,
        Cleared = 2,
    };

    // Optional save record, separate from actor/cell records. Older engines can
    // skip it, and string identities do not depend on plugin load-order slots.
    struct DungeonStates
    {
        constexpr static RecNameInts sRecordId = REC_DCLR;
        std::map<RefId, DungeonStatus> mStates;

        bool observe(const RefId& cellId, bool hasLivingHostiles);
        bool isCleared(const RefId& cellId) const;
        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}

#endif
