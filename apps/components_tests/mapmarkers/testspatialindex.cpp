#include <gtest/gtest.h>

#include <components/mapmarkers/spatialindex.hpp>

#include <limits>
#include <vector>

namespace
{
    TEST(MapMarkersSpatialIndex, InclusiveNegativeBoundsAndDeterministicOrder)
    {
        MapMarkers::SpatialIndex<int> index;
        index.insert(-2, -1, 2);
        index.insert(-2, -1, 1);
        index.insert(-1, -2, 4);
        index.insert(-1, 0, 3);
        index.insert(0, 1, 5);
        index.insert(1, 0, 6);
        std::vector<int> actual;
        const auto stats = index.query(-2, -1, 0, 1, [&](int key) { actual.push_back(key); });
        EXPECT_EQ(actual, (std::vector<int>{ 1, 2, 3, 5 }));
        EXPECT_EQ(stats.xBuckets, 3u);
        EXPECT_EQ(stats.yBuckets, 3u);
        EXPECT_EQ(stats.keys, 4u);
    }

    TEST(MapMarkersSpatialIndex, DuplicateAndErasePruneOnlyEmptyBuckets)
    {
        MapMarkers::SpatialIndex<int> index;
        EXPECT_TRUE(index.empty());
        EXPECT_TRUE(index.insert(-1, -1, 7));
        EXPECT_FALSE(index.insert(-1, -1, 7));
        EXPECT_TRUE(index.insert(-1, -1, 8));
        EXPECT_TRUE(index.insert(-1, 0, 9));
        EXPECT_FALSE(index.erase(0, -1, 7));
        EXPECT_FALSE(index.erase(-1, 1, 7));
        EXPECT_FALSE(index.erase(-1, -1, 10));
        EXPECT_TRUE(index.erase(-1, -1, 7));
        std::vector<int> actual;
        auto stats = index.query(-1, -1, -1, -1, [&](int key) { actual.push_back(key); });
        EXPECT_EQ(actual, (std::vector<int>{ 8 }));
        EXPECT_EQ(stats.keys, 1u);
        EXPECT_TRUE(index.erase(-1, -1, 8));
        stats = index.query(-1, -1, -1, -1, [](int) {});
        EXPECT_EQ(stats.xBuckets, 1u);
        EXPECT_EQ(stats.yBuckets, 0u);
        EXPECT_TRUE(index.erase(-1, 0, 9));
        EXPECT_TRUE(index.empty());
        index.insert(0, 0, 1);
        index.clear();
        EXPECT_TRUE(index.empty());
    }

    TEST(MapMarkersSpatialIndex, EmptyInvertedAndExtremeBoundsAreSafe)
    {
        MapMarkers::SpatialIndex<int> index;
        EXPECT_EQ(index.query(-1, -1, 1, 1, [](int) {}).keys, 0u);
        constexpr int minimum = std::numeric_limits<int>::min();
        constexpr int maximum = std::numeric_limits<int>::max();
        index.insert(minimum, minimum, 1);
        index.insert(maximum, maximum, 2);
        EXPECT_EQ(index.query(minimum, minimum, maximum, maximum, [](int) {}).keys, 2u);
        EXPECT_EQ(index.query(maximum, maximum, minimum, minimum, [](int) {}).xBuckets, 0u);
        EXPECT_EQ(index.query(minimum, maximum, maximum, minimum, [](int) {}).xBuckets, 0u);
        EXPECT_EQ(index.query(maximum, maximum, maximum, maximum, [](int) {}).keys, 1u);
    }

    TEST(MapMarkersSpatialIndex, SmallViewportDoesNotVisitOneHundredThousandMarkers)
    {
        MapMarkers::SpatialIndex<int> index;
        for (int x = -500; x < 500; ++x)
            for (int y = -50; y < 50; ++y)
                index.insert(x, y, (x + 500) * 100 + y + 50);
        std::vector<int> actual;
        const auto stats = index.query(-2, -1, 2, 1, [&](int key) { actual.push_back(key); });
        std::vector<int> expected;
        for (int x = -2; x <= 2; ++x)
            for (int y = -1; y <= 1; ++y)
                expected.push_back((x + 500) * 100 + y + 50);
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(stats.xBuckets, 5u);
        EXPECT_EQ(stats.yBuckets, 15u);
        EXPECT_EQ(stats.keys, 15u);
    }
}
