#include <components/files/configurationmanager.hpp>
#include <components/misc/nativejobpool.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/testing/util.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/clone.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <components/misc/combinedperformance.hpp>
#include <osg/Stats>
#include <osg/observer_ptr>
#include <sstream>
#include <thread>

namespace
{
    using namespace testing;

    TEST(ResourceResourceSystem, nativePagingDecodesColdInputsAndReusesWarmModels)
    {
        using VFS::Path::NormalizedView;
        std::string nif = "NetImmerse File Format, Version 4.0.0.2\n";
        nif.append("\x02\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00", 12);
        TestingOpenMW::VFSTestFile good(nif), corrupt("invalid header\n");
        constexpr NormalizedView a("a.nif"), b("b.nif"), c("c.nif"), d("d.nif"), broken("bad.nif");
        auto vfs = TestingOpenMW::createTestVFS({ {a,&good}, {b,&good}, {c,&good}, {d,&good}, {broken,&corrupt} });
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(vfs.get(), 1.0, &encoder.getStatelessEncoder());
        Misc::NativeJobPool pool(2, 8192);
        const std::array paths{ a, b, c, d, broken };
        resources.getSceneManager()->prepareNifInputs(paths, pool);
        auto* files = resources.getNifFileManager();
        auto first = files->get(a);
        for (auto path : { a, b, c, d })
            EXPECT_TRUE(files->isCached(path));
        EXPECT_EQ(first->mVersion, Nif::NIFFile::VER_MW);
        EXPECT_FALSE(files->isCached(broken));
        EXPECT_ANY_THROW(files->get(broken));
        EXPECT_EQ(pool.statistics().mParallelCalls, 1u);
        resources.getSceneManager()->prepareNifInputs(paths, pool);
        EXPECT_EQ(pool.statistics().mParallelCalls, 1u);
        EXPECT_EQ(files->get(a), first);
    }

    TEST(ResourceResourceSystem, scenemanager_getinstance_should_be_thread_safe)
    {
        const VFS::Manager vfsManager;
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resourceSystem(&vfsManager, 1.0, &encoder.getStatelessEncoder());
        Resource::SceneManager* sceneManager = resourceSystem.getSceneManager();
        const Files::ConfigurationManager configurationManager;
        sceneManager->setShaderPath(configurationManager.getLocalPath() / "resources/shaders");

        auto defines = Shader::getDefaultDefines();

        auto shadowDefines = SceneUtil::ShadowManager::getShadowsDisabledDefines();

        osg::ref_ptr<SceneUtil::LightManager> lightManager
            = new SceneUtil::LightManager(SceneUtil::LightSettings{}, &resourceSystem);
        auto lightDefines = lightManager->getLightDefines();

        for (const auto& define : shadowDefines)
            defines[define.first] = define.second;
        for (const auto& define : lightDefines)
            defines[define.first] = define.second;

        sceneManager->getShaderManager().setGlobalDefines(defines);

        constexpr VFS::Path::NormalizedView noSuchPath("meshes/whatever.nif");
        std::vector<std::thread> threads;

        for (int i = 0; i < 50; ++i)
        {
            threads.emplace_back([=]() { sceneManager->getInstance(noSuchPath); });
        }
        for (std::thread& thread : threads)
            thread.join();
    }

