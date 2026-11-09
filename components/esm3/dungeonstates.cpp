#include "dungeonstates.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

#include <stdexcept>

namespace ESM
{
    bool DungeonStates::observe(const RefId& cellId, bool hasLivingHostiles)
    {
        if (cellId.empty())
            return false;

        const auto it = mStates.find(cellId);
        // As in the original mod, interiors first visited without hostiles are
        // permanently excluded, rather than labelling shops and homes cleared.
        if (it != mStates.end() && it->second == DungeonStatus::Invalid)
            return false;

        const DungeonStatus next = hasLivingHostiles ? DungeonStatus::Uncleared
            : it == mStates.end()                    ? DungeonStatus::Invalid
                                                    : DungeonStatus::Cleared;
        if (it != mStates.end() && it->second == next)
            return false;
        mStates.insert_or_assign(cellId, next);
        return true;
    }

    bool DungeonStates::isCleared(const RefId& cellId) const
    {
        const auto it = mStates.find(cellId);
        return it != mStates.end() && it->second == DungeonStatus::Cleared;
    }

    void DungeonStates::load(ESMReader& esm)
    {
        mStates.clear();
        std::uint32_t version = 0;
        esm.getHNT(version, "VERS");
        if (version != 1)
        {
            esm.skipRecord();
            return;
        }

        // Read only entries bounded by the record, never a save-supplied count.
        while (esm.hasMoreSubs())
        {
            const RefId cellId = esm.getHNRefId("NAME");
            std::uint8_t status = 0;
            esm.getHNT(status, "STAT");
            if (cellId.empty())
                esm.fail("Empty cleared-dungeon cell identity");
            if (status > static_cast<std::uint8_t>(DungeonStatus::Cleared))
                esm.fail("Invalid cleared-dungeon status");
            if (!mStates.emplace(cellId, static_cast<DungeonStatus>(status)).second)
                esm.fail("Duplicate cleared-dungeon cell identity");
        }
    }

    void DungeonStates::save(ESMWriter& esm) const
    {
        esm.writeHNT("VERS", std::uint32_t{ 1 });
        for (const auto& [cellId, status] : mStates)
        {
            if (cellId.empty() || static_cast<std::uint8_t>(status) > static_cast<std::uint8_t>(DungeonStatus::Cleared))
                throw std::runtime_error("Invalid cleared-dungeon state");
            esm.writeHNRefId("NAME", cellId);
            esm.writeHNT("STAT", static_cast<std::uint8_t>(status));
        }
    }
}
