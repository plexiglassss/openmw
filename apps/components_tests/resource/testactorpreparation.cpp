#include <components/misc/combinedperformance.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/vfs/manager.hpp>

#include <gtest/gtest.h>
#include <osg/Group>
#include <osg/Stats>
#include <osgParticle/ParticleSystem>

#include <future>
#include <cstdlib>
#include <string_view>

namespace
{
    struct SeededSceneManager : Resource::SceneManager
    {
        explicit SeededSceneManager(const VFS::Manager* vfs) : SceneManager(vfs, nullptr, nullptr, nullptr, 1.0) {}
        void seed(std::string_view path, osg::Node* node) { mCache->addEntryToObjectCache(std::string(path), node); }
    };

    struct ActorPreparation : testing::Test
    {
        VFS::Manager vfs;
        SeededSceneManager scene{ &vfs };

        double stat(std::string_view name)
        {
            osg::ref_ptr<osg::Stats> stats = new osg::Stats("actor preparation fixture");
            scene.reportStats(1, stats);
            double result = -1;
            EXPECT_TRUE(stats->getAttribute(1, std::string(name), result));
            return result;
        }
    };

    TEST_F(ActorPreparation, ProcessSwitchesRespectRawEnvironmentAndMaster)
    {
        const auto enabled = [](const char* name) {
            const char* value = std::getenv(name);
            return !value || std::string_view(value) != "0";
        };
        const bool master = enabled("OPENMW_COMBINED_PERFORMANCE");
        EXPECT_EQ(Misc::useActorPreparation(), master && enabled("OPENMW_ACTOR_PREPARATION"));
        EXPECT_EQ(Misc::useRecentModelReuse(), master && enabled("OPENMW_RECENT_MODEL_REUSE"));
        RecordProperty("actor_expected", master && enabled("OPENMW_ACTOR_PREPARATION") ? 1 : 0);
        RecordProperty("reuse_expected", master && enabled("OPENMW_RECENT_MODEL_REUSE") ? 1 : 0);
    }

    TEST_F(ActorPreparation, IdentityIncludesModelSkeletonInjectionOwnerSettingsAndVfs)
    {
        unsigned calls = 0;
        const auto create = [&]() -> osg::ref_ptr<osg::Node> { ++calls; return new osg::Group; };
        const auto first = scene.getActorTemplate("model", "skeleton", false, create);
        const auto again = scene.getActorTemplate("model", "skeleton", false, create);
        EXPECT_EQ(first == again, Misc::useActorPreparation());
        EXPECT_NE(first, scene.getActorTemplate("other", "skeleton", false, create));
        EXPECT_NE(first, scene.getActorTemplate("model", "other", false, create));
        EXPECT_NE(first, scene.getActorTemplate("model", "skeleton", true, create));
        Resource::SceneManager other(&vfs, nullptr, nullptr, nullptr, 0.0);
        EXPECT_NE(first, other.getActorTemplate("model", "skeleton", false, create));
        scene.setFilterSettings("linear", "linear", "linear", 1);
        const auto changed = scene.getActorTemplate("model", "skeleton", false, create);
        EXPECT_NE(first, changed);
        vfs.buildIndex();
        const auto rebuilt = scene.getActorTemplate("model", "skeleton", false, create);
        EXPECT_NE(changed, rebuilt);
        scene.clearCache();
        EXPECT_NE(rebuilt, scene.getActorTemplate("model", "skeleton", false, create));
        EXPECT_EQ(calls, Misc::useActorPreparation() ? 8u : 9u);
    }

    TEST_F(ActorPreparation, FactoryCanUsePublicSceneMethodsAndEveryActorIsIndependent)
    {
        osg::ref_ptr<osg::Group> source = new osg::Group;
        source->addChild(new osg::Group);
        const auto create = [&] { return scene.getInstance(source); };
        const auto prepared = scene.getActorTemplate("model", "skeleton", true, create);
        const auto a = scene.getInstance(prepared);
        const auto b = scene.getInstance(prepared);
        EXPECT_NE(a, b);
        ASSERT_TRUE(a->asGroup());
        ASSERT_TRUE(b->asGroup());
        EXPECT_NE(a->asGroup()->getChild(0), b->asGroup()->getChild(0));
        a->asGroup()->getChild(0)->setName("actor-specific state");
        EXPECT_TRUE(b->asGroup()->getChild(0)->getName().empty());
        EXPECT_EQ(stat("RecentModel Count"), 0); // Arbitrary/live nodes are never admitted by the pointer overload.
    }

