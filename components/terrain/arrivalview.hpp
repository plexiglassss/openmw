#ifndef OPENMW_COMPONENTS_TERRAIN_ARRIVALVIEW_H
#define OPENMW_COMPONENTS_TERRAIN_ARRIVALVIEW_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <osg/GraphicsContext>
#include <osgUtil/IncrementalCompileOperation>
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
    struct SceneryView
    {
        virtual ~SceneryView() = default;
        std::size_t mEstimatedBytes = 0;
    };

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
            std::shared_ptr<std::atomic_bool> graphicsReady{};
        };
        struct CameraNode
        {
            Node node;
            bool ground = false;
        };

        ArrivalView(const osg::Vec3f& point, const osg::Vec4i& grid, std::uint64_t revision, std::uint64_t vfsRevision = 0,
            std::uint64_t modelRevision = 0)
            : mPoint(point), mGrid(grid), mRevision(revision), mVfsRevision(vfsRevision), mModelRevision(modelRevision) {}

        ~ArrivalView() { cancel(); }

        bool cancelled() const { return mCancelled.load(std::memory_order_acquire); }
        void cancel()
        {
            std::vector<osg::ref_ptr<osgUtil::IncrementalCompileOperation::CompileSet>> retired;
            {
                const std::lock_guard lock(mMutex);
                mCancelled.store(true, std::memory_order_release);
                mStopped = true;
                mComplete = false;
                retired.swap(mCompileSets);
                mCapacity.notify_all();
            }
            // OSG may already be executing one compile unit. remove() prevents
            // further queued work; that current unit retains its graph until done.
            if (mCompile)
                for (auto& set : retired)
                    mCompile->remove(set);
        }
        bool enqueueCompile(osgUtil::IncrementalCompileOperation::CompileSet* set)
        {
            const std::lock_guard lock(mMutex);
            if (mCancelled || !mCompile)
                return false;
            mCompileSets.emplace_back(set);
            mCompile->add(set, false);
            return true;
        }
        void acknowledgeVisible(const Key& key)
        {
            const std::lock_guard lock(mMutex);
            mOffered.insert(key);
            mCapacity.notify_all();
        }

        bool matches(const osg::Vec3f& position, const osg::Vec4i& grid, float distance, std::uint64_t revision) const
        {
            return mGrid == grid && mRevision == revision && (position - mPoint).length2() <= distance * distance;
        }

        void publish(const std::vector<Entry>& batch)
        {
            const std::lock_guard lock(mMutex);
            if (mCancelled)
                return;
            for (const auto& entry : batch)
                if (entry.node && mNodes.try_emplace(entry.key, entry).second)
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
            return it == mNodes.end() ? nullptr : it->second.node;
        }
        void publishGround(const Entry& entry)
        {
            const std::lock_guard lock(mMutex);
            if (!mCancelled && entry.node && mGround.try_emplace(entry.key, entry.node).second)
                mBytes += entry.bytes + sizeof(Entry);
        }
        bool waitForDistantCapacity(const std::atomic_bool& abort,
            const std::function<bool()>& valid = {}) const
        {
            // The final view owns its visible graphs as usual. Only the not-yet-
            // admitted backlog is limited here. One indivisible chunk may exceed
            // the byte target; the next cannot start until it has been admitted.
            std::unique_lock lock(mMutex);
            while (!abort && !mCancelled && (!valid || valid()))
            {
                std::size_t count = 0, bytes = 0;
                for (const auto& [key, entry] : mNodes)
                    if (mGround.contains(key) && (!mOffered.contains(key)
                            || (entry.graphicsReady && !entry.graphicsReady->load(std::memory_order_acquire))))
                    {
                        ++count;
                        bytes += entry.bytes;
                    }
                if (count < 8 && bytes < 256ull * 1024 * 1024)
                    return true;
                mCapacity.wait_for(lock, std::chrono::milliseconds(5));
            }
            return false;
        }
        void markActiveReady()
        {
            const std::lock_guard lock(mMutex);
            mActiveReady = true;
        }
        bool activeReady() const
        {
            const std::lock_guard lock(mMutex);
            return !mCancelled && mActiveReady && (!mStopped || mComplete);
        }
        CameraNode cameraNode(const Key& key, unsigned frame)
        {
            // Only the cull thread admits distant objects. Queries can use CPU-ready
            // full nodes through find(), but must not certify a ground-only node.
            const std::lock_guard lock(mMutex);
            const auto ground = mGround.find(key);
            const auto full = mNodes.find(key);
            if (ground == mGround.end())
                return { full == mNodes.end() ? nullptr : full->second.node, false };
            if (full != mNodes.end() && full->second.graphicsReady
                && full->second.graphicsReady->load(std::memory_order_acquire))
            {
                if (frame != mFrame)
                {
                    mFrame = frame;
                    mFrameAdmissions = 0;
                    mAttachmentTime = {};
                }
                if (mOffered.contains(key)
                    || (mFrameAdmissions < 4 && mAttachmentTime < std::chrono::milliseconds(2)))
                {
                    if (mOffered.insert(key).second)
                    {
                        ++mFrameAdmissions;
                        mCapacity.notify_all();
                    }
                    return { full->second.node, false };
                }
            }
            return { ground->second, true };
        }
        // Captured by createArrival on the scene owner before starting the worker.
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> mCompile;
        std::uint64_t mGraphicsRevision = 0, mShaderRevision = 0;
        std::set<osg::GraphicsContext*> mCompileContexts;
        std::map<osg::GraphicsContext*, unsigned> mContextIds;
        std::vector<osg::ref_ptr<osg::GraphicsContext>> mContextOwners;
        void publishScenery(std::shared_ptr<SceneryView> scenery)
        {
            const std::lock_guard lock(mMutex);
            mScenery = std::move(scenery);
        }
        std::shared_ptr<SceneryView> scenery() const
        {
            const std::lock_guard lock(mMutex);
            return mScenery;
        }
        void finish(bool success)
        {
            const std::lock_guard lock(mMutex);
            mStopped = true;
            mComplete = success && !mCancelled;
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
            return mBytes + (mScenery ? mScenery->mEstimatedBytes : 0)
                + (mNodes.size() + mOffered.size()) * (sizeof(Key) + 4 * sizeof(void*));
        }
        const osg::Vec3f mPoint;
        const osg::Vec4i mGrid;
        const std::uint64_t mRevision;
        const std::uint64_t mVfsRevision;
        const std::uint64_t mModelRevision;
        const std::chrono::steady_clock::time_point mCreated = std::chrono::steady_clock::now();

    private:
        mutable std::mutex mMutex;
        mutable std::condition_variable mCapacity;
        std::atomic_bool mCancelled{ false };
        std::vector<osg::ref_ptr<osgUtil::IncrementalCompileOperation::CompileSet>> mCompileSets;
        std::map<Key, Entry> mNodes;
        std::map<Key, Node> mGround;
        bool mActiveReady = false;
        std::shared_ptr<SceneryView> mScenery;
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
