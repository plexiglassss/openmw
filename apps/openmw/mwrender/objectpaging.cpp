#include <components/diagnostics/loading.hpp>
#include "objectpaging.hpp"

#include <optional>
#include <typeinfo>
#include <array>

#include <components/misc/combinedperformance.hpp>
#include <components/misc/nativejobpool.hpp>
#include <components/settings/values.hpp>

#include <components/diagnostics/timing.hpp>

#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

#include <osg/LOD>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/MatrixTransform>
#include <osg/Sequence>
#include <osg/Switch>
#include <osgAnimation/BasicAnimationManager>
#include <osgParticle/ParticleProcessor>
#include <osgParticle/ParticleSystemUpdater>
#include <osgUtil/IncrementalCompileOperation>
#include <components/sceneutil/compilepriority.hpp>

#include <components/esm/path.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm4/loadacti.hpp>
#include <components/esm4/loadcont.hpp>
#include <components/esm4/loaddoor.hpp>
#include <components/esm4/loadfurn.hpp>
#include <components/esm4/loadstat.hpp>
#include <components/esm4/loadtree.hpp>
#include <components/misc/boundedcache.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/rng.hpp>
#include <components/nifosg/autotransform.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/asyncpreparation.hpp>
#include <components/sceneutil/scenefootprint.hpp>
#include <components/loadinglistener/reporter.hpp>
#include <osg/GraphicsContext>
#include <osg/State>
#include <components/sceneutil/material.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/optimizer.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/riggeometryosgaextension.hpp>
#include <components/sceneutil/util.hpp>
#include <components/terrain/rayfootprint.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwclass/esm4base.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"

#include "vismask.hpp"

namespace MWRender
{

    namespace
    {
        struct PagedCellRef
        {
            ESM::RefId mRefId;
            ESM::RefNum mRefNum;
            osg::Vec3f mPosition;
            osg::Vec3f mRotation;
            float mScale;
        };

        bool typeFilter(int type, bool far)
        {
            switch (type)
            {
                case ESM::REC_STAT:
                case ESM::REC_ACTI:
                case ESM::REC_DOOR:
                case ESM::REC_STAT4:
                case ESM::REC_DOOR4:
                case ESM::REC_TREE4:
                    return true;
                case ESM::REC_CONT:
                case ESM::REC_ACTI4:
                case ESM::REC_CONT4:
                case ESM::REC_FURN4:
                    return !far;

                default:
                    return false;
            }
        }

        template <typename Record>
        VFS::Path::Normalized getEsm4Model(const Record& record)
        {
            if (MWClass::ESM4Impl::isMarkerModel(record->mModel.getOriginal()))
                return {};
            return record->mModel.getNormalized();
        }

        VFS::Path::Normalized getModel(int type, ESM::RefId id, const MWWorld::ESMStore& store)
        {
            switch (type)
            {
                case ESM::REC_STAT:
                    return store.get<ESM::Static>().searchStatic(id)->mModel.getNormalized();
                case ESM::REC_ACTI:
                    return store.get<ESM::Activator>().searchStatic(id)->mModel.getNormalized();
                case ESM::REC_DOOR:
                    return store.get<ESM::Door>().searchStatic(id)->mModel.getNormalized();
                case ESM::REC_CONT:
                    return store.get<ESM::Container>().searchStatic(id)->mModel.getNormalized();
                case ESM::REC_STAT4:
                    return getEsm4Model(store.get<ESM4::Static>().searchStatic(id));
                case ESM::REC_DOOR4:
                    return getEsm4Model(store.get<ESM4::Door>().searchStatic(id));
                case ESM::REC_TREE4:
                    return getEsm4Model(store.get<ESM4::Tree>().searchStatic(id));
                case ESM::REC_ACTI4:
                    return getEsm4Model(store.get<ESM4::Activator>().searchStatic(id));
                case ESM::REC_CONT4:
                    return getEsm4Model(store.get<ESM4::Container>().searchStatic(id));
                case ESM::REC_FURN4:
                    return getEsm4Model(store.get<ESM4::Furniture>().searchStatic(id));
                default:
                    return {};
            }
        }
    }

    struct ObjectPaging::Selection
    {
        struct Item
        {
            ESM::RefNum mNumber;
            PagedCellRef mRef;
            VFS::Path::Normalized mModel;
            float mDistanceSquared;
        };
        std::vector<Item> mItems;
        std::uint64_t mRevision;
        std::uint64_t mVfsRevision;
        CacheEpoch mEpoch;
    };

    struct ObjectPaging::PendingCompile
    {
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> mOperation;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation::CompileSet> mSet;
    };

    struct ObjectPaging::SceneryBridge : osg::Referenced
    {
        osg::ref_ptr<osg::Node> mNode;
        PendingCompile mCompile;
        std::size_t mBytes = 0;
        ~SceneryBridge() override
        {
            if (mCompile.mOperation && mCompile.mSet)
                mCompile.mOperation->remove(mCompile.mSet);
        }
    };

    struct ObjectPaging::SceneryState
    {
        struct Entry
        {
            osg::ref_ptr<SceneryBridge> mNode;
            std::size_t mBytes;
            std::uint64_t mUse, mPinned = 0;
        };
        // Protected by the manager's tracker mutex, including worker publication.
        std::map<Terrain::SceneryCell, Entry> mBridges;
        std::vector<osg::ref_ptr<osg::Referenced>> mRetired;
        std::size_t mBytes = 0;
        std::size_t mRetiredBytes = 0;
        std::size_t mQueuedRetiredBytes = 0;
        std::uint64_t mUse = 0, mFrame = 1;
        std::size_t mBridgeDraws = 0, mOptimizedDraws = 0, mRequiredBuilds = 0;
        double mNextReport = 0;
        double mAheadTime = -1;
        std::size_t mAheadVisits = 0;
        std::uint64_t mCompileGeneration = 0;
        void erase(std::map<Terrain::SceneryCell, Entry>::iterator it)
        {
            mBytes -= it->second.mBytes;
            mRetiredBytes += it->second.mBytes;
            mRetired.emplace_back(std::move(it->second.mNode));
            mBridges.erase(it);
        }
        void clear()
        {
            while (!mBridges.empty())
                erase(mBridges.begin());
        }
        void trim()
        {
            // Visible cover is required rendering memory. Bound additional
            // retained/speculative cover; never repeatedly rebuild an oversized
            // required cell on every frame merely to meet a speculative budget.
            constexpr std::size_t budget = 1024ull * 1024 * 1024;
            while (mBridges.size() > 128 || mBytes > budget)
            {
                auto oldest = mBridges.end();
                for (auto it = mBridges.begin(); it != mBridges.end(); ++it)
                    if (it->second.mPinned != mFrame
                        && (oldest == mBridges.end() || it->second.mUse < oldest->second.mUse))
                        oldest = it;
                if (oldest == mBridges.end())
                    break;
                erase(oldest);
            }
        }
    };

    struct ObjectPaging::ScenerySnapshot : Terrain::SceneryView
    {
        struct Plan
        {
            std::vector<ChunkRequest> mRequests;
            osg::Vec4i mGrid;
            std::vector<Terrain::SceneryCoverage> mGroups;
            bool matches(std::span<const ChunkRequest> requests, const osg::Vec4i& grid) const
            {
                return grid == mGrid && requests.size() == mRequests.size()
                    && std::equal(requests.begin(), requests.end(), mRequests.begin(), [](const auto& a, const auto& b) {
                        return a.mCenter == b.mCenter && a.mSize == b.mSize && a.mActiveGrid == b.mActiveGrid;
                    });
            }
        };
        std::shared_ptr<const Plan> mPlan;
        struct Batch
        {
            osg::ref_ptr<osg::Node> mNode, mPositioned;
            bool mCompiled = false;
            std::size_t mBytes = 0;
        };
        std::map<Terrain::SceneryCell, osg::ref_ptr<SceneryBridge>> mBridges;
        std::map<ChunkId, Batch> mBatches;
        CacheEpoch mEpoch;
        std::uint64_t mSelection = 0, mShader = 0, mCompile = 0;
    };

    struct ObjectPaging::AsyncState
    {
        using RequestKey = std::pair<ChunkId, std::uint64_t>;
        using Requests = SceneUtil::AsyncPreparation<RequestKey, osg::ref_ptr<osg::Node>>;
        Requests mRequests;
        using Bridges = SceneUtil::AsyncPreparation<Terrain::SceneryCell, osg::ref_ptr<SceneryBridge>>;
        Bridges mBridges;
        struct Retirement : SceneUtil::WorkItem
        {
            std::vector<osg::ref_ptr<osg::Referenced>> mNodes;
            std::size_t mBytes = 0;
            void doWork() override { mNodes.clear(); }
        };
        osg::ref_ptr<Retirement> mRetirement;
        osg::ref_ptr<SceneUtil::WorkQueue> mQueue;
        SceneUtil::CompileReadinessCache mCompleted;
        using Contexts = std::vector<std::pair<const osg::GraphicsContext*, unsigned>>;
        using ResourceGeneration = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
            const osgUtil::IncrementalCompileOperation*, Contexts>;
        using Generation = std::pair<std::uint64_t, ResourceGeneration>;
        Generation mGeneration;
        bool mInitialized = false;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> mOperation;
        osgUtil::IncrementalCompileOperation::ContextSet mContexts;
        std::vector<osg::ref_ptr<osg::GraphicsContext>> mContextOwners;
        explicit AsyncState(SceneUtil::WorkQueue* queue)
            : mRequests(queue), mBridges(queue, 2, 32, 256ull * 1024 * 1024), mQueue(queue) {}
        ~AsyncState()
        {
            if (mRetirement)
            {
                mQueue->cancelWorkItem(mRetirement);
                mRetirement->waitTillDone();
            }
        }
    };

    void ObjectPaging::setWorkQueue(SceneUtil::WorkQueue* queue)
    {
        if (queue && Misc::useAsyncObjectPaging())
            mAsync = std::make_unique<AsyncState>(queue);
        Log(Debug::Info) << "Distant object streaming: " << (mAsync ? "enabled" : "disabled")
                         << "; experimental unmerged cover: " << (usesSceneryCoverage() ? "enabled" : "disabled")
                         << "; active cells and exact queries retain immediate loading";
    }

