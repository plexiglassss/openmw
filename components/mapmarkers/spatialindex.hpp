#ifndef OPENMW_COMPONENTS_MAPMARKERS_SPATIALINDEX_HPP
#define OPENMW_COMPONENTS_MAPMARKERS_SPATIALINDEX_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <set>

namespace MapMarkers
{
    // Occupied integer cell buckets only. Discovery owns the actual records;
    // this index stores their keys and never looks up or loads world data.
    template <class Key, class Compare = std::less<Key>>
    class SpatialIndex
    {
        std::map<int, std::map<int, std::set<Key, Compare>>> mBuckets;

    public:
        struct QueryStats
        {
            std::size_t xBuckets = 0;
            std::size_t yBuckets = 0;
            std::size_t keys = 0;
        };

        bool insert(int x, int y, const Key& key)
        {
            return mBuckets[x][y].insert(key).second;
        }

        bool erase(int x, int y, const Key& key)
        {
            const auto column = mBuckets.find(x);
            if (column == mBuckets.end())
                return false;
            const auto bucket = column->second.find(y);
            if (bucket == column->second.end() || bucket->second.erase(key) == 0)
                return false;
            if (bucket->second.empty())
                column->second.erase(bucket);
            if (column->second.empty())
                mBuckets.erase(column);
            return true;
        }

        void clear() { mBuckets.clear(); }
        bool empty() const { return mBuckets.empty(); }

        // Inclusive bounds. Deterministic x, y, key order matches the original
        // native map index. Unoccupied and out-of-view buckets are not visited.
        // Callers may ignore stats; inlining removes unused counters.
        template <class Visitor>
        QueryStats query(int minX, int minY, int maxX, int maxY, Visitor&& visitor) const
        {
            QueryStats stats;
            if (minX > maxX || minY > maxY)
                return stats;
            for (auto column = mBuckets.lower_bound(minX); column != mBuckets.end() && column->first <= maxX; ++column)
            {
                ++stats.xBuckets;
                for (auto bucket = column->second.lower_bound(minY);
                     bucket != column->second.end() && bucket->first <= maxY; ++bucket)
                {
                    ++stats.yBuckets;
                    for (const auto& key : bucket->second)
                    {
                        ++stats.keys;
                        visitor(key);
                    }
                }
            }
            return stats;
        }
    };
}

#endif
