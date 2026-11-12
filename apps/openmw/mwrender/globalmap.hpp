#ifndef GAME_RENDER_GLOBALMAP_H
#define GAME_RENDER_GLOBALMAP_H

#include <map>
#include <string>
#include <vector>

#include <osg/ref_ptr>

namespace osg
{
    class Texture2D;
    class Image;
    class Group;
    class Camera;
}

namespace ESM
{
    struct GlobalMap;
}

namespace SceneUtil
{
    class WorkQueue;
}

namespace MWRender
{

    class CreateMapWorkItem;

    class GlobalMap
    {
    public:
        GlobalMap(osg::Group* root, SceneUtil::WorkQueue* workQueue);
        ~GlobalMap();

        void render();

        int getWidth() const { return mWidth; }
        int getHeight() const { return mHeight; }

        void worldPosToImageSpace(float x, float z, float& imageX, float& imageY);

        void exploreCell(int cellX, int cellY, osg::ref_ptr<osg::Texture2D> localMapTexture);

        /// Clears the overlay
        void clear();

        /**
         * Removes cameras that have already been rendered. Should be called every frame to ensure that
         * we do not render the same map more than once. Note, this cleanup is difficult to implement in an
         * automated fashion, since we can't alter the scene graph structure from within an update callback.
         */
        void cleanupCameras();

        void removeCamera(osg::Camera* cam);

        bool copyResult(osg::Camera* cam, unsigned int frame);

        /**
         * Mark a camera for cleanup in the next update. For internal use only.
         */
        void markForRemoval(osg::Camera* camera);

        void write(ESM::GlobalMap& map);
        /// Takes ownership of the encoded image. Markers stay with the caller.
        void read(ESM::GlobalMap& map);
        /// Publish the restoration on the scene owner before returning control to gameplay.
        void finishRestore();
        bool readyForDisplay() const;
        void update();

        osg::ref_ptr<osg::Texture2D> getBaseTexture();
        osg::ref_ptr<osg::Texture2D> getOverlayTexture();

        void ensureLoaded();

        void asyncWritePng();

    private:
        struct WritePng;
        struct ReadPng;
        void applySavedImage(osg::ref_ptr<osg::Image> image);
        void flushExploredCells();

        /**
         * Request rendering a 2d quad onto mOverlayTexture.
         * x, y, width and height are the destination coordinates (top-left coordinate origin)
         * @param cpuCopy copy the resulting render onto mOverlayImage as well?
         */
        osg::Camera* requestOverlayTextureUpdate(int x, int y, int width, int height, osg::ref_ptr<osg::Texture2D> texture,
            bool clear, bool cpuCopy, float srcLeft = 0.f, float srcTop = 0.f, float srcRight = 1.f,
            float srcBottom = 1.f);

        osg::ref_ptr<osg::Group> mRoot;

        typedef std::vector<osg::ref_ptr<osg::Camera>> CameraVector;
        CameraVector mActiveCameras;

        CameraVector mCamerasPendingRemoval;

        struct ImageDest
        {
            ImageDest()
                : mX(0)
                , mY(0)
                , mFrameDone(0)
            {
            }

            osg::ref_ptr<osg::Image> mImage;
            int mX, mY;
            unsigned int mFrameDone;
        };

        typedef std::map<osg::ref_ptr<osg::Camera>, ImageDest> ImageDestMap;

        ImageDestMap mPendingImageDest;

        osg::ref_ptr<osg::Texture2D> mBaseTexture;
        osg::ref_ptr<osg::Texture2D> mAlphaTexture;

        // GPU copy of overlay
        // Note, uploads are pushed through a Camera, instead of through mOverlayImage
        osg::ref_ptr<osg::Texture2D> mOverlayTexture;

        // CPU copy of overlay
        osg::ref_ptr<osg::Image> mOverlayImage;

        osg::ref_ptr<SceneUtil::WorkQueue> mWorkQueue;
        osg::ref_ptr<CreateMapWorkItem> mWorkItem;
        osg::ref_ptr<WritePng> mWritePng;
        // Original PNG and original bounds are one save snapshot, including during a resized GPU readback.
        osg::ref_ptr<ReadPng> mSavedOverlay;
        std::map<std::pair<int, int>, osg::ref_ptr<osg::Texture2D>> mQueuedExploration;
        osg::Camera* mRestoreCamera = nullptr;
        bool mRestorePending = false;
        double mBaseBuildMs = 0;

        int mWidth;
        int mHeight;

        int mMinX, mMaxX, mMinY, mMaxY;
    };

}

#endif
