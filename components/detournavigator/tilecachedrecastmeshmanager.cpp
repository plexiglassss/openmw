#include "tilecachedrecastmeshmanager.hpp"

#include "changetype.hpp"
#include "gettilespositions.hpp"
#include "recastmeshbuilder.hpp"
#include "settingsutils.hpp"
#include "stats.hpp"
#include "updateguard.hpp"

#include <components/bullethelpers/aabb.hpp>
#include <components/diagnostics/timing.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/misc/convert.hpp>

#include <boost/geometry/geometry.hpp>

#include <algorithm>
#include <future>
#include <iterator>
#include <limits>

namespace DetourNavigator
{
    namespace
    {
        const TilesPositionsRange infiniteRange{
            .mBegin = TilePosition(std::numeric_limits<int>::min(), std::numeric_limits<int>::min()),
            .mEnd = TilePosition(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()),
        };

        bool shareNavigationGeometry()
        {
            return Misc::useCombinedPerformance()
                && Misc::enabledUnlessZero("OPENMW_SHARED_NAVIGATION_GEOMETRY");
        }

        struct AddHeightfield
        {
            osg::Vec2i mCellPosition;
            int mCellSize;
            RecastMeshBuilder& mBuilder;

            void operator()(const HeightfieldSurface& v)
            {
                mBuilder.addHeightfield(mCellPosition, mCellSize, v.mHeights, v.mSize, v.mMinHeight, v.mMaxHeight);
            }

            void operator()(HeightfieldPlane v) { mBuilder.addHeightfield(mCellPosition, mCellSize, v.mHeight); }
        };

        TilePosition makeTilePosition(const boost::geometry::model::point<int, 2, boost::geometry::cs::cartesian>& v)
        {
            return TilePosition(v.get<0>(), v.get<1>());
        }

        template <class Mutex>
        class MaybeLockGuard
        {
        public:
            explicit MaybeLockGuard(Mutex& mutex, const UpdateGuard* guard)
                : mImpl(guard == nullptr ? std::optional<std::unique_lock<Mutex>>(mutex) : std::nullopt)
            {
            }

        private:
            const std::optional<std::unique_lock<Mutex>> mImpl;
        };

        TilesPositionsRange getIndexRange(const auto& index)
        {
            const auto bounds = index.bounds();
            return TilesPositionsRange{
                .mBegin = makeTilePosition(bounds.min_corner()),
                .mEnd = makeTilePosition(bounds.max_corner()) + TilePosition(1, 1),
            };
        }
    }

    struct TileCachedRecastMeshManager::MeshSnapshot
    {
        using Object = std::tuple<osg::ref_ptr<const Resource::BulletShapeInstance>, ObjectTransform,
            std::reference_wrapper<const btCollisionShape>, btTransform, AreaType>;

        RecastMeshBuilder mBuilder;
        std::vector<Object> mObjects;
        Version mVersion;

        explicit MeshSnapshot(const TileBounds& bounds)
            : mBuilder(bounds)
        {
        }
    };

    struct TileCachedRecastMeshManager::PendingMesh
    {
        // The usual single-worker path needs only a publication identity.
        // Allocate a future's shared state only when another caller joins.
        std::optional<std::promise<std::shared_ptr<RecastMesh>>> mPromise;
        std::shared_future<std::shared_ptr<RecastMesh>> mFuture;
    };

    TileCachedRecastMeshManager::TileCachedRecastMeshManager(const RecastSettings& settings)
        : mSettings(settings)
        , mShareGeometry(shareNavigationGeometry())
        , mRange(infiniteRange)
    {
    }

    void TileCachedRecastMeshManager::setRange(const TilesPositionsRange& range, const UpdateGuard* guard)
    {
        const MaybeLockGuard lock(mMutex, guard);
        if (mRange == range)
            return;

        bool changed = false;
        if (mRange != infiniteRange)
        {
            for (const auto& [id, data] : mObjects)
            {
                const TilesPositionsRange objectRange
                    = makeTilesPositionsRange(data->mObject.getShape(), data->mObject.getTransform(), mSettings);

                getTilesPositions(getIntersection(mRange, objectRange), [&](const TilePosition& v) {
                    if (!isInTilesPositionsRange(range, v))
                    {
                        addChangedTile(v);
                        changed = true;
                    }
                });

                getTilesPositions(getIntersection(range, objectRange), [&](const TilePosition& v) {
                    if (!isInTilesPositionsRange(mRange, v))
                    {
                        addChangedTile(v);
                        changed = true;
                    }
                });
            }

            getTilesPositions(mRange, [&](const TilePosition& v) {
                if (!isInTilesPositionsRange(range, v))
                {
                    mCache.erase(v);
                    mPendingMeshes.erase(v);
                }
            });
        }
        // The initial infinite range is not enumerable. It can still have an
        // in-flight request when a first finite range is selected.
        std::erase_if(mPendingMeshes,
            [&](const auto& entry) { return !isInTilesPositionsRange(range, entry.first); });

        if (changed)
            ++mRevision;

        mRange = range;
    }

