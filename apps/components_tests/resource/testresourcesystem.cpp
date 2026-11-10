#include <components/files/configurationmanager.hpp>
#include <components/misc/nativejobpool.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/testing/util.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
}
