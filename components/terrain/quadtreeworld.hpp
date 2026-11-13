#ifndef COMPONENTS_TERRAIN_QUADTREEWORLD_H
#define COMPONENTS_TERRAIN_QUADTREEWORLD_H

#include "terraingrid.hpp"

#include <atomic>
#include <functional>
#include <span>
#include <memory>
#include <mutex>

#include <components/esm/refid.hpp>

namespace osg
{
    class NodeVisitor;
    class Group;
    class Stats;
}

namespace Terrain
{
    class RootNode;
    class ViewDataMap;
    class ViewData;
    struct ViewDataEntry;

    class DebugChunkManager;
    struct RayFootprint;

    /// @brief Terrain implementation that loads cells into a Quad Tree, with geometry LOD and texture LOD.
    class QuadTreeWorld
        : public TerrainGrid // note: derived from TerrainGrid is only to render default cells (see loadCell)
    {
    public:
        QuadTreeWorld(osg::Group* parent, osg::Group* compileRoot, Resource::ResourceSystem* resourceSystem,
            Storage* storage, unsigned int nodeMask, unsigned int preCompileMask, unsigned int borderMask,
            int compMapResolution, float comMapLevel, float lodFactor, int vertexLodMod, float maxCompGeometrySize,
            bool debugChunks, ESM::RefId worldspace, double expiryDelay);

        ~QuadTreeWorld();

        void accept(osg::NodeVisitor& nv);

        void enable(bool enabled) override;

        void setViewDistance(float distance) override;

        void cacheCell(View* view, int x, int y) override {}
        /// @note Not thread safe.
        void loadCell(int x, int y) override;
        /// @note Not thread safe.
        void unloadCell(int x, int y) override;

        View* createView() override;
        void preload(View* view, const osg::Vec3f& eyePoint, const osg::Vec4i& cellgrid, std::atomic<bool>& abort,
            Loading::Reporter& reporter) override;
        std::shared_ptr<ArrivalView> createArrival(const osg::Vec3f&, const osg::Vec4i&) override;
        std::shared_ptr<ArrivalView> captureArrival(const osg::Vec3f&, const osg::Vec4i&, std::size_t) override;
        bool validArrival(const ArrivalView&) const override;
        void showArrival(std::shared_ptr<ArrivalView>) override;
        void preloadArrival(View*, const std::shared_ptr<ArrivalView>&, std::atomic_bool&,
            Loading::Reporter&, const ArrivalView::Progress&) override;
        void clearAssociatedCaches() override;
        void rebuildViews() override;

        void reportStats(unsigned int frameNumber, osg::Stats* stats) override;

        struct ChunkRequest
        {
            float mSize;
            osg::Vec2f mCenter;
            unsigned int mLodFlags;
            bool mActiveGrid;
        };

        class ChunkManager
        {
        public:
            virtual ~ChunkManager() = default;
            ChunkManager() = default;
            ChunkManager(ESM::RefId worldspace)
                : ChunkManager()
            {
                mWorldspace = worldspace;
            }
            virtual osg::ref_ptr<osg::Node> getChunk(float size, const osg::Vec2f& center, unsigned char lod,
                unsigned int lodFlags, bool activeGrid, const osg::Vec3f& viewPoint, bool compile,
                const std::atomic_bool* abort = nullptr)
                = 0;
            virtual unsigned int getNodeMask() { return 0; }
            // Called only after the segment misses the landscape tile itself. Other
            // managers can have meshes extending outside that tile; unknown means keep.
            virtual bool mayIntersectOutsideTile(const ChunkRequest&, const osg::Vec3f&, const RayFootprint&)
            {
                return true;
            }

            void setViewDistance(float viewDistance) { mViewDistance = viewDistance; }
            float getViewDistance() const { return mViewDistance; }

            // Automatically set by addChunkManager based on getViewDistance()
            unsigned int getMaxLodLevel() const { return mMaxLodLevel; }
            void setMaxLodLevel(unsigned int level) { mMaxLodLevel = level; }

        protected:
            ESM::RefId mWorldspace = ESM::RefId();

        private:
            float mViewDistance = 0.f;
            unsigned int mMaxLodLevel = ~0u;
        };

        // A batch may prepare private resources ahead of use. The loader still requests each
        // scene node at its original position in the ordered terrain traversal.
        class BatchChunkManager : public ChunkManager
        {
        public:
            using ChunkManager::ChunkManager;
            using Fetch = std::function<osg::ref_ptr<osg::Node>()>;
            using Load = std::function<void(std::size_t, const Fetch&)>;
            virtual bool canPreloadBatches() const = 0;
            virtual void preloadChunks(std::span<const ChunkRequest> chunks, const osg::Vec3f& viewPoint,
                const std::atomic_bool& abort, const Load& load) = 0;
        };
        void addChunkManager(ChunkManager*);
        void addBatchChunkManager(BatchChunkManager*);


    private:
        void ensureQuadTreeBuilt();
        void prepareView(View*, const osg::Vec3f&, const osg::Vec4i&, std::atomic_bool&, Loading::Reporter&,
            const std::shared_ptr<ArrivalView>&, const ArrivalView::Progress&);
        std::atomic_uint64_t mArrivalRevision{ 0 };
        std::atomic<std::shared_ptr<ArrivalView>> mVisibleArrival;
        void updateRenderingLod(ViewDataEntry& entry, ViewData* vd);
        ChunkRequest getChunkRequest(const ViewDataEntry& entry, const osg::Vec4i& gridbounds) const;
        void loadRenderingNode(ViewDataEntry& entry, ViewData* vd, float cellWorldSize, const ChunkRequest& request,
            bool compile, const std::atomic_bool* abort = nullptr, const BatchChunkManager::Fetch* fetch = nullptr);
        BatchChunkManager* mBatchChunkManager = nullptr;

        osg::ref_ptr<RootNode> mRootNode;

        osg::ref_ptr<ViewDataMap> mViewDataMap;

        std::vector<ChunkManager*> mChunkManagers;

        std::mutex mQuadTreeMutex;
        bool mQuadTreeBuilt;
        float mLodFactor;
        int mVertexLodMod;
        std::atomic<float> mViewDistance;
        float mMinSize;
        bool mDebugTerrainChunks;
        std::unique_ptr<DebugChunkManager> mDebugChunkManager;
    };

}

#endif