    TilesPositionsRange TileCachedRecastMeshManager::getLimitedObjectsRange() const
    {
        std::optional<TilesPositionsRange> result;
        if (!mWater.empty())
            result = getIndexRange(mWaterIndex);
        if (!mHeightfields.empty())
        {
            const TilesPositionsRange range = getIndexRange(mHeightfieldIndex);
            if (result.has_value())
                result = getUnion(*result, range);
            else
                result = range;
        }
        if (!mObjects.empty())
        {
            const TilesPositionsRange range = getIndexRange(mObjectIndex);
            if (result.has_value())
                result = getUnion(*result, range);
            else
                result = range;
        }
        if (result.has_value())
            return getIntersection(mRange, *result);
        return {};
    }

    void TileCachedRecastMeshManager::setWorldspace(ESM::RefId worldspace, const UpdateGuard* guard)
    {
        const MaybeLockGuard lock(mMutex, guard);
        if (mWorldspace == worldspace)
            return;
        mWorldspace = worldspace;
        ++mGeneration;
        ++mRevision;
        mObjectIndex.clear();
        mObjects.clear();
        mWaterIndex.clear();
        mWater.clear();
        mInfiniteWater = mWater.end();
        mHeightfieldIndex.clear();
        mHeightfields.clear();
        mInfiniteHeightfield = mHeightfields.end();
        mChangedTiles.clear();
        mReportedTiles.clear();
        mCache.clear();
        mPendingMeshes.clear();
    }

    bool TileCachedRecastMeshManager::addObject(ObjectId id, const CollisionShape& shape, const btTransform& transform,
        const AreaType areaType, const UpdateGuard* guard)
    {
        const TilesPositionsRange range = makeTilesPositionsRange(shape.getShape(), transform, mSettings);
        {
            const MaybeLockGuard lock(mMutex, guard);
            const auto it = mObjects.find(id);
            if (it != mObjects.end())
                return false;
            const std::size_t revision = mRevision + 1;
            ObjectData* const dataPtr
                = mObjects
                      .emplace_hint(it, id,
                          std::unique_ptr<ObjectData>(new ObjectData{
                              .mObject = RecastMeshObject(shape, transform, areaType),
                              .mRange = range,
                              .mAabb = CommulativeAabb(revision, BulletHelpers::getAabb(shape.getShape(), transform)),
                              .mGeneration = mGeneration,
                              .mRevision = revision,
                              .mLastNavMeshReportedChange = {},
                              .mLastNavMeshReport = {},
                          }))
                      ->second.get();
            assert(range.mBegin != range.mEnd);
            mObjectIndex.insert(makeObjectIndexValue(range, dataPtr));
            mRevision = revision;
        }
        getTilesPositions(getIntersection(range, mRange), [&](const TilePosition& v) { addChangedTile(v); });
        return true;
    }

    bool TileCachedRecastMeshManager::updateObject(
        ObjectId id, const btTransform& transform, const AreaType areaType, const UpdateGuard* guard)
    {
        TilesPositionsRange newRange;
        TilesPositionsRange oldRange;
        {
            const MaybeLockGuard lock(mMutex, guard);
            const auto it = mObjects.find(id);
            if (it == mObjects.end())
                return false;
            if (!it->second->mObject.update(transform, areaType))
                return false;
            const std::size_t lastChangeRevision = it->second->mLastNavMeshReportedChange.has_value()
                ? it->second->mLastNavMeshReportedChange->mRevision
                : mRevision;
            const btCollisionShape& shape = it->second->mObject.getShape();
            if (!it->second->mAabb.update(lastChangeRevision, BulletHelpers::getAabb(shape, transform)))
                return false;
            newRange = makeTilesPositionsRange(shape, transform, mSettings);
            oldRange = it->second->mRange;
            if (newRange != oldRange)
            {
                mObjectIndex.remove(makeObjectIndexValue(oldRange, it->second.get()));
                mObjectIndex.insert(makeObjectIndexValue(newRange, it->second.get()));
                it->second->mRange = newRange;
            }
            ++mRevision;
            it->second->mRevision = mRevision;
        }
        if (newRange == oldRange)
        {
            getTilesPositions(getIntersection(newRange, mRange), [&](const TilePosition& v) { addChangedTile(v); });
        }
        else
        {
            getTilesPositions(getIntersection(newRange, mRange), [&](const TilePosition& v) { addChangedTile(v); });
            getTilesPositions(getIntersection(oldRange, mRange), [&](const TilePosition& v) {
                if (!isInTilesPositionsRange(newRange, v))
                    addChangedTile(v);
            });
        }
        return true;
    }

