#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/objectcache.hpp>
#include <components/misc/combinedperformance.hpp>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <gtest/gtest.h>

namespace
{
    constexpr VFS::Path::NormalizedView path("meshes/prepared.nif");
    struct PreparedShapeManager : Resource::BulletShapeManager
    {
        PreparedShapeManager() : BulletShapeManager(nullptr, nullptr, nullptr, 1.0) {}
        void seed(osg::ref_ptr<Resource::BulletShape> shape)
        {
            mCache->addEntryToObjectCache(path.value(), shape);
        }
    };

    osg::ref_ptr<Resource::BulletShape> shape()
    {
        osg::ref_ptr<Resource::BulletShape> result = new Resource::BulletShape;
        result->mCollisionShape.reset(new btBoxShape(btVector3(2, 3, 4)));
        result->mAvoidCollisionShape.reset(new btBoxShape(btVector3(3, 4, 5)));
        return result;
    }

    TEST(CollisionPreparationTest, ActualManagerPublishesScaledInstanceBeforeActivation)
    {
        PreparedShapeManager manager;
        auto base = shape();
        manager.seed(base);
        auto cached = manager.cacheInstance(path, 1.5f);
        auto instance = manager.getInstance(path, 1.5f);
        ASSERT_EQ(instance, cached);
        const float scale = Misc::usePreparedCollisionScale() ? 1.5f : 1.f;
        EXPECT_EQ(instance->mCollisionShape->getLocalScaling(), btVector3(scale, scale, scale));
        EXPECT_EQ(instance->mAvoidCollisionShape->getLocalScaling(), btVector3(scale, scale, scale));
        EXPECT_EQ(base->mCollisionShape->getLocalScaling(), btVector3(1, 1, 1));
        EXPECT_NE(manager.getInstance(path, 1.5f), instance);
    }

    TEST(CollisionPreparationTest, ChangedRuntimeScaleUsesSafeFallback)
    {
        PreparedShapeManager manager;
        manager.seed(shape());
        auto previous = manager.cacheInstance(path, 0.75f);
        auto current = manager.getInstance(path, 2.f);
        if (Misc::usePreparedCollisionScale())
            EXPECT_NE(previous, current);
        current->setLocalScaling(btVector3(2, 2, 2));
        EXPECT_EQ(current->mCollisionShape->getLocalScaling(), btVector3(2, 2, 2));
        EXPECT_EQ(current->mAvoidCollisionShape->getLocalScaling(), btVector3(2, 2, 2));
        if (Misc::usePreparedCollisionScale())
            EXPECT_EQ(previous->mCollisionShape->getLocalScaling(), btVector3(0.75, 0.75, 0.75));
    }

    TEST(CollisionPreparationTest, OrdinaryUnscaledPreloadCanStillSupplyScaledActivation)
    {
        PreparedShapeManager manager;
        manager.seed(shape());
        auto ordinary = manager.cacheInstance(path);
        auto current = manager.getInstance(path, 1.75f);
        EXPECT_EQ(ordinary, current);
        current->setLocalScaling(btVector3(1.75, 1.75, 1.75));
        EXPECT_EQ(current->mCollisionShape->getLocalScaling(), btVector3(1.75, 1.75, 1.75));
    }

    TEST(CollisionPreparationTest, ResourceWithoutCollisionShapeCanBePreloaded)
    {
        PreparedShapeManager manager;
        manager.seed(new Resource::BulletShape);
        const auto cached = manager.cacheInstance(path, 2.f);
        ASSERT_TRUE(cached);
        EXPECT_FALSE(cached->mCollisionShape);
        EXPECT_EQ(manager.getInstance(path, 2.f), cached);
    }
}
