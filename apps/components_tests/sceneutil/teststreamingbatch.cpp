#include <components/misc/combinedperformance.hpp>
#include <components/sceneutil/cellobjectindex.hpp>
#include <components/sceneutil/pendingpreload.hpp>
#include <components/sceneutil/preloadplancache.hpp>
#include <components/sceneutil/preloadretention.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <limits>
#include <thread>

namespace
{
    TEST(CellObjectIndexTest, MovementReplacementAndRemovalKeepOneOwner)
    {
        SceneUtil::CellObjectIndex<int, int> index;
        index.set(1, 10);
        index.set(2, 10);
        index.set(3, 20);
        index.set(1, 20);
        index.set(1, 20);
        index.erase(2);
        index.erase(2);
        EXPECT_TRUE(index.take(10).empty());
        EXPECT_EQ(index.take(20), (std::set<int>{ 1, 3 }));
        EXPECT_EQ(index.size(), 0u);
        EXPECT_TRUE(index.take(20).empty());
    }

    TEST(CellObjectIndexTest, UnloadingOneCellDoesNotEnumerateOtherCells)
    {
        SceneUtil::CellObjectIndex<int, int> index;
        for (int i = 0; i < 10000; ++i)
            index.set(i, i / 100);
        for (int i = 10000; i < 10003; ++i)
            index.set(i, 500);
        const auto removed = index.take(500);
        EXPECT_EQ(removed, (std::set<int>{ 10000, 10001, 10002 }));
        EXPECT_EQ(index.size(), 10000u);
        index.clear();
        index.set(10000, 700);
        EXPECT_TRUE(index.take(500).empty());
        EXPECT_EQ(index.take(700), (std::set<int>{ 10000 }));
    }

    TEST(PreloadPlanCacheTest, ReusesTopologyAndRebuildsOnEitherRevision)
    {
        SceneUtil::PreloadPlanCache<int, std::pair<int, int>, std::vector<int>> cache(2);
        int builds = 0;
        auto make = [&] {
            ++builds;
            return std::vector<int>{ builds };
        };
        EXPECT_EQ(cache.get(1, { 1, 1 }, make).front(), 1);
        EXPECT_EQ(cache.get(1, { 1, 1 }, make).front(), 1);
        EXPECT_EQ(cache.get(1, { 2, 1 }, make).front(), 2);
        EXPECT_EQ(cache.get(1, { 2, 2 }, make).front(), 3);
        EXPECT_EQ(builds, 3);
        EXPECT_EQ(cache.hits(), 1u);
    }

    TEST(PreloadPlanCacheTest, CapacityEvictsLeastUsedAndResetDropsPointerPlans)
    {
        SceneUtil::PreloadPlanCache<int, int, int> cache(2);
        cache.get(1, 0, [] { return 1; });
        cache.get(2, 0, [] { return 2; });
        cache.get(1, 0, [] { return 99; });
        cache.get(3, 0, [] { return 3; });
        EXPECT_EQ(cache.size(), 2u);
        EXPECT_EQ(cache.get(2, 0, [] { return 22; }), 22);
        cache.erase(2);
        cache.clear();
        EXPECT_EQ(cache.size(), 0u);
        EXPECT_EQ(cache.get(1, 0, [] { return 11; }), 11);
    }

    TEST(PreloadPlanCacheTest, FailedRebuildDoesNotValidateStalePlanAndCanRetry)
    {
        SceneUtil::PreloadPlanCache<int, int, int> cache(1);
        cache.get(1, 1, [] { return 1; });
        EXPECT_THROW(cache.get(1, 2, []() -> int { throw std::runtime_error("failure"); }), std::runtime_error);
        EXPECT_EQ(cache.get(1, 2, [] { return 2; }), 2);
        EXPECT_EQ(cache.builds(), 2u);
    }

    struct Entry
    {
        double used;
        double cost;
    };

    TEST(PendingPreloadTest, ReusesOnlyUnfinishedMatchingGenerationAndAllowsFailureRetry)
    {
        SceneUtil::PendingPreload<std::string> pending;
        osg::ref_ptr<SceneUtil::WorkItem> first = new SceneUtil::WorkItem;
        pending.insert("mesh", { 1, 2 }, first);
        EXPECT_TRUE(pending.contains("mesh", { 1, 2 }));
        EXPECT_FALSE(pending.contains("other", { 1, 2 }));
        EXPECT_FALSE(pending.contains("mesh", { 2, 2 }));
        EXPECT_FALSE(pending.contains("mesh", { 1, 3 }));
        first->signalDone(); // Failed and successful jobs both finish; failures must be retryable.
        EXPECT_FALSE(pending.contains("mesh", { 1, 2 }));
        osg::ref_ptr<SceneUtil::WorkItem> retry = new SceneUtil::WorkItem;
        pending.insert("mesh", { 1, 2 }, retry);
        EXPECT_TRUE(pending.contains("mesh", { 1, 2 }));
        EXPECT_EQ(pending.size(), 1u);
        pending.clear();
        EXPECT_FALSE(pending.contains("mesh", { 1, 2 }));
        EXPECT_FALSE(retry->isDone()); // Clearing the index does not cancel its owner's real job.
    }