    void TileCachedRecastMeshManager::removeObject(ObjectId id, const UpdateGuard* guard)
    {
        TilesPositionsRange range;
        {
            const MaybeLockGuard lock(mMutex, guard);
            const auto it = mObjects.find(id);
            if (it == mObjects.end())
                return;
            range = it->second->mRange;
            mObjectIndex.remove(makeObjectIndexValue(range, it->second.get()));
            mObjects.erase(it);
            ++mRevision;
        }
        getTilesPositions(getIntersection(range, mRange), [&](const TilePosition& v) { addChangedTile(v); });
    }

    void TileCachedRecastMeshManager::addWater(
        const osg::Vec2i& cellPosition, const int cellSize, const float level, const UpdateGuard* guard)
    {
        const btVector3 shift = Misc::Convert::toBullet(getWaterShift3d(cellPosition, cellSize, level));
        const std::optional<TilesPositionsRange> range = cellSize == std::numeric_limits<int>::max()
            ? std::optional<TilesPositionsRange>()
            : makeTilesPositionsRange(cellSize, shift, mSettings);
        {
            const MaybeLockGuard lock(mMutex, guard);
            auto it = mWater.find(cellPosition);
            if (it != mWater.end())
                return;
            const std::size_t revision = mRevision + 1;
            it = mWater.emplace_hint(it, cellPosition,
                WaterData{
                    .mWater = Water{ .mCellSize = cellSize, .mLevel = level },
                    .mRange = range,
                    .mRevision = revision,
                });
            if (range.has_value())
                mWaterIndex.insert(makeWaterIndexValue(*range, it));
            else
                mInfiniteWater = it;
            mRevision = revision;
        }
        addChangedTiles(range);
    }

    void TileCachedRecastMeshManager::removeWater(const osg::Vec2i& cellPosition, const UpdateGuard* guard)
    {
        std::optional<TilesPositionsRange> range;
        {
            const MaybeLockGuard lock(mMutex, guard);
            const auto it = mWater.find(cellPosition);
            if (it == mWater.end())
                return;
            range = it->second.mRange;
            if (range.has_value())
                mWaterIndex.remove(makeWaterIndexValue(*range, it));
            else
                mInfiniteWater = mWater.end();
            mWater.erase(it);
            ++mRevision;
        }
        addChangedTiles(range);
    }

    void TileCachedRecastMeshManager::addHeightfield(
        const osg::Vec2i& cellPosition, const int cellSize, const HeightfieldShape& shape, const UpdateGuard* guard)
    {
        const btVector3 shift = getHeightfieldShift(shape, cellPosition, cellSize);
        const std::optional<TilesPositionsRange> range = cellSize == std::numeric_limits<int>::max()
            ? std::optional<TilesPositionsRange>()
            : makeTilesPositionsRange(cellSize, shift, mSettings);
        {
            const MaybeLockGuard lock(mMutex, guard);
            auto it = mHeightfields.find(cellPosition);
            if (it != mHeightfields.end())
                return;
            const std::size_t revision = mRevision + 1;
            it = mHeightfields.emplace_hint(it, cellPosition,
                HeightfieldData{
                    .mCellSize = cellSize,
                    .mShape = shape,
                    .mRange = range,
                    .mRevision = revision,
                });
            if (range.has_value())
                mHeightfieldIndex.insert(makeHeightfieldIndexValue(*range, it));
            else
                mInfiniteHeightfield = it;
            mRevision = revision;
        }
        addChangedTiles(range);
    }

