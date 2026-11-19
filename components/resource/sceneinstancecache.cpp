#include "sceneinstancecache.hpp"

#include <exception>

namespace Resource
{
    SceneInstanceCache::TicketPtr SceneInstanceCache::prepare(const osg::Node* base, Revision revision,
        const std::atomic_bool& abort, std::size_t estimatedBytes,
        const std::function<osg::ref_ptr<osg::Node>()>& clone)
    {
        if (!base || abort.load() || estimatedBytes == 0)
            return {};
        const Key key{ base, revision };
        std::uint64_t generation;
        {
            const std::lock_guard lock(mMutex);
            if (mStats.mSize >= mCapacity || mPerTemplate == 0 || estimatedBytes > mByteBudget - mBytes)
                return {};
            auto& bucket = mEntries[key];
            if (bucket.mReady.size() + bucket.mPending >= mPerTemplate)
                return {};
            bucket.mBase = base;
            ++bucket.mPending;
            ++mStats.mSize;
            mBytes += estimatedBytes;
            generation = mGeneration;
        }

        osg::ref_ptr<osg::Node> instance;
        osg::ref_ptr<const osg::Node> retiredBase;
        TicketPtr ticket;
        std::exception_ptr error;
        try
        {
            if (!abort.load())
            {
                ticket = std::make_shared<const Ticket>();
                instance = clone();
            }
        }
        catch (...)
        {
            error = std::current_exception();
        }
        {
            const std::lock_guard lock(mMutex);
            auto it = mEntries.find(key);
            auto& bucket = it->second;
            --bucket.mPending;
            bool published = false;
            if (instance && !abort.load() && generation == mGeneration)
            {
                try
                {
                    bucket.mReady.push_back({ instance, ticket, estimatedBytes });
                    published = true;
                }
                catch (...)
                {
                    error = std::current_exception();
                }
            }
            if (!published)
            {
                --mStats.mSize;
                mBytes -= estimatedBytes;
                ticket.reset();
            }
            if (bucket.mPending == 0 && bucket.mReady.empty())
            {
                retiredBase = std::move(bucket.mBase);
                mEntries.erase(it);
            }
        }
        if (error)
            std::rethrow_exception(error);
        return ticket;
    }

    osg::ref_ptr<osg::Node> SceneInstanceCache::take(const osg::Node* base, Revision revision)
    {
        osg::ref_ptr<const osg::Node> retiredBase;
        const std::lock_guard lock(mMutex);
        ++mStats.mGet;
        auto it = mEntries.find({ base, revision });
        if (it == mEntries.end() || it->second.mReady.empty())
            return {};
        osg::ref_ptr<osg::Node> instance;
        auto& ready = it->second.mReady;
        for (auto entry = ready.begin(); entry != ready.end(); ++entry)
        {
            if (!entry->mOwner.expired())
            {
                instance = std::move(entry->mNode);
                --mStats.mSize;
                mBytes -= entry->mBytes;
                ready.erase(entry);
                ++mStats.mHit;
                break;
            }
            // Leave expired graphs to worker-side maintenance, not the crossing frame.
        }
        if (ready.empty() && it->second.mPending == 0)
        {
            retiredBase = std::move(it->second.mBase);
            mEntries.erase(it);
        }
        return instance;
    }

    void SceneInstanceCache::prune()
    {
        std::vector<osg::ref_ptr<const osg::Node>> retired;
        const std::lock_guard lock(mMutex);
        retired.reserve(mStats.mSize + mEntries.size());
        for (auto it = mEntries.begin(); it != mEntries.end();)
        {
            std::erase_if(it->second.mReady, [&](auto& node) {
                if (!node.mOwner.expired())
                    return false;
                mBytes -= node.mBytes;
                retired.push_back(std::move(node.mNode));
                --mStats.mSize;
                ++mStats.mExpired;
                return true;
            });
            if (it->second.mReady.empty() && it->second.mPending == 0)
            {
                retired.push_back(std::move(it->second.mBase));
                it = mEntries.erase(it);
            }
            else
                ++it;
        }
        // retired is destroyed after the lock.
    }

    void SceneInstanceCache::clear()
    {
        std::vector<osg::ref_ptr<const osg::Node>> retired;
        const std::lock_guard lock(mMutex);
        retired.reserve(mStats.mSize + mEntries.size());
        ++mGeneration;
        for (auto it = mEntries.begin(); it != mEntries.end();)
        {
            auto& bucket = it->second;
            mStats.mSize -= bucket.mReady.size();
            for (auto& node : bucket.mReady)
            {
                mBytes -= node.mBytes;
                retired.push_back(std::move(node.mNode));
            }
            bucket.mReady.clear();
            if (bucket.mPending == 0)
            {
                retired.push_back(std::move(bucket.mBase));
                it = mEntries.erase(it);
            }
            else
                ++it;
        }
        // Keep reservations until their workers return. Clear cannot permit an allocation burst.
    }

    void SceneInstanceCache::releaseGLObjects(osg::State* state)
    {
        std::vector<osg::ref_ptr<osg::Node>> instances;
        {
            const std::lock_guard lock(mMutex);
            for (const auto& [key, bucket] : mEntries)
                for (const auto& instance : bucket.mReady)
                    instances.push_back(instance.mNode);
        }
        for (const auto& instance : instances)
            instance->releaseGLObjects(state);
    }

    CacheStats SceneInstanceCache::getStats() const
    {
        const std::lock_guard lock(mMutex);
        return mStats;
    }

    bool SceneInstanceCache::hasCapacity(const osg::Node* base, Revision revision) const
    {
        const std::lock_guard lock(mMutex);
        if (mStats.mSize >= mCapacity || mBytes >= mByteBudget || mPerTemplate == 0)
            return false;
        const auto found = mEntries.find({ base, revision });
        return found == mEntries.end() || found->second.mPending + found->second.mReady.size() < mPerTemplate;
    }
}