    TEST(ResourceResourceSystem, preparedInstanceIsConsumedOnceAndSettingsInvalidateIt)
    {
        if (!Misc::useCellRenderPreparation())
            GTEST_SKIP() << "Detached preparation comparison switch is disabled";
        // The NIF serialization tests replace osg::Geometry's reader with a dummy
        // Drawable factory. Use a real NIF root so this fixture is independent of
        // that process-wide serializer and still exercises normal template loading.
        std::string nif = "NetImmerse File Format, Version 4.0.0.2\n";
        const auto u32 = [&](std::uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                nif.push_back(static_cast<char>((value >> shift) & 0xff));
        };
        const auto string = [&](std::string_view value) {
            u32(static_cast<std::uint32_t>(value.size()));
            nif.append(value);
        };
        u32(0x04000002);
        u32(1);
        string("NiNode");
        string("preparation fixture");
        u32(0xffffffff); // extra data
        u32(0xffffffff); // controller
        nif.append("\x0e\x00", 2); // flags
        for (int i = 0; i < 16; ++i)
            u32(i == 3 || i == 7 || i == 11 || i == 12 ? 0x3f800000 : 0); // identity transform
        u32(0); // properties
        u32(0); // collision bounds
        u32(0); // children
        u32(0); // effects
        u32(1); // roots
        u32(0); // root index
        TestingOpenMW::VFSTestFile file(nif);
        constexpr VFS::Path::NormalizedView path("meshes/preparation-fixture.nif");
        auto vfs = TestingOpenMW::createTestVFS({ { path, &file } });
        const ToUTF8::Utf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(vfs.get(), 1.0, &encoder.getStatelessEncoder());
        auto* scene = resources.getSceneManager();
        const Files::ConfigurationManager configuration;
        scene->setShaderPath(configuration.getLocalPath() / "resources/shaders");
        auto defines = Shader::getDefaultDefines();
        for (const auto& define : SceneUtil::ShadowManager::getShadowsDisabledDefines())
            defines[define.first] = define.second;
        osg::ref_ptr<SceneUtil::LightManager> lights
            = new SceneUtil::LightManager(SceneUtil::LightSettings{}, &resources);
        for (const auto& define : lights->getLightDefines())
            defines[define.first] = define.second;
        scene->getShaderManager().setGlobalDefines(defines);

        // Exercise actual template publication and the instance path used by the engine.
        const auto model = scene->getTemplate(path);
        ASSERT_TRUE(model);
        EXPECT_EQ(resources.getNifFileManager()->get(path)->mRoots.size(), 1u);
        struct DescribeGraph : osg::NodeVisitor
        {
            std::ostringstream description;
            DescribeGraph()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
                setNodeMaskOverride(~0u);
            }
            void apply(osg::Node& node) override
            {
                const auto bytes = SceneUtil::estimateInstanceBytes(node);
                description << node.libraryName() << "::" << node.className() << ": "
                            << (bytes ? std::to_string(*bytes) : "unsupported") << '\n';
                traverse(node);
            }
        } graph;
        const_cast<osg::Node*>(model.get())->accept(graph);
        SCOPED_TRACE(graph.description.str());
        const auto estimatedBytes = SceneUtil::estimateInstanceBytes(*model);
        ASSERT_TRUE(estimatedBytes) << "Prepared NIF graph contains an unsupported drawable";
        ASSERT_GT(*estimatedBytes, 0u);
        ASSERT_LE(*estimatedBytes, 16u * 1024u * 1024u);
        const auto revision = scene->modelSettingsRevision();
        ASSERT_EQ(scene->getTemplate(path), model);
        std::atomic_bool abort{ false };
        auto ticket = scene->cacheInstance(path, abort);
        EXPECT_EQ(scene->modelSettingsRevision(), revision);
        ASSERT_TRUE(ticket) << "estimated bytes=" << *estimatedBytes << ", revision=" << revision;
        auto first = scene->getInstance(path);
        ASSERT_TRUE(first);
        EXPECT_EQ(first->getNumParents(), 0u);
        auto second = scene->getInstance(path);
        EXPECT_NE(first, second);
        osg::ref_ptr<osg::Stats> stats = new osg::Stats("fixture");
        scene->reportStats(1, stats);
        double hits = 0;
        ASSERT_TRUE(stats->getAttribute(1, "NodeInstance Hit", hits));
        EXPECT_EQ(hits, 1);

        osg::observer_ptr<osg::Node> weak = first;
        first = nullptr;
        EXPECT_FALSE(weak.valid()); // The ticket must not retain a consumed graph.
        ticket = scene->cacheInstance(path, abort);
        ASSERT_TRUE(ticket);
        scene->setFilterSettings("linear", "linear", "linear", 1);
        auto afterSettings = scene->getInstance(path);
        scene->reportStats(2, stats);
        ASSERT_TRUE(stats->getAttribute(2, "NodeInstance Hit", hits));
        EXPECT_EQ(hits, 1);
        EXPECT_EQ(model->getNumParents(), 0u);
    }
}