    TEST(PendingPreloadTest, CompletedKeysArePrunedAndReplacedJobsKeepOwnerReferences)
    {
        SceneUtil::PendingPreload<int> pending;
        osg::ref_ptr<SceneUtil::WorkItem> old = new SceneUtil::WorkItem;
        osg::ref_ptr<SceneUtil::WorkItem> current = new SceneUtil::WorkItem;
        pending.insert(1, { 1, 1 }, old);
        pending.insert(1, { 2, 1 }, current);
        EXPECT_FALSE(old->isDone());
        EXPECT_TRUE(pending.contains(1, { 2, 1 }));
        current->signalDone();
        pending.insert(2, { 2, 1 }, old);
        EXPECT_EQ(pending.size(), 1u);
        EXPECT_FALSE(pending.contains(1, { 2, 1 }));
    }
    auto victim(std::vector<Entry>& entries, double now, bool costAware = true)
    {
        return SceneUtil::selectPreloadVictim(
            entries.begin(), entries.end(), now, costAware, [](const Entry& e) { return e.used; },
            [](const Entry& e) { return e.cost; });
    }

    TEST(PreloadRetentionTest, ProtectsRecentExpensiveWorkButCannotPinObsoleteCells)
    {
        std::vector<Entry> entries{ { 10, 0.5 }, { 10.5, 0 } };
        EXPECT_EQ(victim(entries, 12), entries.begin() + 1);
        EXPECT_EQ(victim(entries, 12, false), entries.begin());
        entries[0].used = 8;
        EXPECT_EQ(victim(entries, 12), entries.begin());
    }

    TEST(PreloadRetentionTest, StrictGraceAndRefreshWinOverCost)
    {
        std::vector<Entry> entries{ { 10, 0 }, { 10.5, 0 } };
        EXPECT_EQ(victim(entries, 11), entries.end());
        EXPECT_EQ(victim(entries, 11.01), entries.begin());
        entries[0].used = 11.01;
        EXPECT_EQ(victim(entries, 12), entries.begin() + 1);
        EXPECT_EQ(victim(entries, 0), entries.end());
    }

    TEST(PreloadRetentionTest, InvalidCostsCannotPoisonRanking)
    {
        EXPECT_EQ(SceneUtil::preloadRetentionBonus(-1), 0);
        EXPECT_EQ(SceneUtil::preloadRetentionBonus(std::numeric_limits<double>::infinity()), 0);
        EXPECT_EQ(SceneUtil::preloadRetentionBonus(std::numeric_limits<double>::quiet_NaN()), 0);
        EXPECT_EQ(SceneUtil::preloadRetentionBonus(1000), 2);
    }

    struct SlicedWork : SceneUtil::WorkItem
    {
        int remaining = 3;
        bool aborted = false;
        bool isMaintenance() const override { return true; }
        bool doWorkSlice() override { return --remaining == 0; }
        void abort() override { aborted = true; }
    };

