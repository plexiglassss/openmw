#include <components/terrain/scenerycoverage.hpp>
#include <components/terrain/nextgrid.hpp>
#include <components/terrain/viewdata.hpp>

#include <gtest/gtest.h>
#include <osg/Group>
#include <array>
#include <limits>

namespace
{
    struct Request
    {
        float mSize;
        osg::Vec2f mCenter;
        unsigned mLodFlags = 0;
        bool mActiveGrid = false;
    };
    template <std::size_t N>
    auto cover(const std::array<Request, N>& requests, osg::Vec4i grid = {})
    {
        return Terrain::sceneryCoverage(std::span<const Request>(requests), grid);
    }

    TEST(SceneryCoverage, KeepsLargeOptimizedMergeBoundary)
    {
        const auto groups = cover(std::array{ Request{ 8, { 4, 4 } } });
        ASSERT_EQ(groups.size(), 1u);
        EXPECT_EQ(groups.front().mCells.size(), 64u);
        EXPECT_EQ(groups.front().mRequests, (std::vector<std::size_t>{ 0 }));
        EXPECT_TRUE(Terrain::sceneryReplacementReady(groups.front(), [](auto) { return true; }));
    }

    TEST(SceneryCoverage, EntireSubcellGroupMustBeReadyIncludingKnownEmpty)
    {
        const auto groups = cover(std::array{ Request{ .5f, { .25f, .25f } },
            Request{ .5f, { .75f, .25f } }, Request{ .5f, { .25f, .75f } }, Request{ .5f, { .75f, .75f } } });
        ASSERT_EQ(groups.size(), 1u);
        EXPECT_EQ(groups.front().mCells.size(), 1u);
        for (std::size_t pending = 0; pending < 4; ++pending)
            EXPECT_FALSE(Terrain::sceneryReplacementReady(groups.front(), [&](auto i) { return i != pending; }));
        EXPECT_TRUE(Terrain::sceneryReplacementReady(groups.front(), [](auto) { return true; }));
    }

    TEST(SceneryCoverage, GridMigrationExcludesActiveOwnershipBeforeReplacement)
    {
        const std::array requests{ Request{ 4, { 0, 0 } } };
        for (int shift = -1; shift <= 1; ++shift)
        {
            const osg::Vec4i grid(shift, -1, shift + 1, 1);
            const auto groups = cover(requests, grid);
            ASSERT_EQ(groups.size(), 1u);
            EXPECT_FALSE(groups.front().mCanReplace);
            EXPECT_FALSE(Terrain::sceneryReplacementReady(groups.front(), [](auto) { return true; }));
            EXPECT_EQ(groups.front().mCells.size(), 14u);
            for (const auto& [x, y] : groups.front().mCells)
                EXPECT_FALSE(x >= grid.x() && x < grid.z() && y >= grid.y() && y < grid.w());
        }
    }

    TEST(SceneryCoverage, ParentAndChildCannotBothReplaceTheSameCover)
    {
        const auto groups = cover(std::array{ Request{ 2, { 1, 1 } }, Request{ 1, { .5f, .5f } } });
        ASSERT_EQ(groups.size(), 1u);
        EXPECT_EQ(groups.front().mCells.size(), 4u);
        EXPECT_FALSE(Terrain::sceneryReplacementReady(groups.front(), [](auto) { return true; }));
    }

    TEST(SceneryCoverage, NegativeBoundariesAndNeighbouringCellsRemainDisjoint)
    {
        const auto groups = cover(std::array{ Request{ 1, { -.5f, -.5f } }, Request{ 1, { .5f, -.5f } },
            Request{ 1, { 1.5f, -.5f }, 0, true } });
        ASSERT_EQ(groups.size(), 2u);
        EXPECT_EQ(groups[0].mCells, (std::set<Terrain::SceneryCell>{ { -1, -1 } }));
        EXPECT_EQ(groups[1].mCells, (std::set<Terrain::SceneryCell>{ { 0, -1 } }));
    }

    TEST(SceneryCoverage, CameraRayAndUnclockedCameraReuseRequireCorrectLayer)
    {
        osg::ref_ptr<Terrain::ViewDataMap> views = new Terrain::ViewDataMap;
        osg::ref_ptr<osg::Group> camera = new osg::Group;
        bool changed;
        auto* visible = views->getViewData(camera, {}, {}, changed);
        visible->add(nullptr);
        visible->getEntry(0).mRenderingNode = new osg::Group;
        visible->getEntry(0).mRenderingComplete = true;
        visible->getEntry(0).mRenderingReadyForCamera = true;
        visible->getEntry(0).mRenderingSeparateScenery = true;
        auto* ray = views->getViewData(nullptr, {}, {}, changed);
        ASSERT_NE(ray, visible);
        ASSERT_EQ(ray->getNumEntries(), 1u);
        EXPECT_TRUE(ray->getEntry(0).needsRendering(false, false));
        EXPECT_FALSE(visible->getEntry(0).needsRendering(true, true));
        ray->getEntry(0).mRenderingSeparateScenery = false;
        EXPECT_FALSE(ray->getEntry(0).needsRendering(false, false));
        EXPECT_TRUE(ray->getEntry(0).needsRendering(false, true));
    }

    TEST(NextGridPreparation, CardinalAndDiagonalIncludeOnlyNewCells)
    {
        for (const auto& direction : std::array{ std::pair{ 10.f, 0.f }, std::pair{ 0.f, -10.f },
                 std::pair{ 10.f, 10.f }, std::pair{ -10.f, -10.f } })
        {
            const auto cells = Terrain::nextGridCells(-5, 3, 1, direction.first, direction.second, 8192);
            EXPECT_EQ(cells.size(), direction.first && direction.second ? 5u : 3u);
            EXPECT_EQ(std::set(cells.begin(), cells.end()).size(), cells.size());
            for (const auto& [x, y] : cells)
                EXPECT_TRUE(x < -6 || x > -4 || y < 2 || y > 4);
        }
    }

    TEST(NextGridPreparation, IdleTeleportAndInvalidMotionNeverSweepCells)
    {
        EXPECT_TRUE(Terrain::nextGridCells(0, 0, 1, 0, 0, 8192).empty());
        EXPECT_TRUE(Terrain::nextGridCells(0, 0, 1, 10000, 0, 8192).empty());
        EXPECT_TRUE(Terrain::nextGridCells(0, 0, 1, std::numeric_limits<float>::quiet_NaN(), 20, 8192).empty());
        EXPECT_TRUE(Terrain::nextGridCells(0, 0, 1, 1, std::numeric_limits<float>::infinity(), 8192).empty());
    }
}
