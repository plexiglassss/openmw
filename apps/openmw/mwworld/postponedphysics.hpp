#ifndef OPENMW_MWWORLD_POSTPONEDPHYSICS_H
#define OPENMW_MWWORLD_POSTPONEDPHYSICS_H

#include "ptr.hpp"
#include "cellstore.hpp"
#include <components/misc/combinedperformance.hpp>

namespace MWWorld
{
    template <class ActiveCell, class Insert>
    bool restorePostponedPhysics(const Ptr& ptr, ActiveCell&& activeCell, Insert&& insert)
    {
        if (!ptr.isInCell() || !activeCell(ptr.getCell()))
            return false;
        // Keep the same accessibility rules as CellStore::forEach: count-zero
        // content references are visited, but removed dynamic references are not.
        if (ptr.getRefData().isDeletedByContentFile()
            || (!ptr.getCellRef().hasContentFile() && ptr.getCellRef().getCount() <= 0))
            return false;
        ptr.getRefData().mPhysicsPostponed = false;
        if (ptr.getRefData().isEnabled() && ptr.getCellRef().getCount() > 0)
            insert(ptr);
        return true;
    }

    template <class Registry, class Cells, class Insert>
    void updatePostponedPhysics(Registry& registry, const Cells& cells, Insert&& insert)
    {
        const auto consume = [&](const Ptr& ptr) {
            return restorePostponedPhysics(
                ptr, [&](const CellStore* cell) { return cells.contains(cell); }, insert);
        };
        if (Misc::useSparsePostponedPhysics())
            registry.forEachPostponedPhysics(consume);
        else
            for (auto* cell : cells)
                cell->forEach([&](const Ptr& ptr) {
                    if (ptr.getRefData().mPhysicsPostponed)
                        consume(ptr);
                    return true;
                });
    }
}

#endif
