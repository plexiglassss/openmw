#include <components/terrain/rayfootprint.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
    TEST(PagingRay, RejectsOnlyDisjointFiniteSegments)
    {
        const Terrain::RayFootprint ray{ { -20, -20, -11 }, { 20, 20, 35 } };
        EXPECT_TRUE(ray.valid());
        EXPECT_TRUE(ray.intersects(0, 0, 2));
        EXPECT_TRUE(ray.intersects(21, 20, 1));
        EXPECT_FALSE(ray.intersects(80, 80, 1));
        EXPECT_FALSE(ray.intersects(10, -10, 4));
        const Terrain::RayFootprint reversed{ ray.mEnd, ray.mStart };
        EXPECT_TRUE(reversed.intersects(21, 20, 1));
        EXPECT_FALSE(reversed.intersects(10, -10, 4));
    }

    TEST(PagingRay, AllowsOversizedOffsetModelsAndVerticalRays)
    {
        const Terrain::RayFootprint ray{ { -4, 0, -300 }, { -4, 0, 300 } };
        EXPECT_FALSE(ray.intersects(60, 0, 16));
        // A scaled, rotated model whose local sphere is offset from its placement.
        const osg::Vec3d modelCenter(0, 35, 0);
        const double radius = (modelCenter.length() + 5) * 2;
        EXPECT_TRUE(ray.intersects(60, 0, radius));
        EXPECT_FALSE(ray.intersects(90, 0, radius));
        EXPECT_TRUE(ray.intersects(-4, 0, 0));
    }

    TEST(PagingRay, UnknownBoundsAndMalformedSegmentsFallBack)
    {
        const Terrain::RayFootprint point{ { 4, 4, 1 }, { 4, 4, 1 } };
        EXPECT_TRUE(point.intersects(0, 0, 4));
        EXPECT_TRUE(point.intersects(0, 0, std::numeric_limits<double>::infinity()));
        EXPECT_FALSE(point.intersects(0, 0, 3));
        Terrain::RayFootprint bad = point;
        bad.mEnd.z() = std::numeric_limits<double>::quiet_NaN();
        EXPECT_FALSE(bad.valid());
    }
}
