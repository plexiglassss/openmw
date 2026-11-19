#ifndef OPENMW_COMPONENTS_SCENEUTIL_PRELOADPLANCACHE_H
#define OPENMW_COMPONENTS_SCENEUTIL_PRELOADPLANCACHE_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>

namespace SceneUtil
{
    // Main-thread plans only. A hit reuses topology, never skips the caller's
    // current eligibility checks, request refreshes or arrival prioritization.
    template <class Key, class Revision, class Value>
    class PreloadPlanCache
    {
    public:
        explicit PreloadPlanCache(std::size_t capacity)
            : mCapacity(capacity)
        {
            if (!capacity)
                throw std::invalid_argument("Preload plan capacity must be positive");
        }

        template <class Make>
        const Value& get(const Key& key, const Revision& revision, Make&& make)
        {
            auto found = mEntries.find(key);
            if (found != mEntries.end() && found->second.mRevision == revision)
            {
                found->second.mUse = ++mUse;
                ++mHits;
                return found->second.mValue;
            }
            // Build before replacing the previous entry: failure must remain retryable.
            Value value = make();
            if (found == mEntries.end() && mEntries.size() >= mCapacity)
                mEntries.erase(std::min_element(mEntries.begin(), mEntries.end(),
                    [](const auto& a, const auto& b) { return a.second.mUse < b.second.mUse; }));
            ++mBuilds;
            return mEntries.insert_or_assign(key, Entry{ revision, std::move(value), ++mUse }).first->second.mValue;
        }

        void erase(const Key& key) { mEntries.erase(key); }
        void clear() { mEntries.clear(); }
        std::size_t size() const { return mEntries.size(); }
        std::size_t hits() const { return mHits; }
        std::size_t builds() const { return mBuilds; }

    private:
        struct Entry
        {
            Revision mRevision;
            Value mValue;
            std::uint64_t mUse;
        };
        std::map<Key, Entry> mEntries;
        std::size_t mCapacity;
        std::uint64_t mUse = 0;
        std::size_t mHits = 0;
        std::size_t mBuilds = 0;
    };
}

#endif
