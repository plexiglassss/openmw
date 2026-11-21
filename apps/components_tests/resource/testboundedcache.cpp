#include <components/resource/boundedcache.hpp>

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <thread>

namespace
{
    using Cache = Resource::BoundedCache<int, std::shared_ptr<int>>;

    TEST(RecentResourceCache, RevisitRefreshesLifetimeAndEvictsLeastRecentlyUsed)
    {
        Cache cache(2, 100, 30);
        auto first = std::make_shared<int>(1);
        const auto generation = cache.generation();
        EXPECT_TRUE(cache.put(1, first, 40, generation, 0));
        EXPECT_TRUE(cache.put(2, std::make_shared<int>(2), 40, generation, 1));
        EXPECT_EQ(cache.get(1, 25), first);
        EXPECT_TRUE(cache.put(3, std::make_shared<int>(3), 40, generation, 26));
        EXPECT_FALSE(cache.get(2, 26));
        cache.prune(45);
        EXPECT_EQ(cache.get(1, 45), first);
        cache.prune(76);
        EXPECT_FALSE(cache.get(1, 76));
        EXPECT_EQ(*first, 1); // Expiry releases only the cache's ownership.
        EXPECT_EQ(cache.stats().mBytes, 0);
    }

    TEST(RecentResourceCache, ByteBudgetOversizeAndEntryBoundAreIndependent)
    {
        Cache cache(10, 100, 30);
        const auto generation = cache.generation();
        EXPECT_TRUE(cache.put(1, std::make_shared<int>(1), 60, generation, 0));
        EXPECT_FALSE(cache.put(2, std::make_shared<int>(2), 101, generation, 1));
        EXPECT_TRUE(cache.get(1, 1));
        EXPECT_TRUE(cache.put(3, std::make_shared<int>(3), 60, generation, 2));
        EXPECT_FALSE(cache.get(1, 2));
        EXPECT_EQ(cache.stats().mBytes, 60);
        EXPECT_EQ(cache.stats().mCache.mSize, 1);
        Cache disabled(0, 100, 30);
        EXPECT_FALSE(disabled.put(1, std::make_shared<int>(), 1, disabled.generation(), 0));
    }

    TEST(RecentResourceCache, ClearRejectsAnAlreadyRunningProducer)
    {
        Cache cache(10, 100, 30);
        std::promise<void> started, resume;
        auto ready = resume.get_future();
        auto producer = std::async(std::launch::async, [&] {
            const auto generation = cache.generation();
            started.set_value();
            ready.wait();
            return cache.put(1, std::make_shared<int>(1), 10, generation, 1);
        });
        started.get_future().wait();
        cache.clear();
        resume.set_value();
        EXPECT_FALSE(producer.get());
        EXPECT_FALSE(cache.get(1, 1));
        EXPECT_TRUE(cache.put(1, std::make_shared<int>(2), 10, cache.generation(), 1));
        EXPECT_EQ(*cache.get(1, 1), 2);
    }

    TEST(RecentResourceCache, ConcurrentConsumersCannotExceedEitherBound)
    {
        Cache cache(8, 60, 30);
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&, i] {
                for (int j = 0; j < 100; ++j)
                {
                    cache.put(i * 100 + j, std::make_shared<int>(j), 10, cache.generation(), 1);
                    cache.get(i * 100 + j, 1);
                    const auto stats = cache.stats();
                    EXPECT_LE(stats.mBytes, 60);
                    EXPECT_LE(stats.mCache.mSize, 6);
                }
            });
        for (auto& thread : threads)
            thread.join();
    }

    TEST(RecentResourceCache, DestructionCanReenterCacheAfterEvictionAndClear)
    {
        Cache cache(1, 1, 30);
        int destroyed = 0;
        const auto make = [&] {
            return std::shared_ptr<int>(new int, [&](int* value) {
                cache.stats(); // Would deadlock if final references were dropped under the mutex.
                ++destroyed;
                delete value;
            });
        };
        EXPECT_TRUE(cache.put(1, make(), 1, cache.generation(), 0));
        EXPECT_TRUE(cache.put(2, make(), 1, cache.generation(), 0));
        EXPECT_EQ(destroyed, 1);
        cache.clear();
        EXPECT_EQ(destroyed, 2);
    }
}