    void TileCachedRecastMeshManager::removeHeightfield(const osg::Vec2i& cellPosition, const UpdateGuard* guard)
    {
        std::optional<TilesPositionsRange> range;
        {
            const MaybeLockGuard lock(mMutex, guard);
            const auto it = mHeightfields.find(cellPosition);
            if (it == mHeightfields.end())
                return;
            range = it->second.mRange;
            if (range.has_value())
                mHeightfieldIndex.remove(makeHeightfieldIndexValue(*range, it));
            else
                mInfiniteHeightfield = mHeightfields.end();
            mHeightfields.erase(it);
            ++mRevision;
        }
        addChangedTiles(range);
    }

    std::shared_ptr<RecastMesh> TileCachedRecastMeshManager::getMesh(
        ESM::RefId worldspace, const TilePosition& tilePosition)
    {
        std::shared_ptr<PendingMesh> pending;
        std::unique_ptr<MeshSnapshot> snapshot;
        {
            const std::lock_guard lock(mMutex);
            if (mWorldspace != worldspace)
                return nullptr;
            if (!isInTilesPositionsRange(mRange, tilePosition))
                return nullptr;
            const auto it = mCache.find(tilePosition);
            if (it != mCache.end() && it->second.mRecastMesh->getVersion() == it->second.mVersion)
                return it->second.mRecastMesh;
            const auto active = mPendingMeshes.find(tilePosition);
            if (mShareGeometry && active != mPendingMeshes.end())
            {
                pending = active->second;
                if (!pending->mPromise)
                {
                    std::promise<std::shared_ptr<RecastMesh>> promise;
                    auto future = promise.get_future().share();
                    pending->mPromise.emplace(std::move(promise));
                    pending->mFuture = std::move(future);
                }
                ++mJoinedMeshRequests;
            }
            else
            {
                snapshot = makeMeshSnapshot(tilePosition);
                if (!snapshot)
                    return nullptr;
                pending = std::make_shared<PendingMesh>();
                mPendingMeshes.insert_or_assign(tilePosition, pending);
            }
        }
        if (!snapshot)
        {
            Diagnostics::Scope timing("navigation", "geometry", "shared wait");
            return pending->mFuture.get(); // Never wait with the manager/update mutex held.
        }
        try
        {
            auto result = buildMesh(std::move(snapshot));
            {
                const std::lock_guard lock(mMutex);
                const auto active = mPendingMeshes.find(tilePosition);
                if (active != mPendingMeshes.end() && active->second == pending)
                {
                    if (mWorldspace == worldspace && mGeneration == result->getVersion().mGeneration
                        && isInTilesPositionsRange(mRange, tilePosition))
                        mCache.insert_or_assign(tilePosition, CachedTile{ result->getVersion(), result });
                    mPendingMeshes.erase(active);
                }
            }
            // An obsolete snapshot still belongs to its original job. Returning
            // null here would tell the updater to mark that navigation tile empty.
            if (pending->mPromise)
                pending->mPromise->set_value(result);
            return result;
        }
        catch (...)
        {
            {
                const std::lock_guard lock(mMutex);
                const auto active = mPendingMeshes.find(tilePosition);
                if (active != mPendingMeshes.end() && active->second == pending)
                    mPendingMeshes.erase(active);
            }
            if (pending->mPromise)
                pending->mPromise->set_exception(std::current_exception());
            throw;
        }
    }

    std::shared_ptr<RecastMesh> TileCachedRecastMeshManager::getCachedMesh(
        ESM::RefId worldspace, const TilePosition& tilePosition) const
    {
        const std::lock_guard lock(mMutex);
        if (mWorldspace != worldspace)
            return nullptr;
        if (!isInTilesPositionsRange(mRange, tilePosition))
            return nullptr;
        const auto it = mCache.find(tilePosition);
        if (it == mCache.end())
            return nullptr;
        return it->second.mRecastMesh;
    }

    std::shared_ptr<RecastMesh> TileCachedRecastMeshManager::getNewMesh(
        ESM::RefId worldspace, const TilePosition& tilePosition) const
    {
        std::unique_ptr<MeshSnapshot> snapshot;
        {
            const std::lock_guard lock(mMutex);
            if (mWorldspace != worldspace)
                return nullptr;
            snapshot = makeMeshSnapshot(tilePosition);
        }
        // The offline navmesh tool intentionally requests fresh input outside
        // the runtime navigation range and does not consume the shared cache.
        return buildMesh(std::move(snapshot));
    }

