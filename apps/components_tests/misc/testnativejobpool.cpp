#include <components/misc/nativejobpool.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace
{
    TEST(NativeJobPoolTest, ProcessesEveryItemExactlyOnce)
    {
        Misc::NativeJobPool pool(4, 8);
        std::vector<std::atomic_int> visits(257);
        pool.parallelFor(visits.size(), [&](std::size_t index) { ++visits[index]; });
        for (const auto& value : visits)
            EXPECT_EQ(value.load(), 1);
        const auto stats = pool.statistics();
        EXPECT_EQ(stats.mParallelCalls, 1u);
        EXPECT_EQ(stats.mItems, visits.size());
        EXPECT_GT(stats.mTasks, 1u);
    }

    TEST(NativeJobPoolTest, UsesSerialFallbackForSmallAndNestedWork)
    {
        Misc::NativeJobPool pool(2, 16);
        std::atomic_int count = 0;
        pool.parallelFor(4, [&](std::size_t) { ++count; });
        EXPECT_EQ(count.load(), 4);

        pool.parallelFor(64, [&](std::size_t index) {
            if (index == 0)
                pool.parallelFor(3, [&](std::size_t) { ++count; });
        });
        EXPECT_EQ(count.load(), 7);
        EXPECT_GE(pool.statistics().mSerialCalls, 2u);
    }

    TEST(NativeJobPoolTest, RethrowsLowestIndexFailureDeterministically)
    {
        Misc::NativeJobPool pool(4, 1);
        try
        {
            pool.parallelFor(64, [](std::size_t index) {
                if (index == 17)
                    throw std::runtime_error("seventeen");
                if (index == 29)
                    throw std::runtime_error("twenty-nine");
            });
            FAIL() << "Expected an exception";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_STREQ(error.what(), "seventeen");
        }
    }

    TEST(NativeJobPoolTest, ZeroWorkersKeepsTheSameResult)
    {
        Misc::NativeJobPool pool(0, 1);
        std::vector<std::size_t> values(128);
        pool.parallelFor(values.size(), [&](std::size_t index) { values[index] = index * index; });
        for (std::size_t index = 0; index < values.size(); ++index)
            EXPECT_EQ(values[index], index * index);
        EXPECT_EQ(pool.statistics().mParallelCalls, 0u);
        EXPECT_EQ(pool.statistics().mSerialCalls, 1u);
    }
}

#include <components/misc/actorneighbours.hpp>

TEST(ActorNeighboursTest, MatchesOrderedBruteForceAndBoundsCrowdedRows)
{
    Misc::NativeJobPool pool(4, 1);
    std::vector<Misc::ActorNeighbours::Position> positions;
    for (int i = 0; i < 185; ++i)
        positions.push_back({ static_cast<float>(i % 17) * 100.f, static_cast<float>(i / 17) * 100.f });
    Misc::ActorNeighbours index(positions, 200.f, pool);
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        std::vector<std::size_t> expected;
        for (std::size_t j = 0; j < positions.size(); ++j)
            if (std::abs(positions[i][0] - positions[j][0]) <= 200.f
                && std::abs(positions[i][1] - positions[j][1]) <= 200.f)
                expected.push_back(j);
        ASSERT_TRUE(index[i].mComplete);
        EXPECT_EQ(index[i].mIndices, expected);
    }
    std::fill(positions.begin(), positions.end(), Misc::ActorNeighbours::Position{ 1.f, 2.f });
    Misc::ActorNeighbours dense(positions, 200.f, pool);
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        EXPECT_FALSE(dense[i].mComplete);
        EXPECT_LE(dense[i].mIndices.size(), 64u);
    }
}
