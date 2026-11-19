#include <components/diagnostics/loading.hpp>
#include <components/misc/nativejobpool.hpp>
#include <components/resource/bgsmfilemanager.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>

#include <chrono>
#include <future>
#include <thread>

namespace
{
    struct TestModels
    {
        ToUTF8::StatelessUtf8Encoder mEncoder{ ToUTF8::WINDOWS_1252 };
        Resource::ImageManager mImages;
        Resource::NifFileManager mNifs;
        Resource::BgsmFileManager mMaterials;
        Resource::SceneManager mScenes;

        explicit TestModels(const VFS::Manager* vfs)
            : mImages(vfs, 1.0)
            , mNifs(vfs, &mEncoder)
            , mMaterials(vfs, 1.0)
            , mScenes(vfs, &mImages, &mNifs, &mMaterials, 1.0)
        {
        }
    };

    // A complete Morrowind NiNode with identity transform, no children and one root.
    std::string makeRootNif(std::string_view name)
    {
        std::string data = "NetImmerse File Format, Version 4.0.0.2\n";
        const auto u32 = [&](std::uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                data.push_back(static_cast<char>((value >> shift) & 0xff));
        };
        const auto string = [&](std::string_view text) {
            u32(static_cast<std::uint32_t>(text.size()));
            data.append(text);
        };
        u32(0x04000002);
        u32(1);
        string("NiNode");
        string(name);
        u32(0xffffffff); // extra data
        u32(0xffffffff); // controller
        data.append("\x0e\x00", 2); // flags
        for (int i = 0; i < 16; ++i)
            u32(i == 3 || i == 7 || i == 11 || i == 12 ? 0x3f800000 : 0); // rotation and scale
        u32(0); // properties
        u32(0); // collision bounds
        u32(0); // children
        u32(0); // effects
        u32(1); // roots
        u32(0); // root index
        return data;
    }

    TEST(TransitionPreparationTest, PrivateDraftsPublishOnlyAtFirstRealRequest)
    {
        using VFS::Path::NormalizedView;
        using namespace Diagnostics::Loading;
        constexpr NormalizedView a("meshes/a.nif"), b("meshes/b.nif"), unused("meshes/unused.nif");
        TestingOpenMW::VFSTestFile file(makeRootNif("fixture"));
        auto vfs = TestingOpenMW::createTestVFS({ { a, &file }, { b, &file }, { unused, &file } });
        TestModels models(vfs.get());
        auto& scenes = models.mScenes;
        Misc::NativeJobPool jobs(2, 16);
        const std::array paths{ b, a, b, unused };
        Metrics metrics;
        Session capture(metrics);
        const auto owner = std::this_thread::get_id();
        bool consumed = false;
        scenes.withPreparedPagedModels(paths, jobs, nullptr, [&] {
            consumed = true;
            EXPECT_EQ(std::this_thread::get_id(), owner);
            EXPECT_FALSE(scenes.checkLoaded(b, 0));
            auto first = scenes.getTemplate(b, false);
            ASSERT_TRUE(first);
            EXPECT_EQ(first->referenceCount(), 2); // cache and caller, even with a live batch
            EXPECT_EQ(scenes.getTemplate(b, false), first);
            EXPECT_FALSE(scenes.checkLoaded(a, 0));
            EXPECT_TRUE(scenes.getTemplate(a, false).valid());
        });
        EXPECT_TRUE(consumed);
        EXPECT_FALSE(scenes.checkLoaded(unused, 0));
        const auto builds = metrics.calls[static_cast<std::size_t>(Stage::PagedModelCpu)]
            + metrics.calls[static_cast<std::size_t>(Stage::TemplateCpu)];
        EXPECT_GE(builds, 2u);
        EXPECT_LE(builds, 3u); // unused lookahead may stop as soon as consumption finishes
        const auto parallel = jobs.statistics().mParallelCalls;
        scenes.withPreparedPagedModels(std::array{ a, b }, jobs, nullptr, [&] {
            EXPECT_TRUE(scenes.checkLoaded(a, 0));
        });
        EXPECT_EQ(jobs.statistics().mParallelCalls, parallel);
    }

