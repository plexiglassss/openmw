#include <components/resource/collisioninstancecache.hpp>
#include <components/misc/combinedperformance.hpp>

#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
    constexpr VFS::Path::NormalizedView mesh("meshes/compound.nif");

    std::shared_ptr<Resource::BulletShape> source()
    {
        std::shared_ptr<Resource::BulletShape> shape = std::make_shared<Resource::BulletShape>();
        auto compound = std::make_unique<btCompoundShape>();
        btTransform transform = btTransform::getIdentity();
        transform.setOrigin(btVector3(5, 2, 1));
        compound->addChildShape(transform, new btBoxShape(btVector3(2, 3, 4)));
        shape->mCollisionShape.reset(compound.release());
        shape->mAvoidCollisionShape.reset(new btBoxShape(btVector3(7, 8, 9)));
        return shape;
    }

    std::shared_ptr<Resource::BulletShapeInstance> prepared(float scale)
    {
        auto instance = Resource::makeInstance(source());
        instance->setLocalScaling(btVector3(scale, scale, scale));
        return instance;
    }

    TEST(CollisionInstanceCacheTest, ExactScaleAndModelSelectExclusiveInstances)
    {
        Resource::CollisionInstanceCache cache;
        auto first = prepared(0.75f), second = prepared(1.5f);
        cache.add(mesh, 0.75f, first);
        cache.add(mesh, 1.5f, second);
        EXPECT_FALSE(cache.take(VFS::Path::NormalizedView("meshes/other.nif"), 0.75f));
        EXPECT_FALSE(cache.take(mesh, std::nextafter(0.75f, 1.f)));
        EXPECT_EQ(cache.take(mesh, 1.5f), second);
        EXPECT_EQ(cache.take(mesh, 0.75f), first);
        EXPECT_FALSE(cache.take(mesh, 0.75f));
        EXPECT_EQ(cache.getStats().mHit, 2u);
        EXPECT_EQ(cache.getStats().mSize, 0u);
    }

    TEST(CollisionInstanceCacheTest, DuplicateOccurrencesDoNotShareMutableShapes)
    {
        Resource::CollisionInstanceCache cache;
        auto first = prepared(2.f), second = prepared(2.f);
        cache.add(mesh, 2.f, first);
        cache.add(mesh, 2.f, second);
        auto a = cache.take(mesh, 2.f), b = cache.take(mesh, 2.f);
        ASSERT_TRUE(a && b);
        EXPECT_NE(a, b);
        a->setLocalScaling(btVector3(3, 3, 3));
        EXPECT_EQ(b->mCollisionShape->getLocalScaling(), btVector3(2, 2, 2));
        EXPECT_EQ(b->mAvoidCollisionShape->getLocalScaling(), btVector3(2, 2, 2));
    }

    TEST(CollisionInstanceCacheTest, NonfiniteKeysNeverEnterOrderedCache)
    {
        Resource::CollisionInstanceCache cache;
        auto instance = prepared(1.f);
        for (float value : { std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::quiet_NaN() })
        {
            cache.add(mesh, value, instance);
            EXPECT_FALSE(cache.take(mesh, value));
        }
        EXPECT_EQ(cache.getStats().mSize, 0u);
        cache.add(mesh, 1.f, nullptr);
        EXPECT_EQ(cache.getStats().mSize, 0u);
    }

    TEST(CollisionInstanceCacheTest, RetentionFollowsPreloaderOwnershipAndClearInvalidates)
    {
        Resource::CollisionInstanceCache cache;
        auto kept = prepared(1.f);
        cache.add(mesh, 1.f, kept);
        cache.add(mesh, 2.f, prepared(2.f));
        cache.removeUnreferencedObjectsInCache();
        EXPECT_EQ(cache.getStats().mSize, 1u);
        EXPECT_EQ(cache.getStats().mExpired, 1u);
        cache.clear();
        EXPECT_FALSE(cache.take(mesh, 1.f));
        EXPECT_EQ(kept->mCollisionShape->getLocalScaling(), btVector3(1, 1, 1));
    }

    TEST(CollisionInstanceCacheTest, ParallelConsumersCanTakeEachInstanceOnlyOnce)
    {
        Resource::CollisionInstanceCache cache;
        for (int i = 0; i < 64; ++i)
            cache.add(mesh, 1.5f, prepared(1.5f));
        std::atomic_uint consumed{ 0 };
        const auto take = [&] {
            while (auto instance = cache.take(mesh, 1.5f))
            {
                EXPECT_EQ(instance->mCollisionShape->getLocalScaling(), btVector3(1.5f, 1.5f, 1.5f));
                ++consumed;
            }
        };
        std::thread a(take), b(take), c(take);
        a.join(); b.join(); c.join();
        EXPECT_EQ(consumed, 64u);
        EXPECT_EQ(cache.getStats().mSize, 0u);
    }

    TEST(CollisionInstanceCacheTest, PreparedCompoundAndAvoidanceBoundsMatchDemandScaling)
    {
        auto base = source();
        auto demand = Resource::makeInstance(base);
        auto early = Resource::makeInstance(base);
        const btVector3 scale(1.75, 1.75, 1.75);
        early->setLocalScaling(scale);
        Resource::CollisionInstanceCache cache;
        cache.add(mesh, 1.75f, early);
        early = cache.take(mesh, 1.75f);
        demand->setLocalScaling(scale);
        early->setLocalScaling(scale); // Same activation call as Object::Object.
        for (bool avoid : { false, true })
        {
            const auto* a = avoid ? demand->mAvoidCollisionShape.get() : demand->mCollisionShape.get();
            const auto* b = avoid ? early->mAvoidCollisionShape.get() : early->mCollisionShape.get();
            btVector3 aMin, aMax, bMin, bMax;
            a->getAabb(btTransform::getIdentity(), aMin, aMax);
            b->getAabb(btTransform::getIdentity(), bMin, bMax);
            EXPECT_EQ(aMin, bMin); EXPECT_EQ(aMax, bMax);
        }
        EXPECT_EQ(base->mCollisionShape->getLocalScaling(), btVector3(1, 1, 1));
    }

    struct CountingBox : btBoxShape
    {
        int mCalls = 0;
        CountingBox() : btBoxShape(btVector3(1, 1, 1)) {}
        void setLocalScaling(const btVector3& scale) override
        {
            ++mCalls;
            btBoxShape::setLocalScaling(scale);
        }
    };

    TEST(CollisionInstanceCacheTest, MatchingScaleSkipsWorkButRepairsAvoidanceScaleIndependently)
    {
        Resource::BulletShape shape;
        auto* collision = new CountingBox;
        auto* avoid = new CountingBox;
        shape.mCollisionShape.reset(collision);
        shape.mAvoidCollisionShape.reset(avoid);
        shape.setLocalScaling(btVector3(2, 2, 2));
        EXPECT_EQ(collision->mCalls, 1); EXPECT_EQ(avoid->mCalls, 1);
        shape.setLocalScaling(btVector3(2, 2, 2));
        const int expected = Misc::usePreparedCollisionScale() ? 1 : 2;
        EXPECT_EQ(collision->mCalls, expected); EXPECT_EQ(avoid->mCalls, expected);
        testing::Test::RecordProperty("matching_scale_calls", collision->mCalls);
        avoid->setLocalScaling(btVector3(3, 3, 3));
        shape.setLocalScaling(btVector3(2, 2, 2));
        EXPECT_EQ(avoid->getLocalScaling(), btVector3(2, 2, 2));
        EXPECT_EQ(collision->mCalls, Misc::usePreparedCollisionScale() ? 1 : 3);
    }

    TEST(CollisionInstanceCacheTest, MatchingCompoundActivationBenchmark)
    {
        std::shared_ptr<Resource::BulletShape> base = std::make_shared<Resource::BulletShape>();
        auto* compound = new btCompoundShape;
        for (int i = 0; i < 128; ++i)
        {
            auto transform = btTransform::getIdentity();
            transform.setOrigin(btVector3(i, i % 7, i % 11));
            compound->addChildShape(transform, new btBoxShape(btVector3(1, 2, 3)));
        }
        base->mCollisionShape.reset(compound);
        auto instance = Resource::makeInstance(base);
        instance->setLocalScaling(btVector3(1.5f, 1.5f, 1.5f));
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i)
            instance->setLocalScaling(btVector3(1.5f, 1.5f, 1.5f));
        std::cout << "COLLISION_SCALE_BENCH prepared=" << Misc::usePreparedCollisionScale()
                  << " children=128 repeats=1000 ms="
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count() << '\n';
        EXPECT_EQ(instance->mCollisionShape->getLocalScaling(), btVector3(1.5f, 1.5f, 1.5f));
    }
}
