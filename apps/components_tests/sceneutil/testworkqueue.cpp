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
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        osg::ref_ptr<BlockingWork> blocker = new BlockingWork;
        queue->addWorkItem(blocker);
        blocker->mStarted.get_future().wait();
        std::vector<int> order;
        osg::ref_ptr<RecordedWork> discarded = new RecordedWork(order, 1);
        osg::ref_ptr<RecordedWork> optional = new RecordedWork(order, 2);
        osg::ref_ptr<RecordedWork> required = new RecordedWork(order, 3);
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
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        osg::ref_ptr<BlockingWork> running = new BlockingWork;
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
}
