#include <components/sceneutil/workqueue.hpp>
#include <components/sceneutil/preparationcost.hpp>

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

namespace
{
struct Item : SceneUtil::WorkItem {
    bool maintenance = false;
    bool isMaintenance() const override { return maintenance; }
};
static std::shared_ptr<Item> item()
{
    return std::make_shared<Item>();
}
TEST(WorkQueueSlicing, LateAbortCannotPublishANonterminalSlice)
{
    SceneUtil::PreparationSample sample;
    std::atomic_bool abort{ false };
    const bool terminal = [&] {
        SceneUtil::PreparationSample::Slice slice(&sample, abort);
        struct AbortAfterReturnDecision
        {
            std::atomic_bool& flag;
            ~AbortAfterReturnDecision() { flag = true; }
        } late{ abort };
        sample.mAssets = 3;
        slice.mFinished = false;
        return slice.mFinished;
    }();
    ASSERT_TRUE(!terminal && !sample.mComplete.load()) << "late abort published a nonterminal slice";
    {
        SceneUtil::PreparationSample::Slice final(&sample, abort);
        final.mFinished = true;
    }
    ASSERT_TRUE(sample.mComplete.load() && !sample.mSuccessful && sample.mSlices == 2)
        << "cancelled continuation did not publish exactly its terminal state";
    ASSERT_TRUE(sample.mQueueSeconds >= 0 && sample.mWorkSeconds >= 0 && sample.mMaxSliceSeconds >= 0)
        << "negative sample time";
}
TEST(WorkQueueSlicing, RequiredContinuationsFollowNewArrivalsBeforePrediction)
{
    SceneUtil::WorkQueue queue(0);
    auto older = item(), nearer = item(), prediction = item(), normal = item();
    queue.addWorkItem(older, true);
    ASSERT_TRUE(queue.removeWorkItem() == older) << "required initial dispatch";
    queue.addWorkItem(normal);
    queue.addWorkItem(nearer);
    queue.promoteWorkItem(nearer, true);
    queue.addWorkItem(prediction);
    queue.promoteWorkItem(prediction, false);
    queue.continueWorkItem(older);
    ASSERT_TRUE(queue.getNumItems() == 4) << "continuation duplicated or dropped job";
    ASSERT_TRUE(queue.removeWorkItem() == nearer) << "older continuation overtook newer required work";
    ASSERT_TRUE(queue.removeWorkItem() == older) << "required continuation lost priority";
    ASSERT_TRUE(queue.removeWorkItem() == prediction) << "predicted promotion was lost";
    ASSERT_TRUE(queue.removeWorkItem() == normal) << "normal job was lost";
}
TEST(WorkQueueSlicing, PredictionPromotionYieldsToOrdinaryWorkOnContinuation)
{
    SceneUtil::WorkQueue queue(0);
    auto prediction = item(), ordinary = item();
    queue.addWorkItem(prediction);
    queue.promoteWorkItem(prediction, false);
    ASSERT_TRUE(queue.removeWorkItem() == prediction) << "prediction initial order";
    queue.addWorkItem(ordinary);
    ASSERT_TRUE(!queue.promoteWorkItem(prediction, false)) << "running prediction reported queued";
    queue.continueWorkItem(prediction);
    ASSERT_TRUE(queue.removeWorkItem() == ordinary) << "one-off prediction acquired sticky required priority";
    ASSERT_TRUE(queue.removeWorkItem() == prediction) << "prediction continuation missing";
}
TEST(WorkQueueSlicing, RequiredContinuationsDoNotStarveCleanup)
{
    SceneUtil::WorkQueue queue(0);
    auto required = item(), maintenance = item();
    maintenance->maintenance = true;
    queue.addWorkItem(required, true);
    queue.addWorkItem(maintenance);
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(queue.removeWorkItem() == required) << "maintenance fairness interval changed early";
        queue.continueWorkItem(required);
    }
    ASSERT_TRUE(queue.removeWorkItem() == maintenance) << "required continuations starved maintenance";
    ASSERT_TRUE(queue.removeWorkItem() == required) << "required missing after maintenance";
}
TEST(WorkQueueSlicing, RunningPredictionYieldsToNewRequiredWork)
{
    struct Work : SceneUtil::WorkItem
    {
        std::promise<void> started, release;
        std::vector<int>& order;
        int calls = 0;
        explicit Work(std::vector<int>& values)
            : order(values)
        {
        }
        bool doWorkSlice() override
        {
            if (++calls == 1)
            {
                order.push_back(0);
                started.set_value();
                release.get_future().wait();
                return false;
            }
            order.push_back(3);
            return true;
        }
    };
    struct Recorded : SceneUtil::WorkItem
    {
        std::vector<int>& order;
        int value;
        Recorded(std::vector<int>& target, int number)
            : order(target)
            , value(number)
        {
        }
        void doWork() override { order.push_back(value); }
    };
    SceneUtil::WorkQueue queue(1);
    std::vector<int> order;
    auto running = std::make_shared<Work>(order);
    auto required = std::make_shared<Recorded>(order, 1);
    auto normal = std::make_shared<Recorded>(order, 2);
    queue.addWorkItem(running);
    running->started.get_future().wait();
    queue.addWorkItem(normal);
    queue.addWorkItem(required);
    queue.promoteWorkItem(required, true);
    ASSERT_TRUE(!running->isDone()) << "running slice prematurely completed";
    running->release.set_value();
    running->waitTillDone();
    normal->waitTillDone();
    required->waitTillDone();
    ASSERT_TRUE(order == std::vector<int>({ 0, 1, 2, 3 })) << "real worker did not yield to required work";
    ASSERT_TRUE(running->calls == 2) << "real worker duplicated continuation";
}
}
