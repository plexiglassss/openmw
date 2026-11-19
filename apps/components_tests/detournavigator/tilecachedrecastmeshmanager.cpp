
#include <components/detournavigator/debug.hpp>
#include <components/detournavigator/settingsutils.hpp>
#include <components/detournavigator/stats.hpp>
#include <components/detournavigator/tilecachedrecastmeshmanager.hpp>
#include <memory>
#include <components/misc/combinedperformance.hpp>

#include <BulletCollision/CollisionShapes/btBoxShape.h>

#include <osg/io_utils>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <thread>

namespace
{
    using namespace testing;
    using namespace DetourNavigator;

    struct DetourNavigatorTileCachedRecastMeshManagerTest : Test
    {
        RecastSettings mSettings;
        const ObjectTransform mObjectTransform{ ESM::Position{ { 0, 0, 0 }, { 0, 0, 0 } }, 0.0f };
        const std::shared_ptr<const Resource::BulletShape> mShape = std::make_shared<Resource::BulletShape>();
        const std::shared_ptr<const Resource::BulletShapeInstance> mInstance
            = std::make_shared<Resource::BulletShapeInstance>(mShape);
        const ESM::RefId mWorldspace = ESM::RefId::stringRefId("worldspace");

        DetourNavigatorTileCachedRecastMeshManagerTest()
        {
            mSettings.mBorderSize = 16;
            mSettings.mCellSize = 0.2f;
            mSettings.mRecastScaleFactor = 0.017647058823529415f;
            mSettings.mTileSize = 64;
        }
    };

    // Exercise the real triangle traversal outside the manager lock. All waits
    // are bounded, including failures before the test can release a producer.
    struct GeometryGate
    {
        mutable std::mutex mMutex;
        std::condition_variable mChanged;
        int mCalls = 0;
        int mBlockedCalls = 1;
        bool mReleased = false;
        bool mThrow = false;
        std::atomic<bool> mDestroyed = false;

        void enter()
        {
            std::unique_lock lock(mMutex);
            const int call = ++mCalls;
            mChanged.notify_all();
            if (call <= mBlockedCalls
                && !mChanged.wait_for(lock, std::chrono::seconds(10), [&] { return mReleased; }))
                throw std::runtime_error("geometry test gate timed out");
            if (mThrow)
                throw std::runtime_error("geometry traversal failed");
        }

        bool waitForCalls(int count)
        {
            std::unique_lock lock(mMutex);
            return mChanged.wait_for(lock, std::chrono::seconds(5), [&] { return mCalls >= count; });
        }

        int calls() const
        {
            const std::lock_guard lock(mMutex);
            return mCalls;
        }

        void release()
        {
            const std::lock_guard lock(mMutex);
            mReleased = true;
            mChanged.notify_all();
        }
    };

    struct CountedGeometry : btConcaveShape
    {
        std::shared_ptr<GeometryGate> mGate;
        btVector3 mScale{ 1, 1, 1 };

        explicit CountedGeometry(std::shared_ptr<GeometryGate> gate)
            : mGate(std::move(gate))
        {
            m_shapeType = CUSTOM_CONCAVE_SHAPE_TYPE;
        }

        ~CountedGeometry() override { mGate->mDestroyed = true; }

        void processAllTriangles(btTriangleCallback* callback, const btVector3&, const btVector3&) const override
        {
            mGate->enter();
            btVector3 vertices[]{ { -20, -20, 0 }, { 20, -20, 0 }, { 20, 20, 0 } };
            callback->processTriangle(vertices, 0, 0);
        }

        void getAabb(const btTransform& transform, btVector3& min, btVector3& max) const override
        {
            min = transform.getOrigin() - btVector3(20, 20, 1);
            max = transform.getOrigin() + btVector3(20, 20, 1);
        }
        void setLocalScaling(const btVector3& scale) override { mScale = scale; }
        const btVector3& getLocalScaling() const override { return mScale; }
        void calculateLocalInertia(btScalar, btVector3& inertia) const override { inertia.setZero(); }
        const char* getName() const override { return "CountedGeometry"; }
    };

    struct NavMeshGeometryPreparation : DetourNavigatorTileCachedRecastMeshManagerTest
    {
        const TilePosition mTile{ 0, 0 };

        bool sharing() const
        {
            return Misc::useCombinedPerformance()
                && Misc::enabledUnlessZero("OPENMW_SHARED_NAVIGATION_GEOMETRY");
        }

        void add(TileCachedRecastMeshManager& manager, const btCollisionShape& shape)
        {
            ASSERT_TRUE(manager.addObject(ObjectId(&shape), CollisionShape(mInstance, shape, mObjectTransform),
                btTransform::getIdentity(), AreaType_ground, nullptr));
            manager.takeChangedTiles(nullptr);
        }

