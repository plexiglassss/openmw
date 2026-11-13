#ifndef OPENMW_MWWORLD_QUERYREVISION_H
#define OPENMW_MWWORLD_QUERYREVISION_H

#include <atomic>
#include <cstdint>

namespace MWWorld
{
    // Cell query membership can change without insertion or movement: scripts
    // can delete/undelete a reference, and save loading can replace its state.
    // This deliberately conservative epoch does not depend on Environment or
    // ownership, so it also covers references not registered in a world yet.
    inline std::atomic<std::uint64_t> sReferenceQueryRevision{ 0 };

    inline std::uint64_t getReferenceQueryRevision() noexcept
    {
        return sReferenceQueryRevision.load(std::memory_order_relaxed);
    }

    inline void invalidateReferenceQueries() noexcept
    {
        sReferenceQueryRevision.fetch_add(1, std::memory_order_relaxed);
    }

    // Keep default memberwise assignment semantics while making replacement of
    // CellRef/RefData observable to query caches. Put this member first so even
    // an assignment that throws after changing part of the state invalidates.
    struct QueryMutation
    {
        QueryMutation() = default;
        QueryMutation(const QueryMutation&) = default;
        QueryMutation(QueryMutation&&) noexcept { invalidateReferenceQueries(); }
        QueryMutation& operator=(const QueryMutation&) noexcept
        {
            invalidateReferenceQueries();
            return *this;
        }
        QueryMutation& operator=(QueryMutation&&) noexcept
        {
            invalidateReferenceQueries();
            return *this;
        }
    };
}

#endif
