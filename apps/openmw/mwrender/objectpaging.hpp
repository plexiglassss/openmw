#ifndef OPENMW_MWRENDER_OBJECTPAGING_H
#define OPENMW_MWRENDER_OBJECTPAGING_H

#include <components/esm3/refnum.hpp>
#include <components/sceneutil/selectionrevision.hpp>
#include <components/resource/resourcemanager.hpp>
#include <components/terrain/quadtreeworld.hpp>
#include <components/terrain/scenerycoverage.hpp>

#include <mutex>

namespace Resource
{
    class SceneManager;
}
namespace SceneUtil { class WorkQueue; }

namespace MWRender
{

    typedef std::tuple<osg::Vec2f, float, bool> ChunkId; // Center, Size, ActiveGrid

    class ObjectPaging : public Resource::NodeResourceManager<ChunkId>, public Terrain::QuadTreeWorld::BatchChunkManager
    {
    public:
        ObjectPaging(Resource::SceneManager* sceneManager, ESM::RefId worldspace);
        ~ObjectPaging();

        osg::ref_ptr<osg::Node> getChunk(float size, const osg::Vec2f& center, unsigned char lod, unsigned int lodFlags,
            bool activeGrid, const osg::Vec3f& viewPoint, bool compile, const std::atomic_bool* abort = nullptr) override;

        using ChunkRequest = Terrain::QuadTreeWorld::ChunkRequest;
        void setWorkQueue(std::shared_ptr<SceneUtil::WorkQueue> queue);
        AsyncResult requestChunk(const ChunkRequest&, const osg::Vec3f&, double referenceTime, bool queue = true) override;
        bool updateAsyncState(double referenceTime) override;
        bool canPrepareAsync() const override;
        bool usesSceneryCoverage() const override;
        void acceptScenery(std::span<const ChunkRequest>, const osg::Vec3f&, const osg::Vec4i&,
            osg::NodeVisitor&, double, std::shared_ptr<Terrain::SceneryView>&,
            const std::shared_ptr<Terrain::SceneryView>&) override;
        std::shared_ptr<Terrain::SceneryView> preloadScenery(std::span<const ChunkRequest>, const osg::Vec3f&,
            const osg::Vec4i&, const std::atomic_bool&, Loading::Reporter&) override;
        void prepareScenery(const ChunkRequest&, const osg::Vec3f&, double) override;
        void clearCache() override;
        void releaseGLObjects(osg::State*) override;
        bool mayIntersectOutsideTile(
            const ChunkRequest& chunk, const osg::Vec3f& viewPoint, const Terrain::RayFootprint& ray) override;
        bool canPreloadBatches() const override;
        void preloadChunks(std::span<const ChunkRequest> chunks, const osg::Vec3f& viewPoint,
            const std::atomic_bool& abort, const Load& load) override;

        unsigned int getNodeMask() override;

        /// @return true if view needs rebuild
        bool enableObject(int type, ESM::RefNum refnum, const osg::Vec3f& pos, const osg::Vec2i& cell, bool enabled);

        /// @return true if view needs rebuild
        bool blacklistObject(int type, ESM::RefNum refnum, const osg::Vec3f& pos, const osg::Vec2i& cell);

        void clear();

        /// Must be called after clear() before rendering starts.
        /// @return true if view needs rebuild
        bool unlockCache();

        void reportStats(unsigned int frameNumber, osg::Stats* stats) const override;

        void getPagedRefnums(const osg::Vec4i& activeGrid, std::vector<ESM::RefNum>& out);

    private:
        struct AsyncState;
        std::unique_ptr<AsyncState> mAsync;
        struct SceneryState;
        struct ScenerySnapshot;
        struct SceneryBridge;
        std::unique_ptr<SceneryState> mScenery;
        osg::ref_ptr<SceneryBridge> getSceneryBridge(Terrain::SceneryCell, const std::atomic_bool*, bool required);
        void invalidateScenery(const osg::Vec3f&, const osg::Vec2i&);
        struct Selection;
        struct PendingCompile;
        Selection selectChunk(const ChunkRequest& chunk, const osg::Vec3f& viewPoint, const std::atomic_bool* abort,
            bool bridge = false);
        osg::ref_ptr<osg::Node> fetchChunk(const ChunkRequest& chunk, const osg::Vec3f& viewPoint, bool compile,
            const std::atomic_bool* abort, const Selection* selected, bool modelsPrepared);
        osg::ref_ptr<osg::Node> createChunk(const ChunkRequest& chunk, const osg::Vec3f& viewPoint, const Selection& selected,
            bool compile, const std::atomic_bool* abort, bool modelsPrepared, PendingCompile& pending,
            bool bridge = false);
        std::atomic_uint64_t mSelectionRevision{ 0 };
        SceneUtil::SelectionRevision mSelectionRevisions; // mRefTrackerMutex
        std::uint64_t selectionRevision(const ChunkId&);
        std::uint64_t selectionRevisionLocked(const ChunkId&) const;
        std::atomic_uint64_t mGraphicsRevision{ 0 };
        using CacheEpoch = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
        CacheEpoch cacheEpoch() const;
        // Protected by mRefTrackerMutex, including clear/publication.
        CacheEpoch mCacheEpoch{};
        void refreshCacheEpoch();
        osg::ref_ptr<osg::Node> getCachedChunk(const ChunkId& id);
        struct ReferenceCache;
        std::unique_ptr<ReferenceCache> mReferenceCache;
        Resource::SceneManager* mSceneManager;
        bool mActiveGrid;
        bool mDebugBatches;
        float mMergeFactor;
        float mMinSize;
        float mMinSizeMergeFactor;
        float mMinSizeCostMultiplier;

        std::mutex mRefTrackerMutex;
        struct RefTracker
        {
            std::set<ESM::RefNum> mDisabled;
            std::set<ESM::RefNum> mBlacklist;
            bool operator==(const RefTracker& other) const
            {
                return mDisabled == other.mDisabled && mBlacklist == other.mBlacklist;
            }
        };
        RefTracker mRefTracker;
        RefTracker mRefTrackerNew;
        bool mRefTrackerLocked;

        const RefTracker& getRefTracker() const { return mRefTracker; }
        RefTracker& getWritableRefTracker() { return mRefTrackerLocked ? mRefTrackerNew : mRefTracker; }

        std::mutex mSizeCacheMutex;
        using SizeCache = std::map<ESM::RefNum, std::pair<float, CacheEpoch>>;
        SizeCache mSizeCache;

        std::mutex mLODNameCacheMutex;
        typedef std::pair<std::string, unsigned char> LODNameCacheKey; // Key: mesh name, lod level
        using LODNameCache = std::map<LODNameCacheKey, VFS::Path::Normalized>; // Cache: key, mesh name to use
        LODNameCache mLODNameCache;
        std::uint64_t mLODNameCacheRevision = 0;
    };

    struct RefnumMarker
    {
        ESM::RefNum mRefnum;
        unsigned int mNumVertices = 0;
    };
}

#endif
