#include <components/sceneutil/asyncpreparation.hpp>
#include <components/terrain/viewdata.hpp>

#include <gtest/gtest.h>
#include <osg/Group>
#include <chrono>
#include <latch>
#include <stdexcept>
#include <thread>

namespace
{
    using Cache = SceneUtil::AsyncPreparation<int, std::shared_ptr<int>>;
    template <class Predicate> bool eventually(Predicate&& predicate)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() > end)
                return false;
            std::this_thread::yield();
        }
        return true;
    }
    Cache::Factory value(int n)
    {
        return [n](const std::atomic_bool&) { return Cache::Prepared{ std::make_shared<int>(n), {} }; };
    }

    void finishCpuQueue(SceneUtil::WorkQueue& queue)
    {
        struct Fence : SceneUtil::WorkItem { void doWork() override {} };
        osg::ref_ptr<Fence> fence = new Fence;
        queue.addWorkItem(fence);
        fence->waitTillDone();
    }

    TEST(AsyncPreparation, SeparateGraphicsCapacityKeepsCpuPreparationMovingWithoutEarlyPublication)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue, 2, 64, 1024ull * 1024 * 1024, 0.25, 1.0, 8);
        auto gate = std::make_shared<std::atomic_bool>(false);
        for (int key = 0; key < 9; ++key)
        {
            cache.request(key, 0, [key, gate](const std::atomic_bool&) {
                return Cache::Prepared{ std::make_shared<int>(key), gate, {}, 1024 };
            });
            finishCpuQueue(*queue);
            EXPECT_FALSE(cache.poll(key, 0).mReady);
        }
        EXPECT_EQ(cache.submitted(), 8u);
        EXPECT_FALSE(cache.contains(8));
        *gate = true;
        for (int key = 0; key < 8; ++key)
        {
            const auto result = cache.takeReady(key, 0);
            ASSERT_TRUE(result.mReady);
            EXPECT_EQ(*result.mValue, key);
        }
        cache.request(8, 0, value(8));
        finishCpuQueue(*queue);
        EXPECT_TRUE(cache.poll(8, 0).mReady);
    }

    TEST(AsyncPreparation, SeparateGraphicsCapacityDoesNotRaiseQueuedCpuLimit)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(0);
        Cache cache(queue, 2, 64, 1024ull * 1024 * 1024, 0.25, 1.0, 8);
        for (int key = 0; key < 9; ++key)
            cache.request(key, 0, value(key));
        EXPECT_EQ(cache.submitted(), 2u);
        EXPECT_EQ(queue->getNumItems(), 2u);
    }

    TEST(AsyncPreparation, GraphicsWaitingGraphsStillConsumeTheByteBudget)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue, 2, 64, 128ull * 1024 * 1024, 0.25, 1.0, 8);
        auto gate = std::make_shared<std::atomic_bool>(false);
        cache.request(1, 0, [gate](const std::atomic_bool&) {
            return Cache::Prepared{ std::make_shared<int>(1), gate, {}, 128ull * 1024 * 1024 };
        });
        finishCpuQueue(*queue);
        cache.request(2, 0, value(2));
        EXPECT_EQ(cache.submitted(), 1u);
        EXPECT_FALSE(cache.poll(1, 0).mReady);
    }

    TEST(AsyncPreparation, CancelledCpuWorkStillConsumesSeparateCpuCapacity)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue, 1, 64, 1024ull * 1024 * 1024, 0.25, 1.0, 8);
        std::latch started(1), release(1);
        cache.request(1, 0, [&](const std::atomic_bool&) {
            started.count_down();
            release.wait();
            return Cache::Prepared{};
        });
        started.wait();
        cache.cancel(1);
        cache.request(2, 0, value(2));
        EXPECT_EQ(cache.submitted(), 1u);
        release.count_down();
        finishCpuQueue(*queue);
        cache.request(2, 0, value(2));
        finishCpuQueue(*queue);
        EXPECT_TRUE(cache.poll(2, 0).mReady);
        EXPECT_FALSE(cache.poll(1, 0).mReady);
    }

    TEST(AsyncPreparation, QueueSaturationNeverExecutesFactoriesOnTheCaller)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(0);
        Cache cache(queue, 2);
        std::atomic_int calls{ 0 };
        auto factory = [&](const std::atomic_bool&) { ++calls; return Cache::Prepared{}; };
        EXPECT_FALSE(cache.request(1, 0, factory).mReady);
        EXPECT_FALSE(cache.request(1, 0, factory).mReady);
        EXPECT_FALSE(cache.request(2, 0, factory).mReady);
        EXPECT_FALSE(cache.request(3, 0, factory).mReady);
        EXPECT_EQ(cache.submitted(), 2u);
        EXPECT_EQ(queue->getNumItems(), 2u);
        EXPECT_EQ(calls, 0);
        cache.clear();
        EXPECT_EQ(queue->getNumItems(), 0u);
        queue->start(1);
        EXPECT_EQ(calls, 0);
    }

    TEST(AsyncPreparation, CompletedHandoverReleasesAdmissionWithoutCancellingGraphics)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue, 1, 1);
        auto gate = std::make_shared<std::atomic_bool>(false);
        std::atomic_int cancelled{ 0 };
        cache.request(1, 0, [&](const std::atomic_bool&) {
            return Cache::Prepared{ std::make_shared<int>(42), gate, [&] { ++cancelled; } };
        });
        EXPECT_FALSE(cache.takeReady(1, 0).mReady);
        EXPECT_TRUE(cache.contains(1));
        *gate = true;
        ASSERT_TRUE(eventually([&] { return cache.poll(1, 0).mReady; }));
        const auto result = cache.takeReady(1, 0);
        ASSERT_TRUE(result.mReady);
        EXPECT_EQ(*result.mValue, 42);
        EXPECT_EQ(cache.size(), 0u);
        ASSERT_TRUE(eventually([&] { return cache.request(2, 0.1, value(7)).mReady; }));
        EXPECT_EQ(cancelled, 0);
    }

    TEST(AsyncPreparation, ResourceLookAheadRetentionSurvivesSlowPolling)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue, 2, 8, 1024, 4, 10);
        cache.request(1, 0, value(1));
        ASSERT_TRUE(eventually([&] { return cache.poll(1, 0).mReady; }));
        cache.update(2);
        EXPECT_TRUE(cache.contains(1));
        EXPECT_TRUE(cache.poll(1, 2).mReady);
        cache.update(6.1);
        EXPECT_FALSE(cache.contains(1));
    }

    TEST(AsyncPreparation, GraphicsWaitDoesNotOccupyCoverageQueueSlots)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache detail(queue, 1), cover(queue, 1);
        auto gate = std::make_shared<std::atomic_bool>(false);
        detail.request(1, 0, [gate](const std::atomic_bool&) {
            return Cache::Prepared{ std::make_shared<int>(1), gate };
        });
        cover.request(1, 0, value(9));
        ASSERT_TRUE(eventually([&] { return cover.poll(1, 0).mReady; }));
        EXPECT_FALSE(detail.poll(1, 0).mReady);
        detail.request(2, 0, value(2));
        EXPECT_EQ(detail.submitted(), 1u);
        EXPECT_EQ(*cover.takeReady(1, 0).mValue, 9);
        *gate = true;
        ASSERT_TRUE(eventually([&] { return detail.poll(1, 0).mReady; }));
    }

    TEST(AsyncPreparation, RequiredProbeDoesNotSubmitAndCancellationRetiresQueuedSpeculation)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(0);
        Cache cache(queue);
        EXPECT_FALSE(cache.poll(1, 0).mReady);
        EXPECT_EQ(cache.submitted(), 0u);
        std::atomic_int calls{ 0 };
        cache.request(1, 0, [&](const std::atomic_bool&) { ++calls; return Cache::Prepared{}; });
        EXPECT_FALSE(cache.poll(1, 0.1).mReady);
        EXPECT_EQ(cache.submitted(), 1u);
        EXPECT_TRUE(cache.cancel(1));
        EXPECT_FALSE(cache.cancel(1));
        EXPECT_EQ(queue->getNumItems(), 0u);
        EXPECT_EQ(cache.size(), 0u);
        cache.update(0.2);
        queue->start(1);
        EXPECT_EQ(calls, 0);
    }

    TEST(AsyncPreparation, RequiredCancellationDoesNotJoinRunningSpeculation)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue);
        std::latch started(1), release(1);
        std::atomic_bool observedAbort{ false };
        cache.request(1, 0, [&](const std::atomic_bool& abort) {
            started.count_down();
            release.wait();
            observedAbort = abort.load();
            return Cache::Prepared{ std::make_shared<int>(1), {} };
        });
        started.wait();
        EXPECT_TRUE(cache.cancel(1)); // must return while worker is still blocked
        EXPECT_FALSE(cache.poll(1, 0).mReady);
        release.count_down();
        ASSERT_TRUE(eventually([&] { return observedAbort.load(); }));
        EXPECT_FALSE(cache.poll(1, 0).mReady);
    }

    TEST(StreamingLookAhead, ColdAndStationaryViewsDoNotInventMotion)
    {
        Terrain::StreamingLookAhead prediction;
        const osg::Vec3f point(10, 20, 30);
        const osg::Vec4i grid(-1, -1, 2, 2);
        auto first = prediction.update(point, grid, 1, 8192);
        ASSERT_TRUE(first);
        EXPECT_EQ(first->mPredicted, point);
        EXPECT_EQ(first->mPredictedGrid, grid);
        EXPECT_EQ(first->mMargin, 8192);
        EXPECT_FALSE(prediction.update(point, grid, 1.01, 8192));
        auto stationary = prediction.update(point, grid, 1.2, 8192);
        ASSERT_TRUE(stationary);
        EXPECT_EQ(stationary->mPredicted, point);
    }

    TEST(StreamingLookAhead, ForecastsMovementAndNegativeGridCrossingsWithinOneCell)
    {
        Terrain::StreamingLookAhead prediction;
        const osg::Vec4i grid(-2, -1, 1, 2);
        prediction.update(osg::Vec3f(-10, 0, 0), grid, 1, 100);
        auto moving = prediction.update(osg::Vec3f(-1, 0, 0), grid, 1.11, 100);
        ASSERT_TRUE(moving);
        EXPECT_GT(moving->mPredicted.x(), 0);
        EXPECT_EQ(moving->mPredictedGrid, osg::Vec4i(-1, -1, 2, 2));
        EXPECT_LE((moving->mPredicted - moving->mCurrent).length(), 100.f);
        prediction.update(osg::Vec3f(90, 0, 0), grid, 1.12, 100);
        auto fast = prediction.update(osg::Vec3f(180, 0, 0), grid, 1.23, 100);
        ASSERT_TRUE(fast);
        EXPECT_LE((fast->mPredicted - fast->mCurrent).length(), 100.001f);
    }

    TEST(StreamingLookAhead, TeleportsAndClockChangesResetForecast)
    {
        Terrain::StreamingLookAhead prediction;
        const osg::Vec4i grid(-1, -1, 2, 2);
        prediction.update(osg::Vec3f(), grid, 1, 100);
        prediction.update(osg::Vec3f(10, 0, 0), grid, 1.1, 100);
        const osg::Vec3f destination(1000, 500, 100);
        auto teleported = prediction.update(destination, grid, 1.1, 100);
        ASSERT_TRUE(teleported);
        EXPECT_EQ(teleported->mPredicted, destination);
        auto rewound = prediction.update(destination, grid, 0.1, 100);
        ASSERT_TRUE(rewound);
        EXPECT_EQ(rewound->mPredicted, destination);
        EXPECT_FALSE(prediction.update(destination, grid, 0.2, 0));
    }

    TEST(StreamingLookAhead, ViewCopiesKeepCameraMotionAndRecyclingResetsIt)
    {
        Terrain::ViewData source, camera;
        const osg::Vec4i grid(-1, -1, 2, 2);
        source.mStreamingLookAhead.update({}, grid, 0, 100);
        camera.mStreamingLookAhead.update({}, grid, 0, 100);
        source.mStreamingLookAhead.update(osg::Vec3f(10, 0, 0), grid, 0.11, 100);
        camera.mStreamingLookAhead.update(osg::Vec3f(0, 10, 0), grid, 0.11, 100);
        camera.copyFrom(source);
        const osg::Vec3f point(0, 10, 0);
        auto moving = camera.mStreamingLookAhead.update(point, grid, 0.22, 100);
        ASSERT_TRUE(moving);
        EXPECT_EQ(moving->mPredicted.x(), 0);
        EXPECT_GT(moving->mPredicted.y(), point.y());
        camera.clear();
        auto fresh = camera.mStreamingLookAhead.update(point, grid, 0.23, 100);
        ASSERT_TRUE(fresh);
        EXPECT_EQ(fresh->mPredicted, point);
    }

    TEST(AsyncPreparation, PollingDoesNotJoinAnUnfinishedWorker)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue, 1);
        std::latch started(1), release(1);
        const auto caller = std::this_thread::get_id();
        std::thread::id producer;
        cache.request(1, 0, [&](const std::atomic_bool&) {
            producer = std::this_thread::get_id();
            started.count_down();
            release.wait();
            return Cache::Prepared{ std::make_shared<int>(42), {} };
        });
        started.wait();
        EXPECT_NE(producer, caller);
        EXPECT_FALSE(cache.request(1, 0, value(99)).mReady);
        EXPECT_FALSE(cache.request(2, 0, value(99)).mReady);
        release.count_down();
        ASSERT_TRUE(eventually([&] { return cache.request(1, 0, value(99)).mReady; }));
        EXPECT_EQ(*cache.request(1, 0, value(99)).mValue, 42);
        EXPECT_EQ(cache.submitted(), 1u);
    }

    TEST(AsyncPreparation, CpuCompletionWaitsForTheExplicitGraphicsGate)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue);
        auto gate = std::make_shared<std::atomic_bool>(false);
        std::latch produced(1);
        cache.request(1, 0, [&](const std::atomic_bool&) {
            produced.count_down();
            return Cache::Prepared{ std::make_shared<int>(7), gate };
        });
        produced.wait();
        EXPECT_FALSE(cache.request(1, 0, value(99)).mReady);
        gate->store(true, std::memory_order_release);
        ASSERT_TRUE(eventually([&] { return cache.request(1, 0, value(99)).mReady; }));
        EXPECT_EQ(*cache.request(1, 0, value(99)).mValue, 7);
    }

    TEST(AsyncPreparation, CancelledCompletionCannotResurrectTheOldGeneration)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue);
        auto gate = std::make_shared<std::atomic_bool>(false);
        std::atomic_int cancelled{ 0 };
        std::latch produced(1);
        cache.request(1, 0, [&](const std::atomic_bool&) {
            produced.count_down();
            return Cache::Prepared{ std::make_shared<int>(7), gate, [&] { ++cancelled; } };
        });
        produced.wait();
        cache.clear();
        ASSERT_TRUE(eventually([&] { return cancelled.load() == 1; }));
        gate->store(true, std::memory_order_release);
        cache.update(1);
        ASSERT_TRUE(eventually([&] { return cache.request(1, 1, value(88)).mReady; }));
        EXPECT_EQ(*cache.request(1, 1, value(88)).mValue, 88);
        EXPECT_EQ(cancelled, 1);
    }

    TEST(AsyncPreparation, FailureStaysDistinctFromEmptyAndRetriesAfterBackoff)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue);
        cache.request(1, 0, [](const std::atomic_bool&) -> Cache::Prepared { throw std::runtime_error("decode"); });
        ASSERT_TRUE(eventually([&] { return cache.request(1, 0, value(99)).mFailed; }));
        EXPECT_FALSE(cache.request(1, 0.9, value(99)).mReady);
        cache.update(1.1);
        ASSERT_TRUE(eventually([&] { return cache.request(1, 1.1, value(99)).mReady; }));
        EXPECT_EQ(*cache.request(1, 1.1, value(99)).mValue, 99);
        cache.request(2, 1.1, [](const std::atomic_bool&) { return Cache::Prepared{}; });
        ASSERT_TRUE(eventually([&] { return cache.request(2, 1.1, value(99)).mReady; }));
        EXPECT_FALSE(cache.request(2, 1.1, value(99)).mValue);
    }

    TEST(AsyncPreparation, BudgetStopsFurtherAdmissionUntilOldResultsRetire)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(1);
        Cache cache(queue, 2, 64, 128ull * 1024 * 1024);
        cache.request(1, 0, [](const std::atomic_bool&) {
            return Cache::Prepared{ std::make_shared<int>(1), {}, {}, 256ull * 1024 * 1024 };
        });
        ASSERT_TRUE(eventually([&] { return cache.request(1, 0, value(99)).mReady; }));
        cache.request(2, 0, value(2));
        EXPECT_EQ(cache.submitted(), 1u);
        cache.update(0.3);
        ASSERT_TRUE(eventually([&] { return cache.request(2, 0.3, value(2)).mReady; }));
        EXPECT_EQ(cache.submitted(), 2u);
    }

    TEST(AsyncPreparation, DestructionCancelsAQueueThatHasNoWorkers)
    {
        osg::ref_ptr<SceneUtil::WorkQueue> queue = new SceneUtil::WorkQueue(0);
        {
            Cache cache(queue);
            cache.request(1, 0, value(1));
            ASSERT_EQ(queue->getNumItems(), 1u);
        }
        EXPECT_EQ(queue->getNumItems(), 0u);
    }

    TEST(AsyncPreparation, PartialViewCopyPreservesPendingAndReplacesWithoutMutation)
    {
        Terrain::ViewData camera;
        camera.add(nullptr);
        auto& entry = camera.getEntry(0);
        osg::ref_ptr<osg::Group> terrain = new osg::Group;
        osg::ref_ptr<osg::Group> partial = new osg::Group;
        partial->addChild(terrain);
        entry.mRenderingNode = partial;
        entry.mRenderingComplete = false;
        Terrain::ViewData ray;
        ray.copyFrom(camera);
        EXPECT_FALSE(ray.getEntry(0).mRenderingComplete);
        EXPECT_TRUE(ray.getEntry(0).needsRendering());
        osg::ref_ptr<osg::Group> complete = new osg::Group;
        complete->addChild(terrain);
        complete->addChild(new osg::Group);
        ray.getEntry(0).mRenderingNode = complete;
        ray.getEntry(0).mRenderingComplete = true;
        EXPECT_FALSE(ray.getEntry(0).needsRendering());
        EXPECT_TRUE(ray.getEntry(0).needsRendering(true));
        Terrain::ViewData copiedCamera;
        copiedCamera.copyFrom(ray);
        EXPECT_TRUE(copiedCamera.getEntry(0).needsRendering(true));
        EXPECT_EQ(camera.getEntry(0).mRenderingNode.get(), partial.get());
        EXPECT_FALSE(camera.getEntry(0).mRenderingComplete);
        EXPECT_EQ(partial->getNumChildren(), 1u);
        EXPECT_EQ(complete->getNumChildren(), 2u);
    }
}
