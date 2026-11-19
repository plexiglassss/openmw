#ifndef OPENMW_COMPONENTS_MISC_BOUNDEDCACHE_H
#define OPENMW_COMPONENTS_MISC_BOUNDEDCACHE_H

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

namespace Misc
{
    // Immutable values, FIFO eviction. Concurrent callers share one retained load;
    // unrelated keys load independently. Failed loads can be retried.
    template <class Key, class Value>
    class BoundedCache
    {
        struct Entry
        {
            std::once_flag ready;
            std::shared_ptr<const Value> value;
        };
        std::mutex mMutex;
        std::map<Key, std::shared_ptr<Entry>> mEntries;
        std::deque<Key> mOrder;
        const std::size_t mCapacity;

    public:
        explicit BoundedCache(std::size_t capacity)
            : mCapacity(std::max(std::size_t{ 1 }, capacity))
        {
        }

        template <class Loader>
        std::shared_ptr<const Value> get(const Key& key, Loader&& loader)
        {
            std::shared_ptr<Entry> entry;
            {
                std::lock_guard lock(mMutex);
                auto it = mEntries.find(key);
                if (it != mEntries.end())
                    entry = it->second;
                else
                {
                    entry = std::make_shared<Entry>();
                    mOrder.push_back(key);
                    try
                    {
                        mEntries.emplace(key, entry);
                    }
                    catch (...)
                    {
                        mOrder.pop_back();
                        throw;
                    }
                    if (mEntries.size() > mCapacity)
                    {
                        mEntries.erase(mOrder.front());
                        mOrder.pop_front();
                    }
                }
            }
            std::call_once(entry->ready, [&] { entry->value = std::make_shared<const Value>(loader()); });
            return entry->value;
        }

        // Existing callers keep their own immutable values alive. In-flight old
        // loads cannot publish into the new generation after clear().
        void clear()
        {
            std::lock_guard lock(mMutex);
            mEntries.clear();
            mOrder.clear();
        }
    };
}
#endif
