#ifndef OPENMW_COMPONENTS_SCENEUTIL_SELECTIONREVISION_H
#define OPENMW_COMPONENTS_SCENEUTIL_SELECTIONREVISION_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <limits>
#include <utility>

namespace SceneUtil
{
    // Owner supplies synchronization. Revisions follow source cells, including
    // all subcell chunks which read a cell before filtering reference positions.
    class SelectionRevision
    {
    public:
        void invalidate(int x, int y, bool activeOnly)
        {
            auto& value = mCells[{ x, y }];
            value.second = ++mRevision;
            if (!activeOnly)
                value.first = mRevision;
        }
        void clear()
        {
            mGlobal = ++mRevision;
            mCells.clear();
        }
        std::uint64_t global() const { return mGlobal; }
        std::uint64_t get(float centerX, float centerY, float size, bool active) const
        {
            const int x = static_cast<int>(std::floor(centerX - size / 2));
            const int y = static_cast<int>(std::floor(centerY - size / 2));
            std::uint64_t result = mGlobal;
            for (auto it = mCells.lower_bound({ x, std::numeric_limits<int>::min() });
                 it != mCells.end() && it->first.first < x + size; ++it)
                if (it->first.second >= y && it->first.second < y + size)
                    result = std::max(result, active ? it->second.second : it->second.first);
            return result;
        }

    private:
        std::uint64_t mRevision = 0, mGlobal = 0;
        std::map<std::pair<int, int>, std::pair<std::uint64_t, std::uint64_t>> mCells;
    };
}

#endif