    bool ObjectPaging::updateAsyncState(double referenceTime)
    {
        if (!mAsync)
            return false;
        auto* operation = mSceneManager->getIncrementalCompileOperation();
        auto contexts = operation ? operation->getContextSet() : osgUtil::IncrementalCompileOperation::ContextSet{};
        AsyncState::Contexts contextIds;
        for (auto it = contexts.begin(); it != contexts.end();)
        {
            if (!*it || !(*it)->getState())
                it = contexts.erase(it);
            else
            {
                contextIds.emplace_back(*it, (*it)->getState()->getContextID());
                ++it;
            }
        }
        std::uint64_t selectionEpoch;
        {
            const std::lock_guard lock(mRefTrackerMutex);
            // Diagnostic cover retains its conservative whole-snapshot boundary.
            selectionEpoch = usesSceneryCoverage() ? mSelectionRevision.load() : mSelectionRevisions.global();
        }
        AsyncState::Generation generation{ selectionEpoch,
            { mSceneManager->getVFS()->getIndexRevision(), mSceneManager->modelSettingsRevision(),
                mGraphicsRevision.load(), mSceneManager->getShaderManager().compileRevision(),
                operation, std::move(contextIds) } };
        bool changed = generation != mAsync->mGeneration;
        if (changed)
        {
            if (mAsync->mInitialized && mAsync->mRequests.size())
                Log(Debug::Info) << "Scenery preparation invalidation: retiredRequests=" << mAsync->mRequests.size()
                                 << " selection=" << (generation.first != mAsync->mGeneration.first)
                                 << " resources=" << (generation.second != mAsync->mGeneration.second);
            mAsync->mRequests.clear();
            mAsync->mBridges.clear();
            // Selection edits already remove affected CPU chunks. Receipts for
            // unchanged cached nodes remain valid across view/grid rebuilds.
            if (generation.second != mAsync->mGeneration.second)
            {
                mAsync->mCompleted.clear();
                const std::lock_guard lock(mRefTrackerMutex);
                ++mScenery->mCompileGeneration;
                if (mAsync->mInitialized)
                    mScenery->clear();
            }
            mAsync->mGeneration = std::move(generation);
            mAsync->mOperation = operation;
            mAsync->mContexts = std::move(contexts);
            mAsync->mContextOwners.assign(mAsync->mContexts.begin(), mAsync->mContexts.end());
        }
        mAsync->mRequests.cancelIf([this](const AsyncState::RequestKey& key) {
            return key.second != selectionRevision(key.first);
        });
        mAsync->mRequests.update(referenceTime);
        mAsync->mBridges.update(referenceTime);
        if (!mAsync->mRetirement || mAsync->mRetirement->isDone())
        {
            mAsync->mRetirement = nullptr;
            const std::lock_guard lock(mRefTrackerMutex);
            if (!mScenery->mRetired.empty())
            {
                mAsync->mRetirement = new AsyncState::Retirement;
                mAsync->mRetirement->mNodes.swap(mScenery->mRetired);
                mAsync->mRetirement->mBytes = mScenery->mRetiredBytes;
                // One release job at a time, ahead of optional preparation.
                mAsync->mQueue->addWorkItem(mAsync->mRetirement, true);
            }
            mScenery->mRetiredBytes = 0;
            mScenery->mQueuedRetiredBytes = mAsync->mRetirement ? mAsync->mRetirement->mBytes : 0;
        }
        changed = changed && mAsync->mInitialized;
        mAsync->mInitialized = true;
        return changed;
    }

    void ObjectPaging::clearCache()
    {
        const std::lock_guard lock(mRefTrackerMutex);
        ++mGraphicsRevision;
        mScenery->clear();
        Resource::GenericResourceManager<ChunkId>::clearCache();
    }

    void ObjectPaging::releaseGLObjects(osg::State* state)
    {
        const std::lock_guard lock(mRefTrackerMutex);
        ++mGraphicsRevision;
        mScenery->clear();
        Resource::GenericResourceManager<ChunkId>::releaseGLObjects(state);
    }

    bool ObjectPaging::canPrepareAsync() const
    {
        return mAsync && mAsync->mOperation && !mAsync->mContexts.empty();
    }

    bool ObjectPaging::usesSceneryCoverage() const
    {
        return mAsync && Misc::useSceneryCoverage();
    }

    ObjectPaging::AsyncResult ObjectPaging::requestChunk(
        const ChunkRequest& chunk, const osg::Vec3f& viewPoint, double referenceTime, bool queue)
    {
        // Active reference ownership and exact intersection semantics stay on the
        // established synchronous path. Future distant chunks may prepare in advance.
        if (chunk.mActiveGrid || !canPrepareAsync())
            return {};
        const ChunkId key(chunk.mCenter, chunk.mSize, false);
        const AsyncState::RequestKey requestKey{ key, selectionRevision(key) };
        auto cached = getCachedChunk(key);
        if (cached && mAsync->mCompleted.contains(cached))
            return { true, true, std::move(cached) };
        if (!queue)
        {
            // Required camera coverage never submits a new job and then builds
            // the same chunk synchronously. Cached CPU preload nodes can use the
            // normal draw path without claiming an independent GL completion.
            const auto result = mAsync->mRequests.poll(requestKey, referenceTime);
            if (result.mReady)
                mAsync->mCompleted.remember(result.mValue);
            else if (!cached && mAsync->mRequests.cancel(requestKey))
            {
                Diagnostics::Loading::Timer cancelled(Diagnostics::Loading::Stage::SceneryCancelled);
            }
            return { true, result.mReady, result.mValue };
        }
        const auto result = mAsync->mRequests.request(requestKey, referenceTime,
            [this, chunk, viewPoint, contexts = mAsync->mContexts, owners = mAsync->mContextOwners,
                operation = mAsync->mOperation]
            (const std::atomic_bool& abort) mutable -> AsyncState::Requests::Prepared {
                Diagnostics::Scope timing("terrain", "worker", "distantObjectPreparation");
                try
                {
                    auto node = fetchChunk(chunk, viewPoint, false, &abort, nullptr, false);
                    if (abort || !node)
                        return { std::move(node), {} };
                    auto prepared = SceneUtil::prepareCompileReadiness(node, contexts);
                    if (!abort)
                        operation->add(prepared.mSet, false);
                    return { std::move(node), std::move(prepared.mReady),
                        [operation, set = prepared.mSet] { operation->remove(set); }, prepared.mEstimatedBytes };
                }
                catch (const std::exception& error)
                {
                    Log(Debug::Warning) << "Distant object preparation failed: " << error.what();
                    throw;
                }
            });
        if (result.mReady)
            mAsync->mCompleted.remember(result.mValue);
        return { true, result.mReady, result.mValue };
    }

    bool ObjectPaging::canPreloadBatches() const
    {
        auto* jobs = MWBase::Environment::get().getNativeJobPoolOrNull();
        return jobs && jobs->workerCount() && Misc::useNativeJobs() && Settings::general().mNativeJobsEnabled;
    }

    osg::ref_ptr<ObjectPaging::SceneryBridge> ObjectPaging::getSceneryBridge(
        Terrain::SceneryCell cell, const std::atomic_bool* abort, bool required)
    {
        // A bridge is deliberately a conservative superset of ordinary distant
        // detail: real reference models, without merge-dependent size rejection.
        // Native LOD nodes still choose their authored distance ranges. It does
        // not create actors, collision, scripts, or active-reference ownership.
        const osg::Vec2f center(cell.first + 0.5f, cell.second + 0.5f);
        const ChunkRequest request{ 1.f, center, 0, false };
        const osg::Vec3f point(center.x() * getCellSize(mWorldspace), center.y() * getCellSize(mWorldspace), 0);
        std::vector<osg::ref_ptr<osg::Referenced>> retired;
        for (;;)
        {
            if (abort && *abort)
                return {};
            {
                const std::lock_guard lock(mRefTrackerMutex);
                refreshCacheEpoch();
                if (auto it = mScenery->mBridges.find(cell); it != mScenery->mBridges.end())
                {
                    it->second.mUse = ++mScenery->mUse;
                    if (required)
                        it->second.mPinned = mScenery->mFrame;
                    return it->second.mNode;
                }
            }
            auto selection = selectChunk(request, point, abort, true);
            PendingCompile pending;
            auto node = createChunk(request, point, selection, false, abort, false, pending, true);
            if (!node || (abort && *abort))
                return {};
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> positioned = new SceneUtil::PositionAttitudeTransform;
            positioned->setPosition(point);
            positioned->addChild(node);
            positioned->getBound();
            SceneUtil::SceneFootprint footprint;
            const auto bytes = footprint.measure(*positioned);
            osg::ref_ptr<SceneryBridge> bridge = new SceneryBridge;
            bridge->mNode = positioned;
            bridge->mBytes = bytes;
            if (auto* operation = mSceneManager->getIncrementalCompileOperation())
                bridge->mCompile = { operation, SceneUtil::prepareCompileSet(*operation, positioned) };
            {
                const std::lock_guard lock(mRefTrackerMutex);
                refreshCacheEpoch();
                if (selection.mRevision != selectionRevisionLocked({ request.mCenter, request.mSize, false })
                    || selection.mEpoch != mCacheEpoch)
                    continue;
                if (abort && *abort)
                    return {};
                if (auto it = mScenery->mBridges.find(cell); it != mScenery->mBridges.end())
                {
                    it->second.mUse = ++mScenery->mUse;
                    if (required)
                        it->second.mPinned = mScenery->mFrame;
                    return it->second.mNode;
                }
                // Independent GL readiness is required for replacement, not for
                // this last-resort normal draw. Queue usual compilation ahead;
                // a deadline miss can compile on draw but can never show a hole.
                if (bridge->mCompile.mOperation)
                    bridge->mCompile.mOperation->add(bridge->mCompile.mSet, false);
                mScenery->mBridges.emplace(cell, SceneryState::Entry{
                    bridge, bytes, ++mScenery->mUse, required ? mScenery->mFrame : 0 });
                mScenery->mBytes += bytes;
                mScenery->trim();
                if (!required)
                {
                    // A loading worker can release obsolete cover itself. Do
                    // not accumulate an entire preload in the retirement list
                    // while the main thread is busy pumping the loading screen.
                    retired.swap(mScenery->mRetired);
                    mScenery->mRetiredBytes = 0;
                }
                return bridge;
            }
        }
    }