    TEST_F(ActorPreparation, ClearDuringFactoryDiscardsOldGenerationBeforePublication)
    {
        std::promise<void> entered, release;
        auto ready = release.get_future().share();
        unsigned calls = 0;
        const auto create = [&]() -> osg::ref_ptr<osg::Node> {
            if (++calls == 1)
            {
                entered.set_value();
                ready.wait();
            }
            osg::ref_ptr<osg::Node> node = new osg::Group;
            node->setName(std::to_string(calls));
            return node;
        };
        auto worker = std::async(std::launch::async, [&] {
            return scene.getActorTemplate("model", "skeleton", false, create);
        });
        entered.get_future().wait();
        scene.clearCache();
        release.set_value();
        const auto result = worker.get();
        EXPECT_EQ(result->getName(), Misc::useActorPreparation() ? "2" : "1");
        const auto next = scene.getActorTemplate("model", "skeleton", false, create);
        EXPECT_EQ(next == result, Misc::useActorPreparation());
    }

    TEST_F(ActorPreparation, RejectedGraphsReuseWeakAdmissionEstimatesAcrossFilters)
    {
        osg::ref_ptr<osg::Group> source = new osg::Group;
        source->addChild(new osgParticle::ParticleSystem); // Unsupported mutable drawable: no retention.
        osg::observer_ptr<const osg::Node> weak = source.get();
        auto a = scene.getRigAttachmentPlan(*source, "head");
        auto b = scene.getRigAttachmentPlan(*source, "hand");
        EXPECT_EQ(stat("RigPlan Count"), 0);
        EXPECT_EQ(stat("RetentionEstimate Hit"), Misc::useActorPreparation() ? 1 : 0);
        EXPECT_EQ(stat("RetentionEstimate Count"), Misc::useActorPreparation() ? 1 : 0);
        a.reset();
        b.reset();
        source = nullptr;
        EXPECT_FALSE(weak.valid()); // Negative admission metadata must not pin the rejected graph.
    }

    TEST_F(ActorPreparation, RigRecipeReusesNormalizedFilterButInvalidatesSettingsAndSource)
    {
        osg::ref_ptr<osg::Group> first = new osg::Group, second = new osg::Group;
        const auto a = scene.getRigAttachmentPlan(*first, "Head");
        EXPECT_EQ(a == scene.getRigAttachmentPlan(*first, "HEAD"), Misc::useActorPreparation());
        EXPECT_NE(a, scene.getRigAttachmentPlan(*first, "hand"));
        EXPECT_NE(a, scene.getRigAttachmentPlan(*second, "Head"));
        scene.setFilterSettings("linear", "linear", "linear", 1);
        EXPECT_NE(a, scene.getRigAttachmentPlan(*first, "Head"));
        scene.clearCache();
        EXPECT_EQ(stat("ActorTemplate Count"), 0);
        EXPECT_EQ(stat("RigPlan Count"), 0);
        EXPECT_EQ(stat("RetentionEstimate Count"), 0);
    }

    TEST_F(ActorPreparation, OnlyConsumedTemplatesSurviveOrdinaryCacheExpiryAndClearReleasesThem)
    {
        constexpr VFS::Path::NormalizedView path("meshes/revisit-fixture.nif");
        osg::ref_ptr<osg::Node> source = new osg::Group;
        scene.seed(path.value(), source);
        EXPECT_EQ(scene.getTemplate(path), source);
        EXPECT_EQ(stat("RecentModel Count"), 0);
        source = nullptr;
        scene.updateCache(1.0);
        scene.updateCache(100.0);
        EXPECT_FALSE(scene.getCachedTemplate(path)); // Speculative reads do not populate the warm cache.

        source = new osg::Group;
        osg::observer_ptr<const osg::Node> weak = source.get();
        scene.seed(path.value(), source);
        auto actor = scene.getInstance(path); // The real path overload supplies the retention admission.
        source = nullptr;
        actor = nullptr;
        scene.updateCache(101.0);
        scene.updateCache(200.0);
        EXPECT_EQ(bool(scene.getCachedTemplate(path)), Misc::useRecentModelReuse());
        EXPECT_EQ(weak.valid(), Misc::useRecentModelReuse());
        EXPECT_EQ(stat("RecentModel Count"), Misc::useRecentModelReuse() ? 1 : 0);
        scene.clearCache();
        EXPECT_FALSE(weak.valid());
        EXPECT_EQ(stat("RecentModel Count"), 0);
    }
}