    TEST(SlicedCleanupTest, MaintenanceYieldsToArrivalsAndCannotStarve)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(0);
        osg::ref_ptr<SlicedWork> cleanup = new SlicedWork;
        queue->addWorkItem(cleanup);
        for (int slice = 0; slice < 3; ++slice)
        {
            for (int i = 0; i < 8; ++i)
            {
                osg::ref_ptr<SceneUtil::WorkItem> arrival = new SceneUtil::WorkItem;
                queue->addWorkItem(arrival, true);
                EXPECT_EQ(queue->removeWorkItem(), arrival);
                arrival->signalDone();
            }
            osg::ref_ptr<SceneUtil::WorkItem> another = new SceneUtil::WorkItem;
            queue->addWorkItem(another, true);
            EXPECT_EQ(queue->removeWorkItem(), cleanup);
            EXPECT_FALSE(cleanup->isDone());
            if (cleanup->doWorkSlice())
                cleanup->signalDone();
            else
                queue->continueWorkItem(cleanup);
            EXPECT_EQ(queue->removeWorkItem(), another);
            another->signalDone();
            // That dispatch counts toward the next fairness interval.
            if (slice < 2)
            {
                // Dispatching a cleanup on an idle queue resets the interval.
                // Use a separate one-shot maintenance item to exercise idle progress.
                osg::ref_ptr<SlicedWork> idle = new SlicedWork;
                queue->addWorkItem(idle, true);
                EXPECT_EQ(queue->removeWorkItem(), idle);
                idle->signalDone();
            }
        }
        EXPECT_TRUE(cleanup->isDone());
        EXPECT_EQ(queue->getNumItems(), 0u);
    }

    TEST(SlicedCleanupTest, CancelledMaintenanceNeverRunsAndSignalsWaiters)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(0);
        osg::ref_ptr<SlicedWork> cleanup = new SlicedWork;
        queue->addWorkItem(cleanup);
        EXPECT_TRUE(queue->promoteWorkItem(cleanup));
        EXPECT_TRUE(queue->cancelWorkItem(cleanup));
        EXPECT_TRUE(cleanup->isDone());
        EXPECT_TRUE(cleanup->aborted);
        EXPECT_EQ(cleanup->remaining, 3);
        EXPECT_EQ(queue->getNumItems(), 0u);
    }

    struct BlockingSlice : SlicedWork
    {
        std::promise<void> started, release;
        bool doWorkSlice() override
        {
            started.set_value();
            release.get_future().wait();
            return false;
        }
    };
    struct StopMarker : SceneUtil::WorkItem
    {
        std::promise<void> stopped;
        void abort() override { stopped.set_value(); }
    };

    TEST(SlicedCleanupTest, StopRejectsRunningContinuationAndCompletesDroppedWork)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        osg::ref_ptr<BlockingSlice> running = new BlockingSlice;
        queue->addWorkItem(running);
        running->started.get_future().wait();
        osg::ref_ptr<StopMarker> dropped = new StopMarker;
        queue->addWorkItem(dropped);
        auto stopped = std::async(std::launch::async, [&] { queue->stop(); });
        dropped->stopped.get_future().wait();
        running->release.set_value();
        stopped.get();
        EXPECT_TRUE(running->isDone());
        EXPECT_TRUE(running->aborted);
        EXPECT_TRUE(dropped->isDone());
        EXPECT_EQ(queue->getNumItems(), 0u);
        osg::ref_ptr<SlicedWork> late = new SlicedWork;
        queue->addWorkItem(late);
        EXPECT_TRUE(late->isDone());
        EXPECT_TRUE(late->aborted);
    }

    struct CountedObject : osg::Referenced
    {
        std::atomic<std::size_t>& count;
        std::atomic<std::size_t>* foreground;
        const std::thread::id owner = std::this_thread::get_id();
        explicit CountedObject(std::atomic<std::size_t>& value, std::atomic<std::size_t>* mainCount = nullptr)
            : count(value)
            , foreground(mainCount)
        {
        }
        ~CountedObject() override
        {
            if (foreground && owner == std::this_thread::get_id())
                ++*foreground;
            ++count;
        }
    };

    TEST(SlicedCleanupTest, WorkerPublishesCompletionOnlyAfterFinalSlice)
    {
        struct Work : SceneUtil::WorkItem
        {
            std::atomic<int> calls{ 0 };
            std::promise<void> finalStarted, release;
            bool isMaintenance() const override { return true; }
            bool doWorkSlice() override
            {
                if (++calls == 1)
                    return false;
                finalStarted.set_value();
                release.get_future().wait();
                return true;
            }
        };
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        osg::ref_ptr<Work> work = new Work;
        auto finalStarted = work->finalStarted.get_future();
        queue->addWorkItem(work);
        const auto started = finalStarted.wait_for(std::chrono::seconds(3));
        EXPECT_EQ(started, std::future_status::ready);
        if (started == std::future_status::ready)
            EXPECT_FALSE(work->isDone());
        work->release.set_value();
        work->waitTillDone();
        EXPECT_TRUE(work->isDone());
        EXPECT_EQ(work->calls, 2);
    }

    TEST(SlicedCleanupTest, DetachedObjectsReleaseOnceInBoundedSlices)
    {
        std::atomic<std::size_t> destroyed{ 0 };
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(0);
        SceneUtil::UnrefQueue unref;
        for (int i = 0; i < 257; ++i)
            unref.push(osg::ref_ptr<osg::Referenced>(new CountedObject(destroyed)));
        unref.flush(*queue);
        std::size_t slices = 0;
        while (queue->getNumItems())
        {
            auto item = queue->removeWorkItem();
            const auto before = destroyed.load();
            if (item->doWorkSlice())
                item->signalDone();
            else
                queue->continueWorkItem(item);
            ++slices;
            if (Misc::useSlicedCleanup())
                EXPECT_LE(destroyed - before, SceneUtil::UnrefQueue::ObjectsPerSlice);
        }
        unref.flush(*queue);
        EXPECT_EQ(destroyed, 257u);
        EXPECT_EQ(unref.getSize(), 0u);
        EXPECT_EQ(unref.getBackpressureWaits(), 0u);
        EXPECT_GE(slices, Misc::useSlicedCleanup() ? 5u : 1u);
    }

    TEST(SlicedCleanupTest, LargeBacklogIsBoundedAndEventuallyReleased)
    {
        std::atomic<std::size_t> destroyed{ 0 };
        std::atomic<std::size_t> foreground{ 0 };
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(2);
        SceneUtil::UnrefQueue unref;
        const auto count = SceneUtil::UnrefQueue::MaxQueuedObjects + 257;
        for (std::size_t i = 0; i < count; ++i)
            unref.push(osg::ref_ptr<osg::Referenced>(new CountedObject(destroyed, &foreground)));
        unref.flush(*queue);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (destroyed != count && std::chrono::steady_clock::now() < deadline)
        {
            unref.flush(*queue);
            std::this_thread::yield();
        }
        EXPECT_EQ(destroyed, count);
        unref.flush(*queue);
        EXPECT_LE(unref.getPeakQueuedObjects(), SceneUtil::UnrefQueue::MaxQueuedObjects);
        EXPECT_EQ(foreground, 0u);
    }
}