    std::shared_ptr<Terrain::SceneryView> ObjectPaging::preloadScenery(std::span<const ChunkRequest> requests, const osg::Vec3f& point,
        const osg::Vec4i& grid, const std::atomic_bool& abort, Loading::Reporter& reporter)
    {
        auto snapshot = std::make_shared<ScenerySnapshot>();
        snapshot->mEpoch = cacheEpoch();
        snapshot->mSelection = mSelectionRevision.load();
        snapshot->mShader = mSceneManager->getShaderManager().compileRevision();
        SceneUtil::SceneFootprint footprint;
        std::set<Terrain::SceneryCell> cells;
        for (const auto& group : Terrain::sceneryCoverage(requests, grid))
            cells.insert(group.mCells.begin(), group.mCells.end());
        std::vector<Terrain::SceneryCell> ordered(cells.begin(), cells.end());
        const auto distance = [&](const Terrain::SceneryCell& cell) {
            return (osg::Vec2f(cell.first + 0.5f, cell.second + 0.5f) * getCellSize(mWorldspace)
                - osg::Vec2f(point.x(), point.y())).length2();
        };
        std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) {
            return distance(a) < distance(b);
        });
        reporter.addTotal(ordered.size());
        for (const auto& cell : ordered)
        {
            if (abort)
                return {};
            if (auto node = getSceneryBridge(cell, &abort, false))
            {
                snapshot->mEstimatedBytes += footprint.measure(*node->mNode);
                snapshot->mBridges.emplace(cell, std::move(node));
            }
            reporter.addProgress(1);
        }
        return snapshot;
    }

    void ObjectPaging::prepareScenery(const ChunkRequest& request, const osg::Vec3f& point, double time)
    {
        if (!usesSceneryCoverage())
        {
            requestChunk(request, point, time, true);
            return;
        }
        {
            const std::lock_guard lock(mRefTrackerMutex);
            if (mScenery->mRetiredBytes + mScenery->mQueuedRetiredBytes > 1024ull * 1024 * 1024)
                return;
        }
        const ChunkId optimizedKey(request.mCenter, request.mSize, false);
        if (auto cached = getCachedChunk(optimizedKey); cached && mAsync->mCompleted.contains(cached))
            return;
        if (mScenery->mAheadTime != time)
        {
            mScenery->mAheadTime = time;
            mScenery->mAheadVisits = 0;
        }
        // CPU coverage has its own bounded queue. A graphics completion wait for
        // an optional merged batch cannot occupy both bridge-preparation slots.
        bool covered = true;
        const int x0 = static_cast<int>(std::floor(request.mCenter.x() - request.mSize / 2));
        const int y0 = static_cast<int>(std::floor(request.mCenter.y() - request.mSize / 2));
        const int x1 = static_cast<int>(std::ceil(request.mCenter.x() + request.mSize / 2));
        const int y1 = static_cast<int>(std::ceil(request.mCenter.y() + request.mSize / 2));
        for (int x = x0; x < x1; ++x)
            for (int y = y0; y < y1; ++y)
            {
                if (++mScenery->mAheadVisits > 256)
                    return;
                const Terrain::SceneryCell cell{ x, y };
                {
                    const std::lock_guard lock(mRefTrackerMutex);
                    refreshCacheEpoch();
                    if (auto it = mScenery->mBridges.find(cell); it != mScenery->mBridges.end())
                    {
                        it->second.mUse = ++mScenery->mUse;
                        continue;
                    }
                }
                covered = false;
                mAsync->mBridges.request(cell, time, [this, cell](const std::atomic_bool& abort) {
                    auto node = getSceneryBridge(cell, &abort, false);
                    const auto bytes = node ? node->mBytes : 0;
                    return AsyncState::Bridges::Prepared{ std::move(node), {}, {}, bytes };
                });
            }
        if (covered)
        {
            // Coverage comes first; merged detail may use spare slots only
            // after the entire predicted batch has a complete bridge.
            const auto result = requestChunk(request, point, time, true);
            if (result.mReady)
                mAsync->mRequests.takeReady({ optimizedKey, selectionRevision(optimizedKey) }, time);
        }
    }

    void ObjectPaging::acceptScenery(std::span<const ChunkRequest> requests, const osg::Vec3f& point,
        const osg::Vec4i& grid, osg::NodeVisitor& visitor, double time,
        std::shared_ptr<Terrain::SceneryView>& current, const std::shared_ptr<Terrain::SceneryView>& incoming)
    {
        auto snapshot = std::make_shared<ScenerySnapshot>();
        snapshot->mEpoch = cacheEpoch();
        snapshot->mSelection = mSelectionRevision.load();
        snapshot->mShader = mSceneManager->getShaderManager().compileRevision();
        snapshot->mCompile = mScenery->mCompileGeneration;
        const auto usable = [&](const std::shared_ptr<Terrain::SceneryView>& candidate) {
            auto result = std::dynamic_pointer_cast<const ScenerySnapshot>(candidate);
            if (result && (result->mEpoch != snapshot->mEpoch || result->mSelection != snapshot->mSelection
                    || result->mShader != snapshot->mShader))
                result.reset();
            return result;
        };
        const auto previous = usable(current);
        const auto prepared = usable(incoming);
        if (previous && previous->mPlan && previous->mPlan->matches(requests, grid))
            snapshot->mPlan = previous->mPlan;
        else
        {
            auto plan = std::make_shared<ScenerySnapshot::Plan>();
            plan->mRequests.assign(requests.begin(), requests.end());
            plan->mGrid = grid;
            plan->mGroups = Terrain::sceneryCoverage(requests, grid);
            snapshot->mPlan = std::move(plan);
        }
        const auto& groups = snapshot->mPlan->mGroups;
        const auto retainedBatch = [&](const ChunkId& key, const osg::ref_ptr<osg::Node>& node,
                                       bool compiled) -> const ScenerySnapshot::Batch* {
            for (const auto* owner : { previous.get(), prepared.get() })
                if (owner && (!compiled || owner->mCompile == snapshot->mCompile))
                    if (const auto old = owner->mBatches.find(key); old != owner->mBatches.end()
                        && old->second.mNode == node && (!compiled || old->second.mCompiled))
                        return &old->second;
            return nullptr;
        };
        std::vector<osg::ref_ptr<osg::Node>> optimized(requests.size());
        std::vector<bool> ready(requests.size(), false);
        const bool asynchronous = canPrepareAsync();
        {
            const std::lock_guard lock(mRefTrackerMutex);
            if (visitor.getFrameStamp())
                mScenery->mFrame = static_cast<std::uint64_t>(visitor.getFrameStamp()->getFrameNumber()) + 1;
            else
                ++mScenery->mFrame;
        }
        for (std::size_t i = 0; i < requests.size(); ++i)
        {
            const auto& request = requests[i];
            if (request.mActiveGrid)
                continue;
            if (!asynchronous)
            {
                optimized[i] = fetchChunk(request, point, false, nullptr, nullptr, false);
                ready[i] = true;
                continue;
            }
            const ChunkId key(request.mCenter, request.mSize, false);
            auto cached = getCachedChunk(key);
            const bool retained = retainedBatch(key, cached, true) != nullptr;
            if (cached && (retained || mAsync->mCompleted.contains(cached)))
            {
                optimized[i] = std::move(cached);
                ready[i] = true;
            }
            else
            {
                // Poll without cancelling or admitting detail work. First cover
                // every required cell, then spend spare worker slots on batching.
                const auto result = mAsync->mRequests.takeReady({ key, selectionRevision(key) }, time);
                if (result.mReady)
                {
                    optimized[i] = result.mValue;
                    ready[i] = true;
                    mAsync->mCompleted.remember(result.mValue);
                }
            }
        }
        {
            // Protect the whole required wave before an individual miss can
            // evict a ready cell that will be needed later in this traversal.
            const std::lock_guard lock(mRefTrackerMutex);
            for (const auto& group : groups)
                if (!Terrain::sceneryReplacementReady(group, [&](std::size_t i) { return ready[i]; }))
                    for (const auto& cell : group.mCells)
                        if (auto it = mScenery->mBridges.find(cell); it != mScenery->mBridges.end())
                            it->second.mPinned = mScenery->mFrame;
        }
        std::vector<std::size_t> prepare;
        for (const auto& group : groups)
        {
            if (Terrain::sceneryReplacementReady(group, [&](std::size_t i) { return ready[i]; }))
            {
                for (const auto i : group.mRequests)
                    if (optimized[i])
                    {
                        const ChunkId key(requests[i].mCenter, requests[i].mSize, false);
                        osg::ref_ptr<osg::Node> positioned;
                        std::size_t bytes = 0;
                        if (const auto* old = retainedBatch(key, optimized[i], false))
                        {
                            positioned = old->mPositioned;
                            bytes = old->mBytes;
                        }
                        if (!positioned)
                        {
                            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> transform
                                = new SceneUtil::PositionAttitudeTransform;
                            transform->setPosition(osg::Vec3f(requests[i].mCenter.x() * getCellSize(mWorldspace),
                                requests[i].mCenter.y() * getCellSize(mWorldspace), 0));
                            transform->addChild(optimized[i]);
                            positioned = transform;
                            SceneUtil::SceneFootprint measured;
                            bytes = measured.measure(*positioned);
                        }
                        positioned->accept(visitor);
                        snapshot->mEstimatedBytes += bytes;
                        snapshot->mBatches.emplace(key, ScenerySnapshot::Batch{ optimized[i], positioned, asynchronous, bytes });
                        ++mScenery->mOptimizedDraws;
                    }
            }
            else
            {
                for (const auto& cell : group.mCells)
                {
                    osg::ref_ptr<SceneryBridge> bridge;
                    for (const auto* owner : { prepared.get(), previous.get() })
                        if (owner)
                            if (auto it = owner->mBridges.find(cell); it != owner->mBridges.end())
                            {
                                bridge = it->second;
                                break;
                            }
                    bool cached;
                    {
                        const std::lock_guard lock(mRefTrackerMutex);
                        cached = bridge || mScenery->mBridges.contains(cell);
                    }
                    if (!cached)
                    {
                        mAsync->mBridges.cancel(cell);
                        ++mScenery->mRequiredBuilds;
                    }
                    std::optional<Diagnostics::Loading::Timer> required;
                    if (!cached)
                        required.emplace(Diagnostics::Loading::Stage::SceneryRequired);
                    if (!bridge)
                        bridge = getSceneryBridge(cell, nullptr, true);
                    if (bridge)
                    {
                        bridge->mNode->accept(visitor);
                        snapshot->mEstimatedBytes += bridge->mBytes;
                        snapshot->mBridges.emplace(cell, std::move(bridge));
                    }
                    ++mScenery->mBridgeDraws;
                }
                if (group.mCanReplace)
                {
                    for (const auto i : group.mRequests)
                        if (!ready[i])
                            prepare.push_back(i);
                        else if (optimized[i])
                        {
                            // A ready sibling must survive even if the final
                            // subcell takes longer than ordinary cache expiry.
                            // Own it as a candidate without drawing it over cover.
                            const ChunkId key(requests[i].mCenter, requests[i].mSize, false);
                            ScenerySnapshot::Batch candidate{ optimized[i], {}, asynchronous, 0 };
                            if (const auto* old = retainedBatch(key, optimized[i], false))
                                candidate = *old;
                            else
                            {
                                SceneUtil::SceneFootprint measured;
                                candidate.mBytes = measured.measure(*optimized[i]);
                            }
                            snapshot->mEstimatedBytes += candidate.mBytes;
                            snapshot->mBatches.emplace(key, std::move(candidate));
                        }
                }
            }
        }
        for (const auto i : prepare)
            requestChunk(requests[i], point, time, true);
        // Keep selected raw nodes and positioned wrappers owned between frames.
        // Resource cache expiry can now observe their continuing use.
        if (current)
        {
            struct Retirement : osg::Referenced
            {
                std::shared_ptr<Terrain::SceneryView> mView;
            };
            osg::ref_ptr<Retirement> retired = new Retirement;
            std::size_t bytes = sizeof(ScenerySnapshot);
            if (const auto old = std::dynamic_pointer_cast<const ScenerySnapshot>(current))
            {
                // Unchanged graphs are also owned by the new snapshot. Charging
                // their full estimate once per frame invents unbounded backlog.
                bytes += (old->mBridges.size() + old->mBatches.size()) * 128;
                for (const auto& [cell, bridge] : old->mBridges)
                    if (auto next = snapshot->mBridges.find(cell);
                        next == snapshot->mBridges.end() || next->second != bridge)
                        bytes += bridge->mBytes;
                for (const auto& [key, batch] : old->mBatches)
                    if (auto next = snapshot->mBatches.find(key);
                        next == snapshot->mBatches.end() || next->second.mNode != batch.mNode)
                        bytes += batch.mBytes;
            }
            else
                bytes += current->mEstimatedBytes;
            retired->mView = std::move(current);
            const std::lock_guard lock(mRefTrackerMutex);
            mScenery->mRetiredBytes += bytes;
            mScenery->mRetired.emplace_back(std::move(retired));
        }
        current = std::move(snapshot);
        {
            const std::lock_guard lock(mRefTrackerMutex);
            mScenery->trim();
            if (time >= mScenery->mNextReport)
            {
                Log(Debug::Info) << "Scenery coverage: bridgeSections=" << mScenery->mBridgeDraws
                    << " optimizedBatches=" << mScenery->mOptimizedDraws
                    << " requiredBridgeBuilds=" << mScenery->mRequiredBuilds
                    << " retainedSections=" << mScenery->mBridges.size()
                    << " estimatedRetainedBytes=" << mScenery->mBytes
                    << " estimatedRetiredBytes=" << mScenery->mRetiredBytes + mScenery->mQueuedRetiredBytes
                    << " estimatedSelectedBytes=" << current->mEstimatedBytes;
                mScenery->mBridgeDraws = mScenery->mOptimizedDraws = mScenery->mRequiredBuilds = 0;
                mScenery->mNextReport = time + 1;
            }
        }
    }

    void ObjectPaging::preloadChunks(std::span<const ChunkRequest> chunks, const osg::Vec3f& viewPoint,
        const std::atomic_bool& abort, const Load& load)
    {
        auto& jobs = *MWBase::Environment::get().getNativeJobPoolOrNull();
        std::vector<std::optional<Selection>> selected(chunks.size());
        std::vector<std::vector<VFS::Path::NormalizedView>> models(chunks.size());
        for (std::size_t i = 0; i < chunks.size() && !abort; ++i)
        {
            const auto& chunk = chunks[i];
            if ((!chunk.mActiveGrid && usesSceneryCoverage()) || (chunk.mActiveGrid && !mActiveGrid)
                || getCachedChunk(ChunkId(chunk.mCenter, chunk.mSize, chunk.mActiveGrid)))
                continue;
            selected[i] = selectChunk(chunk, viewPoint, &abort);
            for (const auto& item : selected[i]->mItems)
                models[i].emplace_back(item.mModel);
        }
        mSceneManager->withPagedModelGroups(models, jobs, abort, [&](std::size_t i, bool prepared) {
            load(i, [&] {
                if (!chunks[i].mActiveGrid && usesSceneryCoverage())
                    return osg::ref_ptr<osg::Node>{};
                return fetchChunk(chunks[i], viewPoint, true, &abort, selected[i] ? &*selected[i] : nullptr, prepared);
            });
        });
    }

    namespace
    {
        // CopyOp may remove Switch/LOD parents or replace special transforms and
        // drawables. Bound ordinary geometry itself, across all branches, and refuse
        // to certify other node classes. Never trust authored LOD centres/radii here.
        bool addPagingVertices(const osg::Node& node, const osg::Matrixd& parent, double& extent)
        {
            if (node.getUpdateCallback() || node.getComputeBoundingSphereCallback()
                || node.getNumChildrenRequiringUpdateTraversal())
                return false;
            if (typeid(node) == typeid(osg::Geometry))
            {
                const auto& geometry = static_cast<const osg::Geometry&>(node);
                const auto* vertices = dynamic_cast<const osg::Vec3Array*>(geometry.getVertexArray());
                if (!vertices || geometry.getComputeBoundingBoxCallback())
                    return false;
                const auto& box = geometry.getBoundingBox();
                if (!box.valid())
                    return false;
                for (unsigned i = 0; i < 8; ++i)
                {
                    const auto p = osg::Vec3d(box.corner(i)) * parent;
                    const double length = p.length();
                    if (!std::isfinite(length))
                        return false;
                    extent = std::max(extent, length);
                }
                return true;
            }
            const auto* group = node.asGroup();
            if (!group)
                return false;
            osg::Matrixd matrix = parent;
            if (typeid(node) == typeid(osg::MatrixTransform))
            {
                const auto& transform = static_cast<const osg::MatrixTransform&>(node);
                const auto& local = transform.getMatrix();
                if (transform.getReferenceFrame() != osg::Transform::RELATIVE_RF
                    || local(0, 3) != 0 || local(1, 3) != 0 || local(2, 3) != 0 || local(3, 3) != 1)
                    return false;
                matrix.preMult(local);
            }
            else if (typeid(node) != typeid(osg::Group) && typeid(node) != typeid(osg::Geode)
                && typeid(node) != typeid(osg::LOD) && typeid(node) != typeid(osg::Switch))
                return false;
            for (unsigned i = 0; i < group->getNumChildren(); ++i)
                if (!addPagingVertices(*group->getChild(i), matrix, extent))
                    return false;
            return true;
        }
    }

    bool ObjectPaging::mayIntersectOutsideTile(
        const ChunkRequest& chunk, const osg::Vec3f& viewPoint, const Terrain::RayFootprint& ray)
    {
        if (chunk.mActiveGrid && !mActiveGrid)
            return false;
        const osg::Vec2f origin = chunk.mCenter * static_cast<float>(getCellSize(mWorldspace));
        const auto cached = getCachedChunk(ChunkId(chunk.mCenter, chunk.mSize, chunk.mActiveGrid));
        if (cached)
        {
            const auto* node = static_cast<const osg::Node*>(cached.get());
            const auto& b = node->getBound();
            return !node->isCullingActive() || !b.valid()
                || ray.intersects(origin.x() + double(b.center().x()), origin.y() + double(b.center().y()), b.radius());
        }

        // Use the same reference, override, scale and LOD selection as the full chunk.
        // A placement in a neighbouring cell can reach into the segment's cell.
        const auto modelRevision = mSceneManager->modelSettingsRevision();
        const auto selected = selectChunk(chunk, viewPoint, nullptr);
        std::map<VFS::Path::NormalizedView, double> extents;
        for (const auto& item : selected.mItems)
        {
            auto [it, inserted] = extents.try_emplace(item.mModel, 0.);
            if (inserted)
            {
                const auto model = mSceneManager->getCachedTemplate(item.mModel);
                if (!model || !addPagingVertices(*model, osg::Matrixd::identity(), it->second))
                    return true;
            }
            const auto& ref = item.mRef;
            const osg::Vec3f local = ref.mPosition - osg::Vec3f(origin.x(), origin.y(), 0);
            if (ray.intersects(origin.x() + double(local.x()), origin.y() + double(local.y()),
                    it->second * std::abs(double(ref.mScale))))
                return true;
        }
        return selected.mEpoch != cacheEpoch() || modelRevision != mSceneManager->modelSettingsRevision()
            || selected.mRevision != selectionRevision({ chunk.mCenter, chunk.mSize, chunk.mActiveGrid })
            || selected.mVfsRevision != mSceneManager->getVFS()->getIndexRevision();
    }

    std::uint64_t ObjectPaging::selectionRevision(const ChunkId& id)
    {
        const std::lock_guard lock(mRefTrackerMutex);
        return selectionRevisionLocked(id);
    }

    std::uint64_t ObjectPaging::selectionRevisionLocked(const ChunkId& id) const
    {
        const auto& [center, size, active] = id;
        return mSelectionRevisions.get(center.x(), center.y(), size, active);
    }

    ObjectPaging::CacheEpoch ObjectPaging::cacheEpoch() const
    {
        return { mGraphicsRevision.load(), mSceneManager->modelSettingsRevision(),
            mSceneManager->getVFS()->getIndexRevision() };
    }

    void ObjectPaging::refreshCacheEpoch()
    {
        const auto epoch = cacheEpoch();
        if (epoch != mCacheEpoch)
        {
            mCache->clear();
            mScenery->clear();
            mCacheEpoch = epoch;
        }
    }

    osg::ref_ptr<osg::Node> ObjectPaging::getCachedChunk(const ChunkId& id)
    {
        const std::lock_guard lock(mRefTrackerMutex);
        refreshCacheEpoch();
        return static_cast<osg::Node*>(mCache->getRefFromObjectCache(id).get());
    }

    osg::ref_ptr<osg::Node> ObjectPaging::getChunk(float size, const osg::Vec2f& center, unsigned char /*lod*/,
        unsigned int lodFlags, bool activeGrid, const osg::Vec3f& viewPoint, bool compile, const std::atomic_bool* abort)
    {
        return fetchChunk({ size, center, lodFlags, activeGrid }, viewPoint, compile, abort, nullptr, false);
    }

    osg::ref_ptr<osg::Node> ObjectPaging::fetchChunk(const ChunkRequest& chunk, const osg::Vec3f& viewPoint,
        bool compile, const std::atomic_bool* abort, const Selection* selected, bool modelsPrepared)
    {
        Diagnostics::Loading::Timer loadingTimer(Diagnostics::Loading::Stage::ObjectChunks);
        Diagnostics::Scope chunkTiming("terrain", "objects", "getChunk");
        if (chunk.mActiveGrid && !mActiveGrid)
            return nullptr;
        const ChunkId id(chunk.mCenter, chunk.mSize, chunk.mActiveGrid);
        for (;;)
        {
            if (abort && *abort)
                return nullptr;
            CacheEpoch epoch;
            {
                const std::lock_guard lock(mRefTrackerMutex);
                refreshCacheEpoch();
                epoch = mCacheEpoch;
                if (auto cached = mCache->getRefFromObjectCache(id))
                    return static_cast<osg::Node*>(cached.get());
            }

            std::optional<Selection> local;
            if (!selected || selected->mEpoch != epoch || selected->mRevision != selectionRevision(id)
                || selected->mVfsRevision != mSceneManager->getVFS()->getIndexRevision())
            {
                local.emplace(selectChunk(chunk, viewPoint, abort));
                selected = &*local;
                modelsPrepared = false;
            }
            if (abort && *abort)
                return nullptr;
            PendingCompile pending;
            auto node = createChunk(chunk, viewPoint, *selected, compile, abort, modelsPrepared, pending);
            if (!node || (abort && *abort))
                return nullptr;
            {
                // An enable/blacklist change may have evicted an older node during assembly.
                // Publish against the same tracker revision, or reselect outside the lock.
                const std::lock_guard lock(mRefTrackerMutex);
                if (epoch == cacheEpoch() && selected->mRevision == selectionRevisionLocked(id)
                    && selected->mVfsRevision == mSceneManager->getVFS()->getIndexRevision())
                {
                    if (auto cached = mCache->getRefFromObjectCache(id))
                        return static_cast<osg::Node*>(cached.get());
                    if (pending.mSet)
                        pending.mOperation->add(pending.mSet, false);
                    mCache->addEntryToObjectCache(id, node.get());
                    return node;
                }
            }
            selected = nullptr;
            modelsPrepared = false;
        }
    }

    namespace
    {
        class CanOptimizeCallback : public SceneUtil::Optimizer::IsOperationPermissibleForObjectCallback
        {
        public:
            bool isOperationPermissibleForObjectImplementation(
                const SceneUtil::Optimizer* optimizer, const osg::Drawable* node, unsigned int option) const override
            {
                return true;
            }
            bool isOperationPermissibleForObjectImplementation(
                const SceneUtil::Optimizer* optimizer, const osg::Node* node, unsigned int option) const override
            {
                return (node->getDataVariance() != osg::Object::DYNAMIC);
            }
        };

        using LODRange = osg::LOD::MinMaxPair;

        LODRange intersection(const LODRange& left, const LODRange& right)
        {
            return { std::max(left.first, right.first), std::min(left.second, right.second) };
        }

        bool empty(const LODRange& r)
        {
            return r.first >= r.second;
        }

        LODRange operator/(const LODRange& r, float div)
        {
            return { r.first / div, r.second / div };
        }

        class CopyOp : public osg::CopyOp
        {
        public:
            bool mActiveGrid = false;
            LODRange mDistances = { 0.f, 0.f };
            osg::Node::NodeMask mCopyMask = ~0u;

            CopyOp(bool activeGrid, osg::Node::NodeMask copyMask)
                : mActiveGrid(activeGrid)
                , mCopyMask(copyMask)
            {
            }

            void copy(const osg::Node* toCopy, osg::Group* attachTo)
            {
                const osg::Group* groupToCopy = toCopy->asGroup();
                if (toCopy->getStateSet() || toCopy->asTransform() || !groupToCopy)
                    attachTo->addChild(operator()(toCopy));
                else
                {
                    for (unsigned int i = 0; i < groupToCopy->getNumChildren(); ++i)
                        attachTo->addChild(operator()(groupToCopy->getChild(i)));
                }
            }

            osg::Node* operator()(const osg::Node* node) const override
            {
                if (!(node->getNodeMask() & mCopyMask))
                    return nullptr;

                if (const osg::Drawable* d = node->asDrawable())
                    return operator()(d);

                if (dynamic_cast<const osgParticle::ParticleProcessor*>(node))
                    return nullptr;
                if (dynamic_cast<const osgParticle::ParticleSystemUpdater*>(node))
                    return nullptr;

                if (const osg::Switch* sw = node->asSwitch())
                {
                    osg::Group* n = new osg::Group;
                    for (unsigned int i = 0; i < sw->getNumChildren(); ++i)
                        if (sw->getValue(i))
                            n->addChild(operator()(sw->getChild(i)));
                    n->setDataVariance(osg::Object::STATIC);
                    return n;
                }
                if (const osg::LOD* lod = dynamic_cast<const osg::LOD*>(node))
                {
                    std::vector<std::pair<osg::ref_ptr<osg::Node>, LODRange>> children;
                    for (unsigned int i = 0; i < lod->getNumChildren(); ++i)
                        if (const auto r = intersection(lod->getRangeList()[i], mDistances); !empty(r))
                            children.emplace_back(operator()(lod->getChild(i)), lod->getRangeList()[i]);
                    if (children.empty())
                        return nullptr;

                    if (children.size() == 1)
                        return children.front().first.release();
                    else
                    {
                        osg::LOD* n = new osg::LOD;
                        for (const auto& [child, range] : children)
                            n->addChild(child, range.first, range.second);
                        n->setRangeMode(lod->getRangeMode());
                        n->setCenterMode(lod->getCenterMode());
                        n->setCenter(lod->getCenter());
                        n->setRadius(lod->getRadius());
                        n->setDataVariance(osg::Object::STATIC);
                        return n;
                    }
                }
                if (const osg::Sequence* sq = dynamic_cast<const osg::Sequence*>(node))
                {
                    osg::Group* n = new osg::Group;
                    n->addChild(operator()(sq->getChild(sq->getValue() != -1 ? sq->getValue() : 0)));
                    n->setDataVariance(osg::Object::STATIC);
                    return n;
                }

                if (!mActiveGrid)
                {
                    if (const auto* autoTransform = dynamic_cast<const NifOsg::AutoTransform*>(node))
                    {
                        osg::MatrixTransform* n = new osg::MatrixTransform();
                        n->setMatrix(autoTransform->computeMatrix(nullptr));

                        for (unsigned int i = 0; i < autoTransform->getNumChildren(); ++i)
                            if (osg::Node* clonedChild = operator()(autoTransform->getChild(i)))
                                n->addChild(clonedChild);

                        n->setDataVariance(osg::Object::STATIC);

                        handleCallbacks(node, n);

                        return n;
                    }
                }

                osg::Node* cloned = static_cast<osg::Node*>(node->clone(*this));
                if (!mActiveGrid)
                    cloned->setDataVariance(osg::Object::STATIC);
                cloned->setUserDataContainer(nullptr);
                cloned->setName("");

                handleCallbacks(node, cloned);

                return cloned;
            }

            void handleCallbacks(const osg::Node* node, osg::Node* cloned) const
            {
                for (const osg::Callback* callback = node->getCullCallback(); callback != nullptr;
                    callback = callback->getNestedCallback())
                {
                    if (node->getCullCallback()->getNestedCallback())
                    {
                        osg::Callback* clonedCallback = osg::clone(callback, osg::CopyOp::SHALLOW_COPY);
                        clonedCallback->setNestedCallback(nullptr);
                        cloned->addCullCallback(clonedCallback);
                    }
                    else
                        cloned->addCullCallback(const_cast<osg::Callback*>(callback));
                }
            }
            osg::Drawable* operator()(const osg::Drawable* drawable) const override
            {
                if (!(drawable->getNodeMask() & mCopyMask))
                    return nullptr;

                if (dynamic_cast<const osgParticle::ParticleSystem*>(drawable))
                    return nullptr;

                if (dynamic_cast<const SceneUtil::OsgaRigGeometry*>(drawable))
                    return nullptr;
                if (const SceneUtil::RigGeometry* rig = dynamic_cast<const SceneUtil::RigGeometry*>(drawable))
                    return operator()(rig->getSourceGeometry());
                if (const SceneUtil::MorphGeometry* morph = dynamic_cast<const SceneUtil::MorphGeometry*>(drawable))
                    return operator()(morph->getSourceGeometry());

                if (getCopyFlags() & DEEP_COPY_DRAWABLES)
                {
                    osg::Drawable* d = static_cast<osg::Drawable*>(drawable->clone(*this));
                    d->setDataVariance(osg::Object::STATIC);
                    d->setUserDataContainer(nullptr);
                    d->setName("");
                    return d;
                }
                else
                    return const_cast<osg::Drawable*>(drawable);
            }
            osg::Callback* operator()(const osg::Callback* callback) const override { return nullptr; }
        };

        class RefnumSet : public osg::Object
        {
        public:
            RefnumSet() {}
            RefnumSet(const RefnumSet& copy, const osg::CopyOp&)
                : mRefnums(copy.mRefnums)
            {
            }
            META_Object(MWRender, RefnumSet)
            std::vector<ESM::RefNum> mRefnums;
        };

        class AnalyzeVisitor : public osg::NodeVisitor
        {
        public:
            AnalyzeVisitor(osg::Node::NodeMask analyzeMask)
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
                , mCurrentStateSet(nullptr)
            {
                setTraversalMask(analyzeMask);
            }

            typedef std::unordered_map<osg::StateSet*, unsigned int> StateSetCounter;
            struct Result
            {
                StateSetCounter mStateSetCounter;
                unsigned int mNumVerts = 0;
            };

            void apply(osg::Node& node) override
            {
                if (node.getStateSet())
                    mCurrentStateSet = node.getStateSet();

                if (osg::Switch* sw = node.asSwitch())
                {
                    for (unsigned int i = 0; i < sw->getNumChildren(); ++i)
                        if (sw->getValue(i))
                            traverse(*sw->getChild(i));
                    return;
                }
                if (osg::LOD* lod = dynamic_cast<osg::LOD*>(&node))
                {
                    for (unsigned int i = 0; i < lod->getNumChildren(); ++i)
                        if (const auto r = intersection(lod->getRangeList()[i], mDistances); !empty(r))
                            traverse(*lod->getChild(i));
                    return;
                }
                if (osg::Sequence* sq = dynamic_cast<osg::Sequence*>(&node))
                {
                    traverse(*sq->getChild(sq->getValue() != -1 ? sq->getValue() : 0));
                    return;
                }

                traverse(node);
            }
            void apply(osg::Geometry& geom) override
            {
                if (osg::Array* array = geom.getVertexArray())
                    mResult.mNumVerts += array->getNumElements();

                ++mResult.mStateSetCounter[mCurrentStateSet];
                ++mGlobalStateSetCounter[mCurrentStateSet];
            }
            Result retrieveResult()
            {
                Result result = mResult;
                mResult = Result();
                mCurrentStateSet = nullptr;
                return result;
            }
            void addInstance(const Result& result)
            {
                for (auto pair : result.mStateSetCounter)
                    mGlobalStateSetCounter[pair.first] += pair.second;
            }
            float getMergeBenefit(const Result& result)
            {
                if (result.mStateSetCounter.empty())
                    return 1;
                float mergeBenefit = 0;
                for (auto pair : result.mStateSetCounter)
                {
                    mergeBenefit += mGlobalStateSetCounter[pair.first];
                }
                mergeBenefit /= result.mStateSetCounter.size();
                return mergeBenefit;
            }

            Result mResult;
            osg::StateSet* mCurrentStateSet;
            StateSetCounter mGlobalStateSetCounter;
            LODRange mDistances = { 0.f, 0.f };
        };

        class DebugVisitor : public osg::NodeVisitor
        {
        public:
            DebugVisitor()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }
            void apply(osg::Drawable& node) override
            {
                osg::ref_ptr<SceneUtil::Material> m(new SceneUtil::Material);
                osg::Vec4f color(
                    Misc::Rng::rollProbability(), Misc::Rng::rollProbability(), Misc::Rng::rollProbability(), 0.f);
                color.normalize();
                m->setDiffuse(osg::Vec4f(0.1f, 0.1f, 0.1f, 1.f));
                m->setAmbient(osg::Vec4f(0.1f, 0.1f, 0.1f, 1.f));
                m->setEmission(osg::Vec4f(color));
                m->setVertexColorMode(SceneUtil::VertexColorModes::None);
                osg::ref_ptr<osg::StateSet> stateset = node.getStateSet()
                    ? osg::clone(node.getStateSet(), osg::CopyOp::SHALLOW_COPY)
                    : new osg::StateSet;
                stateset->setAttribute(m);
                m->updateStateSet(stateset);
                node.setStateSet(stateset);
            }
        };

        class AddRefnumMarkerVisitor : public osg::NodeVisitor
        {
        public:
            AddRefnumMarkerVisitor(ESM::RefNum refnum)
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
                , mRefnum(refnum)
            {
            }
            ESM::RefNum mRefnum;
            void apply(osg::Geometry& node) override
            {
                osg::ref_ptr<RefnumMarker> marker(new RefnumMarker);
                marker->mRefnum = mRefnum;
                if (osg::Array* array = node.getVertexArray())
                    marker->mNumVertices = array->getNumElements();
                node.getOrCreateUserDataContainer()->addUserObject(marker);
            }
        };
    }

    struct ObjectPaging::ReferenceCache
    {
        Misc::BoundedCache<std::pair<int, int>, std::map<ESM::RefNum, PagedCellRef>> cells{ 64 };
    };

    ObjectPaging::~ObjectPaging()
    {
        // Jobs capture this manager. Drain them while all members are alive.
        mAsync.reset();
    }

    ObjectPaging::ObjectPaging(Resource::SceneManager* sceneManager, ESM::RefId worldspace)
        : GenericResourceManager<ChunkId>(nullptr, Settings::cells().mCacheExpiryDelay)
        , Terrain::QuadTreeWorld::BatchChunkManager(worldspace)
        , mScenery(std::make_unique<SceneryState>())
        , mReferenceCache(std::make_unique<ReferenceCache>())
        , mSceneManager(sceneManager)
        , mActiveGrid(Settings::terrain().mObjectPagingActiveGrid)
        , mDebugBatches(Settings::terrain().mDebugChunks)
        , mMergeFactor(Settings::terrain().mObjectPagingMergeFactor)
        , mMinSize(Settings::terrain().mObjectPagingMinSize)
        , mMinSizeMergeFactor(Settings::terrain().mObjectPagingMinSizeMergeFactor)
        , mMinSizeCostMultiplier(Settings::terrain().mObjectPagingMinSizeCostMultiplier)
        , mRefTrackerLocked(false)
    {
    }

    namespace
    {
        PagedCellRef makePagedCellRef(const ESM::CellRef& value)
        {
            return PagedCellRef{
                .mRefId = value.mRefID,
                .mRefNum = value.mRefNum,
                .mPosition = value.mPos.asVec3(),
                .mRotation = value.mPos.asRotationVec3(),
                .mScale = value.mScale,
            };
        }

        PagedCellRef makePagedCellRef(const ESM4::Reference& value)
        {
            return PagedCellRef{
                .mRefId = value.mBaseObj,
                .mRefNum = value.mId,
                .mPosition = value.mPos.asVec3(),
                .mRotation = value.mPos.asRotationVec3(),
                .mScale = value.mScale,
            };
        }

        std::map<ESM::RefNum, PagedCellRef> collectESM3References(
            float size, const osg::Vec2i& startCell, const MWWorld::ESMStore& store, bool* complete = nullptr)
        {
            std::map<ESM::RefNum, PagedCellRef> refs;
            ESM::ReadersCache readers;
            for (int cellX = startCell.x(); cellX < startCell.x() + size; ++cellX)
            {
                for (int cellY = startCell.y(); cellY < startCell.y() + size; ++cellY)
                {
                    const ESM::Cell* cell = store.get<ESM::Cell>().searchStatic(cellX, cellY);
                    if (!cell)
                        continue;
                    for (size_t i = 0; i < cell->mContextList.size(); ++i)
                    {
                        try
                        {
                            const std::size_t index = static_cast<std::size_t>(cell->mContextList[i].index);
                            const ESM::ReadersCache::BusyItem reader = readers.get(index);
                            cell->restore(*reader, i);
                            ESM::CellRef ref;
                            ESM::MovedCellRef cMRef;
                            bool deleted = false;
                            bool moved = false;
                            while (ESM::Cell::getNextRef(
                                *reader, ref, deleted, cMRef, moved, ESM::Cell::GetNextRefMode::LoadOnlyNotMoved))
                            {
                                if (moved)
                                    continue;

                                if (std::find(cell->mMovedRefs.begin(), cell->mMovedRefs.end(), ref.mRefNum)
                                    != cell->mMovedRefs.end())
                                    continue;

                                int type = store.findStatic(ref.mRefID);
                                if (!typeFilter(type, size >= 2))
                                    continue;
                                if (deleted)
                                {
                                    refs.erase(ref.mRefNum);
                                    continue;
                                }
                                refs.insert_or_assign(ref.mRefNum, makePagedCellRef(ref));
                            }
                        }
                        catch (const std::exception& e)
                        {
                            if (complete)
                                *complete = false;
                            Log(Debug::Warning) << "Failed to collect references from cell \"" << cell->getDescription()
                                                << "\": " << e.what();
                            continue;
                        }
                    }
                    for (const auto& [ref, deleted] : cell->mLeasedRefs)
                    {
                        if (deleted)
                        {
                            refs.erase(ref.mRefNum);
                            continue;
                        }
                        int type = store.findStatic(ref.mRefID);
                        if (!typeFilter(type, size >= 2))
                            continue;
                        refs.insert_or_assign(ref.mRefNum, makePagedCellRef(ref));
                    }
                }
            }
            return refs;
        }

        std::map<ESM::RefNum, PagedCellRef> collectESM4References(
            float size, const osg::Vec2i& startCell, ESM::RefId worldspace)
        {
            std::map<ESM::RefNum, PagedCellRef> refs;
            const auto& store = MWBase::Environment::get().getWorld()->getStore();
            for (int cellX = startCell.x(); cellX < startCell.x() + size; ++cellX)
            {
                for (int cellY = startCell.y(); cellY < startCell.y() + size; ++cellY)
                {
                    const ESM4::Cell* cell
                        = store.get<ESM4::Cell>().searchExterior(ESM::ExteriorCellLocation(cellX, cellY, worldspace));
                    if (!cell)
                        continue;
                    for (const ESM4::Reference* ref4 : store.get<ESM4::Reference>().getByCell(cell->mId))
                    {
                        if (ref4->mFlags & ESM4::Rec_Disabled)
                            continue;
                        int type = store.findStatic(ref4->mBaseObj);
                        if (!typeFilter(type, size >= 2))
                            continue;
                        if (!ref4->mEsp.parent.isZeroOrUnset())
                        {
                            const ESM4::Reference* parentRef
                                = store.get<ESM4::Reference>().searchStatic(ref4->mEsp.parent);
                            if (parentRef)
                            {
                                bool parentDisabled = parentRef->mFlags & ESM4::Rec_Disabled;
                                bool inversed = ref4->mEsp.flags & ESM4::EnableParent::Flag_Inversed;
                                if (parentDisabled != inversed)
                                    continue;
                            }
                        }
                        refs.insert_or_assign(ref4->mId, makePagedCellRef(*ref4));
                    }
                }
            }
            return refs;
        }
    }

    ObjectPaging::Selection ObjectPaging::selectChunk(
        const ChunkRequest& chunk, const osg::Vec3f& viewPoint, const std::atomic_bool* abort, bool bridge)
    {
        Diagnostics::Loading::Timer selectionTimer(Diagnostics::Loading::Stage::PagedCandidateSelection);
        Selection result{ {}, selectionRevision({ chunk.mCenter, chunk.mSize, chunk.mActiveGrid }),
            mSceneManager->getVFS()->getIndexRevision(), cacheEpoch() };
        const auto& [size, center, flags, activeGrid] = chunk;
        const auto lod = static_cast<unsigned char>(flags >> (4 * 4));
        const auto cellSize = getCellSize(mWorldspace);
        const float minSize = mMinSizeMergeFactor ? mMinSize * mMinSizeMergeFactor : mMinSize;
        const osg::Vec2i startCell(static_cast<int>(std::floor(center.x() - size / 2.f)),
            static_cast<int>(std::floor(center.y() - size / 2.f)));
        const MWBase::World& world = *MWBase::Environment::get().getWorld();
        const MWWorld::ESMStore& store = world.getStore();

        std::map<ESM::RefNum, PagedCellRef> refs;

        if (mWorldspace == ESM::Cell::sDefaultWorldspaceId)
        {
            Diagnostics::Scope refsTiming("terrain", "objects", "collectReferences");
            Diagnostics::Loading::Timer refsLoading(Diagnostics::Loading::Stage::ObjectReferences);
            if (size <= 1.f)
            {
                // Every subcell chunk reads the same full cell before filtering
                // positions. Cache that exact resolved list, including moved and
                // deleted-reference handling. Dynamic filters remain below.
                struct IncompleteReferences
                {
                    std::map<ESM::RefNum, PagedCellRef> refs;
                };
                try
                {
                    const auto raw = mReferenceCache->cells.get({ startCell.x(), startCell.y() }, [&] {
                        Diagnostics::Scope readTiming("terrain", "objects", "readCellReferences");
                        Diagnostics::Loading::Timer readLoading(Diagnostics::Loading::Stage::ObjectReferenceRead);
                        bool complete = true;
                        auto result = collectESM3References(1.f, startCell, store, &complete);
                        if (!complete)
                            throw IncompleteReferences{ std::move(result) };
                        return result;
                    });
                    refs = *raw;
                }
                catch (IncompleteReferences& error)
                {
                    refs = std::move(error.refs);
                }
            }
            else
                refs = collectESM3References(size, startCell, store);
        }
        else
        {
            Diagnostics::Loading::Timer refsLoading(Diagnostics::Loading::Stage::ObjectReferences);
            refs = collectESM4References(size, startCell, mWorldspace);
        }

        if (activeGrid && !refs.empty())
        {
            std::lock_guard<std::mutex> lock(mRefTrackerMutex);
            const std::set<ESM::RefNum>& blacklist = getRefTracker().mBlacklist;
            if (blacklist.size() < refs.size())
            {
                for (ESM::RefNum ref : blacklist)
                    refs.erase(ref);
            }
            else
            {
                std::erase_if(refs, [&](const auto& ref) { return blacklist.contains(ref.first); });
            }
        }

        const osg::Vec2f minBound = (center - osg::Vec2f(size / 2.f, size / 2.f));
        const osg::Vec2f maxBound = (center + osg::Vec2f(size / 2.f, size / 2.f));
        const osg::Vec2i floorMinBound(
            static_cast<int>(std::floor(minBound.x())), static_cast<int>(std::floor(minBound.y())));
        const osg::Vec2i ceilMaxBound(
            static_cast<int>(std::ceil(maxBound.x())), static_cast<int>(std::ceil(maxBound.y())));
        for (const auto& [refNum, ref] : refs)
        {
            if (abort && abort->load(std::memory_order_relaxed))
                return result;
            if (size < 1.f)
            {
                const osg::Vec3f cellPos = ref.mPosition / static_cast<float>(cellSize);
                if ((minBound.x() > floorMinBound.x() && cellPos.x() < minBound.x())
                    || (minBound.y() > floorMinBound.y() && cellPos.y() < minBound.y())
                    || (maxBound.x() < ceilMaxBound.x() && cellPos.x() >= maxBound.x())
                    || (maxBound.y() < ceilMaxBound.y() && cellPos.y() >= maxBound.y()))
                    continue;
            }

            const float dSqr = (viewPoint - ref.mPosition).length2();
            if (!activeGrid && !bridge)
            {
                std::lock_guard<std::mutex> lock(mSizeCacheMutex);
                SizeCache::iterator found = mSizeCache.find(refNum);
                if (found != mSizeCache.end() && found->second.second == result.mEpoch
                    && found->second.first < dSqr * minSize * minSize)
                    continue;
            }

            if (Misc::ResourceHelpers::isHiddenMarker(ref.mRefId))
                continue;

            const int type = store.findStatic(ref.mRefId);
            VFS::Path::Normalized model = getModel(type, ref.mRefId, store);
            if (model.empty())
                continue;
            model = Misc::ResourceHelpers::correctMeshPath(model);

            if (activeGrid && type != ESM::REC_STAT && type != ESM::REC_STAT4)
            {
                model = Misc::ResourceHelpers::correctActorModelPath(model, mSceneManager->getVFS());
                constexpr VFS::Path::ExtensionView nif("nif");
                if (model.extension() == nif)
                {
                    VFS::Path::Normalized kfname = model;
                    constexpr VFS::Path::ExtensionView kf("kf");
                    kfname.changeExtension(kf);
                    if (mSceneManager->getVFS()->exists(kfname))
                        continue;
                }
            }

            if (!activeGrid)
            {
                std::lock_guard<std::mutex> lock(mLODNameCacheMutex);
                const auto revision = mSceneManager->getVFS()->getIndexRevision();
                if (mLODNameCacheRevision != revision)
                {
                    mLODNameCache.clear();
                    mLODNameCacheRevision = revision;
                }
                LODNameCacheKey key{ model, lod };
                LODNameCache::const_iterator found = mLODNameCache.lower_bound(key);
                if (found != mLODNameCache.end() && found->first == key)
                    model = found->second;
                else
                    model = mLODNameCache
                                .emplace_hint(found, std::move(key),
                                    Misc::ResourceHelpers::getLODMeshName(world.getESMVersions()[refNum.mContentFile],
                                        model, *mSceneManager->getVFS(), lod))
                                ->second;
            }

            result.mItems.push_back({ refNum, ref, std::move(model), dSqr });
        }
        return result;
    }

    osg::ref_ptr<osg::Node> ObjectPaging::createChunk(const ChunkRequest& chunk, const osg::Vec3f& viewPoint, const Selection& selected,
        bool compile, const std::atomic_bool* abort, bool modelsPrepared, PendingCompile& pending, bool bridge)
    {
        Diagnostics::Scope createTiming("terrain", "objects", "createChunk");
        const auto& size = chunk.mSize;
        const auto& center = chunk.mCenter;
        const bool activeGrid = chunk.mActiveGrid;
        struct InstanceList
        {
            std::vector<const PagedCellRef*> mInstances;
            AnalyzeVisitor::Result mAnalyzeResult;
            bool mNeedCompile = false;
        };
        typedef std::map<osg::ref_ptr<const osg::Node>, InstanceList> NodeMap;
        NodeMap nodes;
        std::vector<NodeMap::iterator> firstReferenceOrder;
        // Only retained while building this chunk; paths include the selected LOD.
        std::map<VFS::Path::Normalized, osg::ref_ptr<const osg::Node>> chunkTemplates;
        const osg::ref_ptr<RefnumSet> refnumSet = activeGrid ? new RefnumSet : nullptr;

        // Mask_UpdateVisitor is used in such cases in NIF loader:
        // 1. For collision nodes, which is not supposed to be rendered.
        // 2. For nodes masked via Flag_Hidden (VisController can change this flag value at runtime).
        // Since ObjectPaging does not handle VisController, we can just ignore both types of nodes.
        constexpr auto copyMask = ~Mask_UpdateVisitor;

        const int cellSize = getCellSize(mWorldspace);
        const float smallestDistanceToChunk = (size > 1 / 8.f) ? (size * cellSize) : 0.f;
        const float higherDistanceToChunk
            = activeGrid ? ((size < 1) ? 5 : 3) * cellSize * size + 1 : smallestDistanceToChunk + 1;
        const LODRange lodDistances = (activeGrid || bridge) ? LODRange{ 0.f, std::numeric_limits<float>::max() }
                                                 : LODRange{ smallestDistanceToChunk, higherDistanceToChunk };

        AnalyzeVisitor analyzeVisitor(copyMask);
        const float minSize = mMinSizeMergeFactor ? mMinSize * mMinSizeMergeFactor : mMinSize;
        auto* jobs = MWBase::Environment::get().getNativeJobPoolOrNull();
        const bool prepare = !modelsPrepared && canPreloadBatches();
        const auto consume = [&](const ESM::RefNum& refNum, const PagedCellRef& ref,
                                 const VFS::Path::Normalized& model, float dSqr) {
            osg::ref_ptr<const osg::Node> cnode = [&] {
                Diagnostics::Scope modelTiming("terrain", "objects", "getModelTemplate");
                Diagnostics::Loading::Timer modelLoading(Diagnostics::Loading::Stage::ObjectTemplates);
                if (auto found = chunkTemplates.find(model); found != chunkTemplates.end())
                    return found->second;
                auto node = mSceneManager->getTemplate(model, false);
                chunkTemplates.emplace(model, node);
                return node;
            }();

            if (activeGrid)
            {
                if (cnode->getNumChildrenRequiringUpdateTraversal() > 0
                    || SceneUtil::hasUserDescription(cnode, Constants::NightDayLabel)
                    || SceneUtil::hasUserDescription(cnode, Constants::HerbalismLabel)
                    || (cnode->getName() == "Collada visual scene group"
                        && dynamic_cast<const osgAnimation::BasicAnimationManager*>(cnode->getUpdateCallback())))
                    return;
                else
                    refnumSet->mRefnums.push_back(refNum);
            }

            {
                std::lock_guard<std::mutex> lock(mRefTrackerMutex);
                if (getRefTracker().mDisabled.count(refNum))
                    return;
            }

            const float radius2 = cnode->getBound().radius2() * ref.mScale * ref.mScale;
            if (radius2 < dSqr * minSize * minSize && !activeGrid && !bridge)
            {
                std::lock_guard<std::mutex> lock(mSizeCacheMutex);
                mSizeCache[refNum] = { radius2, selected.mEpoch };
                return;
            }

            const auto emplaced = nodes.emplace(std::move(cnode), InstanceList());
            if (emplaced.second)
            {
                firstReferenceOrder.push_back(emplaced.first);
                const osg::Node* const nodePtr = emplaced.first->first.get();
                if (!bridge)
                {
                    Diagnostics::Loading::Timer analysisLoading(Diagnostics::Loading::Stage::ObjectAnalysis);
                    analyzeVisitor.mDistances = lodDistances / ref.mScale;
                    // OSG has no const visitor. Analysis does not modify the template.
                    const_cast<osg::Node*>(nodePtr)->accept(analyzeVisitor);
                    emplaced.first->second.mAnalyzeResult = analyzeVisitor.retrieveResult();
                }
                emplaced.first->second.mNeedCompile = compile && nodePtr->referenceCount() <= 2;
            }
            else if (!bridge)
                analyzeVisitor.addInstance(emplaced.first->second.mAnalyzeResult);
            emplaced.first->second.mInstances.push_back(&ref);
        };

        // First-use order controls scene attachment. Worker completion order and node addresses do not.
        std::vector<const Selection::Item*> batch;
        auto flush = [&] {
            std::vector<VFS::Path::NormalizedView> paths;
            for (const auto& entry : batch)
                if (!chunkTemplates.contains(entry->mModel)
                    && std::find(paths.begin(), paths.end(), entry->mModel) == paths.end())
                    paths.emplace_back(entry->mModel);
            const auto consumeBatch = [&] {
                for (const auto& entry : batch)
                {
                    if (abort && abort->load(std::memory_order_relaxed))
                        return;
                    consume(entry->mNumber, entry->mRef, entry->mModel, entry->mDistanceSquared);
                }
            };
            if (compile)
                mSceneManager->withPreparedPagedModels(paths, *jobs, abort, consumeBatch);
            else
            {
                // Foreground traversal keeps its existing, smaller NIF decoding workload.
                mSceneManager->prepareNifInputs(paths, *jobs, abort);
                consumeBatch();
            }
            batch.clear();
        };
        for (const auto& item : selected.mItems)
        {
            if (abort && abort->load(std::memory_order_relaxed))
                return nullptr;
            Diagnostics::Loading::Timer selectionTiming(Diagnostics::Loading::Stage::ObjectSelection);
            if (prepare)
            {
                batch.push_back(&item);
                if (batch.size() == 32)
                    flush();
            }
            else
                consume(item.mNumber, item.mRef, item.mModel, item.mDistanceSquared);
        }
        if (!batch.empty())
            flush();

        const osg::Vec3f worldCenter
            = osg::Vec3f(center.x(), center.y(), 0) * static_cast<float>(getCellSize(mWorldspace));
        osg::ref_ptr<osg::Group> group = new osg::Group;
        osg::ref_ptr<osg::Group> mergeGroup = new osg::Group;
        osg::ref_ptr<Resource::TemplateMultiRef> templateRefs = new Resource::TemplateMultiRef;
        osgUtil::StateToCompile stateToCompile(0, nullptr);
        CopyOp copyop(activeGrid, copyMask);
        for (const auto entry : firstReferenceOrder)
        {
            if (abort && abort->load(std::memory_order_relaxed))
                return nullptr;
            const auto& pair = *entry;
            Diagnostics::Loading::Timer instancesLoading(Diagnostics::Loading::Stage::ObjectInstances);
            const osg::Node* cnode = pair.first;

            const AnalyzeVisitor::Result& analyzeResult = pair.second.mAnalyzeResult;

            const float mergeCost = analyzeResult.mNumVerts * size;
            const float mergeBenefit = analyzeVisitor.getMergeBenefit(analyzeResult) * mMergeFactor;
            const bool merge = !bridge && mergeBenefit > mergeCost;

            const float factor2
                = mergeBenefit > 0 ? std::min(1.f, mergeCost * mMinSizeCostMultiplier / mergeBenefit) : 1;
            const float minSizeMergeFactor2 = (1 - factor2) * mMinSizeMergeFactor + factor2;
            const float minSizeMerged = minSizeMergeFactor2 > 0 ? mMinSize * minSizeMergeFactor2 : mMinSize;

            unsigned int numinstances = 0;
            for (const PagedCellRef* refPtr : pair.second.mInstances)
            {
                const PagedCellRef& ref = *refPtr;

                if (!bridge && !activeGrid && minSizeMerged != minSize
                    && cnode->getBound().radius2() * ref.mScale * ref.mScale
                        < (viewPoint - ref.mPosition).length2() * minSizeMerged * minSizeMerged)
                    continue;

                const osg::Vec3f nodePos = ref.mPosition - worldCenter;
                const osg::Quat nodeAttitude = osg::Quat(ref.mRotation.z(), osg::Vec3f(0, 0, -1))
                    * osg::Quat(ref.mRotation.y(), osg::Vec3f(0, -1, 0))
                    * osg::Quat(ref.mRotation.x(), osg::Vec3f(-1, 0, 0));
                const osg::Vec3f nodeScale(ref.mScale, ref.mScale, ref.mScale);

                osg::ref_ptr<osg::Group> trans;
                if (merge)
                {
                    // Optimizer currently supports only MatrixTransforms.
                    osg::Matrixf matrix;
                    matrix.preMultTranslate(nodePos);
                    matrix.preMultRotate(nodeAttitude);
                    matrix.preMultScale(nodeScale);
                    trans = new osg::MatrixTransform(matrix);
                    trans->setDataVariance(osg::Object::STATIC);
                }
                else
                {
                    trans = new SceneUtil::PositionAttitudeTransform;
                    SceneUtil::PositionAttitudeTransform* pat
                        = static_cast<SceneUtil::PositionAttitudeTransform*>(trans.get());
                    pat->setPosition(nodePos);
                    pat->setScale(nodeScale);
                    pat->setAttitude(nodeAttitude);
                }

                // DO NOT COPY AND PASTE THIS CODE. Cloning osg::Geometry without also cloning its contained Arrays is
                // generally unsafe. In this specific case the operation is safe under the following two assumptions:
                // - When Arrays are removed or replaced in the cloned geometry, the original Arrays in their place must
                // outlive the cloned geometry regardless. (ensured by TemplateMultiRef)
                // - Arrays that we add or replace in the cloned geometry must be explicitely forbidden from reusing
                // BufferObjects of the original geometry. (ensured by needvbo() in optimizer.cpp)
                copyop.setCopyFlags(merge ? osg::CopyOp::DEEP_COPY_NODES | osg::CopyOp::DEEP_COPY_DRAWABLES
                                          : osg::CopyOp::DEEP_COPY_NODES);
                copyop.mDistances = lodDistances / ref.mScale;
                copyop.copy(cnode, trans);

                if (activeGrid)
                {
                    if (merge)
                    {
                        AddRefnumMarkerVisitor visitor(ref.mRefNum);
                        trans->accept(visitor);
                    }
                    else
                    {
                        osg::ref_ptr<RefnumMarker> marker = new RefnumMarker;
                        marker->mRefnum = ref.mRefNum;
                        trans->getOrCreateUserDataContainer()->addUserObject(marker);
                    }
                }

                osg::Group* const attachTo = merge ? mergeGroup : group;
                attachTo->addChild(trans);
                ++numinstances;
            }
            if (numinstances > 0)
            {
                // add a ref to the original template to help verify the safety of shallow cloning operations
                // in addition, we hint to the cache that it's still being used and should be kept in cache
                templateRefs->addRef(cnode);

                if (pair.second.mNeedCompile)
                {
                    int mode = osgUtil::GLObjectsVisitor::COMPILE_STATE_ATTRIBUTES;
                    if (!merge)
                        mode |= osgUtil::GLObjectsVisitor::COMPILE_DISPLAY_LISTS;
                    stateToCompile._mode = mode;
                    const_cast<osg::Node*>(cnode)->accept(stateToCompile);
                }
            }
        }

        const osg::Vec3f relativeViewPoint = viewPoint - worldCenter;

        if (abort && abort->load(std::memory_order_relaxed))
            return nullptr;
        if (mergeGroup->getNumChildren())
        {
            SceneUtil::Optimizer optimizer;
            if (size > 1 / 8.f)
            {
                optimizer.setViewPoint(relativeViewPoint);
                optimizer.setMergeAlphaBlending(true);
            }
            optimizer.setIsOperationPermissibleForObjectCallback(new CanOptimizeCallback);
            const unsigned int options = SceneUtil::Optimizer::FLATTEN_STATIC_TRANSFORMS
                | SceneUtil::Optimizer::REMOVE_REDUNDANT_NODES | SceneUtil::Optimizer::MERGE_GEOMETRY;

            {
                Diagnostics::Scope mergeTiming("terrain", "objects", "mergeGeometry");
                Diagnostics::Loading::Timer mergeLoading(Diagnostics::Loading::Stage::ObjectMerge);
                optimizer.optimize(mergeGroup, options);
            }

            group->addChild(mergeGroup);

            if (mDebugBatches)
            {
                DebugVisitor dv;
                mergeGroup->accept(dv);
            }
            if (compile)
            {
                stateToCompile._mode = osgUtil::GLObjectsVisitor::COMPILE_DISPLAY_LISTS;
                mergeGroup->accept(stateToCompile);
            }
        }

        osgUtil::IncrementalCompileOperation* const ico = mSceneManager->getIncrementalCompileOperation();
        if (!stateToCompile.empty() && ico)
        {
            Diagnostics::Loading::Timer compileLoading(Diagnostics::Loading::Stage::ObjectCompile);
            auto compileSet = SceneUtil::prepareCompileSet(*ico, group, &stateToCompile);
            pending.mOperation = ico;
            pending.mSet = std::move(compileSet);
        }

        Diagnostics::Loading::Timer finishLoading(Diagnostics::Loading::Stage::ObjectFinish);
        group->getBound();
        group->setNodeMask(Mask_Static);
        osg::UserDataContainer* udc = group->getOrCreateUserDataContainer();
        if (activeGrid)
        {
            std::sort(refnumSet->mRefnums.begin(), refnumSet->mRefnums.end());
            refnumSet->mRefnums.erase(
                std::unique(refnumSet->mRefnums.begin(), refnumSet->mRefnums.end()), refnumSet->mRefnums.end());
            udc->addUserObject(refnumSet);
            group->addCullCallback(new SceneUtil::LightListCallback);
        }
        udc->addUserObject(templateRefs);

        return group;
    }

    unsigned int ObjectPaging::getNodeMask()
    {
        return Mask_Static;
    }

    namespace
    {
        osg::Vec2f clampToCell(const osg::Vec3f& cellPos, const osg::Vec2i& cell)
        {
            return osg::Vec2f(std::clamp(cellPos.x(), static_cast<float>(cell.x()), cell.x() + 1.f),
                std::clamp(cellPos.y(), static_cast<float>(cell.y()), cell.y() + 1.f));
        }

        class CollectIntersecting
        {
        public:
            explicit CollectIntersecting(
                bool activeGridOnly, const osg::Vec3f& position, const osg::Vec2i& cell, ESM::RefId worldspace)
                : mActiveGridOnly(activeGridOnly)
                , mPosition(clampToCell(position / static_cast<float>(getCellSize(worldspace)), cell))
            {
            }

            void operator()(const ChunkId& id, osg::Object* /*obj*/)
            {
                if (mActiveGridOnly && !std::get<2>(id))
                    return;
                if (intersects(id))
                    mCollected.push_back(id);
            }

            const std::vector<ChunkId>& getCollected() const { return mCollected; }

        private:
            bool intersects(ChunkId id) const
            {
                const osg::Vec2f center = std::get<0>(id);
                const float halfSize = std::get<1>(id) / 2;
                return mPosition.x() >= center.x() - halfSize && mPosition.y() >= center.y() - halfSize
                    && mPosition.x() <= center.x() + halfSize && mPosition.y() <= center.y() + halfSize;
            }

            bool mActiveGridOnly;
            osg::Vec2f mPosition;
            std::vector<ChunkId> mCollected;
        };
    }

    bool ObjectPaging::enableObject(
        int type, ESM::RefNum refnum, const osg::Vec3f& pos, const osg::Vec2i& cell, bool enabled)
    {
        if (!typeFilter(type, false))
            return false;

        std::lock_guard<std::mutex> lock(mRefTrackerMutex);
        if (enabled && !getWritableRefTracker().mDisabled.erase(refnum))
            return false;
        if (!enabled && !getWritableRefTracker().mDisabled.insert(refnum).second)
            return false;
        if (mRefTrackerLocked)
            return false;
        ++mSelectionRevision;
        mSelectionRevisions.invalidate(cell.x(), cell.y(), false);
        invalidateScenery(pos, cell);

        CollectIntersecting ccf(false, pos, cell, mWorldspace);
        mCache->call(ccf);
        if (ccf.getCollected().empty())
            return false;
        for (const ChunkId& chunk : ccf.getCollected())
            mCache->removeFromObjectCache(chunk);
        return true;
    }

    bool ObjectPaging::blacklistObject(int type, ESM::RefNum refnum, const osg::Vec3f& pos, const osg::Vec2i& cell)
    {
        if (!typeFilter(type, false))
            return false;

        std::lock_guard<std::mutex> lock(mRefTrackerMutex);
        if (!getWritableRefTracker().mBlacklist.insert(refnum).second)
            return false;
        if (mRefTrackerLocked)
            return false;
        ++mSelectionRevision;
        mSelectionRevisions.invalidate(cell.x(), cell.y(), true);
        invalidateScenery(pos, cell);

        CollectIntersecting ccf(true, pos, cell, mWorldspace);
        mCache->call(ccf);
        if (ccf.getCollected().empty())
            return false;
        for (const ChunkId& chunk : ccf.getCollected())
            mCache->removeFromObjectCache(chunk);
        return true;
    }

    void ObjectPaging::clear()
    {
        mReferenceCache->cells.clear();
        std::lock_guard<std::mutex> lock(mRefTrackerMutex);
        ++mSelectionRevision;
        mSelectionRevisions.clear();
        mScenery->clear();
        mRefTrackerNew.mDisabled.clear();
        mRefTrackerNew.mBlacklist.clear();
        mRefTrackerLocked = true;
    }

    bool ObjectPaging::unlockCache()
    {
        const std::lock_guard lock(mRefTrackerMutex);
        if (!mRefTrackerLocked)
            return false;
        mRefTrackerLocked = false;
        if (mRefTracker == mRefTrackerNew)
            return false;
        mRefTracker = mRefTrackerNew;
        ++mSelectionRevision;
        mSelectionRevisions.clear();
        mCache->clear();
        mScenery->clear();
        return true;
    }

    void ObjectPaging::invalidateScenery(const osg::Vec3f& position, const osg::Vec2i& cell)
    {
        // Caller holds mRefTrackerMutex. The ownership cell, rather than mesh
        // bounds, controls participation in the far/active handover.
        if (auto it = mScenery->mBridges.find({ cell.x(), cell.y() }); it != mScenery->mBridges.end())
            mScenery->erase(it);
        const auto clamped = clampToCell(position / static_cast<float>(getCellSize(mWorldspace)), cell);
        const Terrain::SceneryCell edge{ static_cast<int>(std::floor(clamped.x())),
            static_cast<int>(std::floor(clamped.y())) };
        if (auto it = mScenery->mBridges.find(edge); it != mScenery->mBridges.end())
            mScenery->erase(it);
    }

    namespace
    {
        struct GetRefnumsFunctor
        {
            GetRefnumsFunctor(std::vector<ESM::RefNum>& output)
                : mOutput(output)
            {
            }
            void operator()(MWRender::ChunkId chunkId, osg::Object* obj)
            {
                if (!std::get<2>(chunkId))
                    return;
                const osg::Vec2f& center = std::get<0>(chunkId);
                const bool activeGrid = (center.x() > mActiveGrid.x() || center.y() > mActiveGrid.y()
                    || center.x() < mActiveGrid.z() || center.y() < mActiveGrid.w());
                if (!activeGrid)
                    return;

                osg::UserDataContainer* udc = obj->getUserDataContainer();
                if (udc && udc->getNumUserObjects())
                {
                    RefnumSet* refnums = dynamic_cast<RefnumSet*>(udc->getUserObject(0));
                    if (!refnums)
                        return;
                    mOutput.insert(mOutput.end(), refnums->mRefnums.begin(), refnums->mRefnums.end());
                }
            }
            osg::Vec4i mActiveGrid;
            std::vector<ESM::RefNum>& mOutput;
        };
    }

    void ObjectPaging::getPagedRefnums(const osg::Vec4i& activeGrid, std::vector<ESM::RefNum>& out)
    {
        const std::lock_guard lock(mRefTrackerMutex);
        refreshCacheEpoch();
        GetRefnumsFunctor grf(out);
        grf.mActiveGrid = activeGrid;
        mCache->call(grf);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    void ObjectPaging::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        Resource::reportStats("Object Chunk", frameNumber, mCache->getStats(), *stats);
    }

}
