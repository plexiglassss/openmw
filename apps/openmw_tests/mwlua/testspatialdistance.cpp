#include "apps/openmw/mwlua/nativequeryservice.hpp"
#include <components/misc/combinedperformance.hpp>
#include <components/misc/nativejobpool.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <random>

namespace
{
    using namespace MWLua;
    NativePositions positions(std::size_t count = 512)
    {
        auto values = std::make_shared<std::vector<NativeDistanceTarget>>();
        for (std::size_t i = 0; i < count; ++i)
            values->push_back({ .mObjectId = { static_cast<unsigned>(i + 1), 0 },
                .mPosition = osg::Vec3f(float(i * 100), float(i % 13), float(i % 5)),
                .mSourceIndex = i + 1, .mHasObject = i % 2 == 0 });
        return values;
    }
    void equal(const NativeDistanceSamples& a, const NativeDistanceSamples& b)
    {
        ASSERT_EQ(a->size(), b->size());
        const auto sameFloat = [](float x, float y) { return x == y || (std::isnan(x) && std::isnan(y)); };
        for (std::size_t i = 0; i < a->size(); ++i)
        {
            EXPECT_EQ((*a)[i].mObjectId, (*b)[i].mObjectId);
            EXPECT_EQ((*a)[i].mSourceIndex, (*b)[i].mSourceIndex);
            EXPECT_EQ((*a)[i].mHasObject, (*b)[i].mHasObject);
            EXPECT_TRUE(sameFloat((*a)[i].mDistance2D, (*b)[i].mDistance2D));
            EXPECT_TRUE(sameFloat((*a)[i].mDistance3D, (*b)[i].mDistance3D));
            EXPECT_TRUE(sameFloat((*a)[i].mHeightDifference, (*b)[i].mHeightDifference));
        }
    }
    TEST(SpatialDistance, ColdWarmAndIndependentSwitchKeepExactResults)
    {
        Misc::NativeJobPool jobs(2, 8);
        NativeQueryService service(jobs);
        const auto values = positions();
        NativeDistanceOptions options{ .mMaxDistance = 300, .mSortByDistance = true };
        for (int i = 0; i < 16; ++i)
        {
            equal(service.querySnapshot(values, {}, options), NativeQueryService::compute(*values, {}, options));
            if (i < 7) EXPECT_EQ(service.mDistanceIndicesBuilt, 0u);
        }
        EXPECT_EQ(service.mDistanceIndicesBuilt, Misc::useSpatialDistanceQueries() ? 1u : 0u);
        EXPECT_EQ(service.mDistanceIndicesReused, Misc::useSpatialDistanceQueries() ? 8u : 0u);
    }
    TEST(SpatialDistance, DenseSmallAndOneOffInputsKeepLinearPath)
    {
        Misc::NativeJobPool jobs(0, 1);
        NativeQueryService service(jobs);
        auto small = positions(12), dense = positions();
        for (int i = 0; i < 16; ++i)
        {
            equal(service.querySnapshot(small, {}, { .mMaxDistance = 1 }),
                NativeQueryService::compute(*small, {}, { .mMaxDistance = 1 }));
            service.querySnapshot(dense, {}, {});
            service.querySnapshot(dense, {}, { .mMaxDistance = 1e9 });
            service.querySnapshot(positions(), {}, { .mMaxDistance = 1 });
        }
        EXPECT_EQ(service.mDistanceIndicesBuilt, 0u);
    }
    TEST(SpatialDistance, IndexedKernelPreservesDuplicatesTiesAndBothDimensions)
    {
        auto values = *positions();
        values[0].mPosition = { 3, 4, 0 };
        values[1] = values[0];
        values[1].mSourceIndex = 400;
        values[2].mPosition = { -3, -4, 0 };
        values[3].mPosition = { 0, 0, 500 };
        values[4].mPosition = { 0, 0, std::numeric_limits<float>::quiet_NaN() };
        std::vector<Misc::SpatialIndex::Position> xy;
        for (const auto& v : values) xy.push_back({ v.mPosition.x(), v.mPosition.y() });
        Misc::SpatialIndex index(xy);
        ASSERT_TRUE(index.query({0, 0}, 300).has_value());
        for (bool filter2D : { false, true })
            for (bool sorted : { false, true })
                for (double radius : { 0., 5., 300., -1., 1e300 })
                {
                    NativeDistanceOptions options{ .mMaxDistance = radius, .mSortByDistance = sorted,
                        .mFilterBy2D = filter2D };
                    equal(NativeQueryService::compute(values, {}, options, nullptr, &index),
                        NativeQueryService::compute(values, {}, options));
                }
    }
    TEST(SpatialDistance, RandomRoundedAndUnderflowBoundariesMatchLinearKernel)
    {
        auto values = *positions(2048);
        std::mt19937 random(89);
        std::uniform_real_distribution<float> coordinate(-32000, 32000);
        for (auto& v : values) v.mPosition = { coordinate(random), coordinate(random), coordinate(random) };
        values[0].mPosition = { -1024, 0, 0 };
        values[1].mPosition = { 1e-23f, 0, 0 };
        std::vector<Misc::SpatialIndex::Position> xy;
        for (const auto& v : values) xy.push_back({ v.mPosition.x(), v.mPosition.y() });
        Misc::SpatialIndex index(xy);
        for (int i = 0; i < 80; ++i)
        {
            osg::Vec3f origin = i < 2 ? osg::Vec3f(i == 0 ? .001f : 0.f, 0, 0)
                : osg::Vec3f(coordinate(random), coordinate(random), coordinate(random));
            NativeDistanceOptions options{ .mMaxDistance = i == 0 ? 1024.0009765625 : i == 1 ? 0 : 1000.,
                .mSortByDistance = i % 2 == 0, .mFilterBy2D = i % 3 == 0 };
            equal(NativeQueryService::compute(values, origin, options, nullptr, &index),
                NativeQueryService::compute(values, origin, options));
        }
    }
    TEST(SpatialDistance, NonFinitePositionsAndOriginsUseExactFallback)
    {
        auto values = *positions();
        values[1].mPosition.x() = std::numeric_limits<float>::quiet_NaN();
        std::vector<Misc::SpatialIndex::Position> xy;
        for (const auto& v : values) xy.push_back({ v.mPosition.x(), v.mPosition.y() });
        Misc::SpatialIndex index(xy);
        for (const auto origin : { osg::Vec3f(), osg::Vec3f(INFINITY, 0, 0), osg::Vec3f(0, 0, NAN) })
            for (bool by2D : { false, true })
            {
                NativeDistanceOptions options{ .mMaxDistance = 1000, .mFilterBy2D = by2D };
                equal(NativeQueryService::compute(values, origin, options, nullptr, &index),
                    NativeQueryService::compute(values, origin, options));
            }
    }
    TEST(SpatialDistance, EvictionAndResetRetainWorkerOwnedImmutableIndex)
    {
        Misc::NativeJobPool jobs(0, 1);
        NativeQueryService service(jobs);
        const auto values = positions();
        const NativeDistanceOptions options{ .mMaxDistance = 250 };
        std::shared_ptr<const Misc::SpatialIndex> retained;
        for (int i = 0; i < 8; ++i) retained = service.prepareDistanceIndex(values, {}, options);
        for (int i = 0; i < 130; ++i) service.prepareDistanceIndex(positions(), {}, options);
        service.reset();
        EXPECT_EQ(bool(retained), Misc::useSpatialDistanceQueries());
        equal(NativeQueryService::compute(*values, {}, options, nullptr, retained.get()),
            NativeQueryService::compute(*values, {}, options));
        EXPECT_EQ(service.prepareDistanceIndex(values, {}, options), nullptr);
    }
}
