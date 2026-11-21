#include <components/resource/sceneinstancecache.hpp>
#include <components/sceneutil/clone.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/templateref.hpp>
#include <components/resource/boundedcache.hpp>

#include <osg/Group>
#include <osg/ValueObject>
#include <osgParticle/ParticleSystem>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace
{
    TEST(ActorRetainedMemory, IncludesHiddenRigMorphAndRetainedSourceArrays)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setVertexArray(new osg::Vec3Array(65536));
        osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
        rig->setSourceGeometry(geometry);
        osg::ref_ptr<SceneUtil::MorphGeometry> morph = new SceneUtil::MorphGeometry;
        morph->setSourceGeometry(geometry);
        osg::ref_ptr<osg::Group> source = new osg::Group;
        source->addChild(rig); source->addChild(morph);
        osg::ref_ptr<osg::Group> cleaned = new osg::Group;
        SceneUtil::addTemplateRef(*cleaned, source);
        const auto bytes = SceneUtil::estimateRetainedSceneBytes(*cleaned);
        ASSERT_TRUE(bytes);
        EXPECT_GT(*bytes, 65536u * (64u + 6u * 12u));
        Resource::BoundedCache<int, osg::ref_ptr<const osg::Node>> cache(10, 1024 * 1024, 30);
        EXPECT_FALSE(cache.put(1, cleaned, *bytes, cache.generation(), 0));
        source->addChild(new osgParticle::ParticleSystem);
        EXPECT_FALSE(SceneUtil::estimateRetainedSceneBytes(*cleaned));
    }

    TEST(ActorRetainedMemory, ReusedRecipeStillProducesIndependentRigAndMorphInstances)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setVertexArray(new osg::Vec3Array(4));
        osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
        rig->setSourceGeometry(geometry);
        osg::ref_ptr<SceneUtil::MorphGeometry> morph = new SceneUtil::MorphGeometry;
        morph->setSourceGeometry(geometry);
        osg::ref_ptr<osg::Group> source = new osg::Group;
        source->addChild(rig); source->addChild(morph);
        SceneUtil::CopyOp aCopy, bCopy;
        osg::ref_ptr<osg::Group> a = static_cast<osg::Group*>(source->clone(aCopy));
        osg::ref_ptr<osg::Group> b = static_cast<osg::Group*>(source->clone(bCopy));
        EXPECT_NE(a->getChild(0), b->getChild(0));
        EXPECT_NE(a->getChild(1), b->getChild(1));
        EXPECT_NE(a->getChild(0), rig.get());
        a->getChild(1)->setNodeMask(0);
        EXPECT_NE(b->getChild(1)->getNodeMask(), 0);
        EXPECT_NE(source->getChild(1)->getNodeMask(), 0);
    }

    TEST(InstancePreparation, BudgetIncludesRigPrivateAttributesAndHiddenGeometry)
    {
        osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
        source->setVertexArray(new osg::Vec3Array(65536));
        source->setNormalArray(new osg::Vec3Array(65536));
        const auto ordinaryBytes = SceneUtil::estimateInstanceBytes(*source);
        ASSERT_TRUE(ordinaryBytes);
        EXPECT_GE(*ordinaryBytes, 3u * 65536u * 24u);
        osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
        rig->setSourceGeometry(source);
        const auto bytes = SceneUtil::estimateInstanceBytes(*rig);
        ASSERT_TRUE(bytes);
        EXPECT_GE(*bytes, 65536u * (64u + 3u * 24u));
        osg::ref_ptr<osg::Group> many = new osg::Group;
        for (int i = 0; i < 4; ++i)
            many->addChild(rig);
        EXPECT_GT(*SceneUtil::estimateInstanceBytes(*many), 16u * 1024u * 1024u);
        osg::ref_ptr<osgParticle::ParticleSystem> particles = new osgParticle::ParticleSystem;
        EXPECT_FALSE(SceneUtil::estimateInstanceBytes(*particles));
    }

    TEST(InstancePreparation, MorphClonesPreserveSourceMetadataAcrossWorkers)
    {
        osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
        source->setVertexArray(new osg::Vec3Array(64));
        source->setUserValue("fixture", 42);
        const auto originalObjects = source->getUserDataContainer()->getNumUserObjects();
        osg::ref_ptr<SceneUtil::MorphGeometry> original = new SceneUtil::MorphGeometry;
        original->setSourceGeometry(source);
        EXPECT_EQ(source->getUserDataContainer()->getNumUserObjects(), originalObjects);
        const auto bytes = SceneUtil::estimateInstanceBytes(*original);
        ASSERT_TRUE(bytes);
        EXPECT_GE(*bytes, 3u * 64u * 12u);
        std::vector<std::thread> workers;
        std::atomic_bool valid{ true };
        for (int i = 0; i < 8; ++i)
            workers.emplace_back([&] {
                for (int j = 0; j < 25; ++j)
                {
                    SceneUtil::CopyOp copy;
                    osg::ref_ptr<osg::Node> clone = static_cast<osg::Node*>(original->clone(copy));
                    int value = 0;
                    if (!source->getUserValue("fixture", value) || value != 42)
                        valid = false;
                }
            });
        for (auto& worker : workers)
            worker.join();
        EXPECT_TRUE(valid);
        EXPECT_EQ(source->getUserDataContainer()->getNumUserObjects(), originalObjects);
    }

    TEST(InstancePreparation, RetiredGraphsCanReenterCacheOutsideItsMutex)
    {
        Resource::SceneInstanceCache cache;
        struct Reentrant : osg::Group
        {
            Resource::SceneInstanceCache& cache;
            bool& destroyed;
            Reentrant(Resource::SceneInstanceCache& c, bool& d)
                : cache(c)
                , destroyed(d)
            {
            }
            ~Reentrant() override
            {
                cache.getStats();
                destroyed = true;
            }
        };
        std::atomic_bool abort{ false };
        bool baseDestroyed = false, cloneDestroyed = false;
        osg::ref_ptr<osg::Node> base = new Reentrant(cache, baseDestroyed);
        auto ticket = cache.prepare(base, { 1, 1 }, abort, 1, [&] { return new Reentrant(cache, cloneDestroyed); });
        base = nullptr;
        ticket.reset();
        cache.prune();
        EXPECT_TRUE(baseDestroyed);
        EXPECT_TRUE(cloneDestroyed);
    }
}
