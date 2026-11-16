#ifndef OPENMW_COMPONENTS_RESOURCE_SCENEINSTANCECACHE_H
#define OPENMW_COMPONENTS_RESOURCE_SCENEINSTANCECACHE_H

#include "cachestats.hpp"

#include <osg/Node>

#include <atomic>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace Resource
{
    /// One-use, detached clones. Foreground consumers never wait for a clone in progress.
    /// A preloader keeps a ticket alive; consumption never leaves a live scene node owned by that ticket.
    class SceneInstanceCache
    {
    public:
        struct Revision
        {
            std::uint64_t mSettings;
            std::uint64_t mVfs;
            auto operator<=>(const Revision&) const = default;
        };

        struct Ticket
        {
        };
        using TicketPtr = std::shared_ptr<const Ticket>;

        explicit SceneInstanceCache(
            std::size_t capacity = 512, std::size_t perTemplate = 32, std::size_t byteBudget = 128 * 1024 * 1024)
            : mCapacity(capacity)
            , mPerTemplate(perTemplate)
            , mByteBudget(byteBudget)
        {
        }

        TicketPtr prepare(const osg::Node* base, Revision revision, const std::atomic_bool& abort,
            std::size_t estimatedBytes, const std::function<osg::ref_ptr<osg::Node>()>& clone);
        osg::ref_ptr<osg::Node> take(const osg::Node* base, Revision revision);
        bool hasCapacity(const osg::Node* base, Revision revision) const;
        void prune();
        void clear();
        void releaseGLObjects(osg::State* state);
        CacheStats getStats() const;

    private:
        struct Key
        {
            const osg::Node* mBase;
            Revision mRevision;
            bool operator<(const Key& other) const
            {
                if (mBase != other.mBase)
                    return std::less<const osg::Node*>{}(mBase, other.mBase);
                return mRevision < other.mRevision;
            }
        };
        struct Prepared
        {
            osg::ref_ptr<osg::Node> mNode;
            std::weak_ptr<const Ticket> mOwner;
            std::size_t mBytes;
        };
        struct Bucket
        {
            osg::ref_ptr<const osg::Node> mBase;
            std::vector<Prepared> mReady;
            std::size_t mPending = 0;
        };

        const std::size_t mCapacity;
        const std::size_t mPerTemplate;
        const std::size_t mByteBudget;
        std::size_t mBytes = 0;
        mutable std::mutex mMutex;
        std::map<Key, Bucket> mEntries;
        std::uint64_t mGeneration = 0;
        // Includes reservations, so multiple workers cannot overshoot the limit.
        CacheStats mStats;
    };
}
#endif