    TEST(TransitionPreparationTest, CancellationDuringAReadLeavesNoSceneAndCanRetry)
    {
        using VFS::Path::NormalizedView;
        struct InterruptingFile : TestingOpenMW::VFSTestFile
        {
            std::atomic_bool& mAbort;
            explicit InterruptingFile(std::atomic_bool& abort)
                : VFSTestFile(makeRootNif("cancel"))
                , mAbort(abort)
            {
            }
            Files::IStreamPtr open() override
            {
                mAbort.store(true);
                return VFSTestFile::open();
            }
        };
        constexpr NormalizedView a("meshes/cancel-a.nif"), b("meshes/cancel-b.nif");
        std::atomic_bool abort{ false };
        InterruptingFile file(abort);
        auto vfs = TestingOpenMW::createTestVFS({ { a, &file }, { b, &file } });
        TestModels models(vfs.get());
        auto& scenes = models.mScenes;
        Misc::NativeJobPool jobs(2, 16);
        const std::array paths{ a, b };
        scenes.withPreparedPagedModels(paths, jobs, &abort, [&] {
            if (!abort.load())
                EXPECT_TRUE(scenes.getTemplate(a, false).valid());
        });
        EXPECT_TRUE(abort.load());
        scenes.withPreparedPagedModels(paths, jobs, &abort, [] { ADD_FAILURE() << "Started after cancellation"; });
        // Either side may have completed a model. Only a real consumer can have published one.
        EXPECT_FALSE(scenes.checkLoaded(b, 0));
        scenes.withPreparedPagedModels(paths, jobs, nullptr, [&] {
            EXPECT_TRUE(scenes.getTemplate(a, false).valid());
            EXPECT_TRUE(scenes.getTemplate(b, false).valid());
        });
    }

    TEST(TransitionPreparationTest, SettingsChangeAndCacheClearDiscardUnpublishedModels)
    {
        using VFS::Path::NormalizedView;
        using namespace Diagnostics::Loading;
        constexpr NormalizedView a("meshes/revision-a.nif"), b("meshes/revision-b.nif");
        struct ObservedFile : TestingOpenMW::VFSTestFile
        {
            using VFSTestFile::VFSTestFile;
            std::promise<void> mOpened;
            std::once_flag mFirst;
            Files::IStreamPtr open() override
            {
                std::call_once(mFirst, [&] { mOpened.set_value(); });
                return VFSTestFile::open();
            }
        };
        TestingOpenMW::VFSTestFile first(makeRootNif("first"));
        ObservedFile ahead(makeRootNif("revision"));
        auto opened = ahead.mOpened.get_future();
        auto vfs = TestingOpenMW::createTestVFS({ { a, &first }, { b, &ahead } });
        TestModels models(vfs.get());
        auto& scenes = models.mScenes;
        Misc::NativeJobPool jobs(2, 16);
        Metrics metrics;
        Session capture(metrics);
        scenes.withPreparedPagedModels(std::array{ a, b }, jobs, nullptr, [&] {
            ASSERT_EQ(opened.wait_for(std::chrono::seconds(10)), std::future_status::ready);
            scenes.setAutoUseNormalMaps(true);
            EXPECT_TRUE(scenes.getTemplate(a, false).valid());
            scenes.clearCache();
            EXPECT_TRUE(scenes.getTemplate(b, false).valid());
        });
        EXPECT_EQ(metrics.calls[static_cast<std::size_t>(Stage::PagedModelCpu)], 1u);
        EXPECT_EQ(metrics.calls[static_cast<std::size_t>(Stage::TemplateCpu)], 2u);
    }

