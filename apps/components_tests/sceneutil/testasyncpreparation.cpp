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
