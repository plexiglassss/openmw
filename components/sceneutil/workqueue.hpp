#ifndef OPENMW_COMPONENTS_SCENEUTIL_WORKQUEUE_H
#define OPENMW_COMPONENTS_SCENEUTIL_WORKQUEUE_H

#include <osg/Referenced>
#include <osg/ref_ptr>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace SceneUtil
{

    class WorkItem : public osg::Referenced
    {
    public:
        /// Override in a derived WorkItem to perform actual work.
        virtual void doWork() {}

        /// Return false to yield an unfinished item. Completion is published
        /// only after its final slice, or cancellation of queued work.
        virtual bool doWorkSlice()
        {
            doWork();
            return true;
        }
        virtual bool isMaintenance() const { return false; }

        bool isDone() const;

        /// Wait until the work is completed. Usually called from the main thread.
        void waitTillDone();

        /// Internal use by the WorkQueue.
        void signalDone();

        /// Set abort flag in order to return from doWork() as soon as possible. May not be respected by all WorkItems.
        virtual void abort() {}

    private:
        std::atomic_bool mDone{ false };
        std::mutex mMutex;
        std::condition_variable mCondition;
    };

    class WorkThread;

    /// @brief A work queue that users can push work items onto, to be completed by one or more background threads.
    /// @note Work items will be processed in the order that they were given in, however
    /// if multiple work threads are involved then it is possible for a later item to complete before earlier items.
    class WorkQueue : public osg::Referenced
    {
    public:
        WorkQueue(std::size_t workerThreads);
        ~WorkQueue();

        void start(std::size_t workerThreads);

        void stop();

        /// Add a new work item to the back of the queue.
        /// @par The work item's waitTillDone() method may be used by the caller to wait until the work is complete.
        /// @param front If true, add item to the front of the queue. If false (default), add to the back.
        void addWorkItem(osg::ref_ptr<WorkItem> item, bool front = false);

        /// Abort an item and remove it if it has not started. Running work keeps its
        /// owner and completion signal until doWork returns; it is never joined here.
        bool cancelWorkItem(WorkItem* item);
        /// Promote queued prediction to required work without enqueuing it twice.
        bool promoteWorkItem(WorkItem* item);

        /// Internal continuation; rejects shutdown races and signals completion.
        void continueWorkItem(osg::ref_ptr<WorkItem> item);

        /// Get the next work item from the front of the queue. If the queue is empty, waits until a new item is added.
        /// If the workqueue is in the process of being destroyed, may return nullptr.
        /// @par Used internally by the WorkThread.
        osg::ref_ptr<WorkItem> removeWorkItem();

        size_t getNumItems() const;

        size_t getNumActiveThreads() const;

    private:
        bool mIsReleased;
        std::deque<osg::ref_ptr<WorkItem>> mQueue;
        std::deque<osg::ref_ptr<WorkItem>> mMaintenance;
        std::size_t mNormalSinceMaintenance = 0;

        mutable std::mutex mMutex;
        std::condition_variable mCondition;

        std::vector<std::unique_ptr<WorkThread>> mThreads;
    };

    /// Internally used by WorkQueue.
    class WorkThread
    {
    public:
        WorkThread(WorkQueue& workQueue);

        ~WorkThread();

        bool isActive() const;

    private:
        WorkQueue* mWorkQueue;
        std::atomic<bool> mActive;
        std::thread mThread;

        void run();
    };

}

#endif
