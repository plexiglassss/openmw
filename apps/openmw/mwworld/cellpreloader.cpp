#include "cellpreloader.hpp"

#include <chrono>
#include <components/esm/util.hpp>

#include <components/diagnostics/timing.hpp>
#include <components/diagnostics/loading.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <optional>
#include <span>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/loadinglistener/reporter.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/framestats.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/keyframemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/asyncpreparation.hpp>
#include <components/sceneutil/preloadretention.hpp>
#include <components/sceneutil/preparationcost.hpp>
#include <components/sceneutil/scenefootprint.hpp>
#include <components/settings/values.hpp>
#include <components/terrain/view.hpp>
#include <components/terrain/world.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwrender/landmanager.hpp"

#include "cellstore.hpp"
#include "class.hpp"
#include "manualref.hpp"
#include "preloaddependencies.hpp"

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
        void appendActorResources(const MWWorld::ConstPtr& ptr)
        {
            const auto& settings = Settings::models();
            const ActorPreloadModels paths{ settings.mXbaseanim.get(), settings.mBaseanim.get(),
                settings.mBaseanimfemale.get(), settings.mBaseanimkna.get(), settings.mXargonianswimkna.get() };
            const auto begin = mAnimations.size();
            if (ptr.getType() == ESM::REC_NPC_)
            {
                const auto& npc = *ptr.get<ESM::NPC>()->mBase;
                if (const auto* race = mStore.get<ESM::Race>().search(npc.mRace))
                    appendActorAnimationPreloadModels(npc, *race, paths, mAnimations);
            }
            else if (ptr.getType() == ESM::REC_CREA)
                appendActorAnimationPreloadModels(*ptr.get<ESM::Creature>()->mBase, paths, mAnimations);
            mOut.insert(mOut.end(), mAnimations.begin() + begin, mAnimations.end());
            appendInitialEquipment(ptr);
        }

        void appendInitialEquipment(const MWWorld::ConstPtr& ptr)
        {
            if (ptr.getRefData().getCustomData())
                return; // The existing class path already captures current equipped items.
            if (ptr.getType() == ESM::REC_NPC_)
            {
                const auto* npc = ptr.get<ESM::NPC>()->mBase;
                appendEquipmentPreloadModels(mStore, npc->mInventory, !npc->isMale(), mOptions, mOut);
            }
            else if (ptr.getType() == ESM::REC_CREA)
                appendEquipmentPreloadModels(
                    mStore, ptr.get<ESM::Creature>()->mBase->mInventory, false, mOptions, mOut);
        }

        bool operator()(const MWWorld::ConstPtr& ptr)
        {
            const auto start = mOut.size();
            ptr.getClass().getModelsToPreload(ptr, mOut);
            mCollisionScales.insert(mCollisionScales.end(), mOut.size() - start,
                ptr.getClass().isActor() ? 1.f : ptr.getCellRef().getScale());

            // Exterior statics normally use ObjectPaging's own copy path. Reserve the
            // bounded instance budget for objects that still get individual scene nodes.
            if (!mExterior || (ptr.getType() != ESM::REC_STAT && ptr.getType() != ESM::REC_STAT4))
                mInstances.insert(mInstances.end(), mOut.begin() + start, mOut.end());

            // New actors have no runtime inventory yet. Inspect base records only:
            // do not create custom data, auto-equip, spawn, or advance the game's RNG.
            if (!Misc::useActorResourcePreload())
                return true;
            appendActorResources(ptr);
            if (ptr.getType() == ESM::REC_LEVC && mCandidateRecordsLeft
                && mPredictedLists.insert(ptr.getCellRef().getRefId()).second)
            {
                const std::array roots{ ptr.getCellRef().getRefId() };
                auto options = mOptions;
                options.mMaxCandidates = std::min(options.mMaxCandidates, mCandidateRecordsLeft);
                for (const auto& id : collectPreloadCandidates(mStore, roots, options))
                {
                    if (!mPredictedActors.insert(id).second)
                        continue;
                    --mCandidateRecordsLeft;
                    const auto type = mStore.find(id);
                    if (type != ESM::REC_NPC_ && type != ESM::REC_CREA)
                        continue;
                    try
                    {
                        ManualRef candidate(mStore, id);
                        const auto actor = candidate.getPtr();
                        actor.getClass().getModelsToPreload(actor, mOut);
                        appendActorResources(actor);
                    }
                    catch (const std::exception&)
                    {
                        // A broken optional candidate must not discard the cell's other resources.
                    }
                }
            }
            // Additional actor/equipment candidates have no placed object transform.
            mCollisionScales.resize(mOut.size(), 1.f);
            return true;
        }

        std::vector<VFS::Path::NormalizedView>& mOut;
        std::vector<VFS::Path::NormalizedView>& mInstances;
        std::vector<VFS::Path::NormalizedView>& mAnimations;
        std::vector<float>& mCollisionScales;
        bool mExterior;
        const ESMStore& mStore;
        PreloadDependencyOptions mOptions;
        std::size_t mCandidateRecordsLeft = 128;
        std::set<ESM::RefId> mPredictedActors;
        std::set<ESM::RefId> mPredictedLists;
    };

    struct CellPreloader::StaticPreloads
    {
        struct Resources
        {
            std::vector<osg::ref_ptr<const osg::Referenced>> mObjects;
            std::vector<std::shared_ptr<const void>> mOwnedObjects;
            std::vector<Resource::SceneInstanceCache::TicketPtr> mInstances;
        };
        using Jobs = SceneUtil::AsyncPreparation<ESM::ExteriorCellLocation, std::shared_ptr<Resources>>;
        Jobs mJobs;
        std::map<ESM::ExteriorCellLocation, std::shared_ptr<SceneUtil::PreparationSample>> mSamples;
        SceneUtil::PreparationCost mCost;
        std::pair<std::uint64_t, std::uint64_t> mGeneration;
        explicit StaticPreloads(std::shared_ptr<SceneUtil::WorkQueue> queue)
            : mJobs(queue, 2, 8, 256ull * 1024 * 1024, 4.0, 10.0) {}

        double observe()
        {
            double oldest = 0;
            for (auto it = mSamples.begin(); it != mSamples.end();)
            {
                const auto& sample = *it->second;
                if (sample.mComplete.load(std::memory_order_acquire))
                {
                    if (sample.mSuccessful)
                        mCost.observe(sample.mQueueSeconds, sample.mWorkSeconds, sample.mAssets, sample.mColdAssets);
                    it = mSamples.erase(it);
                }
                else if (!mJobs.contains(it->first))
                    it = mSamples.erase(it); // Cancelled queued work may never run.
                else
                {
                    oldest = std::max(oldest, sample.age());
                    ++it;
                }
            }
            return oldest;
        }
    };

    double CellPreloader::preparationLeadTime(double configured)
    {
        double oldest = 0;
        for (auto& [cell, entry] : mPreloadCells)
        {
            if (!cell->isExterior() || !entry.mTiming)
                continue;
            const auto& sample = *entry.mTiming;
            if (sample.mComplete.load(std::memory_order_acquire))
            {
                if (sample.mSuccessful)
                {
                    mPreparationCost.observe(sample.mQueueSeconds, sample.mWorkSeconds, sample.mAssets, sample.mColdAssets);
                    entry.mPreparationSeconds = sample.mWorkSeconds;
                }
                entry.mTiming.reset();
            }
            else
                oldest = std::max(oldest, sample.age());
        }
        double lead = mPreparationCost.leadTime(configured, oldest);
        if (mStaticPreloads)
        {
            oldest = mStaticPreloads->observe();
            lead = std::max(lead, mStaticPreloads->mCost.leadTime(configured, oldest));
        }
        return lead;
    }

    void CellPreloader::preloadStaticExterior(ESM::ExteriorCellLocation location, double timestamp)
    {
        if (!mWorkQueue || !mMaxCacheSize || location.mWorldspace != ESM::Cell::sDefaultWorldspaceId)
            return; // Other record formats keep their existing resource preloader.
        for (const auto& [cell, entry] : mPreloadCells)
            if (cell->isExterior() && cell->getCell()->getExteriorCellLocation() == location)
                return;
        if (!mStaticPreloads)
            mStaticPreloads = std::make_unique<StaticPreloads>(mWorkQueue);
        auto* scene = mResourceSystem->getSceneManager();
        const auto generation = std::pair(scene->modelSettingsRevision(), scene->getVFS()->getIndexRevision());
        if (generation != mStaticPreloads->mGeneration)
        {
            mStaticPreloads->mJobs.clear();
            mStaticPreloads->mSamples.clear();
            mStaticPreloads->mCost = {};
            mStaticPreloads->mGeneration = generation;
        }
        if (mStaticPreloads->mJobs.contains(location))
        {
            mStaticPreloads->mJobs.poll(location, timestamp);
            return;
        }
        if (mStaticPreloads->mJobs.size() >= std::min<std::size_t>(8, mMaxCacheSize))
            return;
        const ESMStore* store = &*MWBase::Environment::get().getESMStore();
        const auto* source = store->get<ESM::Cell>().searchStatic(location.mX, location.mY);
        if (!source)
            return;
        auto sample = Misc::useCostAwarePreload() ? std::make_shared<SceneUtil::PreparationSample>() : nullptr;
        // Copy immutable plugin metadata. Do not call WorldModel::getExterior,
        // load a CellStore, register references, choose spawns or emit Lua events.
        mStaticPreloads->mJobs.request(location, timestamp,
            [cell = *source, store, scene, shapes = mBulletShapeManager,
                instances = mPreloadInstances, sample](const std::atomic_bool& abort) -> StaticPreloads::Jobs::Prepared {
                SceneUtil::PreparationSample::Run measured(sample.get(), abort);
                std::map<ESM::RefNum, std::pair<ESM::RefId, float>> refs;
                ESM::ReadersCache readers;
                for (std::size_t i = 0; i < cell.mContextList.size() && !abort; ++i)
                {
                    const auto reader = readers.get(static_cast<std::size_t>(cell.mContextList[i].index));
                    cell.restore(*reader, i);
                    ESM::CellRef ref;
                    ESM::MovedCellRef movedRef;
                    bool deleted = false, moved = false;
                    while (!abort && ESM::Cell::getNextRef(*reader, ref, deleted, movedRef, moved,
                        ESM::Cell::GetNextRefMode::LoadOnlyNotMoved))
                    {
                        if (moved || std::find(cell.mMovedRefs.begin(), cell.mMovedRefs.end(), ref.mRefNum)
                                != cell.mMovedRefs.end())
                            continue;
                        if (deleted)
                            refs.erase(ref.mRefNum);
                        else
                            refs.insert_or_assign(ref.mRefNum, std::pair(ref.mRefID, ref.mScale));
                    }
                }
                for (const auto& [ref, deleted] : cell.mLeasedRefs)
                    if (deleted)
                        refs.erase(ref.mRefNum);
                    else
                        refs.insert_or_assign(ref.mRefNum, std::pair(ref.mRefID, ref.mScale));
                struct Model
                {
                    std::vector<float> mScales;
                    bool mIndividual = false;
                };
                std::map<VFS::Path::Normalized, Model> models;
                const auto model = [](const auto* record) {
                    return record ? record->mModel.getNormalized() : VFS::Path::NormalizedView{};
                };
                for (const auto& [number, reference] : refs)
                {
                    if (abort)
                        return {};
                    VFS::Path::NormalizedView path;
                    const auto& [id, scale] = reference;
                    const int type = store->findStatic(id);
                    switch (type)
                    {
                        case ESM::REC_STAT: path = model(store->get<ESM::Static>().searchStatic(id)); break;
                        case ESM::REC_ACTI: path = model(store->get<ESM::Activator>().searchStatic(id)); break;
                        case ESM::REC_DOOR: path = model(store->get<ESM::Door>().searchStatic(id)); break;
                        case ESM::REC_CONT: path = model(store->get<ESM::Container>().searchStatic(id)); break;
                        case ESM::REC_LIGH: path = model(store->get<ESM::Light>().searchStatic(id)); break;
                        default: continue;
                    }
                    if (path.empty())
                        continue;
                    const auto corrected = Misc::ResourceHelpers::correctMeshPath(path);
                    if (!models.contains(corrected) && models.size() >= 128)
                        continue; // Optional preparation is bounded; activation is unchanged.
                    auto& value = models[corrected];
                    if (value.mScales.size() < 128)
                        value.mScales.push_back(scale);
                    value.mIndividual |= type != ESM::REC_STAT;
                }
                auto prepared = std::make_shared<StaticPreloads::Resources>();
                SceneUtil::SceneFootprint footprint;
                std::size_t bytes = 0, count = 0;
                for (const auto& [path, model] : models)
                {
                    if (abort)
                        break;
                    try
                    {
                        if (sample)
                        {
                            ++sample->mAssets;
                            sample->mColdAssets += !scene->getCachedTemplate(path);
                        }
                        auto node = scene->getTemplate(path);
                        bytes += footprint.measure(const_cast<osg::Node&>(*node));
                        prepared->mObjects.emplace_back(std::move(node));
                        prepared->mOwnedObjects.emplace_back(shapes->getShape(path));
                        bytes += 1024 * 1024; // Collision admission allowance, not measured allocation.
                        for (std::size_t i = 0; instances && i < model.mScales.size() && count < 128 && !abort; ++i, ++count)
                        {
                            prepared->mOwnedObjects.emplace_back(shapes->cacheInstance(path, model.mScales[i]));
                            if (model.mIndividual)
                                if (auto ticket = scene->cacheInstance(path, abort))
                                    prepared->mInstances.push_back(std::move(ticket));
                        }
                    }
                    catch (const std::exception& error)
                    {
                        Log(Debug::Warning) << "Static resource preparation failed for " << path << ": " << error.what();
                    }
                }
                return { std::move(prepared), {}, {}, bytes };
            });
        if (sample && mStaticPreloads->mJobs.contains(location))
            mStaticPreloads->mSamples.emplace(location, std::move(sample));
    }

    /// Worker thread item: preload models in a cell.
    class PreloadItem : public SceneUtil::WorkItem
    {
    public:
        std::shared_ptr<SceneUtil::PreparationSample> mTiming;
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
            , mPreloadAdditionalAnimations(
                  Misc::useActorResourcePreload() && Settings::game().mUseAdditionalAnimSources)
            , mAbort(false)
        {
            mTerrainView = mTerrain->createView();

            const auto& player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            // Do not initialize player stats as a side effect of resource prediction.
            const int level
                = player.getRefData().getCustomData() ? player.getClass().getCreatureStats(player).getLevel() : 0;
            const PreloadDependencyOptions options{ level, Settings::game().mDelevelCreatures,
                Settings::game().mDelevelItems };
            std::vector<VFS::Path::NormalizedView> meshes, instanceMeshes, animationMeshes;
            ListModelsVisitor visitor{ meshes, instanceMeshes, animationMeshes, mCollisionScales, mIsExterior,
                *MWBase::Environment::get().getESMStore(), options };
            cell->forEachConst(visitor);
            // The worker must not retain views into mutable records or settings.
            mMeshes = snapshotPreloadModels(meshes);
            mInstanceMeshes = snapshotPreloadModels(instanceMeshes);
            mAnimationMeshes = snapshotPreloadModels(animationMeshes);
        }

        void abort() override { mAbort = true; }

        // Keep one immutable snapshot and its resources across queue continuations.
        // No scene attachment or gameplay runs on these workers.
        void doWork() override
        {
            while (!doWorkSlice()) {}
        }

        bool doWorkSlice() override
        {
            SceneUtil::PreparationSample::Slice measured(mTiming.get(), mAbort);
            const bool sliced = Misc::useSlicedCellPreload();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
            std::size_t operations = 0;
            while (!mAbort && mStage != Stage::Done)
            {
                prepareNext();
                if (sliced && (++operations >= 16 || std::chrono::steady_clock::now() >= deadline))
                    break;
            }
            measured.mFinished = mStage == Stage::Done || mAbort;
            if (measured.mFinished)
            {
                // Release worker-only indexing state here, not in a future
                // main-thread cache eviction. Resource references remain pinned.
                mResolvedMeshes.clear();
                mLoadedMeshes.clear();
                mAnimationPaths.clear();
                mAnimationRoots.clear();
                mDirectoryCursor.reset();
                mDirectoryEnd.reset();
                mAnimationDirectories.clear();
            }
            return measured.mFinished;
        }

    private:
        enum class Stage { Terrain, Meshes, Instances, AnimationRoots, AnimationFiles, Done };

        static VFS::Path::Normalized meshPath(VFS::Path::NormalizedView path)
        {
            return path.value().starts_with("meshes/") ? VFS::Path::Normalized(path)
                                                       : Misc::ResourceHelpers::correctMeshPath(path);
        }

        void prepareNext()
        {
            const VFS::Manager& vfs = *mSceneManager->getVFS();
            switch (mStage)
            {
                case Stage::Terrain:
                    mStage = Stage::Meshes;
                    if (mIsExterior)
                    {
                        try
                        {
                            mTerrain->cacheCell(mTerrainView.get(), mCellLocation.mX, mCellLocation.mY);
                            mPreloadedOwnedObjects.insert(mLandManager->getLand(mCellLocation));
                        }
                        catch (const std::exception& e)
                        {
                            Log(Debug::Warning) << "Failed to cache terrain for exterior cell " << mCellLocation
                                                << ": " << e.what();
                        }
                    }
                    return;
                case Stage::Meshes:
                    if (mMeshCursor < mMeshes.size())
                        prepareMesh(mMeshCursor++);
                    else
                        mStage = Stage::Instances;
                    return;
                case Stage::Instances:
                    if (mPreloadInstances && Misc::useCellRenderPreparation()
                        && mInstanceCursor < mInstanceMeshes.size())
                    {
                        const auto& path = mInstanceMeshes[mInstanceCursor++];
                        const auto found = mResolvedMeshes.find(path);
                        if (found == mResolvedMeshes.end() || found->second.empty())
                            return;
                        try
                        {
                            if (auto ticket = mSceneManager->cacheInstance(found->second, mAbort))
                                mInstanceTickets.push_back(std::move(ticket));
                        }
                        catch (const std::exception& e)
                        {
                            Log(Debug::Warning) << "Failed to prepare instance for " << path << ": " << e.what();
                        }
                    }
                    else
                        mStage = Stage::AnimationRoots;
                    return;
                case Stage::AnimationRoots:
                    if (mAnimationCursor < mAnimationMeshes.size() && mAnimationPaths.size() < 4096)
                    {
                        const auto requested = meshPath(mAnimationMeshes[mAnimationCursor++]);
                        const std::array roots{ requested, Misc::ResourceHelpers::correctActorModelPath(requested, &vfs) };
                        for (const auto& root : roots)
                        {
                            if (mAbort || mAnimationPaths.size() >= 4096)
                                break;
                            if (!mAnimationRoots.insert(root).second)
                                continue;
                            if (mPreloadAdditionalAnimations)
                                if (const auto directory = Misc::ResourceHelpers::additionalAnimationPath(root))
                                    mAnimationDirectories.insert(*directory);
                            try
                            {
                                auto keyframe = root;
                                if (keyframe.extension() == VFS::Path::ExtensionView("nif"))
                                    keyframe.changeExtension(VFS::Path::ExtensionView("kf"));
                                if (vfs.exists(keyframe) && mAnimationPaths.insert(keyframe).second)
                                    mPreloadedOwnedObjects.insert(mKeyframeManager->get(keyframe));
                            }
                            catch (const std::exception&) {}
                        }
                    }
                    else
                    {
                        mNextDirectory = mAnimationDirectories.begin();
                        mStage = Stage::AnimationFiles;
                    }
                    return;
                case Stage::AnimationFiles:
                    if (mAnimationPaths.size() >= 4096 || mNextDirectory == mAnimationDirectories.end())
                    {
                        mStage = Stage::Done;
                        return;
                    }
                    if (!mDirectoryCursor)
                    {
                        const auto range = vfs.getRecursiveDirectoryIterator(mNextDirectory->value());
                        mDirectoryCursor = range.begin();
                        mDirectoryEnd = range.end();
                    }
                    if (*mDirectoryCursor == *mDirectoryEnd)
                    {
                        mDirectoryCursor.reset();
                        mDirectoryEnd.reset();
                        ++mNextDirectory;
                        return;
                    }
                    {
                        const auto name = **mDirectoryCursor;
                        ++*mDirectoryCursor;
                        if (!mAnimationPaths.insert(name).second)
                            return;
                        try
                        {
                            if (name.extension() == VFS::Path::ExtensionView("kf"))
                                mPreloadedOwnedObjects.insert(mKeyframeManager->get(name));
                            else if (name.extension() == VFS::Path::ExtensionView("nif"))
                                mPreloadedObjects.insert(mSceneManager->getTemplate(name));
                        }
                        catch (const std::exception&) {}
                    }
                    return;
                case Stage::Done:
                    return;
            }
        }

        void prepareMesh(std::size_t i)
        {
            const VFS::Path::NormalizedView path = mMeshes[i];
            try
            {
                const VFS::Manager& vfs = *mSceneManager->getVFS();
                auto resolved = mResolvedMeshes.find(path);
                if (resolved == mResolvedMeshes.end())
                {
                    auto corrected = Misc::ResourceHelpers::correctActorModelPath(meshPath(path), &vfs);
                    if (!vfs.exists(corrected))
                        corrected = VFS::Path::Normalized{};
                    resolved = mResolvedMeshes.emplace(path, std::move(corrected)).first;
                }
                const auto& mesh = resolved->second;
                if (mesh.empty())
                    return;
                if (mLoadedMeshes.contains(mesh))
                {
                    if (mPreloadInstances)
                        mPreloadedOwnedObjects.insert(mBulletShapeManager->cacheInstance(mesh, mCollisionScales[i]));
                    return;
                }
                if (Misc::getFileName(mesh).starts_with('x') && mesh.extension() == VFS::Path::ExtensionView("nif"))
                {
                    auto keyframe = mesh;
                    keyframe.changeExtension(VFS::Path::ExtensionView("kf"));
                    if (vfs.exists(keyframe))
                        mPreloadedOwnedObjects.insert(mKeyframeManager->get(keyframe));
                }
                if (mTiming)
                {
                    ++mTiming->mAssets;
                    mTiming->mColdAssets += !mSceneManager->getCachedTemplate(mesh);
                }
                mPreloadedObjects.insert(mSceneManager->getTemplate(mesh));
                if (mPreloadInstances)
                    mPreloadedOwnedObjects.insert(mBulletShapeManager->cacheInstance(mesh, mCollisionScales[i]));
                else
                    mPreloadedOwnedObjects.insert(mBulletShapeManager->getShape(mesh));
                mLoadedMeshes.insert(mesh);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Failed to preload mesh \"" << path << "\" from cell " << mCellId << ": " << e.what();
            }
        }

        Stage mStage = Stage::Terrain;
        std::size_t mMeshCursor = 0, mInstanceCursor = 0, mAnimationCursor = 0;
        std::map<VFS::Path::NormalizedView, VFS::Path::Normalized> mResolvedMeshes;
        std::set<VFS::Path::Normalized> mLoadedMeshes, mAnimationPaths, mAnimationRoots, mAnimationDirectories;
        std::set<VFS::Path::Normalized>::const_iterator mNextDirectory;
        std::optional<VFS::RecursiveDirectoryIterator> mDirectoryCursor, mDirectoryEnd;

        bool mIsExterior;
        ESM::ExteriorCellLocation mCellLocation;
        ESM::RefId mCellId;
        std::vector<VFS::Path::Normalized> mMeshes;
        std::vector<float> mCollisionScales;
        std::vector<VFS::Path::Normalized> mInstanceMeshes;
        std::vector<VFS::Path::Normalized> mAnimationMeshes;
        std::vector<Resource::SceneInstanceCache::TicketPtr> mInstanceTickets;
        Resource::SceneManager* mSceneManager;
        Resource::BulletShapeManager* mBulletShapeManager;
        Resource::KeyframeManager* mKeyframeManager;
        Terrain::World* mTerrain;
        MWRender::LandManager* mLandManager;
        bool mPreloadInstances;
        bool mPreloadAdditionalAnimations;

        std::atomic<bool> mAbort;

        std::unique_ptr<Terrain::View> mTerrainView;

        // Keep the loaded objects alive for as long as this cell is preloaded. Two sets
        // because the render-owned ones are still osg::Object; they merge once those move
        // behind the render backend.
        std::set<osg::ref_ptr<const osg::Object>> mPreloadedObjects;
        std::set<std::shared_ptr<const void>> mPreloadedOwnedObjects;
    };

    class TerrainPreloadItem : public SceneUtil::WorkItem
    {
    public:
        explicit TerrainPreloadItem(const std::vector<std::shared_ptr<Terrain::View>>& views, Terrain::World* world,
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
                    mWorld->preload(mTerrainViews[i].get(), mPreloadPositions[i].mPosition, mPreloadPositions[i].mCellBounds,
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
        std::vector<std::shared_ptr<Terrain::View>> mTerrainViews;
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
        std::unique_ptr<Terrain::View> mWorkView;
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
        bool finishedVisible() const { return mReady.load(); }
        bool waitVisible(Loading::Listener& listener)
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
                mWorld->preloadArrival(mWorkView.get(), mView, mAbort, mProgress, [&](std::size_t near, std::size_t far) {
                    if (!mAbort && mWorld->validArrival(*mView))
                    {
                        Log(Debug::Info) << "Exterior arrival active ready: nearChunks=" << near
                            << " distantChunks=" << far << " workerMs=" << elapsed();
                    }
                });
                if (mView->complete() && !mAbort)
                {
                    mReady = mWorld->validArrival(*mView);
                    Log(Debug::Info) << "Exterior arrival visible CPU ready: workerMs=" << elapsed()
                        << " estimatedBytes=" << mView->bytes();
                }
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

        bool isMaintenance() const override { return Misc::useSlicedCleanup(); }
        bool doWorkSlice() override
        {
            Diagnostics::Scope measured("resource-cleanup", "cache", "manager-step");
            if (!Misc::useSlicedCleanup())
            {
                doWork();
                return true;
            }
            return mResourceSystem->updateCacheStep(mReferenceTime, mCursor);
        }

    private:
        double mReferenceTime;
        Resource::ResourceSystem* mResourceSystem;
        Resource::ResourceSystem::CacheUpdateCursor mCursor;
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
            // Refresh useful predictions and move queued arrival work ahead without duplicating it.
            found->second.mTimeStamp = timestamp;
            return;
        }

        // A disabled cache cannot admit new jobs (or choose an oldest entry).
        if (mMaxCacheSize == 0)
            return;

        while (mPreloadCells.size() >= mMaxCacheSize)
        {
            Diagnostics::Scope measured("preload-cache", "admission", "evict");
            const auto lastUse = [](const auto& v) { return v.second.mTimeStamp; };
            const auto cost = [](const auto& v) { return v.second.preparationSeconds(); };
            const auto oldestCell = SceneUtil::selectPreloadVictim(
                mPreloadCells.begin(), mPreloadCells.end(), timestamp, Misc::usePreloadRetention(), lastUse, cost);
            if (oldestCell != mPreloadCells.end())
            {
                if (Misc::usePreloadRetention()
                    && oldestCell
                        != SceneUtil::selectPreloadVictim(
                            mPreloadCells.begin(), mPreloadCells.end(), timestamp, false, lastUse, cost))
                    ++mCostRetentions;
                mWorkQueue->cancelWorkItem(oldestCell->second.mWorkItem);
                mPreloadCells.erase(oldestCell);
                ++mEvicted;
            }
            else
                return;
        }

        auto item = std::make_shared<PreloadItem>(&cell, mResourceSystem->getSceneManager(), mBulletShapeManager,
            mResourceSystem->getKeyframeManager(), mTerrain, mLandManager, mPreloadInstances);
        if ((cell.isExterior() && Misc::useCostAwarePreload()) || Misc::usePreloadRetention())
            item->mTiming = std::make_shared<SceneUtil::PreparationSample>();
        mWorkQueue->addWorkItem(item);

        mPreloadCells.emplace(&cell, PreloadEntry(timestamp, item, item->mTiming));
        ++mAdded;
    }

    void CellPreloader::notifyLoaded(CellStore* cell)
    {
        if (mStaticPreloads && cell->isExterior())
            mStaticPreloads->mJobs.cancel(cell->getCell()->getExteriorCellLocation());
        PreloadMap::iterator found = mPreloadCells.find(cell);
        if (found != mPreloadCells.end())
        {
            if (found->second.mWorkItem)
            {
                mWorkQueue->cancelWorkItem(found->second.mWorkItem);
                found->second.mWorkItem = nullptr;
            }

            mPreloadCells.erase(found);
            ++mLoaded;
        }
    }

    void CellPreloader::prioritize(const CellStore& cell, bool required)
    {
        const auto found = mPreloadCells.find(&cell);
        if (mWorkQueue && found != mPreloadCells.end())
            mWorkQueue->promoteWorkItem(found->second.mWorkItem, required);
    }

    void CellPreloader::clear()
    {
        clearArrivals();
        mPreparationCost = {};
        if (mStaticPreloads)
        {
            mStaticPreloads->mJobs.clear();
            mStaticPreloads->mSamples.clear();
            mStaticPreloads->mCost = {};
        }
        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end();)
        {
            if (it->second.mWorkItem)
            {
                mWorkQueue->cancelWorkItem(it->second.mWorkItem);
                it->second.mWorkItem = nullptr;
            }

            mPreloadCells.erase(it++);
        }
    }

    void CellPreloader::updateCache(double timestamp)
    {
        if (mStaticPreloads)
            mStaticPreloads->mJobs.update(timestamp);
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
                    mWorkQueue->cancelWorkItem(it->second.mWorkItem);
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
            mUpdateCacheItem = std::make_shared<UpdateCacheItem>(mResourceSystem, timestamp);
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

    void CellPreloader::setWorkQueue(std::shared_ptr<SceneUtil::WorkQueue> workQueue)
    {
        mStaticPreloads.reset();
        mWorkQueue = std::move(workQueue);
    }

    void CellPreloader::retireArrival()
    {
        std::erase_if(mRetiredArrivals, [](const auto& job) { return job->isDone(); });
        if (mArrivalWorld)
            mArrivalWorld->showArrival({});
        if (mArrivalJob)
        {
            if (!mArrivalJob->isDone())
            {
                if (Misc::useResponsiveArrivalCancellation() && mWorkQueue)
                {
                    const bool removed = mWorkQueue->cancelWorkItem(mArrivalJob.get());
                    if (!removed && !mArrivalJob->isDone())
                        mRetiredArrivals.push_back(mArrivalJob);
                    Log(Debug::Info) << "Exterior arrival cancelled: queuedRemoved=" << removed
                        << " runningRetired=" << mRetiredArrivals.size();
                }
                else
                {
                    mArrivalJob->abort();
                    mRetiredArrivals.push_back(mArrivalJob);
                }
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
        {
            if (required && Misc::useResponsiveArrivalCancellation())
                mWorkQueue->promoteWorkItem(mArrivalJob.get(), true);
            return true;
        }
        std::erase_if(mRetiredArrivals, [](const auto& job) { return job->isDone(); });
        // Speculation waits for the previous prediction to stop. A required arrival
        // cancels its queued predecessor; only already-running workers are retained.
        // Thus retirees are bounded by the worker count, without a main-thread join.
        if (!mRetiredArrivals.empty())
        {
            if (!required)
                return false;
            if (!Misc::useResponsiveArrivalCancellation())
            {
                for (auto& previous : mRetiredArrivals)
                    previous->waitTillDone();
                mRetiredArrivals.clear();
            }
        }
        retireArrival();
        auto view = world->createArrival(target.mPosition, target.mCellBounds);
        if (!view)
            return false;
        auto job = std::make_shared<ExteriorArrivalJob>(world, std::move(view));
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
        if ((mArrivalJob->isDone() && !mArrivalJob->finishedVisible()) || !mArrivalJob->waitVisible(listener))
        {
            retireArrival();
            return false;
        }
        world->showArrival(mArrivalJob->mView);
        Log(Debug::Info) << "Exterior arrival visible CPU wait: predicted=" << predicted << " waitMs="
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
                mTerrainPreloadItem = std::make_shared<TerrainPreloadItem>(mTerrainViews, mTerrain, positions);
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
        mPreparationCost = {};
        mStaticPreloads.reset(); // joins detached readers before their resource/store owners disappear
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

    void CellPreloader::reportStats(unsigned int frameNumber, Misc::FrameStats& stats) const
    {
        stats.setAttribute(frameNumber, "CellPreloader Count", static_cast<double>(mPreloadCells.size()));
        stats.setAttribute(frameNumber, "CellPreloader Added", static_cast<double>(mAdded));
        stats.setAttribute(frameNumber, "CellPreloader Evicted", static_cast<double>(mEvicted));
        stats.setAttribute(frameNumber, "CellPreloader Loaded", static_cast<double>(mLoaded));
        stats.setAttribute(frameNumber, "CellPreloader Expired", static_cast<double>(mExpired));
        stats.setAttribute(frameNumber, "CellPreloader Cost Retentions", static_cast<double>(mCostRetentions));
        stats.setAttribute(frameNumber, "CellPreloader Static Jobs",
            mStaticPreloads ? static_cast<double>(mStaticPreloads->mJobs.size()) : 0.0);
    }
}