    void TileCachedRecastMeshManager::reportNavMeshChange(
        const TilePosition& tilePosition, Version recastMeshVersion, Version navMeshVersion)
    {
        const std::lock_guard lock(mMutex);
        for (auto it = mObjectIndex.qbegin(makeIndexQuery(tilePosition)); it != mObjectIndex.qend(); ++it)
        {
            ObjectData& object = *it->second;
            if (recastMeshVersion.mGeneration != object.mGeneration)
                continue;
            if (object.mLastNavMeshReport.has_value() && navMeshVersion < object.mLastNavMeshReport->mNavMeshVersion)
                continue;
            object.mLastNavMeshReport = { recastMeshVersion.mRevision, navMeshVersion };
            if (!object.mLastNavMeshReportedChange.has_value()
                || object.mLastNavMeshReportedChange->mNavMeshVersion < object.mLastNavMeshReport->mNavMeshVersion)
                object.mLastNavMeshReportedChange = object.mLastNavMeshReport;
        }
    }

    void TileCachedRecastMeshManager::addChangedTile(const TilePosition& tilePosition)
    {
        mChangedTiles.push_back(tilePosition);
    }

    std::map<osg::Vec2i, ChangeType> TileCachedRecastMeshManager::takeChangedTiles(const UpdateGuard* guard)
    {
        std::map<osg::Vec2i, ChangeType> result;
        {
            const MaybeLockGuard lock(mMutex, guard);
            std::sort(mChangedTiles.begin(), mChangedTiles.end());
            mChangedTiles.erase(std::unique(mChangedTiles.begin(), mChangedTiles.end()), mChangedTiles.end());
            mReportedTilesBuffer.clear();
            mReportedTilesBuffer.reserve(mReportedTiles.size() + mChangedTiles.size());
            auto reported = mReportedTiles.cbegin();
            for (const TilePosition& tilePosition : mChangedTiles)
            {
                while (reported != mReportedTiles.cend() && *reported < tilePosition)
                {
                    mReportedTilesBuffer.push_back(*reported);
                    ++reported;
                }
                const bool wasReported = reported != mReportedTiles.cend() && *reported == tilePosition;
                if (wasReported)
                    ++reported;
                if (const auto it = mCache.find(tilePosition); it != mCache.end())
                    ++it->second.mVersion.mRevision;
                mPendingMeshes.erase(tilePosition);

                ChangeType changeType = ChangeType::remove;
                bool hasInput = false;
                if (isInTilesPositionsRange(mRange, tilePosition))
                {
                    const auto query = makeIndexQuery(tilePosition);
                    hasInput = mWaterIndex.qbegin(query) != mWaterIndex.qend()
                        || mHeightfieldIndex.qbegin(query) != mHeightfieldIndex.qend()
                        || mObjectIndex.qbegin(query) != mObjectIndex.qend();
                }
                if (hasInput)
                {
                    mReportedTilesBuffer.push_back(tilePosition);
                    changeType = wasReported ? ChangeType::update : ChangeType::add;
                }
                result.emplace(tilePosition, changeType);
            }
            mReportedTilesBuffer.insert(mReportedTilesBuffer.end(), reported, mReportedTiles.cend());
            mReportedTiles.swap(mReportedTilesBuffer);
            mChangedTiles.clear();
        }
        return result;
    }

    TileCachedRecastMeshManagerStats TileCachedRecastMeshManager::getStats() const
    {
        const std::lock_guard lock(mMutex);
        return TileCachedRecastMeshManagerStats{
            .mTiles = mCache.size(),
            .mObjects = mObjects.size(),
            .mHeightfields = mHeightfields.size(),
            .mWater = mWater.size(),
            .mPendingMeshTiles = mPendingMeshes.size(),
            .mJoinedMeshRequests = mJoinedMeshRequests,
            .mSnapshotObjects = mSnapshotObjects,
            .mSnapshotObjectCapacity = mSnapshotObjectCapacity,
        };
    }

    TileCachedRecastMeshManager::IndexPoint TileCachedRecastMeshManager::makeIndexPoint(
        const TilePosition& tilePosition)
    {
        return IndexPoint(tilePosition.x(), tilePosition.y());
    }

    TileCachedRecastMeshManager::IndexBox TileCachedRecastMeshManager::makeIndexBox(const TilesPositionsRange& range)
    {
        assert(range.mBegin != range.mEnd);
        return IndexBox(makeIndexPoint(range.mBegin), makeIndexPoint(range.mEnd - TilePosition(1, 1)));
    }

