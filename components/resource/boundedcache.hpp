#ifndef OPENMW_COMPONENTS_RESOURCE_BOUNDEDCACHE_H
#define OPENMW_COMPONENTS_RESOURCE_BOUNDEDCACHE_H

#include "cachestats.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <list>
#include <iterator>
#include <utility>
#include <map>
#include <mutex>
#include <vector>

namespace Resource
{
    inline double cacheClock()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Additional ownership of immutable resources only. The byte count is an admission
    // estimate, not measured process memory. Destruction never runs under the cache lock.
    template <class Key, class Value>
    class BoundedCache
    {
        struct Entry
        {
            Value mValue;
            std::size_t mBytes;
            double mTouched;
            typename std::list<Key>::iterator mOrder;
        };
        mutable std::mutex mMutex;
        std::map<Key, Entry> mEntries;
        std::list<Key> mOrder;
        std::size_t mBytes = 0;
        const std::size_t mCapacity;
        const std::size_t mBudget;
        const double mLifetime;
        std::uint64_t mGeneration = 0;
        CacheStats mStats;

        void erase(typename std::map<Key, Entry>::iterator it, std::vector<Value>& retired)
        {
            // Allocate retirement space before changing either index/accounting.
            // Allocation failure leaves the live cache structurally intact.
            retired.push_back(std::move(it->second.mValue));
            mBytes -= it->second.mBytes;
            mOrder.erase(it->second.mOrder);
            mEntries.erase(it);
            ++mStats.mExpired;
        }

    public:
        struct Stats
        {
            CacheStats mCache;
            std::size_t mBytes;
            std::size_t mBudget;
        };

        BoundedCache(std::size_t capacity, std::size_t budget, double lifetime)
            : mCapacity(capacity), mBudget(budget), mLifetime(lifetime)
        {
        }

        ~BoundedCache() { clear(); }

        std::uint64_t generation() const
        {
            const std::lock_guard lock(mMutex);
            return mGeneration;
        }

        Value get(const Key& key, double now = cacheClock())
        {
            std::vector<Value> retired;
            const std::lock_guard lock(mMutex);
            ++mStats.mGet;
            auto it = mEntries.find(key);
            if (it == mEntries.end())
                return {};
            if (!std::isfinite(now) || now < it->second.mTouched || now - it->second.mTouched > mLifetime)
            {
                erase(it, retired);
                return {};
            }
            it->second.mTouched = now;
            mOrder.splice(mOrder.end(), mOrder, it->second.mOrder);
            ++mStats.mHit;
            return it->second.mValue;
        }

        bool put(const Key& key, Value value, std::size_t bytes, std::uint64_t generation,
            double now = cacheClock())
        {
            std::vector<Value> retired;
            const std::lock_guard lock(mMutex);
            if (generation != mGeneration || !mCapacity || !bytes || bytes > mBudget || !std::isfinite(now))
                return false;
            if (auto it = mEntries.find(key); it != mEntries.end())
                erase(it, retired);
            while (mEntries.size() >= mCapacity || mBytes > mBudget - bytes)
                erase(mEntries.find(mOrder.front()), retired);
            mOrder.push_back(key);
            try
            {
                // Retain the parameter until after the lock is released, including
                // the allocation-failure path while constructing the map node.
                mEntries.emplace(key, Entry{ value, bytes, now, std::prev(mOrder.end()) });
            }
            catch (...)
            {
                mOrder.pop_back();
                throw;
            }
            mBytes += bytes;
            return true;
        }

        void prune(double now = cacheClock())
        {
            std::vector<Value> retired;
            const std::lock_guard lock(mMutex);
            for (auto it = mEntries.begin(); it != mEntries.end();)
            {
                auto current = it++;
                if (!std::isfinite(now) || now < current->second.mTouched || now - current->second.mTouched > mLifetime)
                    erase(current, retired);
            }
        }

        void clear()
        {
            std::map<Key, Entry> retired;
            const std::lock_guard lock(mMutex);
            ++mGeneration;
            mStats.mExpired += mEntries.size();
            retired.swap(mEntries);
            mOrder.clear();
            mBytes = 0;
        }

        std::vector<Value> values() const
        {
            const std::lock_guard lock(mMutex);
            std::vector<Value> result;
            result.reserve(mEntries.size());
            for (const auto& [key, entry] : mEntries)
                result.push_back(entry.mValue);
            return result;
        }

        Stats stats() const
        {
            const std::lock_guard lock(mMutex);
            auto stats = mStats;
            stats.mSize = mEntries.size();
            return { stats, mBytes, mBudget };
        }
    };
}
#endif