        auto request(TileCachedRecastMeshManager& manager)
        {
            return std::async(std::launch::async, [&] { return manager.getMesh(mWorldspace, mTile); });
        }

        bool waitForFollower(TileCachedRecastMeshManager& manager, GeometryGate& gate)
        {
            if (!sharing())
                return gate.waitForCalls(2);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (manager.getStats().mJoinedMeshRequests == 1)
                    return true;
                std::this_thread::yield();
            }
            return false;
        }

        static void expectSameGeometry(const RecastMesh& a, const RecastMesh& b)
        {
            EXPECT_EQ(a.getMesh().getIndices(), b.getMesh().getIndices());
            EXPECT_EQ(a.getMesh().getVertices(), b.getMesh().getVertices());
            EXPECT_EQ(a.getMesh().getAreaTypes(), b.getMesh().getAreaTypes());
            EXPECT_FALSE(a.getWater() < b.getWater());
            EXPECT_FALSE(b.getWater() < a.getWater());
            EXPECT_FALSE(a.getHeightfields() < b.getHeightfields());
            EXPECT_FALSE(b.getHeightfields() < a.getHeightfields());
            EXPECT_FALSE(a.getFlatHeightfields() < b.getFlatHeightfields());
            EXPECT_FALSE(b.getFlatHeightfields() < a.getFlatHeightfields());
        }
    };

    TEST_F(NavMeshGeometryPreparation, ConcurrentRequestsShareActualTriangleTraversal)
    {
        auto gate = std::make_shared<GeometryGate>();
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        auto first = request(manager);
        ASSERT_TRUE(gate->waitForCalls(1));
        auto second = request(manager);
        EXPECT_TRUE(waitForFollower(manager, *gate));
        gate->release();
        const auto a = first.get();
        const auto b = second.get();
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(gate->calls(), sharing() ? 1 : 2);
        RecordProperty("triangle_traversals", gate->calls());
        EXPECT_EQ(a == b, sharing());
        EXPECT_GT(a->getMesh().getTrianglesCount(), 0);
        expectSameGeometry(*a, *b);
        EXPECT_EQ(manager.getStats().mPendingMeshTiles, 0);
    }

    TEST_F(NavMeshGeometryPreparation, UnrelatedTileChangeDoesNotDiscardOrRetryBuild)
    {
        auto gate = std::make_shared<GeometryGate>();
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        auto first = request(manager);
        ASSERT_TRUE(gate->waitForCalls(1));
        const btBoxShape farShape(btVector3(20, 20, 1));
        ASSERT_TRUE(manager.addObject(ObjectId(&farShape), CollisionShape(mInstance, farShape, mObjectTransform),
            btTransform(btQuaternion::getIdentity(), btVector3(100000, 100000, 0)), AreaType_ground, nullptr));
        manager.takeChangedTiles(nullptr);
        gate->release();
        const auto mesh = first.get();
        ASSERT_NE(mesh, nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), mesh);
        EXPECT_EQ(gate->calls(), 1);
    }

    TEST_F(NavMeshGeometryPreparation, ChangedTileCannotBeOverwrittenByEarlierBuild)
    {
        auto gate = std::make_shared<GeometryGate>();
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        auto first = request(manager);
        ASSERT_TRUE(gate->waitForCalls(1));
        manager.removeObject(ObjectId(&shape), nullptr);
        ASSERT_TRUE(manager.addObject(ObjectId(&shape), CollisionShape(mInstance, shape, mObjectTransform),
            btTransform::getIdentity(), AreaType_water, nullptr));
        manager.takeChangedTiles(nullptr);
        const auto current = manager.getMesh(mWorldspace, mTile);
        gate->release();
        const auto old = first.get();
        ASSERT_NE(old, nullptr); // nullptr means an empty tile to the updater.
        ASSERT_NE(current, nullptr);
        EXPECT_NE(old, current);
        EXPECT_NE(old->getMesh().getAreaTypes(), current->getMesh().getAreaTypes());
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), current);
        EXPECT_EQ(gate->calls(), 2);
    }

    TEST_F(NavMeshGeometryPreparation, RangeExitAndReentryCannotRestoreObsoleteBuild)
    {
        auto gate = std::make_shared<GeometryGate>();
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        auto first = request(manager); // Initial infinite range also needs invalidation.
        ASSERT_TRUE(gate->waitForCalls(1));
        manager.setRange({ TilePosition(10, 10), TilePosition(11, 11) }, nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), nullptr);
        manager.setRange({ TilePosition(0, 0), TilePosition(1, 1) }, nullptr);
        manager.takeChangedTiles(nullptr);
        const auto current = manager.getMesh(mWorldspace, mTile);
        gate->release();
        const auto old = first.get();
        ASSERT_NE(old, nullptr);
        ASSERT_NE(current, nullptr);
        EXPECT_NE(old, current);
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), current);
        EXPECT_EQ(gate->calls(), 2);
    }

    TEST_F(NavMeshGeometryPreparation, WorldChangeClearsFiniteAndInfiniteInputs)
    {
        auto gate = std::make_shared<GeometryGate>();
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        manager.addWater({ 0, 0 }, 8192, 12, nullptr);
        manager.addWater({ 1, 1 }, std::numeric_limits<int>::max(), 34, nullptr);
        manager.addHeightfield({ -1, 0 }, 8192, HeightfieldPlane{ 56 }, nullptr);
        manager.addHeightfield({ 0, 0 }, std::numeric_limits<int>::max(), HeightfieldPlane{ 78 }, nullptr);
        manager.takeChangedTiles(nullptr);
        auto first = request(manager);
        ASSERT_TRUE(gate->waitForCalls(1));
        const auto world = ESM::RefId::stringRefId("other-world");
        manager.setWorldspace(world, nullptr);
        EXPECT_EQ(manager.getMesh(world, mTile), nullptr);
        EXPECT_TRUE(manager.takeChangedTiles(nullptr).empty());
        add(manager, shape);
        const auto current = manager.getMesh(world, mTile);
        gate->release();
        const auto old = first.get();
        ASSERT_NE(old, nullptr);
        ASSERT_NE(current, nullptr);
        EXPECT_EQ(old->getWater().size(), 2);
        EXPECT_EQ(old->getFlatHeightfields().size(), 2);
        EXPECT_TRUE(current->getWater().empty());
        EXPECT_TRUE(current->getFlatHeightfields().empty());
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), nullptr);
        EXPECT_EQ(manager.getMesh(world, mTile), current);
    }

    TEST_F(NavMeshGeometryPreparation, FailureReachesFollowersAndAllowsRetry)
    {
        auto gate = std::make_shared<GeometryGate>();
        gate->mBlockedCalls = 2;
        gate->mThrow = true;
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        auto first = request(manager);
        ASSERT_TRUE(gate->waitForCalls(1));
        auto second = request(manager);
        EXPECT_TRUE(waitForFollower(manager, *gate));
        gate->release();
        EXPECT_THROW(first.get(), std::runtime_error);
        EXPECT_THROW(second.get(), std::runtime_error);
        EXPECT_EQ(manager.getStats().mPendingMeshTiles, 0);
        EXPECT_EQ(manager.getStats().mTiles, 0);
        gate->mThrow = false; // Both failed callers have joined.
        EXPECT_NE(manager.getMesh(mWorldspace, mTile), nullptr);
        EXPECT_EQ(gate->calls(), sharing() ? 2 : 3);
    }

    TEST_F(NavMeshGeometryPreparation, SnapshotRetainsShapeAfterWorldAndOwnerRemoval)
    {
        auto gate = std::make_shared<GeometryGate>();
        std::shared_ptr<Resource::BulletShape> source = std::make_shared<Resource::BulletShape>();
        std::shared_ptr<Resource::BulletShapeInstance> instance = std::make_shared<Resource::BulletShapeInstance>(source);
        source->mCollisionShape.reset(new CountedGeometry(gate));
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        ASSERT_TRUE(manager.addObject(ObjectId(source.get()),
            CollisionShape(instance, *source->mCollisionShape, mObjectTransform), btTransform::getIdentity(),
            AreaType_ground, nullptr));
        manager.takeChangedTiles(nullptr);
        auto first = request(manager);
        ASSERT_TRUE(gate->waitForCalls(1));
        manager.setWorldspace(ESM::RefId::stringRefId("other-world"), nullptr);
        instance = nullptr;
        source = nullptr;
        EXPECT_FALSE(gate->mDestroyed);
        gate->release();
        auto mesh = first.get();
        ASSERT_NE(mesh, nullptr);
        EXPECT_GT(mesh->getMesh().getTrianglesCount(), 0);
        EXPECT_FALSE(gate->mDestroyed);
        mesh.reset();
        EXPECT_TRUE(gate->mDestroyed);
    }

    TEST_F(NavMeshGeometryPreparation, UncachedToolPathIgnoresRangeAndMatchesCachedGeometry)
    {
        auto gate = std::make_shared<GeometryGate>();
        gate->release();
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        manager.addWater({ 0, 0 }, 8192, 12, nullptr);
        manager.addHeightfield({ 0, 0 }, 8192, HeightfieldPlane{ 56 }, nullptr);
        manager.takeChangedTiles(nullptr);
        const auto cached = manager.getMesh(mWorldspace, mTile);
        manager.setRange({ TilePosition(10, 10), TilePosition(11, 11) }, nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), nullptr);
        const auto fresh = manager.getNewMesh(mWorldspace, mTile);
        ASSERT_NE(cached, nullptr);
        ASSERT_NE(fresh, nullptr);
        EXPECT_NE(cached, fresh);
        expectSameGeometry(*cached, *fresh);
        EXPECT_EQ(gate->calls(), 2);
    }

    TEST_F(NavMeshGeometryPreparation, SnapshotAllocationDependsOnLocalMatches)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape shape(btVector3(20, 20, 1));
        add(manager, shape);
        const auto original = manager.getNewMesh(mWorldspace, mTile);
        std::array<int, 1024> ids{};
        for (const auto& id : ids)
            ASSERT_TRUE(manager.addObject(ObjectId(&id), CollisionShape(mInstance, shape, mObjectTransform),
                btTransform(btQuaternion::getIdentity(), btVector3(100000, 100000, 0)), AreaType_ground, nullptr));
        manager.takeChangedTiles(nullptr);
        const auto mesh = manager.getMesh(mWorldspace, mTile);
        ASSERT_NE(original, nullptr);
        ASSERT_NE(mesh, nullptr);
        expectSameGeometry(*original, *mesh);
        const auto stats = manager.getStats();
        EXPECT_EQ(stats.mObjects, 1025);
        EXPECT_EQ(stats.mSnapshotObjects, 1);
        EXPECT_EQ(stats.mSnapshotObjectCapacity, sharing() ? 1 : 1025);
        RecordProperty("active_objects", static_cast<int>(stats.mObjects));
        RecordProperty("snapshot_objects", static_cast<int>(stats.mSnapshotObjects));
        RecordProperty("snapshot_capacity", static_cast<int>(stats.mSnapshotObjectCapacity));
    }

    TEST_F(NavMeshGeometryPreparation, EarlierCompletionDoesNotRemoveNewPendingBuild)
    {
        auto oldGate = std::make_shared<GeometryGate>();
        auto newGate = std::make_shared<GeometryGate>();
        CountedGeometry oldShape(oldGate);
        CountedGeometry newShape(newGate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, oldShape);
        auto first = request(manager);
        ASSERT_TRUE(oldGate->waitForCalls(1));
        manager.removeObject(ObjectId(&oldShape), nullptr);
        add(manager, newShape);
        auto second = request(manager);
        ASSERT_TRUE(newGate->waitForCalls(1));
        oldGate->release();
        ASSERT_NE(first.get(), nullptr);
        EXPECT_EQ(manager.getStats().mPendingMeshTiles, 1);
        newGate->release();
        const auto current = second.get();
        ASSERT_NE(current, nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), current);
        EXPECT_EQ(newGate->calls(), 1);
    }

    TEST_F(NavMeshGeometryPreparation, WorldExitAndReentryDoesNotRestoreEarlierGeneration)
    {
        auto gate = std::make_shared<GeometryGate>();
        CountedGeometry shape(gate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        auto first = request(manager);
        ASSERT_TRUE(gate->waitForCalls(1));
        manager.setWorldspace(ESM::RefId::stringRefId("other-world"), nullptr);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, shape);
        const auto current = manager.getMesh(mWorldspace, mTile);
        gate->release();
        const auto old = first.get();
        ASSERT_NE(old, nullptr);
        ASSERT_NE(current, nullptr);
        EXPECT_NE(old->getVersion().mGeneration, current->getVersion().mGeneration);
        EXPECT_EQ(manager.getMesh(mWorldspace, mTile), current);
    }

    TEST_F(NavMeshGeometryPreparation, DifferentTilesCanTraverseConcurrently)
    {
        auto firstGate = std::make_shared<GeometryGate>();
        auto secondGate = std::make_shared<GeometryGate>();
        CountedGeometry firstShape(firstGate);
        CountedGeometry secondShape(secondGate);
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        add(manager, firstShape);
        const float offset = 10 * getTileSize(mSettings) / mSettings.mRecastScaleFactor;
        ASSERT_TRUE(manager.addObject(ObjectId(&secondShape),
            CollisionShape(mInstance, secondShape, mObjectTransform),
            btTransform(btQuaternion::getIdentity(), btVector3(offset, offset, 0)), AreaType_ground, nullptr));
        manager.takeChangedTiles(nullptr);
        auto first = request(manager);
        ASSERT_TRUE(firstGate->waitForCalls(1));
        auto second = std::async(std::launch::async,
            [&] { return manager.getMesh(mWorldspace, TilePosition(10, 10)); });
        EXPECT_TRUE(secondGate->waitForCalls(1));
        EXPECT_EQ(manager.getStats().mPendingMeshTiles, 2);
        firstGate->release();
        secondGate->release();
        EXPECT_NE(first.get(), nullptr);
        EXPECT_NE(second.get(), nullptr);
        EXPECT_EQ(manager.getStats().mJoinedMeshRequests, 0);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, get_mesh_for_empty_should_return_nullptr)
    {
        TileCachedRecastMeshManager manager(mSettings);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(0, 0)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, get_revision_for_empty_should_return_zero)
    {
        const TileCachedRecastMeshManager manager(mSettings);
        EXPECT_EQ(manager.getRevision(), 0);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_object_for_new_object_should_return_true)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        EXPECT_TRUE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_object_for_existing_object_should_return_false)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_FALSE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_object_should_add_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        ASSERT_TRUE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        for (int x = -1; x < 1; ++x)
            for (int y = -1; y < 1; ++y)
                ASSERT_NE(manager.getMesh(mWorldspace, TilePosition(x, y)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_object_should_return_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        const TilesPositionsRange range{
            .mBegin = TilePosition(0, 0),
            .mEnd = TilePosition(1, 1),
        };
        manager.setRange(range, nullptr);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::add)));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, update_object_for_changed_object_should_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const btTransform transform(
            btMatrix3x3::getIdentity(), btVector3(getTileSize(mSettings) / mSettings.mRecastScaleFactor, 0, 0));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        const TilesPositionsRange range{
            .mBegin = TilePosition(-1, -1),
            .mEnd = TilePosition(2, 2),
        };
        manager.setRange(range, nullptr);
        manager.addObject(ObjectId(&boxShape), shape, transform, AreaType::AreaType_ground, nullptr);
        manager.takeChangedTiles(nullptr);
        EXPECT_TRUE(
            manager.updateObject(ObjectId(&boxShape), btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        EXPECT_THAT(manager.takeChangedTiles(nullptr),
            ElementsAre(std::pair(TilePosition(-1, -1), ChangeType::add),
                std::pair(TilePosition(-1, 0), ChangeType::add), std::pair(TilePosition(0, -1), ChangeType::update),
                std::pair(TilePosition(0, 0), ChangeType::update), std::pair(TilePosition(1, -1), ChangeType::remove),
                std::pair(TilePosition(1, 0), ChangeType::remove)));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, object_removals_report_types)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape1(btVector3(20, 20, 100));
        const btBoxShape boxShape2(btVector3(20, 20, 100));
        const CollisionShape shape1(mInstance, boxShape1, mObjectTransform);
        const CollisionShape shape2(mInstance, boxShape2, mObjectTransform);
        const TilesPositionsRange range{
            .mBegin = TilePosition(0, 0),
            .mEnd = TilePosition(1, 1),
        };
        manager.setRange(range, nullptr);
        manager.addWater(osg::Vec2i(0, 0), std::numeric_limits<int>::max(), 0.0f, nullptr);
        manager.addObject(ObjectId(&boxShape1), shape1, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        manager.addObject(ObjectId(&boxShape2), shape2, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::add)));
        manager.removeObject(ObjectId(&boxShape1), nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::update)));
        manager.removeObject(ObjectId(&boxShape2), nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::remove)));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, pending_remove_reports_add)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape1(btVector3(20, 20, 100));
        const btBoxShape boxShape2(btVector3(20, 20, 100));
        const CollisionShape shape1(mInstance, boxShape1, mObjectTransform);
        const CollisionShape shape2(mInstance, boxShape2, mObjectTransform);
        const TilesPositionsRange range{
            .mBegin = TilePosition(0, 0),
            .mEnd = TilePosition(1, 1),
        };
        manager.setRange(range, nullptr);
        manager.addObject(ObjectId(&boxShape1), shape1, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        manager.addObject(ObjectId(&boxShape2), shape2, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        manager.removeObject(ObjectId(&boxShape1), nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::add)));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        update_object_for_not_changed_object_should_not_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        manager.takeChangedTiles(nullptr);
        EXPECT_FALSE(
            manager.updateObject(ObjectId(&boxShape), btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        EXPECT_THAT(manager.takeChangedTiles(nullptr), IsEmpty());
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, remove_object_should_return_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        const TilesPositionsRange range{
            .mBegin = TilePosition(0, 0),
            .mEnd = TilePosition(1, 1),
        };
        manager.setRange(range, nullptr);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        manager.takeChangedTiles(nullptr);
        manager.removeObject(ObjectId(&boxShape), nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::remove)));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_mesh_after_add_object_should_return_recast_mesh_for_each_used_tile)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, 0)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, 0)), nullptr);
    }

    TEST_F(
        DetourNavigatorTileCachedRecastMeshManagerTest, get_mesh_after_add_object_should_return_nullptr_for_unused_tile)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(1, 0)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_mesh_for_moved_object_should_return_recast_mesh_for_each_used_tile)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const TilesPositionsRange range{
            .mBegin = TilePosition(-1, -1),
            .mEnd = TilePosition(2, 2),
        };
        manager.setRange(range, nullptr);
        manager.setWorldspace(mWorldspace, nullptr);

        const btBoxShape boxShape(btVector3(20, 20, 100));
        const btTransform transform(
            btMatrix3x3::getIdentity(), btVector3(getTileSize(mSettings) / mSettings.mRecastScaleFactor, 0, 0));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);

        manager.addObject(ObjectId(&boxShape), shape, transform, AreaType::AreaType_ground, nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, 0)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(1, 0)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(1, -1)), nullptr);

        manager.updateObject(ObjectId(&boxShape), btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, 0)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, 0)), nullptr);
    }

    TEST_F(
        DetourNavigatorTileCachedRecastMeshManagerTest, get_mesh_for_moved_object_should_return_nullptr_for_unused_tile)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);

        const btBoxShape boxShape(btVector3(20, 20, 100));
        const btTransform transform(
            btMatrix3x3::getIdentity(), btVector3(getTileSize(mSettings) / mSettings.mRecastScaleFactor, 0, 0));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);

        manager.addObject(ObjectId(&boxShape), shape, transform, AreaType::AreaType_ground, nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(-1, -1)), nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(-1, 0)), nullptr);

        manager.updateObject(ObjectId(&boxShape), btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(1, 0)), nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(1, -1)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_mesh_for_removed_object_should_return_nullptr_for_all_previously_used_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        manager.removeObject(ObjectId(&boxShape), nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(-1, -1)), nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(-1, 0)), nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(0, -1)), nullptr);
        EXPECT_EQ(manager.getMesh(mWorldspace, TilePosition(0, 0)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_mesh_for_not_changed_object_after_update_should_return_recast_mesh_for_same_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);

        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, 0)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, 0)), nullptr);

        manager.updateObject(ObjectId(&boxShape), btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(-1, 0)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, -1)), nullptr);
        EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(0, 0)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_revision_after_add_object_new_should_return_incremented_value)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const auto initialRevision = manager.getRevision();
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_EQ(manager.getRevision(), initialRevision + 1);
    }

    TEST_F(
        DetourNavigatorTileCachedRecastMeshManagerTest, get_revision_after_add_object_existing_should_return_same_value)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        const auto beforeAddRevision = manager.getRevision();
        EXPECT_FALSE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        EXPECT_EQ(manager.getRevision(), beforeAddRevision);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_revision_after_update_moved_object_should_return_incremented_value)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const btTransform transform(
            btMatrix3x3::getIdentity(), btVector3(getTileSize(mSettings) / mSettings.mRecastScaleFactor, 0, 0));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, transform, AreaType::AreaType_ground, nullptr);
        const auto beforeUpdateRevision = manager.getRevision();
        manager.updateObject(ObjectId(&boxShape), btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_EQ(manager.getRevision(), beforeUpdateRevision + 1);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_revision_after_update_not_changed_object_should_return_same_value)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        const auto beforeUpdateRevision = manager.getRevision();
        manager.updateObject(ObjectId(&boxShape), btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        EXPECT_EQ(manager.getRevision(), beforeUpdateRevision);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_revision_after_remove_existing_object_should_return_incremented_value)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        const auto beforeRemoveRevision = manager.getRevision();
        manager.removeObject(ObjectId(&boxShape), nullptr);
        EXPECT_EQ(manager.getRevision(), beforeRemoveRevision + 1);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest,
        get_revision_after_remove_absent_object_should_return_same_value)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const auto beforeRemoveRevision = manager.getRevision();
        manager.removeObject(ObjectId(&manager), nullptr);
        EXPECT_EQ(manager.getRevision(), beforeRemoveRevision);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_water_for_new_water_should_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = 8192;
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        const auto changedTiles = manager.takeChangedTiles(nullptr);
        EXPECT_EQ(changedTiles.begin()->first, TilePosition(-1, -1));
        EXPECT_EQ(changedTiles.rbegin()->first, TilePosition(11, 11));
        for (const auto& [k, v] : changedTiles)
            EXPECT_EQ(v, ChangeType::add) << k;
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_water_for_not_max_int_should_add_new_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = 8192;
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        for (int x = -1; x < 12; ++x)
            for (int y = -1; y < 12; ++y)
                ASSERT_NE(manager.getMesh(mWorldspace, TilePosition(x, y)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_water_for_max_int_should_not_add_new_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        ASSERT_TRUE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = std::numeric_limits<int>::max();
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        for (int x = -6; x < 6; ++x)
            for (int y = -6; y < 6; ++y)
                ASSERT_EQ(manager.getMesh(mWorldspace, TilePosition(x, y)) != nullptr,
                    -1 <= x && x <= 0 && -1 <= y && y <= 0);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, remove_water_for_absent_cell_should_not_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.removeWater(osg::Vec2i(0, 0), nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre());
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, remove_water_for_existing_cell_should_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = 8192;
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        manager.takeChangedTiles(nullptr);
        manager.removeWater(cellPosition, nullptr);
        const auto changedTiles = manager.takeChangedTiles(nullptr);
        EXPECT_EQ(changedTiles.begin()->first, TilePosition(-1, -1));
        EXPECT_EQ(changedTiles.rbegin()->first, TilePosition(11, 11));
        for (const auto& [k, v] : changedTiles)
            EXPECT_EQ(v, ChangeType::remove) << k;
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, water_removal_reports_types)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = 8192;
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        manager.takeChangedTiles(nullptr);
        manager.removeWater(cellPosition, nullptr);
        const auto changedTiles = manager.takeChangedTiles(nullptr);
        EXPECT_EQ(changedTiles.size(), 169);
        for (const auto& [k, v] : changedTiles)
        {
            const bool hasObject = -1 <= k.x() && k.x() <= 0 && -1 <= k.y() && k.y() <= 0;
            EXPECT_EQ(v, hasObject ? ChangeType::update : ChangeType::remove) << k;
        }
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, remove_water_for_existing_cell_should_remove_empty_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = 8192;
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        manager.removeWater(cellPosition, nullptr);
        for (int x = -6; x < 6; ++x)
            for (int y = -6; y < 6; ++y)
                ASSERT_EQ(manager.getMesh(mWorldspace, TilePosition(x, y)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, remove_water_for_existing_cell_should_leave_not_empty_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        ASSERT_TRUE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = 8192;
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        manager.removeWater(cellPosition, nullptr);
        for (int x = -6; x < 6; ++x)
            for (int y = -6; y < 6; ++y)
                ASSERT_EQ(manager.getMesh(mWorldspace, TilePosition(x, y)) != nullptr,
                    -1 <= x && x <= 0 && -1 <= y && y <= 0);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, remove_object_should_not_remove_tile_with_water)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const osg::Vec2i cellPosition(0, 0);
        const int cellSize = 8192;
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        ASSERT_TRUE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        manager.addWater(cellPosition, cellSize, 0.0f, nullptr);
        manager.removeObject(ObjectId(&boxShape), nullptr);
        for (int x = -1; x < 12; ++x)
            for (int y = -1; y < 12; ++y)
                ASSERT_NE(manager.getMesh(mWorldspace, TilePosition(x, y)), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, set_new_worldspace_should_remove_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const TilesPositionsRange range{
            .mBegin = TilePosition(0, 0),
            .mEnd = TilePosition(1, 1),
        };
        manager.setRange(range, nullptr);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(nullptr, boxShape, mObjectTransform);
        ASSERT_TRUE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::add)));
        const ESM::RefId otherWorldspace(ESM::FormId::fromUint32(0x1));
        manager.setWorldspace(otherWorldspace, nullptr);
        ASSERT_EQ(manager.getMesh(otherWorldspace, TilePosition(0, 0)), nullptr);
        ASSERT_TRUE(manager.addObject(
            ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr));
        EXPECT_THAT(manager.takeChangedTiles(nullptr), ElementsAre(std::pair(TilePosition(0, 0), ChangeType::add)));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, set_range_should_add_changed_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        const btBoxShape boxShape(btVector3(20, 20, 100));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        const TilesPositionsRange range1{
            .mBegin = TilePosition(0, 0),
            .mEnd = TilePosition(1, 1),
        };
        manager.setRange(range1, nullptr);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);
        const TilesPositionsRange range2{
            .mBegin = TilePosition(-1, -1),
            .mEnd = TilePosition(0, 0),
        };
        manager.takeChangedTiles(nullptr);
        manager.setRange(range2, nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr),
            ElementsAre(
                std::pair(TilePosition(-1, -1), ChangeType::add), std::pair(TilePosition(0, 0), ChangeType::remove)));
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, set_range_should_remove_cached_recast_meshes_outside_range)
    {
        TileCachedRecastMeshManager manager(mSettings);

        manager.setWorldspace(mWorldspace, nullptr);

        const btBoxShape boxShape(btVector3(100, 100, 20));
        const CollisionShape shape(mInstance, boxShape, mObjectTransform);
        const TilesPositionsRange range1{
            .mBegin = TilePosition(0, 0),
            .mEnd = TilePosition(1, 1),
        };
        manager.setRange(range1, nullptr);
        manager.addObject(ObjectId(&boxShape), shape, btTransform::getIdentity(), AreaType::AreaType_ground, nullptr);

        const TilePosition tilePosition(0, 0);

        ASSERT_EQ(manager.getCachedMesh(mWorldspace, tilePosition), nullptr);
        ASSERT_NE(manager.getMesh(mWorldspace, tilePosition), nullptr);
        ASSERT_NE(manager.getCachedMesh(mWorldspace, tilePosition), nullptr);

        const TilesPositionsRange range2{
            .mBegin = TilePosition(-1, -1),
            .mEnd = TilePosition(0, 0),
        };
        manager.takeChangedTiles(nullptr);
        manager.setRange(range2, nullptr);

        ASSERT_EQ(manager.getCachedMesh(mWorldspace, tilePosition), nullptr);
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_heightfield_plane_should_add_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const osg::Vec2i cellPosition(1, 2);
        const int cellSize = 32;
        const HeightfieldPlane plane{
            .mHeight = 1000,
        };
        manager.addHeightfield(cellPosition, cellSize, plane, nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr),
            ElementsAre( //
                std::pair(TilePosition(-1, -1), ChangeType::add), //
                std::pair(TilePosition(-1, 0), ChangeType::add), //
                std::pair(TilePosition(0, -1), ChangeType::add), //
                std::pair(TilePosition(0, 0), ChangeType::add) //
                ));
        for (int x = -1; x < 1; ++x)
            for (int y = -1; y < 1; ++y)
                EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(x, y)), nullptr) << x << " " << y;
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_heightfield_surface_should_add_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const osg::Vec2i cellPosition(1, 2);
        const int cellSize = 32;
        const std::array<float, 25> heights{ {
            1000, 1100, 1200, 1300, 1400, // 0
            1100, 1200, 1300, 1400, 1600, // 1
            1200, 1300, 1400, 1500, 1500, // 2
            1400, 1500, 1600, 1700, 1800, // 3
            1600, 1700, 1800, 1900, 2000, // 4
        } };
        const HeightfieldSurface surface{
            .mHeights = heights.data(),
            .mSize = 5,
            .mMinHeight = 1000,
            .mMaxHeight = 2000,
        };
        manager.addHeightfield(cellPosition, cellSize, surface, nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr),
            ElementsAre( //
                std::pair(TilePosition(-1, -1), ChangeType::add), //
                std::pair(TilePosition(-1, 0), ChangeType::add), //
                std::pair(TilePosition(0, -1), ChangeType::add), //
                std::pair(TilePosition(0, 0), ChangeType::add) //
                ));
        for (int x = -1; x < 1; ++x)
            for (int y = -1; y < 1; ++y)
                EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(x, y)), nullptr) << x << " " << y;
    }

    TEST_F(DetourNavigatorTileCachedRecastMeshManagerTest, add_water_should_add_tiles)
    {
        TileCachedRecastMeshManager manager(mSettings);
        manager.setWorldspace(mWorldspace, nullptr);
        const osg::Vec2i cellPosition(1, 2);
        const int cellSize = 32;
        const float level = 1000;
        manager.addWater(cellPosition, cellSize, level, nullptr);
        EXPECT_THAT(manager.takeChangedTiles(nullptr),
            ElementsAre( //
                std::pair(TilePosition(-1, -1), ChangeType::add), //
                std::pair(TilePosition(-1, 0), ChangeType::add), //
                std::pair(TilePosition(0, -1), ChangeType::add), //
                std::pair(TilePosition(0, 0), ChangeType::add) //
                ));
        for (int x = -1; x < 1; ++x)
            for (int y = -1; y < 1; ++y)
                EXPECT_NE(manager.getMesh(mWorldspace, TilePosition(x, y)), nullptr) << x << " " << y;
    }
}
