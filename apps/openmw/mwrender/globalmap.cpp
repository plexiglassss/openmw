#include "globalmap.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/Image>
#include <osg/Texture2D>

#include <osgDB/WriteFile>

#include <components/files/memorystream.hpp>
#include <components/settings/values.hpp>

#include <components/debug/debuglog.hpp>

#include <components/resource/imagemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/shader/shadermanager.hpp>

#include <components/sceneutil/depth.hpp>
#include <components/sceneutil/nodecallback.hpp>
#include <components/sceneutil/workqueue.hpp>

#include <components/vfs/pathutil.hpp>

#include <components/esm3/globalmap.hpp>
#include <components/esm3/loadland.hpp>

#include "../mwbase/environment.hpp"

#include "../mwworld/esmstore.hpp"

#include "vismask.hpp"

namespace
{

    using MapClock = std::chrono::steady_clock;
    double elapsedMs(MapClock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(MapClock::now() - start).count();
    }

    // Most explored-map pixels are initially empty. Let the allocator supply zeroed pages rather
    // than touching the whole image just before a saved overlay replaces it.
    osg::ref_ptr<osg::Image> blankOverlay(int width, int height)
    {
        if (width <= 0 || height <= 0
            || std::size_t(width) > std::numeric_limits<std::size_t>::max() / std::size_t(height) / 4)
            throw std::runtime_error("Invalid global map dimensions");
        osg::ref_ptr<osg::Image> image = new osg::Image;
        auto* pixels = static_cast<unsigned char*>(std::calloc(std::size_t(width) * height, 4));
        if (!pixels)
            throw std::bad_alloc();
        image->setImage(width, height, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, pixels, osg::Image::USE_MALLOC_FREE);
        return image;
    }

    // Create a screen-aligned quad with given texture coordinates.
    // Assumes a top-left origin of the sampled image.
    osg::ref_ptr<osg::Geometry> createTexturedQuad(
        float leftTexCoord, float topTexCoord, float rightTexCoord, float bottomTexCoord)
    {
        osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;

        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        verts->push_back(osg::Vec3f(-1, -1, 0));
        verts->push_back(osg::Vec3f(-1, 1, 0));
        verts->push_back(osg::Vec3f(1, 1, 0));
        verts->push_back(osg::Vec3f(1, -1, 0));

        geom->setVertexArray(verts);

        osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
        texcoords->push_back(osg::Vec2f(leftTexCoord, 1.f - bottomTexCoord));
        texcoords->push_back(osg::Vec2f(leftTexCoord, 1.f - topTexCoord));
        texcoords->push_back(osg::Vec2f(rightTexCoord, 1.f - topTexCoord));
        texcoords->push_back(osg::Vec2f(rightTexCoord, 1.f - bottomTexCoord));

        osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
        colors->push_back(osg::Vec4(1.f, 1.f, 1.f, 1.f));
        geom->setColorArray(colors, osg::Array::BIND_OVERALL);

        geom->setTexCoordArray(0, texcoords, osg::Array::BIND_PER_VERTEX);

        geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));

        return geom;
    }

    class CameraUpdateGlobalCallback : public SceneUtil::NodeCallback<CameraUpdateGlobalCallback, osg::Camera*>
    {
    public:
        CameraUpdateGlobalCallback(MWRender::GlobalMap* parent)
            : mRendered(false)
            , mParent(parent)
        {
        }

        void operator()(osg::Camera* node, osg::NodeVisitor* nv)
        {
            if (mRendered)
            {
                if (mParent->copyResult(node, nv->getTraversalNumber()))
                {
                    node->setNodeMask(0);
                    mParent->markForRemoval(node);
                }
                return;
            }

            traverse(node, nv);

            mRendered = true;
        }

    private:
        bool mRendered;
        MWRender::GlobalMap* mParent;
    };

    std::vector<char> writePng(const osg::Image& overlayImage)
    {
        std::ostringstream ostream;
        osgDB::ReaderWriter* readerwriter = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        if (!readerwriter)
        {
            Log(Debug::Error) << "Error: Can't write map overlay: no png readerwriter found";
            return std::vector<char>();
        }

        osgDB::ReaderWriter::WriteResult result = readerwriter->writeImage(overlayImage, ostream);
        if (!result.success())
        {
            Log(Debug::Warning) << "Error: Can't write map overlay: " << result.message() << " code "
                                << result.status();
            return std::vector<char>();
        }

        std::string data = ostream.str();
        return std::vector<char>(data.begin(), data.end());
    }
}

