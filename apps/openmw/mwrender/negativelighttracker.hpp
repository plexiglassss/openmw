#ifndef OPENMW_MWRENDER_NEGATIVELIGHTTRACKER_H
#define OPENMW_MWRENDER_NEGATIVELIGHTTRACKER_H

#include <cstddef>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace MWRender
{
    // Scene-thread membership of actually attached native light sources. Identities
    // are non-owning and never dereferenced. Previews do not attach to this tracker.
    // Updates are transactional: replacing a source or moving an owner cannot
    // publish an intermediate empty-cell state. Pulse/flicker never update it.
    class NegativeLightTracker
    {
    public:
        using Identity = const void*;
        using Callback = std::function<void(Identity)>;

        void setCallback(Callback callback) { mCallback = std::move(callback); }

        std::size_t count(Identity cell) const
        {
            const auto found = mCells.find(cell);
            return found == mCells.end() ? 0 : found->second;
        }

        void set(Identity owner, Identity cell, std::size_t lights)
        {
            const auto found = mOwners.find(owner);
            const Identity oldCell = found == mOwners.end() ? nullptr : found->second.first;
            const std::size_t oldLights = found == mOwners.end() ? 0 : found->second.second;
            if (!cell)
                lights = 0;
            if (oldCell == cell && oldLights == lights)
                return;

            const bool oldPresent = count(oldCell) != 0;
            const bool newPresent = count(cell) != 0;
            if (found != mOwners.end())
            {
                auto old = mCells.find(oldCell);
                old->second -= oldLights;
                if (old->second == 0)
                    mCells.erase(old);
                mOwners.erase(found);
            }
            if (lights)
            {
                mOwners.emplace(owner, std::pair(cell, lights));
                mCells[cell] += lights;
            }

            if (mCallback && oldCell && oldPresent != (count(oldCell) != 0))
                mCallback(oldCell);
            if (mCallback && cell && cell != oldCell && newPresent != (count(cell) != 0))
                mCallback(cell);
        }

        void erase(Identity owner) { set(owner, nullptr, 0); }

        void clear()
        {
            std::vector<Identity> cells;
            cells.reserve(mCells.size());
            for (const auto& [cell, count] : mCells)
                cells.push_back(cell);
            mOwners.clear();
            mCells.clear();
            if (mCallback)
                for (Identity cell : cells)
                    mCallback(cell);
        }

    private:
        std::map<Identity, std::pair<Identity, std::size_t>> mOwners;
        std::map<Identity, std::size_t> mCells;
        Callback mCallback;
    };
}

#endif