    TileCachedRecastMeshManager::ObjectIndexValue TileCachedRecastMeshManager::makeObjectIndexValue(
        const TilesPositionsRange& range, ObjectData* id)
    {
        return { makeIndexBox(range), id };
    }

    TileCachedRecastMeshManager::WaterIndexValue TileCachedRecastMeshManager::makeWaterIndexValue(
        const TilesPositionsRange& range, std::map<osg::Vec2i, WaterData>::const_iterator it)
    {
        return { makeIndexBox(range), it };
    }

    TileCachedRecastMeshManager::HeightfieldIndexValue TileCachedRecastMeshManager::makeHeightfieldIndexValue(
        const TilesPositionsRange& range, std::map<osg::Vec2i, HeightfieldData>::const_iterator it)
    {
        return { makeIndexBox(range), it };
    }

    auto TileCachedRecastMeshManager::makeIndexQuery(const TilePosition& tilePosition)
        -> decltype(boost::geometry::index::intersects(IndexBox()))
    {
        const IndexPoint point = makeIndexPoint(tilePosition);
        return boost::geometry::index::intersects(IndexBox(point, point));
    }

    std::unique_ptr<TileCachedRecastMeshManager::MeshSnapshot>
    TileCachedRecastMeshManager::makeMeshSnapshot(const TilePosition& tilePosition) const
    {
        Diagnostics::Scope timing("navigation", "geometry", "snapshot");
        auto snapshot = std::make_unique<MeshSnapshot>(makeRealTileBoundsWithBorder(mSettings, tilePosition));
        auto& builder = snapshot->mBuilder;
        auto& objects = snapshot->mObjects;
        bool hasInput = false;
        for (auto it = mWaterIndex.qbegin(makeIndexQuery(tilePosition)); it != mWaterIndex.qend(); ++it)
        {
            const auto& [cellPosition, data] = *it->second;
            builder.addWater(cellPosition, data.mWater);
            hasInput = true;
        }
        for (auto it = mHeightfieldIndex.qbegin(makeIndexQuery(tilePosition)); it != mHeightfieldIndex.qend(); ++it)
        {
            const auto& [cellPosition, data] = *it->second;
            std::visit(AddHeightfield{ cellPosition, data.mCellSize, builder }, data.mShape);
            hasInput = true;
        }
        const auto first = mObjectIndex.qbegin(makeIndexQuery(tilePosition));
        const auto end = mObjectIndex.qend();
        objects.reserve(mShareGeometry ? static_cast<std::size_t>(std::distance(first, end)) : mObjects.size());
        for (auto it = first; it != end; ++it)
        {
            const auto& object = it->second->mObject;
            objects.emplace_back(object.getInstance(), object.getObjectTransform(), object.getShape(),
                object.getTransform(), object.getAreaType());
            hasInput = true;
        }
        if (hasInput)
        {
            if (mInfiniteWater != mWater.end())
                builder.addWater(mInfiniteWater->first, mInfiniteWater->second.mWater);
            if (mInfiniteHeightfield != mHeightfields.end())
                std::visit(
                    AddHeightfield{ mInfiniteHeightfield->first, mInfiniteHeightfield->second.mCellSize, builder },
                    mInfiniteHeightfield->second.mShape);
            snapshot->mVersion = { mGeneration, mRevision };
        }
        mSnapshotObjects = objects.size();
        mSnapshotObjectCapacity = objects.capacity();
        if (!hasInput)
            return nullptr;
        return snapshot;
    }

    std::shared_ptr<RecastMesh> TileCachedRecastMeshManager::buildMesh(std::unique_ptr<MeshSnapshot> snapshot)
    {
        if (!snapshot)
            return nullptr;
        Diagnostics::Scope timing("navigation", "geometry", "build");
        for (const auto& [instance, objectTransform, shape, transform, areaType] : snapshot->mObjects)
            snapshot->mBuilder.addObject(shape, transform, areaType, instance->getSource(), objectTransform);
        return std::move(snapshot->mBuilder).create(snapshot->mVersion);
    }

    void TileCachedRecastMeshManager::addChangedTiles(const std::optional<TilesPositionsRange>& range)
    {
        if (range.has_value())
            getTilesPositions(*range, [&](const TilePosition& v) { addChangedTile(v); });
    }
}