namespace MWRender
{

    class CreateMapWorkItem : public SceneUtil::WorkItem
    {
    public:
        CreateMapWorkItem(int width, int height, int minX, int minY, int maxX, int maxY, int cellSize,
            const MWWorld::Store<ESM::Land>& landStore, osg::ref_ptr<osg::Image> colorLut)
            : mWidth(width)
            , mHeight(height)
            , mMinX(minX)
            , mMinY(minY)
            , mMaxX(maxX)
            , mMaxY(maxY)
            , mCellSize(cellSize)
            , mLandStore(landStore)
            , mColorLut(colorLut)
        {
        }

        void doWork() override
        {
            const auto start = MapClock::now();
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(mWidth, mHeight, 1, GL_RGB, GL_UNSIGNED_BYTE);

            osg::ref_ptr<osg::Image> alphaImage = new osg::Image;
            alphaImage->allocateImage(mWidth, mHeight, 1, GL_ALPHA, GL_UNSIGNED_BYTE);

            // Convert the VFS-provided palette with the same OSG formats as the destination, once per colour.
            osg::ref_ptr<osg::Image> rgb = new osg::Image;
            rgb->allocateImage(256, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
            for (unsigned int i = 0; i < 256; ++i)
                rgb->setColor(mColorLut->getColor(i, 0), i, 0);
            const auto* colours = rgb->data(); // one tightly packed row of RGB bytes
            std::vector<unsigned char> emptyRow(3 * mCellSize);
            for (int i = 0; i < mCellSize; ++i)
                std::memcpy(emptyRow.data() + 3 * i, colours, 3);
            std::vector<int> heightSample(mCellSize);
            for (int i = 0; i < mCellSize; ++i)
                heightSample[i] = i * 9 / mCellSize;
            for (int y = mMinY; y <= mMaxY; ++y)
            {
                for (int x = mMinX; x <= mMaxX; ++x)
                {
                    const ESM::Land* land = mLandStore.search(x, y);
                    const bool hasHeights = land && (land->mDataTypes & ESM::Land::DATA_WNAM);
                    const int left = (x - mMinX) * mCellSize;
                    const int bottom = (y - mMinY) * mCellSize;
                    for (int row = 0; row < mCellSize; ++row)
                    {
                        auto* dst = image->data(left, bottom + row);
                        auto* alpha = alphaImage->data(left, bottom + row);
                        if (!hasHeights)
                        {
                            std::memcpy(dst, emptyRow.data(), emptyRow.size());
                            std::memset(alpha, 0, mCellSize);
                            continue;
                        }
                        const auto* heights = land->mWnam.data() + heightSample[row] * 9;
                        for (int column = 0; column < mCellSize; ++column)
                        {
                            const int index = int(heights[heightSample[column]]) + 128;
                            std::memcpy(dst, colours + index * 3, 3);
                            dst += 3;
                            *alpha++ = index < 128 ? 0 : 255;
                        }
                    }
                }
            }

            mBaseTexture = new osg::Texture2D;
            mBaseTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            mBaseTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            mBaseTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            mBaseTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            mBaseTexture->setImage(image);
            mBaseTexture->setResizeNonPowerOfTwoHint(false);

            mAlphaTexture = new osg::Texture2D;
            mAlphaTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            mAlphaTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            mAlphaTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            mAlphaTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            mAlphaTexture->setImage(alphaImage);
            mAlphaTexture->setResizeNonPowerOfTwoHint(false);

            mOverlayImage = blankOverlay(mWidth, mHeight);

            mOverlayTexture = new osg::Texture2D;
            mOverlayTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            mOverlayTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            mOverlayTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            mOverlayTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            mOverlayTexture->setResizeNonPowerOfTwoHint(false);
            mOverlayTexture->setInternalFormat(GL_RGBA);
            mOverlayTexture->setTextureSize(mWidth, mHeight);
            mBuildMs = elapsedMs(start);
        }

        double mBuildMs = 0;
        int mWidth, mHeight;
        int mMinX, mMinY, mMaxX, mMaxY;
        int mCellSize;
        const MWWorld::Store<ESM::Land>& mLandStore;
        osg::ref_ptr<osg::Image> mColorLut;

        osg::ref_ptr<osg::Texture2D> mBaseTexture;
        osg::ref_ptr<osg::Texture2D> mAlphaTexture;

        osg::ref_ptr<osg::Image> mOverlayImage;
        osg::ref_ptr<osg::Texture2D> mOverlayTexture;
    };

