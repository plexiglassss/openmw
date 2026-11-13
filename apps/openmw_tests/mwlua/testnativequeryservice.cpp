#include "apps/openmw/mwlua/nativequeryservice.hpp"

#include <components/misc/nativejobpool.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
    std::vector<MWLua::NativeDistanceTarget> targets()
    {
        return {
            { .mPosition = osg::Vec3f(3.f, 4.f, 0.f), .mSourceIndex = 1 },
            { .mPosition = osg::Vec3f(0.f, 0.f, 12.f), .mSourceIndex = 2 },
            { .mPosition = osg::Vec3f(6.f, 8.f, 0.f), .mSourceIndex = 3 },
            { .mPosition = osg::Vec3f(1.f, 1.f, -2.f), .mSourceIndex = 4 },
        };
    }

    TEST(NativeQueryServiceTest, SerialAndParallelDistanceKernelsMatch)
    {
        Misc::NativeJobPool serialPool(0, 1);
        Misc::NativeJobPool parallelPool(4, 1);
        MWLua::NativeQueryService serial(serialPool);
        MWLua::NativeQueryService parallel(parallelPool);
        MWLua::NativeDistanceOptions options;
        options.mMaxDistance = 20.f;
        options.mSortByDistance = true;

        const auto serialResult = serial.queryTargets(targets(), osg::Vec3f(), options);
        const auto parallelResult = parallel.queryTargets(targets(), osg::Vec3f(), options);
        ASSERT_EQ(serialResult->size(), parallelResult->size());
        for (std::size_t index = 0; index < serialResult->size(); ++index)
        {
            EXPECT_EQ((*serialResult)[index].mSourceIndex, (*parallelResult)[index].mSourceIndex);
            EXPECT_FLOAT_EQ((*serialResult)[index].mDistance3D, (*parallelResult)[index].mDistance3D);
            EXPECT_FLOAT_EQ((*serialResult)[index].mDistance2D, (*parallelResult)[index].mDistance2D);
            EXPECT_FLOAT_EQ((*serialResult)[index].mHeightDifference,
                (*parallelResult)[index].mHeightDifference);
        }
    }

    TEST(NativeQueryServiceTest, FiltersInTwoOrThreeDimensionsAndPreservesSourceIndices)
    {
        Misc::NativeJobPool pool(4, 1);
        MWLua::NativeQueryService service(pool);
        MWLua::NativeDistanceOptions options;
        options.mMaxDistance = 6.f;

        auto result = service.queryTargets(targets(), osg::Vec3f(), options);
        ASSERT_EQ(result->size(), 2u);
        EXPECT_EQ((*result)[0].mSourceIndex, 1u);
        EXPECT_EQ((*result)[1].mSourceIndex, 4u);
        EXPECT_FLOAT_EQ((*result)[0].mDistance3D, 5.f);
        EXPECT_FLOAT_EQ((*result)[0].mDistance2D, 5.f);
        EXPECT_FLOAT_EQ((*result)[1].mHeightDifference, 2.f);

        options.mFilterBy2D = true;
        result = service.queryTargets(targets(), osg::Vec3f(), options);
        ASSERT_EQ(result->size(), 3u);
        EXPECT_EQ((*result)[1].mSourceIndex, 2u);
        EXPECT_FLOAT_EQ((*result)[1].mDistance2D, 0.f);
        EXPECT_FLOAT_EQ((*result)[1].mDistance3D, 12.f);
    }

    TEST(NativeQueryServiceTest, ComparesTheRoundedVectorLengthAtRadiusBoundary)
    {
        Misc::NativeJobPool pool(0, 1);
        MWLua::NativeQueryService service(pool);
        MWLua::NativeDistanceOptions options;
        options.mMaxDistance = 300;
        const osg::Vec3f p(300.f, .063f, 0.f);
        ASSERT_EQ(p.length(), 300.f);
        const auto result = service.queryTargets({ { .mPosition = p, .mSourceIndex = 1 } }, {}, options);
        ASSERT_EQ(result->size(), 1u);
    }

    TEST(NativeQueryServiceTest, StableSortKeepsEqualDistanceInputOrder)
    {
        Misc::NativeJobPool pool(4, 1);
        MWLua::NativeQueryService service(pool);
        std::vector<MWLua::NativeDistanceTarget> values{
            { .mPosition = osg::Vec3f(1.f, 0.f, 0.f), .mSourceIndex = 7 },
            { .mPosition = osg::Vec3f(-1.f, 0.f, 0.f), .mSourceIndex = 3 },
        };
        MWLua::NativeDistanceOptions options;
        options.mSortByDistance = true;
        const auto result = service.queryTargets(std::move(values), osg::Vec3f(), options);
        ASSERT_EQ(result->size(), 2u);
        EXPECT_EQ((*result)[0].mSourceIndex, 7u);
        EXPECT_EQ((*result)[1].mSourceIndex, 3u);
    }
}
