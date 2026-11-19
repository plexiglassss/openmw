#include <components/resource/sceneinstancecache.hpp>

#include <osg/Group>
#include <osg/observer_ptr>

#include <gtest/gtest.h>

#include <future>
#include <stdexcept>
#include <thread>

namespace
{
    using Cache = Resource::SceneInstanceCache;
    constexpr Cache::Revision revision{ 1, 2 };
    struct SceneInstanceCacheTest : testing::Test
    {
        Cache cache{ 2, 1, 100 };
        osg::ref_ptr<osg::Node> base = new osg::Group;
        std::atomic_bool abort{ false };
        Cache::TicketPtr prepare(std::size_t cost = 10)
        {
            return cache.prepare(base, revision, abort, cost, [] { return new osg::Group; });
        }
    };

    TEST_F(SceneInstanceCacheTest, ConsumedInstanceIsPrivateAndNotRetainedByPreloaderTicket)
    {
        auto ticket = prepare();
        ASSERT_TRUE(ticket);
        auto node = cache.take(base, revision);
        ASSERT_TRUE(node);
        EXPECT_NE(node, base);
        EXPECT_EQ(node->getNumParents(), 0u);
        EXPECT_FALSE(cache.take(base, revision));
        osg::observer_ptr<osg::Node> observer(node);
        node = nullptr;
        EXPECT_FALSE(observer.valid());
        EXPECT_EQ(cache.getStats().mHit, 1u);
    }

    TEST_F(SceneInstanceCacheTest, RejectsDifferentTemplateSettingsAndVfs)
    {
        auto ticket = prepare();
        osg::ref_ptr<osg::Node> other = new osg::Group;
        EXPECT_FALSE(cache.take(other, revision));
        EXPECT_FALSE(cache.take(base, { 2, 2 }));
        EXPECT_FALSE(cache.take(base, { 1, 3 }));
        EXPECT_TRUE(cache.take(base, revision));
    }

    TEST_F(SceneInstanceCacheTest, EnforcesTemplateCountAndEstimatedByteBudgetBeforeFactory)
    {
        auto first = prepare(90);
        ASSERT_TRUE(first);
        EXPECT_FALSE(prepare());
        osg::ref_ptr<osg::Node> other = new osg::Group;
        int called = 0;
        auto clone = [&] {
            ++called;
            return new osg::Group;
        };
        EXPECT_FALSE(cache.prepare(other, revision, abort, 11, clone));
        EXPECT_EQ(called, 0);
        auto second = cache.prepare(other, revision, abort, 10, clone);
        EXPECT_TRUE(second);
        EXPECT_EQ(called, 1);
        EXPECT_EQ(cache.getStats().mSize, 2u);
    }

    TEST_F(SceneInstanceCacheTest, ExpiredTicketsAreNeverConsumedAndReturnBudget)
    {
        auto ticket = prepare(100);
        ticket.reset();
        EXPECT_FALSE(cache.take(base, revision));
        EXPECT_EQ(cache.getStats().mSize, 1u); // Cleanup is left to the worker.
        cache.prune();
        EXPECT_EQ(cache.getStats().mExpired, 1u);
        ticket = prepare(100);
        ticket.reset();
        cache.prune();
        EXPECT_EQ(cache.getStats().mSize, 0u);
        EXPECT_EQ(cache.getStats().mExpired, 2u);
        EXPECT_TRUE(prepare(100));
    }

    TEST_F(SceneInstanceCacheTest, CancelledAndThrowingFactoriesReleaseReservations)
    {
        abort = true;
        bool called = false;
        EXPECT_FALSE(cache.prepare(base, revision, abort, 100, [&] {
            called = true;
            return new osg::Group;
        }));
        EXPECT_FALSE(called);
        abort = false;
        EXPECT_THROW(cache.prepare(base, revision, abort, 100,
                         []() -> osg::ref_ptr<osg::Node> { throw std::runtime_error("fixture"); }),
            std::runtime_error);
        EXPECT_EQ(cache.getStats().mSize, 0u);
        EXPECT_FALSE(cache.prepare(base, revision, abort, 100, [&] {
            abort = true;
            return new osg::Group;
        }));
        EXPECT_EQ(cache.getStats().mSize, 0u);
    }

    TEST_F(SceneInstanceCacheTest, ForegroundDoesNotJoinAndClearRejectsAnInFlightClone)
    {
        std::promise<void> started, release;
        auto released = release.get_future();
        Cache::TicketPtr result;
        std::thread worker([&] {
            result = cache.prepare(base, revision, abort, 100, [&] {
                started.set_value();
                released.wait();
                return new osg::Group;
            });
        });
        started.get_future().wait();
        EXPECT_FALSE(cache.take(base, revision));
        cache.clear();
        // Reservation survives clear until the worker returns, avoiding oversubscription.
        EXPECT_EQ(cache.getStats().mSize, 1u);
        EXPECT_FALSE(prepare());
        release.set_value();
        worker.join();
        EXPECT_FALSE(result);
        EXPECT_FALSE(cache.take(base, revision));
        EXPECT_EQ(cache.getStats().mSize, 0u);
        EXPECT_TRUE(prepare(100));
    }
}