    struct GlobalMap::ReadPng final : SceneUtil::WorkItem
    {
        const ESM::GlobalMap::Bounds mBounds;
        const std::vector<char> mBytes;
        const MapClock::time_point mSubmitted = MapClock::now();
        std::atomic_bool mCancelled{ false };
        osg::ref_ptr<osg::Image> mImage;
        std::string mError;
        double mQueueMs = 0;
        double mDecodeMs = 0;

        explicit ReadPng(ESM::GlobalMap& map)
            : mBounds(map.mBounds)
            , mBytes(std::move(map.mImageData))
        {
        }

        void abort() override { mCancelled.store(true, std::memory_order_relaxed); }

        void doWork() override
        {
            if (mCancelled.load(std::memory_order_relaxed))
                return;
            const auto start = MapClock::now();
            mQueueMs = elapsedMs(mSubmitted);
            try
            {
                auto* reader = osgDB::Registry::instance()->getReaderWriterForExtension("png");
                if (reader)
                {
                    Files::IMemStream stream(mBytes.data(), mBytes.size());
                    auto result = reader->readImage(stream);
                    if (result.success() && result.getImage())
                        mImage = result.getImage();
                    else
                        mError = result.statusMessage();
                }
                else
                    mError = "no PNG reader";
            }
            catch (const std::exception& error)
            {
                mError = error.what();
            }
            catch (...)
            {
                mError = "unexpected PNG reader error";
            }
            mDecodeMs = elapsedMs(start);
        }
    };

    struct GlobalMap::WritePng final : public SceneUtil::WorkItem
    {
        osg::ref_ptr<const osg::Image> mOverlayImage;
        std::vector<char> mImageData;

        explicit WritePng(osg::ref_ptr<const osg::Image> overlayImage)
            : mOverlayImage(std::move(overlayImage))
        {
        }

        void doWork() override { mImageData = writePng(*mOverlayImage); }
    };

    GlobalMap::GlobalMap(osg::Group* root, SceneUtil::WorkQueue* workQueue)
        : mRoot(new osg::Group)
        , mWorkQueue(workQueue)
        , mWidth(0)
        , mHeight(0)
        , mMinX(0)
        , mMaxX(0)
        , mMinY(0)
        , mMaxY(0)
    {
        root->addChild(mRoot);

        // Bind a dummy alpha texture at top of map subgraph
        osg::ref_ptr<osg::Image> fallbackImage = new osg::Image;
        fallbackImage->allocateImage(1, 1, 1, GL_ALPHA, GL_UNSIGNED_BYTE);
        *fallbackImage->data(0, 0) = 0xFF;

        osg::ref_ptr<osg::Texture2D> dummyTex = new osg::Texture2D(fallbackImage);
        dummyTex->setWrap(osg::Texture2D::WRAP_S, osg::Texture2D::REPEAT);
        dummyTex->setWrap(osg::Texture2D::WRAP_T, osg::Texture2D::REPEAT);
        dummyTex->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::NEAREST);
        dummyTex->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::NEAREST);
        dummyTex->setInternalFormat(GL_ALPHA);

