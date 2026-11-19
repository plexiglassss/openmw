#ifndef OPENMW_COMPONENTS_UNREFQUEUE_H
#define OPENMW_COMPONENTS_UNREFQUEUE_H

#include <osg/Referenced>
#include <osg/ref_ptr>

#include "workqueue.hpp"
#include <deque>
#include <vector>

namespace SceneUtil
{
    class WorkQueue;

    /// @brief Handles unreferencing of objects through the WorkQueue. Typical use scenario
    /// would be the main thread pushing objects that are no longer needed, and the background thread deleting them.
    class UnrefQueue
    {
    public:
        // Limit retained queued references, not estimated bytes of shared graphs.
        static constexpr std::size_t MaxQueuedObjects = 16384;
        static constexpr std::size_t ObjectsPerSlice = 64;
        /// Adds an object to the list of objects to be unreferenced. Call from the main thread.
        void push(osg::ref_ptr<osg::Referenced>&& obj) { mObjects.push_back(std::move(obj)); }

        void push(const osg::ref_ptr<osg::Referenced>& obj) { mObjects.push_back(obj); }

        /// Adds a WorkItem to the given WorkQueue that will clear the list of objects in a worker thread,
        /// thus unreferencing them. Call from the main thread.
        void flush(SceneUtil::WorkQueue& workQueue);

        std::size_t getSize() const { return mObjects.size() + mQueuedObjects; }
        std::size_t getPeakQueuedObjects() const { return mPeakQueuedObjects; }
        std::size_t getBackpressureWaits() const { return mBackpressureWaits; }

    private:
        std::vector<osg::ref_ptr<osg::Referenced>> mObjects;
        struct Pending
        {
            osg::ref_ptr<WorkItem> mItem;
            std::size_t mObjects;
        };
        std::deque<Pending> mPending;
        std::size_t mQueuedObjects = 0;
        std::size_t mPeakQueuedObjects = 0;
        std::size_t mBackpressureWaits = 0;
        void reap();
    };
}

#endif
