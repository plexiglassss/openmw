#include <components/sceneutil/workqueue.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <future>

namespace
{
    struct BlockingWork : SceneUtil::WorkItem
    {
        std::promise<void> mStarted;
        std::promise<void> mRelease;
        std::atomic_bool mAborted{ false };
        void abort() override { mAborted = true; }
        void doWork() override
        {
            mStarted.set_value();
            mRelease.get_future().wait();
        }
    };

    struct RecordedWork : SceneUtil::WorkItem
    {
        std::vector<int>& mOrder;
        int mValue;
        bool mAborted = false;
        RecordedWork(std::vector<int>& order, int value) : mOrder(order), mValue(value) {}
        void abort() override { mAborted = true; }
        void doWork() override { mOrder.push_back(mValue); }
    };

    TEST(WorkQueueCancellation, RemovesQueuedWorkAndPromotesRequiredPrediction)
    {
        std::shared_ptr<SceneUtil::WorkQueue> queue = std::make_shared<SceneUtil::WorkQueue>(1);
        std::shared_ptr<BlockingWork> blocker = std::make_shared<BlockingWork>();
        queue->addWorkItem(blocker);
        blocker->mStarted.get_future().wait();
        std::vector<int> order;
        std::shared_ptr<RecordedWork> discarded = std::make_shared<RecordedWork>(order, 1);
        std::shared_ptr<RecordedWork> optional = std::make_shared<RecordedWork>(order, 2);
        std::shared_ptr<RecordedWork> required = std::make_shared<RecordedWork>(order, 3);
        queue->addWorkItem(discarded);
        queue->addWorkItem(optional);
        queue->addWorkItem(required);
        EXPECT_TRUE(queue->cancelWorkItem(discarded));
        EXPECT_TRUE(discarded->isDone());
        EXPECT_TRUE(discarded->mAborted);
        EXPECT_FALSE(queue->cancelWorkItem(discarded));
        EXPECT_TRUE(queue->promoteWorkItem(required));
        EXPECT_EQ(queue->getNumItems(), 2u);
        blocker->mRelease.set_value();
        optional->waitTillDone();
        required->waitTillDone();
        EXPECT_EQ(order, (std::vector<int>{ 3, 2 }));
    }

    TEST(WorkQueueCancellation, RunningWorkRetainsOwnershipAndItsRealCompletion)
    {
        std::shared_ptr<SceneUtil::WorkQueue> queue = std::make_shared<SceneUtil::WorkQueue>(1);
        std::shared_ptr<BlockingWork> running = std::make_shared<BlockingWork>();
        queue->addWorkItem(running);
        running->mStarted.get_future().wait();
        EXPECT_FALSE(queue->cancelWorkItem(running));
        EXPECT_TRUE(running->mAborted);
        EXPECT_FALSE(running->isDone());
        EXPECT_FALSE(queue->promoteWorkItem(running));
        running->mRelease.set_value();
        running->waitTillDone();
        EXPECT_TRUE(running->isDone());
    }

    TEST(WorkQueueCancellation, ArrivalPromotionsPreserveNearestFirstWithoutDuplicateJobs)
    {
        std::shared_ptr<SceneUtil::WorkQueue> queue = std::make_shared<SceneUtil::WorkQueue>(1);
        std::shared_ptr<BlockingWork> blocker = std::make_shared<BlockingWork>();
        queue->addWorkItem(blocker);
        blocker->mStarted.get_future().wait();
        std::vector<int> order;
        std::vector<std::shared_ptr<RecordedWork>> cells;
        for (int i : { 1, 2, 3 })
        {
            cells.emplace_back(new RecordedWork(order, i));
            queue->addWorkItem(cells.back());
        }
        for (int repeat = 0; repeat < 3; ++repeat)
            for (auto it = cells.rbegin(); it != cells.rend(); ++it)
                EXPECT_TRUE(queue->promoteWorkItem(*it));
        EXPECT_EQ(queue->getNumItems(), 3u);
        blocker->mRelease.set_value();
        cells.back()->waitTillDone();
        EXPECT_EQ(order, (std::vector<int>{ 1, 2, 3 }));
    }
}