        mRoot->getOrCreateStateSet()->addUniform(new osg::Uniform("alphaMap", 1));
        mRoot->getOrCreateStateSet()->setTextureAttribute(1, dummyTex);
    }

    GlobalMap::~GlobalMap()
    {
        if (mRestorePending)
            mSavedOverlay->abort();
        for (auto& camera : mCamerasPendingRemoval)
            removeCamera(camera);
        for (auto& camera : mActiveCameras)
            removeCamera(camera);

        if (mWorkItem)
            mWorkItem->waitTillDone();
    }

    void GlobalMap::render()
    {
        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();

        // get the size of the world
        MWWorld::Store<ESM::Cell>::iterator it = esmStore.get<ESM::Cell>().extBegin();
        for (; it != esmStore.get<ESM::Cell>().extEnd(); ++it)
        {
            if (it->getGridX() < mMinX)
                mMinX = it->getGridX();
            if (it->getGridX() > mMaxX)
                mMaxX = it->getGridX();
            if (it->getGridY() < mMinY)
                mMinY = it->getGridY();
            if (it->getGridY() > mMaxY)
                mMaxY = it->getGridY();
        }

        const int cellSize = Settings::map().mGlobalMapCellSize;

        mWidth = cellSize * (mMaxX - mMinX + 1);
        mHeight = cellSize * (mMaxY - mMinY + 1);

        // Load color LUT texture
        constexpr VFS::Path::NormalizedView colorLutPath("textures/omw_map_color_palette.dds");
        auto resourceSystem = MWBase::Environment::get().getResourceSystem();
        osg::ref_ptr<osg::Image> colorLut = resourceSystem->getImageManager()->getImage(colorLutPath);

        // Validate LUT dimensions
        if (!colorLut || colorLut->s() != 256 || colorLut->t() != 1)
        {
            throw std::runtime_error("Global map color LUT must be 256x1 pixels, got "
                + std::to_string(colorLut ? colorLut->s() : 0) + "x" + std::to_string(colorLut ? colorLut->t() : 0));
        }

        mWorkItem = new CreateMapWorkItem(
            mWidth, mHeight, mMinX, mMinY, mMaxX, mMaxY, cellSize, esmStore.get<ESM::Land>(), colorLut);
        mWorkQueue->addWorkItem(mWorkItem);
    }

    void GlobalMap::worldPosToImageSpace(float x, float z, float& imageX, float& imageY)
    {
        imageX = (float(x / float(Constants::CellSizeInUnits) - mMinX) / (mMaxX - mMinX + 1)) * getWidth();

        imageY = (1.f - float(z / float(Constants::CellSizeInUnits) - mMinY) / (mMaxY - mMinY + 1)) * getHeight();
    }

    osg::Camera* GlobalMap::requestOverlayTextureUpdate(int x, int y, int width, int height,
        osg::ref_ptr<osg::Texture2D> texture, bool clear, bool cpuCopy, float srcLeft, float srcTop, float srcRight,
        float srcBottom)
    {
        osg::ref_ptr<osg::Camera> camera(new osg::Camera);
        camera->setNodeMask(Mask_RenderToTexture);
        camera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
        camera->setViewMatrix(osg::Matrix::identity());
        camera->setProjectionMatrix(osg::Matrix::identity());
        camera->setProjectionResizePolicy(osg::Camera::FIXED);
        // Local map, blank background, saved overlay, then exploration. Equal render-order values can
        // otherwise put the blank camera after the restore in the first frame of a load.
        camera->setRenderOrder(osg::Camera::PRE_RENDER, !clear ? 3 : (texture ? 2 : 1));
        y = mHeight - y - height; // convert top-left origin to bottom-left
        camera->setViewport(x, y, width, height);

        if (clear)
        {
            camera->setClearMask(GL_COLOR_BUFFER_BIT);
            camera->setClearColor(osg::Vec4(0, 0, 0, 0));
        }
        else
            camera->setClearMask(GL_NONE);

        camera->setUpdateCallback(new CameraUpdateGlobalCallback(this));

        camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT, osg::Camera::PIXEL_BUFFER_RTT);
        camera->attach(osg::Camera::COLOR_BUFFER, mOverlayTexture);

        // no need for a depth buffer
        camera->setImplicitBufferAttachmentMask(osg::DisplaySettings::IMPLICIT_COLOR_BUFFER_ATTACHMENT);

        if (cpuCopy)
        {
            // Attach an image to copy the render back to the CPU when finished
            osg::ref_ptr<osg::Image> image(new osg::Image);
            image->setPixelFormat(mOverlayImage->getPixelFormat());
            image->setDataType(mOverlayImage->getDataType());
            camera->attach(osg::Camera::COLOR_BUFFER, image);

            ImageDest imageDest;
            imageDest.mImage = image;
            imageDest.mX = x;
            imageDest.mY = y;
            mPendingImageDest[camera] = std::move(imageDest);
        }

        // Create a quad rendering the updated texture
        if (texture)
        {
            osg::ref_ptr<osg::Geometry> geom = createTexturedQuad(srcLeft, srcTop, srcRight, srcBottom);
            osg::ref_ptr<osg::Depth> depth = new SceneUtil::AutoDepth;
            depth->setWriteMask(false);
            osg::StateSet* stateset = geom->getOrCreateStateSet();
            stateset->setAttribute(depth);
            stateset->setTextureAttribute(0, texture, osg::StateAttribute::ON);
            stateset->addUniform(new osg::Uniform("diffuseMap", 0));
            stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);

            if (mAlphaTexture)
            {
                osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;

                float x1 = x / static_cast<float>(mWidth);
                float x2 = (x + width) / static_cast<float>(mWidth);
                float y1 = y / static_cast<float>(mHeight);
                float y2 = (y + height) / static_cast<float>(mHeight);
                texcoords->push_back(osg::Vec2f(x1, y1));
                texcoords->push_back(osg::Vec2f(x1, y2));
                texcoords->push_back(osg::Vec2f(x2, y2));
                texcoords->push_back(osg::Vec2f(x2, y1));
                geom->setTexCoordArray(1, texcoords, osg::Array::BIND_PER_VERTEX);

                stateset->setTextureAttribute(1, mAlphaTexture, osg::StateAttribute::ON);
            }

            auto& shaderManager = MWBase::Environment::get().getResourceSystem()->getSceneManager()->getShaderManager();

            geom->getOrCreateStateSet()->setAttributeAndModes(shaderManager.getProgram("globalmap"));

            camera->addChild(geom);
        }

        mRoot->addChild(camera);

        mActiveCameras.push_back(camera);
        return camera.get();
    }

    void GlobalMap::exploreCell(int cellX, int cellY, osg::ref_ptr<osg::Texture2D> localMapTexture)
    {
        if (!localMapTexture || cellX > mMaxX || cellX < mMinX || cellY > mMaxY || cellY < mMinY)
            return;
        if (mRestorePending || mRestoreCamera || (mWorkItem && !mWorkItem->isDone()))
        {
            mQueuedExploration[{ cellX, cellY }] = std::move(localMapTexture);
            return;
        }
        ensureLoaded();

        const int cellSize = Settings::map().mGlobalMapCellSize;
        const int originX = (cellX - mMinX) * cellSize;
        // +1 because we want the top left corner of the cell, not the bottom left
        const int originY = (cellY - mMinY + 1) * cellSize;

        requestOverlayTextureUpdate(
            originX, mHeight - originY, cellSize, cellSize, std::move(localMapTexture), false, true);
    }

    void GlobalMap::clear()
    {
        if (mRestorePending)
            mSavedOverlay->abort();
        mRestorePending = false;
        mRestoreCamera = nullptr;
        mSavedOverlay = nullptr;
        mWritePng = nullptr;
        mQueuedExploration.clear();
        mPendingImageDest.clear();
        for (const auto& camera : mActiveCameras)
        {
            camera->setNodeMask(0);
            removeCamera(camera);
        }
        mActiveCameras.clear();
        cleanupCameras();
        if (!mOverlayImage)
            return; // the base builder will publish a blank image
        mOverlayImage = blankOverlay(mWidth, mHeight);

        // just push a Camera to clear the FBO, instead of setImage()/dirty()
        // easier, since we don't need to worry about synchronizing access :)
        requestOverlayTextureUpdate(0, 0, mWidth, mHeight, osg::ref_ptr<osg::Texture2D>(), true, false);
    }

    void GlobalMap::write(ESM::GlobalMap& map)
    {
        if (mSavedOverlay)
        {
            map.mBounds = mSavedOverlay->mBounds;
            map.mImageData = mSavedOverlay->mBytes;
            return;
        }
        ensureLoaded();

        map.mBounds.mMinX = mMinX;
        map.mBounds.mMaxX = mMaxX;
        map.mBounds.mMinY = mMinY;
        map.mBounds.mMaxY = mMaxY;

        if (mWritePng != nullptr)
        {
            mWritePng->waitTillDone();
            map.mImageData = std::move(mWritePng->mImageData);
            mWritePng = nullptr;
            return;
        }

        map.mImageData = writePng(*mOverlayImage);
    }

    struct Box
    {
        int mLeft, mTop, mRight, mBottom;

        Box(int left, int top, int right, int bottom)
            : mLeft(left)
            , mTop(top)
            , mRight(right)
            , mBottom(bottom)
        {
        }
        bool operator==(const Box& other) const
        {
            return mLeft == other.mLeft && mTop == other.mTop && mRight == other.mRight && mBottom == other.mBottom;
        }
    };

    void GlobalMap::read(ESM::GlobalMap& map)
    {
        if (mSavedOverlay || mRestorePending || !mPendingImageDest.empty())
            clear();
        mWritePng = nullptr;
        if (map.mImageData.empty() || map.mBounds.mMinX > map.mBounds.mMaxX || map.mBounds.mMinY > map.mBounds.mMaxY)
            return;
        mSavedOverlay = new ReadPng(map);
        mRestorePending = true;
        mWorkQueue->addWorkItem(mSavedOverlay, true);
    }

    bool GlobalMap::readyForDisplay() const
    {
        return !mRestorePending && (mBaseTexture || (mWorkItem && mWorkItem->isDone()));
    }

    void GlobalMap::flushExploredCells()
    {
        if (mRestoreCamera || mRestorePending)
            return;
        auto cells = std::move(mQueuedExploration);
        mQueuedExploration.clear();
        for (auto& [position, image] : cells)
            exploreCell(position.first, position.second, std::move(image));
    }

    void GlobalMap::update()
    {
        cleanupCameras();
        if (!mQueuedExploration.empty() && readyForDisplay())
            flushExploredCells();
    }

    void GlobalMap::finishRestore()
    {
        if (!mRestorePending)
            return;
        const auto baseStart = MapClock::now();
        ensureLoaded();
        const auto baseWait = elapsedMs(baseStart);
        // Take a local reference: applying a disjoint or invalid map can discard its original bytes.
        auto job = mSavedOverlay;
        const auto decodeStart = MapClock::now();
        job->waitTillDone();
        const auto decodeWait = elapsedMs(decodeStart);
        mRestorePending = false;
        const auto applyStart = MapClock::now();
        if (job->mImage)
        {
            osg::ref_ptr<osg::Image> image;
            image.swap(job->mImage);
            applySavedImage(std::move(image));
        }
        else
        {
            Log(Debug::Warning) << "Can't read saved world map overlay: " << job->mError;
            mSavedOverlay = nullptr;
        }
        flushExploredCells();
        Log(Debug::Info) << "Save load world map: bytes=" << job->mBytes.size() << " baseBuildMs=" << mBaseBuildMs
                         << " baseWaitMs=" << baseWait << " pngQueueMs=" << job->mQueueMs
                         << " pngDecodeMs=" << job->mDecodeMs << " pngWaitMs=" << decodeWait
                         << " overlayApplyMs=" << elapsedMs(applyStart);
    }

    void GlobalMap::applySavedImage(osg::ref_ptr<osg::Image> image)
    {
        const auto& bounds = mSavedOverlay->mBounds;
        int imageWidth = image->s();
        int imageHeight = image->t();

        const std::int64_t xLength = std::int64_t(bounds.mMaxX) - bounds.mMinX + 1;
        const std::int64_t yLength = std::int64_t(bounds.mMaxY) - bounds.mMinY + 1;

        // Size of one cell in image space
        const int cellImageSizeSrc = static_cast<int>(imageWidth / xLength);
        if (cellImageSizeSrc <= 0 || imageHeight / yLength != cellImageSizeSrc)
        {
            Log(Debug::Warning) << "Invalid saved world map image dimensions";
            mSavedOverlay = nullptr;
            return;
        }

        // If cell bounds of the currently loaded content and the loaded savegame do not match,
        // we need to resize source/dest boxes to accommodate
        // This means nonexisting cells will be dropped silently
        const int cellImageSizeDst = Settings::map().mGlobalMapCellSize;

        // Completely off-screen? -> no need to blit anything
        if (bounds.mMaxX < mMinX || bounds.mMaxY < mMinY || bounds.mMinX > mMaxX || bounds.mMinY > mMaxY)
        {
            mSavedOverlay = nullptr;
            return;
        }

        const std::int64_t leftDiff = std::int64_t(mMinX) - bounds.mMinX;
        const std::int64_t topDiff = std::int64_t(bounds.mMaxY) - mMaxY;
        const std::int64_t rightDiff = std::int64_t(bounds.mMaxX) - mMaxX;
        const std::int64_t bottomDiff = std::int64_t(mMinY) - bounds.mMinY;
        const auto clip = [](std::int64_t value, int size) {
            return static_cast<int>(std::clamp<std::int64_t>(value, 0, size));
        };
        Box srcBox(clip(leftDiff * cellImageSizeSrc, imageWidth), clip(topDiff * cellImageSizeSrc, imageHeight),
            clip(imageWidth - rightDiff * cellImageSizeSrc, imageWidth),
            clip(imageHeight - bottomDiff * cellImageSizeSrc, imageHeight));

        Box destBox(clip(-leftDiff * cellImageSizeDst, mWidth), clip(-topDiff * cellImageSizeDst, mHeight),
            clip(mWidth + rightDiff * cellImageSizeDst, mWidth), clip(mHeight + bottomDiff * cellImageSizeDst, mHeight));

        osg::ref_ptr<osg::Texture2D> texture(new osg::Texture2D);
        texture->setImage(image);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setResizeNonPowerOfTwoHint(false);

        if (srcBox == destBox && imageWidth == mWidth && imageHeight == mHeight)
        {
            mOverlayImage = image;

            mRestoreCamera = requestOverlayTextureUpdate(0, 0, mWidth, mHeight, std::move(texture), true, false);
        }
        else
        {
            // Dimensions don't match. This could mean a changed map region, or a changed map resolution.
            // In the latter case, we'll want filtering.
            // Create a RTT Camera and draw the image onto mOverlayImage in the next frame.
            mRestoreCamera = requestOverlayTextureUpdate(destBox.mLeft, destBox.mTop, destBox.mRight - destBox.mLeft,
                destBox.mBottom - destBox.mTop, std::move(texture), true, true, srcBox.mLeft / float(imageWidth),
                srcBox.mTop / float(imageHeight), srcBox.mRight / float(imageWidth),
                srcBox.mBottom / float(imageHeight));
        }
    }

    osg::ref_ptr<osg::Texture2D> GlobalMap::getBaseTexture()
    {
        ensureLoaded();
        return mBaseTexture;
    }

    osg::ref_ptr<osg::Texture2D> GlobalMap::getOverlayTexture()
    {
        ensureLoaded();
        return mOverlayTexture;
    }

    void GlobalMap::ensureLoaded()
    {
        if (mWorkItem)
        {
            mWorkItem->waitTillDone();

            mBaseBuildMs = mWorkItem->mBuildMs;
            mOverlayImage = mWorkItem->mOverlayImage;
            mBaseTexture = mWorkItem->mBaseTexture;
            mAlphaTexture = mWorkItem->mAlphaTexture;
            mOverlayTexture = mWorkItem->mOverlayTexture;

            requestOverlayTextureUpdate(0, 0, mWidth, mHeight, osg::ref_ptr<osg::Texture2D>(), true, false);

            mWorkItem = nullptr;
        }
    }

    bool GlobalMap::copyResult(osg::Camera* camera, unsigned int frame)
    {
        ImageDestMap::iterator it = mPendingImageDest.find(camera);
        if (it == mPendingImageDest.end())
        {
            if (camera == mRestoreCamera)
                mRestoreCamera = nullptr;
            return true;
        }
        else
        {
            // A later exploration tile must never be overwritten by the restored full-image readback.
            if (mRestoreCamera && camera != mRestoreCamera)
                return false;
            ImageDest& imageDest = it->second;
            if (imageDest.mFrameDone == 0)
                imageDest.mFrameDone
                    = frame + 2; // wait an extra frame to ensure the draw thread has completed its frame.
            if (imageDest.mFrameDone > frame)
            {
                ++it;
                return false;
            }

            mOverlayImage->copySubImage(imageDest.mX, imageDest.mY, 0, imageDest.mImage);
            if (camera == mRestoreCamera)
                mRestoreCamera = nullptr;
            mSavedOverlay = nullptr;
            mWritePng = nullptr;
            mPendingImageDest.erase(it);
            return true;
        }
    }

    void GlobalMap::markForRemoval(osg::Camera* camera)
    {
        CameraVector::iterator found = std::find(mActiveCameras.begin(), mActiveCameras.end(), camera);
        if (found == mActiveCameras.end())
        {
            Log(Debug::Error) << "Error: GlobalMap trying to remove an inactive camera";
            return;
        }
        mActiveCameras.erase(found);
        mCamerasPendingRemoval.push_back(camera);
    }

    void GlobalMap::cleanupCameras()
    {
        for (auto& camera : mCamerasPendingRemoval)
            removeCamera(camera);

        mCamerasPendingRemoval.clear();
    }

    void GlobalMap::removeCamera(osg::Camera* cam)
    {
        cam->removeChildren(0, cam->getNumChildren());
        mRoot->removeChild(cam);
    }

    void GlobalMap::asyncWritePng()
    {
        if (mSavedOverlay || !mOverlayImage)
            return;
        // Use deep copy to avoid any sychronization
        mWritePng = new WritePng(new osg::Image(*mOverlayImage, osg::CopyOp::DEEP_COPY_ALL));
        mWorkQueue->addWorkItem(mWritePng, /*front=*/true);
    }
}
