#ifndef OPENMW_COMPONENTS_TERRAIN_ARRIVALVIEW_H
#define OPENMW_COMPONENTS_TERRAIN_ARRIVALVIEW_H

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include <osg/Node>
#include <osg/Vec3f>
#include <osg/Vec4i>

namespace Terrain
{
    class QuadTreeNode;

    // Only completed scene nodes cross the worker/cull boundary. The worker's mutable
    // ViewData, its LOD flags and its neighbour index stay on the worker.
    class ArrivalView
    {
    public:
        using Key = std::pair<const QuadTreeNode*, unsigned int>;
        using Node = osg::ref_ptr<osg::Node>;
        using Progress = std::function<void(std::size_t, std::size_t)>;
        struct Entry
        {
            Key key;
            Node node;
            std::size_t bytes;
        };

        ArrivalView(const osg::Vec3f& point, const osg::Vec4i& grid, std::uint64_t revision, std::uint64_t vfsRevision = 0,
            std::uint64_t modelRevision = 0)
            : mPoint(point), mGrid(grid), mRevision(revision), mVfsRevision(vfsRevision), mModelRevision(modelRevision) {}

        bool matches(const osg::Vec3f& position, const osg::Vec4i& grid, float distance, std::uint64_t revision) const
        {
            return mGrid == grid && mRevision == revision && (position - mPoint).length2() <= distance * distance;
        }

        void publish(const std::vector<Entry>& batch)
        {
            const std::lock_guard lock(mMutex);
            for (const auto& entry : batch)
                if (entry.node && mNodes.try_emplace(entry.key, entry.node).second)
                    mBytes += entry.bytes + sizeof(Entry);
        }

        // Render only nodes already published by the worker. Each camera shares the first
        // attachment budget, including if a different camera selected different LOD nodes.
        bool admitDistant(unsigned frame, const Key& key)
        {
            const std::lock_guard lock(mMutex);
            if (mReturnReady || mOffered.contains(key))
                return true;
            if (frame != mFrame)
            {
                mFrame = frame;
                mFrameAdmissions = 0;
                mAttachmentTime = {};
            }
            if (mFrameAdmissions >= 4 || mAttachmentTime >= std::chrono::milliseconds(2))
                return false;
            mOffered.insert(key);
            ++mFrameAdmissions;
            return true;
        }
        void chargeTraversal(std::chrono::steady_clock::duration duration)
        {
            const std::lock_guard lock(mMutex);
            mAttachmentTime += duration;
        }
        void presentRetainedView()
        {
            const std::lock_guard lock(mMutex);
            mReturnReady = mComplete;
        }
        Node find(const Key& key) const
        {
            const std::lock_guard lock(mMutex);
            const auto it = mNodes.find(key);
            return it == mNodes.end() ? nullptr : it->second;
        }
        void finish(bool success)
        {
            const std::lock_guard lock(mMutex);
            mStopped = true;
            mComplete = success;
        }
        bool streaming() const
        {
            const std::lock_guard lock(mMutex);
            return !mStopped;
        }
        bool complete() const { const std::lock_guard lock(mMutex); return mComplete; }
        std::size_t bytes() const
        {
            const std::lock_guard lock(mMutex);
            return mBytes + (mNodes.size() + mOffered.size()) * (sizeof(Key) + 4 * sizeof(void*));
        }
        const osg::Vec3f mPoint;
        const osg::Vec4i mGrid;
        const std::uint64_t mRevision;
        const std::uint64_t mVfsRevision;
        const std::uint64_t mModelRevision;

    private:
        mutable std::mutex mMutex;
        std::map<Key, Node> mNodes;
        std::set<Key> mOffered;
        unsigned mFrame = std::numeric_limits<unsigned>::max();
        std::size_t mFrameAdmissions = 0;
        bool mReturnReady = false;
        std::chrono::steady_clock::duration mAttachmentTime{};
        bool mStopped = false;
        std::size_t mBytes = 0;
        bool mComplete = false;
    };
}
#endif