    TEST(TransitionPreparationTest, AdjacentChunksShareColdModelsWithoutPublishingAhead)
    {
        using VFS::Path::NormalizedView;
        using namespace Diagnostics::Loading;
        constexpr NormalizedView a("meshes/first.nif"), b("meshes/next.nif");
        struct WatchedFile : TestingOpenMW::VFSTestFile
        {
            using VFSTestFile::VFSTestFile;
            std::promise<void> mOpened;
            std::once_flag mFirst;
            Files::IStreamPtr open() override
            {
                std::call_once(mFirst, [&] { mOpened.set_value(); });
                return VFSTestFile::open();
            }
        };
        TestingOpenMW::VFSTestFile first(makeRootNif("first"));
        WatchedFile next(makeRootNif("next"));
        auto opened = next.mOpened.get_future();
        auto vfs = TestingOpenMW::createTestVFS({ { a, &first }, { b, &next } });
        TestModels models(vfs.get());
        Misc::NativeJobPool jobs(2, 16);
        std::atomic_bool abort{ false };
        const std::vector<std::vector<NormalizedView>> chunks{ { a, a }, { b, a }, {} };
        std::vector<std::size_t> order;
        Metrics metrics;
        Session timing(metrics);
        const auto owner = std::this_thread::get_id();
        models.mScenes.withPagedModelGroups(chunks, jobs, abort, [&](std::size_t i, bool prepared) {
            EXPECT_TRUE(prepared);
            EXPECT_EQ(std::this_thread::get_id(), owner);
            order.push_back(i);
            if (i == 0)
            {
                ASSERT_EQ(opened.wait_for(std::chrono::seconds(10)), std::future_status::ready);
                EXPECT_FALSE(models.mScenes.checkLoaded(b, 0));
            }
            for (const auto path : chunks[i])
                EXPECT_TRUE(models.mScenes.getTemplate(path, false));
        });
        EXPECT_EQ(order, (std::vector<std::size_t>{ 0, 1, 2 }));
        EXPECT_EQ(metrics.calls[static_cast<std::size_t>(Stage::PagedChunkGroup)], 1u);
        EXPECT_EQ(metrics.calls[static_cast<std::size_t>(Stage::TemplateCpu)]
            + metrics.calls[static_cast<std::size_t>(Stage::PagedModelCpu)], 2u);
    }

    TEST(TransitionPreparationTest, DenseChunkUsesBoundedFallbackAndAbortStopsFollowingChunks)
    {
        using VFS::Path::Normalized;
        using VFS::Path::NormalizedView;
        constexpr NormalizedView tail("meshes/tail.nif");
        TestingOpenMW::VFSTestFile model(makeRootNif("tail"));
        auto vfs = TestingOpenMW::createTestVFS({ { tail, &model } });
        TestModels models(vfs.get());
        Misc::NativeJobPool jobs(2, 16);
        std::atomic_bool abort{ false };
        std::vector<Normalized> names;
        for (int i = 0; i < 100; ++i)
            names.emplace_back("meshes/dense" + std::to_string(i) + ".nif");
        std::vector<std::vector<NormalizedView>> chunks(4);
        for (const auto& name : names)
            chunks[1].emplace_back(name);
        chunks[2].push_back(tail);
        std::vector<bool> prepared;
        models.mScenes.withPagedModelGroups(chunks, jobs, abort, [&](std::size_t i, bool grouped) {
            EXPECT_EQ(i, prepared.size());
            prepared.push_back(grouped);
            if (i == 2)
            {
                EXPECT_TRUE(models.mScenes.getTemplate(tail, false));
                abort = true;
            }
        });
        EXPECT_EQ(prepared, (std::vector<bool>{ true, false, true }));
        models.mScenes.withPagedModelGroups(chunks, jobs, abort,
            [](std::size_t, bool) { ADD_FAILURE() << "An aborted group must not consume another chunk"; });
    }
}
