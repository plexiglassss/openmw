#include "unrefqueue.hpp"

#include <components/diagnostics/timing.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/sceneutil/workqueue.hpp>

namespace SceneUtil
{
    namespace
    {
        struct ClearVector final : SceneUtil::WorkItem
        {
            std::vector<std::shared_ptr<void>> mObjects;

            explicit ClearVector(std::vector<std::shared_ptr<void>>&& objects)
                : mObjects(std::move(objects))
            {
            }

            void doWork() override { mObjects.clear(); }

            bool isMaintenance() const override { return Misc::useSlicedCleanup(); }
            bool doWorkSlice() override
            {
                Diagnostics::Scope measured("resource-cleanup", "detached-objects", "slice");
                if (!Misc::useSlicedCleanup())
                {
                    doWork();
                    return true;
                }
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
                for (std::size_t i = 0; i < UnrefQueue::ObjectsPerSlice && !mObjects.empty(); ++i)
                {
                    mObjects.pop_back();
                    // A single destructor is indivisible; this is a cooperative budget.
                    if (std::chrono::steady_clock::now() >= deadline)
                        break;
                }
                return mObjects.empty();
            }
        };
    }

    void UnrefQueue::flush(SceneUtil::WorkQueue& workQueue, bool waitForCapacity)
    {
        reap();
        if (mObjects.empty())
            return;

        if (Misc::useSlicedCleanup())
        {
            std::size_t begin = 0;
            while (begin < mObjects.size())
            {
                reap();
                if (mQueuedObjects == MaxQueuedObjects)
                {
                    if (!waitForCapacity)
                    {
                        mObjects.erase(mObjects.begin(), mObjects.begin() + begin);
                        return;
                    }
                    // Only under a large detached backlog: cap extra retention
                    // without dropping references or waiting on unrelated jobs.
                    Diagnostics::Scope measured("resource-cleanup", "backlog-cap", "wait");
                    ++mBackpressureWaits;
                    workQueue.promoteWorkItem(mPending.front().mItem);
                    mPending.front().mItem->waitTillDone();
                    continue;
                }
                // Completion releases accounting credit. Keep each owned batch
                // small too, so a full backlog never waits for a whole cell's
                // retained vector just to admit a few more references.
                const auto count
                    = std::min({ MaxQueuedObjects - mQueuedObjects, mObjects.size() - begin, ObjectsPerSlice });
                std::vector<std::shared_ptr<void>> objects(count);
                for (std::size_t i = 0; i < count; ++i)
                    objects[i].swap(mObjects[begin + i]);
                // Transfer ownership before waiting for another slice; keeping
                // source references would postpone release until the final clear.
                auto item = std::make_shared<ClearVector>(std::move(objects));
                mPending.push_back({ item, count });
                mQueuedObjects += count;
                mPeakQueuedObjects = std::max(mPeakQueuedObjects, mQueuedObjects);
                workQueue.addWorkItem(item);
                begin += count;
            }
            mObjects.clear();
            return;
        }

        // Transfer references explicitly and retain the source vector capacity.
        std::vector<std::shared_ptr<void>> objects(mObjects.size());
        for (std::size_t i = 0; i < mObjects.size(); ++i)
            objects[i].swap(mObjects[i]);
        workQueue.addWorkItem(std::make_shared<ClearVector>(std::move(objects)));
        mObjects.clear();
    }

    void UnrefQueue::reap()
    {
        std::erase_if(mPending, [&](const Pending& pending) {
            if (!pending.mItem->isDone())
                return false;
            mQueuedObjects -= pending.mObjects;
            return true;
        });
    }
}
