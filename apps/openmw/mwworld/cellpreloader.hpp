#ifndef OPENMW_MWWORLD_CELLPRELOADER_H
#define OPENMW_MWWORLD_CELLPRELOADER_H

#include "positioncellgrid.hpp"

#include <components/sceneutil/workqueue.hpp>
#include <components/sceneutil/preparationcost.hpp>
#include <components/esm/exteriorcelllocation.hpp>

#include <map>
#include <memory>
#include <span>

namespace Misc
{
    class FrameStats;
}

namespace Resource
{
    class ResourceSystem;
    class BulletShapeManager;
}

namespace Terrain
{
    class World;
    class View;
    class ArrivalView;
}

namespace MWRender
{
    class LandManager;
}
namespace MWPhysics { class PhysicsSystem; }

namespace Loading
{
    class Listener;
}

namespace MWWorld
{
    class CellStore;
    class TerrainPreloadItem;
    class ExteriorArrivalJob;
    class PreparedCell;

    class CellPreloader
    {
    public:
        CellPreloader(Resource::ResourceSystem* resourceSystem, Resource::BulletShapeManager* bulletShapeManager,
            Terrain::World* terrain, MWRender::LandManager* landManager, MWPhysics::PhysicsSystem* physics = nullptr);
        ~CellPreloader();

        /// Ask a background thread to preload rendering meshes and collision shapes for objects in this cell.
        /// @note The cell itself must be in State_Loaded or State_Preloaded.
        void preload(MWWorld::CellStore& cell, double timestamp);
        void prioritize(const MWWorld::CellStore& cell, bool required = false);
        void preloadStaticExterior(ESM::ExteriorCellLocation cell, double timestamp);
        double preparationLeadTime(double configured);

        void notifyLoaded(MWWorld::CellStore* cell);
        void prepareCell(MWWorld::CellStore& cell, double timestamp);
        std::shared_ptr<PreparedCell> takePreparedCell(MWWorld::CellStore& cell, double timestamp);
        void recordPreparedCellUsage(const PreparedCell& cell);
        void stagePreparedCells();

        void clear();

        /// Removes preloaded cells that have not had a preload request for a while.
        void updateCache(double timestamp);

        /// How long to keep a preloaded cell in cache after it's no longer requested.
        void setExpiryDelay(double expiryDelay);

        /// The minimum number of preloaded cells before unused cells get thrown out.
        void setMinCacheSize(std::size_t value) { mMinCacheSize = value; }

        /// The maximum number of preloaded cells.
        void setMaxCacheSize(std::size_t value) { mMaxCacheSize = value; }

        /// Enables the creation of instances in the preloading thread.
        void setPreloadInstances(bool preload);

        std::size_t getMaxCacheSize() const { return mMaxCacheSize; }

        std::size_t getCacheSize() const { return mPreloadCells.size(); }

        void setWorkQueue(std::shared_ptr<SceneUtil::WorkQueue> workQueue);
        void setPreparedWorkQueue(std::shared_ptr<SceneUtil::WorkQueue> workQueue);

        void setTerrainPreloadPositions(std::span<const PositionCellGrid> positions);

        void syncTerrainLoad(Loading::Listener& listener);
        void cancelPrediction() { retireArrival(); }
        void updateArrivalGrid(Terrain::World*, const osg::Vec4i&);
        void predictArrival(Terrain::World* world, const PositionCellGrid& target);
        bool ensureArrival(Terrain::World* world, const PositionCellGrid& target, Loading::Listener& listener,
            double timestamp);
        bool keepStreamingArrival(std::span<const PositionCellGrid> targets);
        void leaveExterior(Terrain::World* world, const PositionCellGrid& current, double timestamp);
        void clearArrivals();

        void abortTerrainPreloadExcept(const PositionCellGrid* exceptPos);
        bool isTerrainLoaded(const PositionCellGrid& position, double referenceTime) const;
        void setTerrain(Terrain::World* terrain);

        void reportStats(unsigned int frameNumber, Misc::FrameStats& stats) const;

    private:
        void clearAllTasks();
        struct StaticPreloads;
        struct PreparedCells;
        std::unique_ptr<PreparedCells> mPreparedCells;
        std::unique_ptr<StaticPreloads> mStaticPreloads;
        SceneUtil::PreparationCost mPreparationCost;
        void retireArrival();
        bool startArrival(Terrain::World* world, const PositionCellGrid& target, bool required);
        std::shared_ptr<ExteriorArrivalJob> mArrivalJob;
        std::vector<std::shared_ptr<ExteriorArrivalJob>> mRetiredArrivals;
        Terrain::World* mArrivalWorld = nullptr;
        Terrain::World* mReturnWorld = nullptr;
        std::shared_ptr<Terrain::ArrivalView> mReturnView;
        double mReturnTime = 0;


        Resource::ResourceSystem* mResourceSystem;
        Resource::BulletShapeManager* mBulletShapeManager;
        MWPhysics::PhysicsSystem* mPhysics;
        Terrain::World* mTerrain;
        MWRender::LandManager* mLandManager;
        std::shared_ptr<SceneUtil::WorkQueue> mWorkQueue;
        std::shared_ptr<SceneUtil::WorkQueue> mPreparedWorkQueue;
        double mExpiryDelay;
        std::size_t mMinCacheSize = 0;
        std::size_t mMaxCacheSize = 0;
        bool mPreloadInstances;

        double mLastResourceCacheUpdate;

        struct PreloadEntry
        {
            PreloadEntry(double timestamp, std::shared_ptr<SceneUtil::WorkItem> workItem,
                std::shared_ptr<SceneUtil::PreparationSample> timing = {})
                : mTimeStamp(timestamp)
                , mWorkItem(std::move(workItem))
                , mTiming(std::move(timing))
            {
            }
            PreloadEntry()
                : mTimeStamp(0.0)
            {
            }

            double mTimeStamp;
            std::shared_ptr<SceneUtil::WorkItem> mWorkItem;
            std::shared_ptr<SceneUtil::PreparationSample> mTiming;
            double mPreparationSeconds = 0;

            double preparationSeconds() const
            {
                if (mTiming && mTiming->mComplete.load(std::memory_order_acquire) && mTiming->mSuccessful)
                    return mTiming->mWorkSeconds;
                return mPreparationSeconds;
            }
        };
        typedef std::map<const MWWorld::CellStore*, PreloadEntry> PreloadMap;

        // Cells that are currently being preloaded, or have already finished preloading
        PreloadMap mPreloadCells;

        std::vector<std::shared_ptr<Terrain::View>> mTerrainViews;
        std::vector<PositionCellGrid> mTerrainPreloadPositions;
        std::shared_ptr<TerrainPreloadItem> mTerrainPreloadItem;
        std::shared_ptr<SceneUtil::WorkItem> mUpdateCacheItem;

        std::vector<PositionCellGrid> mLoadedTerrainPositions;
        double mLoadedTerrainTimestamp;
        std::size_t mEvicted = 0;
        std::size_t mAdded = 0;
        std::size_t mExpired = 0;
        std::size_t mLoaded = 0;
        std::size_t mCostRetentions = 0;
    };

}

#endif
