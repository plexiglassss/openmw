#include "cellpreloader.hpp"

#include <chrono>
#include <components/esm/util.hpp>

#include <components/diagnostics/timing.hpp>
#include <components/diagnostics/loading.hpp>

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <span>

#include <osg/Stats>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/loadinglistener/reporter.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/keyframemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/terrain/view.hpp>
#include <components/terrain/world.hpp>
#include <components/vfs/manager.hpp>

#include "../mwrender/landmanager.hpp"

#include "cellstore.hpp"
#include "class.hpp"

namespace MWWorld
{
    namespace
    {
        bool contains(std::span<const PositionCellGrid> positions, const PositionCellGrid& contained, float tolerance)
        {
            const float squaredTolerance = tolerance * tolerance;
            const auto predicate = [&](const PositionCellGrid& v) {
                return (contained.mPosition - v.mPosition).length2() < squaredTolerance
                    && contained.mCellBounds == v.mCellBounds;
            };
            return std::ranges::any_of(positions, predicate);
        }

        bool contains(
            std::span<const PositionCellGrid> container, std::span<const PositionCellGrid> contained, float tolerance)
        {
            const auto predicate = [&](const PositionCellGrid& v) { return contains(container, v, tolerance); };
            return std::ranges::all_of(contained, predicate);
        }
    }

    struct ListModelsVisitor
    {
        bool operator()(const MWWorld::ConstPtr& ptr)
        {
            ptr.getClass().getModelsToPreload(ptr, mOut);

            return true;
        }

        std::vector<VFS::Path::NormalizedView>& mOut;
    };

    /// Worker thread item: preload models in a cell.
    class PreloadItem : public SceneUtil::WorkItem
    {
    public:
        /// Constructor to be called from the main thread.
        explicit PreloadItem(MWWorld::CellStore* cell, Resource::SceneManager* sceneManager,
            Resource::BulletShapeManager* bulletShapeManager, Resource::KeyframeManager* keyframeManager,
            Terrain::World* terrain, MWRender::LandManager* landManager, bool preloadInstances)
            : mIsExterior(cell->getCell()->isExterior())
            , mCellLocation(cell->getCell()->getExteriorCellLocation())
            , mCellId(cell->getCell()->getId())
            , mSceneManager(sceneManager)
            , mBulletShapeManager(bulletShapeManager)
            , mKeyframeManager(keyframeManager)
            , mTerrain(terrain)
            , mLandManager(landManager)
            , mPreloadInstances(preloadInstances)
            , mAbort(false)
        {
            mTerrainView = mTerrain->createView();

            ListModelsVisitor visitor{ mMeshes };
            cell->forEachConst(visitor);
        }

        void abort() override { mAbort = true; }

        /// Preload work to be called from the worker thread.
        void doWork() override
        {
            if (mIsExterior)
            {
                try
                {
                    mTerrain->cacheCell(mTerrainView.get(), mCellLocation.mX, mCellLocation.mY);
                    mPreloadedObjects.insert(mLandManager->getLand(mCellLocation));
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to cache terrain for exterior cell " << mCellLocation << ": "
                                        << e.what();
                }
            }

            // Job-local: paths and the VFS are stable for this preload snapshot.
            // Preserve one collision instance per occurrence, but resolve each source path once.
            std::map<VFS::Path::NormalizedView, VFS::Path::Normalized> resolvedMeshes;
            std::set<VFS::Path::Normalized> loadedMeshes;
            VFS::Path::Normalized mesh;
            VFS::Path::Normalized kfname;
            for (VFS::Path::NormalizedView path : mMeshes)
            {
                if (mAbort)
                    break;

                try
                {
                    const VFS::Manager& vfs = *mSceneManager->getVFS();
                    auto resolved = resolvedMeshes.find(path);
                    if (resolved == resolvedMeshes.end())
                    {
                        auto corrected = Misc::ResourceHelpers::correctMeshPath(path);
                        corrected = Misc::ResourceHelpers::correctActorModelPath(corrected, &vfs);
                        if (!vfs.exists(corrected))
                            corrected = VFS::Path::Normalized{};
                        resolved = resolvedMeshes.emplace(path, std::move(corrected)).first;
                    }
                    mesh = resolved->second;

                    if (mesh.empty())
                        continue;

                    if (loadedMeshes.contains(mesh))
                    {
                        if (mPreloadInstances)
                            mPreloadedObjects.insert(mBulletShapeManager->cacheInstance(mesh));
                        continue;
                    }

                    constexpr VFS::Path::ExtensionView nif("nif");
                    if (Misc::getFileName(mesh).starts_with('x') && mesh.extension() == nif)
                    {
                        kfname = mesh;
                        constexpr VFS::Path::ExtensionView kf("kf");
                        kfname.changeExtension(kf);
                        if (vfs.exists(kfname))
                            mPreloadedObjects.insert(mKeyframeManager->get(kfname));
                    }

                    mPreloadedObjects.insert(mSceneManager->getTemplate(mesh));
                    if (mPreloadInstances)
                        mPreloadedObjects.insert(mBulletShapeManager->cacheInstance(mesh));
                    else
                        mPreloadedObjects.insert(mBulletShapeManager->getShape(mesh));
                    loadedMeshes.insert(mesh);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to preload mesh \"" << path << "\" from cell " << mCellId << ": "
                                        << e.what();
                }
            }
        }

