#ifndef OPENMW_COMPONENTS_SCENEUTIL_PENDINGPRELOAD_H
#define OPENMW_COMPONENTS_SCENEUTIL_PENDINGPRELOAD_H

#include "workqueue.hpp"
#include <cstdint>
#include <map>
#include <utility>

namespace SceneUtil
{
    template <class Key>
    class PendingPreload
    {
    public:
        using Generation = std::pair<std::uint64_t, std::uint64_t>;
        bool contains(const Key& key, Generation generation) const
        {
            const auto found = mItems.find(key);
            return found != mItems.end() && found->second.mGeneration == generation && !found->second.mItem->isDone();
        }
        void insert(const Key& key, Generation generation, std::shared_ptr<WorkItem> item)
        {
            prune();
            mItems.insert_or_assign(key, Entry{ generation, std::move(item) });
        }
        void prune()
        {
            std::erase_if(mItems, [](const auto& entry) { return entry.second.mItem->isDone(); });
        }
        void clear() { mItems.clear(); }
        std::size_t size() const { return mItems.size(); }

    private:
        struct Entry
        {
            Generation mGeneration;
            std::shared_ptr<WorkItem> mItem;
        };
        std::map<Key, Entry> mItems;
    };
}

#endif
