#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwrender/globalmap.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

#include <components/esm3/globalmap.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/settings/values.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>

#include <gtest/gtest.h>
#include <osg/Group>
#include <osg/Image>
#include <osg/Texture2D>
#include <osgDB/Registry>

#include <filesystem>
#include <sstream>

namespace
{
    ESM::GlobalMap makeSavedMap(ESM::GlobalMap::Bounds bounds, int cellSize, const osg::Vec4& colour)
    {
        ESM::GlobalMap map;
        map.mBounds = bounds;
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage((bounds.mMaxX - bounds.mMinX + 1) * cellSize,
            (bounds.mMaxY - bounds.mMinY + 1) * cellSize, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        for (int y = 0; y < image->t(); ++y)
            for (int x = 0; x < image->s(); ++x)
                image->setColor(colour, x, y);
        auto* png = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        if (!png)
            throw std::runtime_error("PNG reader/writer unavailable");
        std::ostringstream out;
        if (!png->writeImage(*image, out).success())
            throw std::runtime_error("Couldn't encode map fixture");
        const std::string bytes = out.str();
        map.mImageData.assign(bytes.begin(), bytes.end());
        return map;
    }

    class SavedGlobalMapRestoreTest : public testing::Test
    {
    protected:
        MWBase::Environment mEnvironment;
        MWWorld::ESMStore mStore;
        VFS::Manager mVfs;
        ToUTF8::StatelessUtf8Encoder mEncoder{ ToUTF8::WINDOWS_1252 };
        Resource::ResourceSystem mResources{ &mVfs, 1.0, &mEncoder };
        osg::ref_ptr<osg::Group> mRoot = new osg::Group;
        // Explicit execution lets the test save while a real decode is queued, without wall-clock assumptions.
        osg::ref_ptr<SceneUtil::WorkQueue> mJobs = new SceneUtil::WorkQueue(0);
        MWRender::GlobalMap mMap{ mRoot.get(), mJobs.get() };
        int mSize = Settings::map().mGlobalMapCellSize;

        static void complete(const osg::ref_ptr<SceneUtil::WorkItem>& item)
        {
            item->doWork();
            item->signalDone();
        }

        void SetUp() override
        {
            mEnvironment.setESMStore(mStore);
            mEnvironment.setResourceSystem(mResources);
            const auto files = std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR) / "files";
            mVfs.addArchive(std::make_unique<VFS::FileSystemArchive>(files / "data"));
            mVfs.buildIndex();
            mResources.getSceneManager()->setShaderPath(files / "shaders");
            mMap.render();
            complete(mJobs->removeWorkItem());
            mMap.ensureLoaded();
        }

        void expectSave(const ESM::GlobalMap& original)
        {
            ESM::GlobalMap written;
            mMap.write(written);
            EXPECT_EQ(written.mImageData, original.mImageData);
            EXPECT_EQ(written.mBounds.mMinX, original.mBounds.mMinX);
            EXPECT_EQ(written.mBounds.mMaxX, original.mBounds.mMaxX);
            EXPECT_EQ(written.mBounds.mMinY, original.mBounds.mMinY);
            EXPECT_EQ(written.mBounds.mMaxY, original.mBounds.mMaxY);
        }
    };

    TEST_F(SavedGlobalMapRestoreTest, ImmediateSavePreservesTheOriginalBeforeAndAfterHandoff)
    {
        const auto original = makeSavedMap({ 0, 0, 0, 0 }, mSize, { 0.1f, 0.45f, 0.75f, 1.f });
        auto input = original;
        mMap.read(input);
        EXPECT_FALSE(mMap.readyForDisplay());
        mMap.asyncWritePng();
        EXPECT_EQ(mJobs->getNumItems(), 1u);
        expectSave(original);
        complete(mJobs->removeWorkItem());
        mMap.finishRestore();
        EXPECT_TRUE(mMap.readyForDisplay());
        EXPECT_TRUE(mMap.getBaseTexture().valid());
        EXPECT_TRUE(mMap.getOverlayTexture().valid());
        expectSave(original);
    }

    TEST_F(SavedGlobalMapRestoreTest, ReloadDropsThePreviousDecoderAndSaveEncoder)
    {
        mMap.asyncWritePng();
        auto oldWrite = mJobs->removeWorkItem();
        auto first = makeSavedMap({ 0, 0, 0, 0 }, mSize, { 1.f, 0.f, 0.f, 1.f });
        mMap.read(first);
        auto oldRead = mJobs->removeWorkItem();
        mMap.clear();
        const auto second = makeSavedMap({ 0, 0, 0, 0 }, mSize, { 0.f, 1.f, 0.f, 1.f });
        auto input = second;
        mMap.read(input);
        complete(mJobs->removeWorkItem());
        complete(oldWrite);
        complete(oldRead);
        mMap.finishRestore();
        expectSave(second);
    }

    TEST_F(SavedGlobalMapRestoreTest, ChangedBoundsRetainTheirPngUntilGpuReadback)
    {
        const auto original = makeSavedMap({ -1, 0, 0, 0 }, mSize + 1, { 0.3f, 0.7f, 0.9f, 1.f });
        auto input = original;
        mMap.read(input);
        complete(mJobs->removeWorkItem());
        mMap.finishRestore();
        mMap.asyncWritePng();
        EXPECT_EQ(mJobs->getNumItems(), 0u);
        expectSave(original);
    }

    TEST_F(SavedGlobalMapRestoreTest, DamagedImageLeavesAUsableBlankMap)
    {
        ESM::GlobalMap broken;
        broken.mBounds = { 0, 0, 0, 0 };
        broken.mImageData = { 'b', 'a', 'd' };
        mMap.read(broken);
        complete(mJobs->removeWorkItem());
        mMap.finishRestore();
        ESM::GlobalMap out;
        mMap.write(out);
        ASSERT_FALSE(out.mImageData.empty());
        auto* png = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        ASSERT_NE(png, nullptr);
        std::istringstream input(std::string(out.mImageData.begin(), out.mImageData.end()));
        auto decoded = png->readImage(input);
        ASSERT_TRUE(decoded.success());
        ASSERT_TRUE(decoded.getImage());
        EXPECT_EQ(decoded.getImage()->s(), mSize);
        EXPECT_EQ(decoded.getImage()->getColor(0, 0).a(), 0.f);
    }
}