    private:
        bool mIsExterior;
        ESM::ExteriorCellLocation mCellLocation;
        ESM::RefId mCellId;
        std::vector<VFS::Path::NormalizedView> mMeshes;
        Resource::SceneManager* mSceneManager;
        Resource::BulletShapeManager* mBulletShapeManager;
        Resource::KeyframeManager* mKeyframeManager;
        Terrain::World* mTerrain;
        MWRender::LandManager* mLandManager;
        bool mPreloadInstances;

        std::atomic<bool> mAbort;

        osg::ref_ptr<Terrain::View> mTerrainView;

        // keep a ref to the loaded objects to make sure it stays loaded as long as this cell is in the preloaded state
        std::set<osg::ref_ptr<const osg::Object>> mPreloadedObjects;
    };

    class TerrainPreloadItem : public SceneUtil::WorkItem
    {
    public:
        explicit TerrainPreloadItem(const std::vector<osg::ref_ptr<Terrain::View>>& views, Terrain::World* world,
            std::span<const PositionCellGrid> preloadPositions)
            : mAbort(false)
            , mTerrainViews(views)
            , mWorld(world)
            , mPreloadPositions(preloadPositions.begin(), preloadPositions.end())
        {
        }

        void doWork() override
        {
            Diagnostics::Scope workTiming("terrain", "worker", "terrainJob");
            {
                Diagnostics::Loading::JobRun jobRun(mTiming, mAbort);
                for (unsigned int i = 0; i < mTerrainViews.size() && i < mPreloadPositions.size() && !mAbort; ++i)
                {
                    {
                        Diagnostics::Loading::Timer timer(Diagnostics::Loading::Stage::ViewReset);
                        mTerrainViews[i]->reset();
                    }
                    mWorld->preload(mTerrainViews[i], mPreloadPositions[i].mPosition, mPreloadPositions[i].mCellBounds,
                        mAbort, mLoadingReporter);
                }
            } // Publish the worker snapshot before notifying the waiting main thread.
            mLoadingReporter.complete();
        }

        void abort() override { mAbort = true; }

        void markEnqueued() { mTiming.enqueued(); }

        void wait(Loading::Listener& listener, Loading::Reporter::WaitStats& stats) const
        {
            mLoadingReporter.waitWithStats(listener, stats);
        }

        void reportTimings(const Loading::Reporter::WaitStats& wait, bool waitRequested) const
        {
            const auto job = mTiming.snapshot();
            Log(Debug::Info) << "Terrain preload detail: job=" << job.id << " enqueued=" << job.enqueueRecorded
                             << " started=" << job.started << " finished=" << job.finished
                             << " abortRequested=" << job.abortRequested << " exception=" << job.exception
                             << " enqueueToStartMs=" << job.enqueueToStartMs << " workMs=" << job.workMs
                             << " waitRequested=" << waitRequested << " mainWaitMs=" << wait.elapsedMs
                             << " initialLockMs=" << wait.initialLockMs << " listenerMs=" << wait.listenerMs
                             << " conditionMs=" << wait.conditionMs << " progressUpdates=" << wait.progressUpdates
                             << " conditionWaits=" << wait.conditionWaits
                             << " (worker stages inclusive; worker and main wait overlap; condition includes relock)"
                             << job.metrics;
        }

