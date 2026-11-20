#include "workqueue.hpp"

#include <components/debug/debuglog.hpp>

#include <numeric>
#include <algorithm>

namespace SceneUtil
{

    void WorkItem::waitTillDone()
    {
        if (mDone)
            return;

        std::unique_lock<std::mutex> lock(mMutex);
        while (!mDone)
        {
            mCondition.wait(lock);
        }
    }

    void WorkItem::signalDone()
    {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mDone = true;
        }
        mCondition.notify_all();
    }

    bool WorkItem::isDone() const
    {
        return mDone;
    }

    WorkQueue::WorkQueue(std::size_t workerThreads)
        : mIsReleased(false)
    {
        start(workerThreads);
    }

    WorkQueue::~WorkQueue()
    {
        stop();
    }

    void WorkQueue::start(std::size_t workerThreads)
    {
        {
            const std::lock_guard lock(mMutex);
            mIsReleased = false;
        }
        while (mThreads.size() < workerThreads)
            mThreads.emplace_back(std::make_unique<WorkThread>(*this));
    }

    void WorkQueue::stop()
    {
        std::deque<std::shared_ptr<WorkItem>> discarded, maintenance;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mIsReleased = true;
            discarded.swap(mQueue);
            maintenance.swap(mMaintenance);
            mCondition.notify_all();
        }
        // Abort, destruct and notify outside the queue lock. Destructors may
        // touch other queues, and owners must not wait forever for dropped work.
        for (auto& items : { &discarded, &maintenance })
            for (auto& item : *items)
            {
                item->abort();
                item->signalDone();
            }
        discarded.clear();
        maintenance.clear();
        mThreads.clear();
    }

    void WorkQueue::addWorkItem(std::shared_ptr<WorkItem> item, bool front)
    {
        if (item->isDone())
        {
            Log(Debug::Error) << "Error: trying to add a work item that is already completed";
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mMutex);
            if (!mIsReleased)
            {
                auto& queue = item->isMaintenance() ? mMaintenance : mQueue;
                if (front)
                {
                    item->mHighPriority = true;
                    queue.push_front(std::move(item));
                }
                else if (item->mHighPriority && !item->isMaintenance())
                {
                    // A required continuation follows other required jobs, including
                    // newly promoted nearer cells, but precedes speculative work.
                    const auto next = std::find_if(queue.begin(), queue.end(),
                        [](const auto& value) { return !value->mHighPriority.load(); });
                    queue.insert(next, std::move(item));
                }
                else
                    queue.push_back(std::move(item));
                mCondition.notify_one();
                return;
            }
        }
        item->abort();
        item->signalDone();
    }

    void WorkQueue::continueWorkItem(std::shared_ptr<WorkItem> item)
    {
        // A yielded item is still unfinished. addWorkItem's released-queue
        // path guarantees completion even when stop races the last slice.
        addWorkItem(std::move(item));
    }

    bool WorkQueue::promoteWorkItem(WorkItem* item, bool required)
    {
        if (!item || item->isDone())
            return false;
        const std::lock_guard lock(mMutex);
        if (required)
            item->mHighPriority = true;
        for (auto* queue : { &mQueue, &mMaintenance })
        {
            const auto it = std::find_if(queue->begin(), queue->end(), [item](const auto& value) { return value.get() == item; });
            if (it != queue->end())
            {
                // Prediction is a one-off promotion. Only a required arrival
                // retains priority across slices, and predictions cannot overtake it.
                const auto first = item->mHighPriority ? queue->begin()
                    : std::find_if(queue->begin(), it, [](const auto& value) { return !value->mHighPriority.load(); });
                std::rotate(first, it, std::next(it));
                return true;
            }
        }
        return false;
    }

    bool WorkQueue::cancelWorkItem(WorkItem* item)
    {
        if (!item)
            return false;
        item->abort();
        std::shared_ptr<WorkItem> removed;
        {
            const std::lock_guard lock(mMutex);
            for (auto* queue : { &mQueue, &mMaintenance })
            {
                const auto it = std::find_if(queue->begin(), queue->end(), [item](const auto& value) { return value.get() == item; });
                if (it != queue->end())
                {
                    removed = std::move(*it);
                    queue->erase(it);
                    break;
                }
            }
        }
        if (!removed)
            return false;
        removed->signalDone();
        return true;
    }

    std::shared_ptr<WorkItem> WorkQueue::removeWorkItem()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        while (mQueue.empty() && mMaintenance.empty() && !mIsReleased)
        {
            mCondition.wait(lock);
        }
        // Count actual dispatches, including promoted arrivals. Maintenance
        // gets one bounded slice after at most eight ordinary jobs.
        if (!mMaintenance.empty() && (mQueue.empty() || mNormalSinceMaintenance >= 8))
        {
            std::shared_ptr<WorkItem> item = std::move(mMaintenance.front());
            mMaintenance.pop_front();
            mNormalSinceMaintenance = 0;
            return item;
        }
        if (!mQueue.empty())
        {
            std::shared_ptr<WorkItem> item = std::move(mQueue.front());
            mQueue.pop_front();
            mNormalSinceMaintenance = std::min<std::size_t>(8, mNormalSinceMaintenance + 1);
            return item;
        }
        return nullptr;
    }

    size_t WorkQueue::getNumItems() const
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mQueue.size() + mMaintenance.size();
    }

    size_t WorkQueue::getNumActiveThreads() const
    {
        return std::accumulate(
            mThreads.begin(), mThreads.end(), 0u, [](auto r, const auto& t) { return r + t->isActive(); });
    }

    WorkThread::WorkThread(WorkQueue& workQueue)
        : mWorkQueue(&workQueue)
        , mActive(false)
        , mThread([this] { run(); })
    {
    }

    WorkThread::~WorkThread()
    {
        mThread.join();
    }

    void WorkThread::run()
    {
        while (true)
        {
            std::shared_ptr<WorkItem> item = mWorkQueue->removeWorkItem();
            if (!item)
                return;
            mActive = true;
            if (item->doWorkSlice())
                item->signalDone();
            else
                mWorkQueue->continueWorkItem(std::move(item));
            mActive = false;
        }
    }

    bool WorkThread::isActive() const
    {
        return mActive;
    }

}