    private:
        std::atomic<bool> mAbort;
        std::vector<osg::ref_ptr<Terrain::View>> mTerrainViews;
        Terrain::World* mWorld;
        std::vector<PositionCellGrid> mPreloadPositions;
        Loading::Reporter mLoadingReporter;
        Diagnostics::Loading::Job mTiming;
    };

    /// Worker thread item: update the resource system's cache, effectively deleting unused entries.
    class ExteriorArrivalJob : public SceneUtil::WorkItem
    {
        std::atomic_bool mAbort{ false };
        std::atomic_bool mReady{ false };
        Loading::Reporter mProgress;
        Terrain::World* mWorld;
        osg::ref_ptr<Terrain::View> mWorkView;
    public:
        std::shared_ptr<Terrain::ArrivalView> mView;
        ExteriorArrivalJob(Terrain::World* world, std::shared_ptr<Terrain::ArrivalView> view)
            : mWorld(world), mWorkView(world->createView()), mView(std::move(view)) {}
        void abort() override { mAbort = true; }
        bool matches(Terrain::World* world, const PositionCellGrid& pos, float tolerance) const
        {
            return mWorld == world && !mAbort && world->validArrival(*mView)
                && mView->matches(pos.mPosition, pos.mCellBounds, tolerance, mView->mRevision);
        }
        bool finishedNear() const { return mReady.load(); }
        bool waitNear(Loading::Listener& listener)
        {
            mProgress.wait(listener);
            return mReady && !mAbort && mWorld->validArrival(*mView);
        }
        void doWork() override
        {
            using Clock = std::chrono::steady_clock;
            const auto begin = Clock::now();
            const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(Clock::now()-begin).count(); };
            try
            {
                mWorld->preloadArrival(mWorkView, mView, mAbort, mProgress, [&](std::size_t near, std::size_t far) {
                    if (!mAbort && mWorld->validArrival(*mView))
                    {
                        mReady = true;
                        Log(Debug::Info) << "Exterior arrival near ready: nearChunks=" << near
                            << " distantChunks=" << far << " workerMs=" << elapsed();
                    }
                    mProgress.complete();
                });
                if (mView->complete() && !mAbort)
                    Log(Debug::Info) << "Exterior arrival distant ready: workerMs=" << elapsed()
                        << " estimatedBytes=" << mView->bytes();
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Exterior arrival preparation stopped: " << e.what();
            }
            catch (...)
            {
                Log(Debug::Warning) << "Exterior arrival preparation stopped with an unknown error";
            }
            // A failed or interrupted far pass must allow the normal visitor to complete missing entries.
            if (!mView->complete())
                mView->finish(false);
            mProgress.complete();
        }
    };

    class UpdateCacheItem : public SceneUtil::WorkItem
    {
    public:
        UpdateCacheItem(Resource::ResourceSystem* resourceSystem, double referenceTime)
            : mReferenceTime(referenceTime)
            , mResourceSystem(resourceSystem)
        {
        }

        void doWork() override { mResourceSystem->updateCache(mReferenceTime); }

    private:
        double mReferenceTime;
        Resource::ResourceSystem* mResourceSystem;
    };

    CellPreloader::CellPreloader(Resource::ResourceSystem* resourceSystem,
        Resource::BulletShapeManager* bulletShapeManager, Terrain::World* terrain, MWRender::LandManager* landManager)
        : mResourceSystem(resourceSystem)
        , mBulletShapeManager(bulletShapeManager)
        , mTerrain(terrain)
        , mLandManager(landManager)
        , mExpiryDelay(0.0)
        , mPreloadInstances(true)
        , mLastResourceCacheUpdate(0.0)
        , mLoadedTerrainTimestamp(0.0)
    {
    }

    CellPreloader::~CellPreloader()
    {
        clearArrivals();
        clearAllTasks();
    }

    void CellPreloader::preload(CellStore& cell, double timestamp)
    {
        if (!mWorkQueue)
        {
            Log(Debug::Error) << "Error: can't preload, no work queue set";
            return;
        }
        if (cell.getState() == CellStore::State_Unloaded)
        {
            Log(Debug::Error) << "Error: can't preload objects for unloaded cell";
            return;
        }

        PreloadMap::iterator found = mPreloadCells.find(&cell);
        if (found != mPreloadCells.end())
        {
            // already preloaded, nothing to do other than updating the timestamp
            found->second.mTimeStamp = timestamp;
            return;
        }

        // A disabled cache cannot admit new jobs (or choose an oldest entry).
        if (mMaxCacheSize == 0)
            return;

        while (mPreloadCells.size() >= mMaxCacheSize)
        {
            // throw out oldest cell to make room
            PreloadMap::iterator oldestCell = mPreloadCells.begin();
            double oldestTimestamp = std::numeric_limits<double>::max();
            double threshold = 1.0; // seconds
            for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end(); ++it)
            {
                if (it->second.mTimeStamp < oldestTimestamp)
                {
                    oldestTimestamp = it->second.mTimeStamp;
                    oldestCell = it;
                }
            }

            if (oldestTimestamp + threshold < timestamp)
            {
                oldestCell->second.mWorkItem->abort();
                mPreloadCells.erase(oldestCell);
                ++mEvicted;
            }
            else
                return;
        }

        osg::ref_ptr<PreloadItem> item(new PreloadItem(&cell, mResourceSystem->getSceneManager(), mBulletShapeManager,
            mResourceSystem->getKeyframeManager(), mTerrain, mLandManager, mPreloadInstances));
        mWorkQueue->addWorkItem(item);

        mPreloadCells.emplace(&cell, PreloadEntry(timestamp, item));
        ++mAdded;
    }

    void CellPreloader::notifyLoaded(CellStore* cell)
    {
        PreloadMap::iterator found = mPreloadCells.find(cell);
        if (found != mPreloadCells.end())
        {
            if (found->second.mWorkItem)
            {
                found->second.mWorkItem->abort();
                found->second.mWorkItem = nullptr;
            }

            mPreloadCells.erase(found);
            ++mLoaded;
        }
    }

    void CellPreloader::clear()
    {
        clearArrivals();
        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end();)
        {
            if (it->second.mWorkItem)
            {
                it->second.mWorkItem->abort();
                it->second.mWorkItem = nullptr;
            }

            mPreloadCells.erase(it++);
        }
    }

    void CellPreloader::updateCache(double timestamp)
    {
        std::erase_if(mRetiredArrivals, [](const auto& job) { return job->isDone(); });
        if (mReturnView && (timestamp - mReturnTime > 120.0 || !mReturnWorld->validArrival(*mReturnView)))
        {
            mReturnWorld = nullptr;
            mReturnView.reset();
        }
        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end();)
        {
            if (mPreloadCells.size() >= mMinCacheSize && it->second.mTimeStamp < timestamp - mExpiryDelay)
            {
                if (it->second.mWorkItem)
                {
                    it->second.mWorkItem->abort();
                    it->second.mWorkItem = nullptr;
                }
                mPreloadCells.erase(it++);
                ++mExpired;
            }
            else
                ++it;
        }

        if (timestamp - mLastResourceCacheUpdate > 1.0 && (!mUpdateCacheItem || mUpdateCacheItem->isDone()))
        {
            // the resource cache is cleared from the worker thread so that we're not holding up the main thread with
            // delete operations
            mUpdateCacheItem = new UpdateCacheItem(mResourceSystem, timestamp);
            mWorkQueue->addWorkItem(mUpdateCacheItem, true);
            mLastResourceCacheUpdate = timestamp;
        }

        if (mTerrainPreloadItem && mTerrainPreloadItem->isDone())
        {
            mLoadedTerrainPositions = mTerrainPreloadPositions;
            mLoadedTerrainTimestamp = timestamp;
        }
    }

    void CellPreloader::setExpiryDelay(double expiryDelay)
    {
        mExpiryDelay = expiryDelay;
    }

    void CellPreloader::setPreloadInstances(bool preload)
    {
        mPreloadInstances = preload;
    }

    void CellPreloader::setWorkQueue(osg::ref_ptr<SceneUtil::WorkQueue> workQueue)
    {
        mWorkQueue = workQueue;
    }

    void CellPreloader::retireArrival()
    {
        if (mArrivalWorld)
            mArrivalWorld->showArrival({});
        if (mArrivalJob)
        {
            if (!mArrivalJob->isDone())
            {
                mArrivalJob->abort();
                mRetiredArrivals.push_back(mArrivalJob);
            }
            mArrivalJob = nullptr;
        }
        mArrivalWorld = nullptr;
    }

    void CellPreloader::updateArrivalGrid(Terrain::World* world, const osg::Vec4i& grid)
    {
        if (mArrivalWorld && (mArrivalWorld != world || (mArrivalJob && mArrivalJob->mView->mGrid != grid)
                || (!mArrivalJob && mReturnView && mReturnView->mGrid != grid)))
            retireArrival();
        if (mReturnView && (mReturnWorld != world || mReturnView->mGrid != grid))
        {
            mReturnWorld = nullptr;
            mReturnView.reset();
        }
    }

    void CellPreloader::clearArrivals()
    {
        retireArrival();
        for (auto& job : mRetiredArrivals)
            job->waitTillDone();
        mRetiredArrivals.clear();
        mReturnView.reset();
        mReturnWorld = nullptr;
    }

    bool CellPreloader::startArrival(Terrain::World* world, const PositionCellGrid& target, bool required)
    {
        if (!mWorkQueue || !world)
            return false;
        if (mArrivalJob && mArrivalJob->matches(world, target, terrainPreloadMergeDistance))
            return true;
        // Changing direction in a room with many exits cannot grow the queue without bound.
        if (!mRetiredArrivals.empty())
        {
            if (!required)
                return false;
            for (auto& previous : mRetiredArrivals)
                previous->waitTillDone();
            mRetiredArrivals.clear();
        }
        retireArrival();
        auto view = world->createArrival(target.mPosition, target.mCellBounds);
        if (!view)
            return false;
        auto job = osg::ref_ptr<ExteriorArrivalJob>(new ExteriorArrivalJob(world, std::move(view)));
        mWorkQueue->addWorkItem(job, required);
        mArrivalWorld = world;
        mArrivalJob = std::move(job);
        return true;
    }

    void CellPreloader::predictArrival(Terrain::World* world, const PositionCellGrid& target)
    {
        if (mReturnWorld == world && mReturnView && world->validArrival(*mReturnView)
            && mReturnView->matches(target.mPosition, target.mCellBounds, ESM::getCellSize(world->getWorldspace()),
                mReturnView->mRevision))
            return;
        startArrival(world, target, false);
    }

    bool CellPreloader::ensureArrival(Terrain::World* world, const PositionCellGrid& target,
        Loading::Listener& listener, double timestamp)
    {
        if (mReturnView && mReturnWorld == world && world->validArrival(*mReturnView)
            && timestamp - mReturnTime <= 120.0 && mReturnView->matches(target.mPosition, target.mCellBounds,
                ESM::getCellSize(world->getWorldspace()), mReturnView->mRevision))
        {
            retireArrival();
            mArrivalWorld = world;
            mReturnView->presentRetainedView();
            world->showArrival(mReturnView);
            Log(Debug::Info) << "Exterior arrival reused last exterior: estimatedBytes=" << mReturnView->bytes();
            return true;
        }
        const bool predicted = mArrivalJob && mArrivalJob->matches(world, target, terrainPreloadMergeDistance);
        if (!startArrival(world, target, true))
            return false;
        const auto begin = std::chrono::steady_clock::now();
        if ((mArrivalJob->isDone() && !mArrivalJob->finishedNear()) || !mArrivalJob->waitNear(listener))
        {
            retireArrival();
            return false;
        }
        world->showArrival(mArrivalJob->mView);
        Log(Debug::Info) << "Exterior arrival near wait: predicted=" << predicted << " waitMs="
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-begin).count();
        return true;
    }

    bool CellPreloader::keepStreamingArrival(std::span<const PositionCellGrid> targets)
    {
        if (targets.size() != 1 || !mArrivalJob || mArrivalJob->isDone() || !mArrivalWorld)
            return false;
        return mArrivalJob->matches(mArrivalWorld, targets.front(), ESM::getCellSize(mArrivalWorld->getWorldspace()));
    }

    void CellPreloader::leaveExterior(Terrain::World* world, const PositionCellGrid& current, double timestamp)
    {
        constexpr std::size_t returnBudget = 128u * 1024u * 1024u;
        std::shared_ptr<Terrain::ArrivalView> view;
        if (mArrivalJob && mArrivalJob->matches(world, current, ESM::getCellSize(world->getWorldspace())))
            view = mArrivalJob->mView;
        else if (mReturnWorld == world && mReturnView && world->validArrival(*mReturnView)
            && mReturnView->matches(current.mPosition, current.mCellBounds, ESM::getCellSize(world->getWorldspace()),
                mReturnView->mRevision))
            view = mReturnView;
        if (!view || !view->complete() || view->bytes() > returnBudget)
            view = world->captureArrival(current.mPosition, current.mCellBounds, returnBudget);
        mReturnView.reset();
        mReturnWorld = nullptr;
        if (view && view->complete() && view->bytes() <= returnBudget)
        {
            mReturnWorld = world;
            mReturnView = std::move(view);
            mReturnTime = timestamp;
        }
        retireArrival();
    }

    void CellPreloader::syncTerrainLoad(Loading::Listener& listener)
    {
        if (mTerrainPreloadItem != nullptr)
        {
            Loading::Reporter::WaitStats wait;
            const bool waitRequested = !mTerrainPreloadItem->isDone();
            if (waitRequested)
                mTerrainPreloadItem->wait(listener, wait);
            mTerrainPreloadItem->reportTimings(wait, waitRequested);
        }
    }

    void CellPreloader::abortTerrainPreloadExcept(const PositionCellGrid* exceptPos)
    {
        if (exceptPos != nullptr && contains(mTerrainPreloadPositions, *exceptPos, Constants::CellSizeInUnits))
            return;
        if (mTerrainPreloadItem && !mTerrainPreloadItem->isDone())
        {
            mTerrainPreloadItem->abort();
            mTerrainPreloadItem->waitTillDone();
        }
        setTerrainPreloadPositions({});
    }

    void CellPreloader::setTerrainPreloadPositions(std::span<const PositionCellGrid> positions)
    {
        if (positions.empty())
        {
            mTerrainPreloadPositions.clear();
            mLoadedTerrainPositions.clear();
        }
        else if (contains(mTerrainPreloadPositions, positions, terrainPreloadMergeDistance))
            return;
        if (mTerrainPreloadItem && !mTerrainPreloadItem->isDone())
        {
            const bool sameGrids = std::ranges::equal(mTerrainPreloadPositions, positions,
                [](const PositionCellGrid& l, const PositionCellGrid& r) { return l.mCellBounds == r.mCellBounds; });
            if (!sameGrids)
            {
                mTerrainPreloadItem->abort();
                // Do not record the aborted target as loaded in updateCache.
                mTerrainPreloadPositions.clear();
            }
            return;
        }
        else
        {
            if (mTerrainViews.size() > positions.size())
                mTerrainViews.resize(positions.size());
            else if (mTerrainViews.size() < positions.size())
            {
                for (size_t i = mTerrainViews.size(); i < positions.size(); ++i)
                    mTerrainViews.emplace_back(mTerrain->createView());
            }

            mTerrainPreloadPositions.assign(positions.begin(), positions.end());
            if (!positions.empty())
            {
                mTerrainPreloadItem = new TerrainPreloadItem(mTerrainViews, mTerrain, positions);
                mTerrainPreloadItem->markEnqueued();
                mWorkQueue->addWorkItem(mTerrainPreloadItem);
            }
        }
    }

    bool CellPreloader::isTerrainLoaded(const PositionCellGrid& position, double referenceTime) const
    {
        return mLoadedTerrainTimestamp + mResourceSystem->getSceneManager()->getExpiryDelay() > referenceTime
            && contains(mLoadedTerrainPositions, position, Constants::CellSizeInUnits);
    }

    void CellPreloader::setTerrain(Terrain::World* terrain)
    {
        if (terrain != mTerrain)
        {
            clearAllTasks();
            mTerrain = terrain;
        }
    }

    void CellPreloader::clearAllTasks()
    {
        if (mTerrainPreloadItem)
        {
            mTerrainPreloadItem->abort();
            mTerrainPreloadItem->waitTillDone();
            mTerrainPreloadItem = nullptr;
        }

        if (mUpdateCacheItem)
        {
            mUpdateCacheItem->waitTillDone();
            mUpdateCacheItem = nullptr;
        }

        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end(); ++it)
            it->second.mWorkItem->abort();

        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end(); ++it)
            it->second.mWorkItem->waitTillDone();

        mPreloadCells.clear();
    }

    void CellPreloader::reportStats(unsigned int frameNumber, osg::Stats& stats) const
    {
        stats.setAttribute(frameNumber, "CellPreloader Count", static_cast<double>(mPreloadCells.size()));
        stats.setAttribute(frameNumber, "CellPreloader Added", static_cast<double>(mAdded));
        stats.setAttribute(frameNumber, "CellPreloader Evicted", static_cast<double>(mEvicted));
        stats.setAttribute(frameNumber, "CellPreloader Loaded", static_cast<double>(mLoaded));
        stats.setAttribute(frameNumber, "CellPreloader Expired", static_cast<double>(mExpired));
    }
}

