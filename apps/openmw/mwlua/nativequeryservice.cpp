#include "nativequeryservice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <components/misc/nativejobpool.hpp>
#include <components/misc/combinedperformance.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/worldmodel.hpp"

namespace MWLua
{
    NativeQueryService::NativeQueryService(Misc::NativeJobPool& jobs)
        : mAsync(jobs)
        , mJobs(jobs)
    {
    }

    NativeDistanceSamples NativeQueryService::queryObjects(
        const ObjectIdList& ids, const osg::Vec3f& origin, const NativeDistanceOptions& options)
    {
        return querySnapshot(snapshotObjects(ids), origin, options);
    }

    NativeQueryService::PositionReadScope::PositionReadScope(NativeQueryService& service)
        : mService(service)
    {
        if (mService.mPositionReadDepth++ == 0)
            mService.invalidatePositions();
    }

    NativeQueryService::PositionReadScope::~PositionReadScope()
    {
        if (--mService.mPositionReadDepth == 0)
            mService.invalidatePositions();
    }

    bool NativeQueryService::sharePositions() const
    {
        return mPositionReadDepth != 0 && Misc::useSharedPositionReads();
    }

    void NativeQueryService::invalidatePositions()
    {
        mPositionLists.clear();
        mPositionReads.clear();
        mSnapshotTargets = 0;
    }

    void NativeQueryService::checkPositionRevision()
    {
        const auto revision = MWBase::Environment::get().getWorldModel()->getPtrRegistryRevision();
        if (revision != mPositionRevision)
        {
            invalidatePositions();
            mPositionRevision = revision;
        }
    }

    std::optional<osg::Vec3f> NativeQueryService::objectPosition(ObjectId id)
    {
        const bool share = sharePositions();
        if (share)
        {
            checkPositionRevision();
            if (const auto it = mPositionReads.find(id); it != mPositionReads.end())
            {
                ++mPositionsReused;
                return it->second;
            }
        }
        const MWWorld::Ptr ptr = MWBase::Environment::get().getWorldModel()->getPtr(id);
        std::optional<osg::Vec3f> position;
        if (!ptr.isEmpty() && !ptr.mRef->isDeleted())
            position = ptr.getRefData().getPosition().asVec3();
        ++mPositionsResolved;
        if (share && mPositionReads.size() < 65536)
            mPositionReads.emplace(id, position);
        return position;
    }

    NativePositions NativeQueryService::snapshotObjects(const ObjectIdList& ids)
    {
        static const auto empty = std::make_shared<const std::vector<NativeDistanceTarget>>();
        if (!ids || ids->empty())
            return empty;
        const bool share = sharePositions();
        if (share)
        {
            checkPositionRevision();
            if (const auto it = mPositionLists.find(ids.get()); it != mPositionLists.end())
            {
                ++mPositionListsReused;
                return it->second.mValues;
            }
        }
        auto values = std::make_shared<std::vector<NativeDistanceTarget>>();
        values->reserve(ids->size());
        std::size_t index = 0;
        for (const auto id : *ids)
        {
            ++index;
            if (const auto position = objectPosition(id))
                values->push_back({ id, *position, index, true });
        }
        ++mPositionListsBuilt;
        if (share && mPositionLists.size() < 128 && ids->size() <= 65536 - mSnapshotTargets)
        {
            mSnapshotTargets += ids->size();
            mPositionLists.emplace(ids.get(), CachedList{ ids, values });
        }
        return values;
    }

    void NativeQueryService::reset()
    {
        invalidatePositions();
        mDistanceIndices.clear();
        mDistanceIndexOrder.clear();
        mIndexedTargets = 0;
        mAsync.clear();
        mSession = std::make_shared<const int>(0);
    }

    std::vector<NativeDistanceTarget> NativeQueryService::copyObjects(const ObjectIdList& ids)
    {
        if (!ids)
            return {};
        std::vector<NativeDistanceTarget> targets;
        targets.reserve(ids->size());
        MWWorld::WorldModel& world = *MWBase::Environment::get().getWorldModel();
        std::size_t sourceIndex = 0;
        for (const ObjectId& id : *ids)
        {
            ++sourceIndex;
            const MWWorld::Ptr ptr = world.getPtr(id);
            if (ptr.isEmpty() || ptr.mRef->isDeleted())
                continue;
            targets.push_back({
                .mObjectId = id,
                .mPosition = ptr.getRefData().getPosition().asVec3(),
                .mSourceIndex = sourceIndex,
                .mHasObject = true,
            });
        }
        return targets;
    }

    NativeDistanceSamples NativeQueryService::queryTargets(
        std::vector<NativeDistanceTarget> targets, const osg::Vec3f& origin, const NativeDistanceOptions& options)
    {
        return compute(targets, origin, options, &mJobs);
    }

    std::shared_ptr<const Misc::SpatialIndex> NativeQueryService::prepareDistanceIndex(
        const NativePositions& positions, const osg::Vec3f& origin, const NativeDistanceOptions& options)
    {
        if (!Misc::useSpatialDistanceQueries() || positions->size() < 64 || positions->size() > 65536
            || !std::isfinite(options.mMaxDistance) || options.mMaxDistance < 0
            || !std::isfinite(origin.x()) || !std::isfinite(origin.y()))
            return nullptr;
        auto found = mDistanceIndices.find(positions.get());
        if (found == mDistanceIndices.end())
        {
            while (!mDistanceIndexOrder.empty()
                && (mDistanceIndices.size() >= 128 || mIndexedTargets + positions->size() > 65536))
            {
                const auto oldest = mDistanceIndexOrder.front();
                mIndexedTargets -= mDistanceIndices.at(oldest).mPositions->size();
                mDistanceIndices.erase(oldest);
                mDistanceIndexOrder.pop_front();
            }
            mIndexedTargets += positions->size();
            mDistanceIndexOrder.push_back(positions.get());
            found = mDistanceIndices.emplace(positions.get(), CachedIndex{ positions, nullptr, 0 }).first;
        }
        auto& entry = found->second;
        if (entry.mIndex)
        {
            ++mDistanceIndicesReused;
            return entry.mIndex;
        }
        // One-off inputs keep the linear kernel. Amortize construction only
        // after repeated finite-radius queries against the same immutable data.
        if (entry.mUses < 8 && ++entry.mUses < 8)
            return nullptr;
        if (!Misc::spatialQueryIsSelective(positions->size(),
                [&](auto i) { const auto& p = (*positions)[i].mPosition;
                    return Misc::SpatialIndex::Position{ p.x(), p.y() }; },
                { origin.x(), origin.y() }, options.mMaxDistance))
            return nullptr;
        std::vector<Misc::SpatialIndex::Position> points;
        points.reserve(positions->size());
        for (const auto& target : *positions)
        {
            const auto& p = target.mPosition;
            points.push_back({ p.x(), p.y() });
        }
        entry.mIndex = std::make_shared<const Misc::SpatialIndex>(points);
        ++mDistanceIndicesBuilt;
        return entry.mIndex;
    }

    NativeDistanceSamples NativeQueryService::querySnapshot(const NativePositions& positions,
        const osg::Vec3f& origin, const NativeDistanceOptions& options)
    {
        const auto index = prepareDistanceIndex(positions, origin, options);
        return compute(*positions, origin, options, &mJobs, index.get());
    }

    NativeDistanceSamples NativeQueryService::compute(const std::vector<NativeDistanceTarget>& targets,
        const osg::Vec3f& origin, const NativeDistanceOptions& options, Misc::NativeJobPool* pool,
        const Misc::SpatialIndex* index)
    {
        struct Computed
        {
            NativeDistanceSample mSample;
            bool mAccepted = false;
        };

        const auto candidates = index
            ? index->query({ origin.x(), origin.y() }, options.mMaxDistance) : std::nullopt;
        const std::size_t count = candidates ? candidates->size() : targets.size();
        std::vector<Computed> computed(count);
        auto measure = [&](std::size_t i) {
            const NativeDistanceTarget& target = targets[candidates ? (*candidates)[i] : i];
            const osg::Vec3f delta = origin - target.mPosition;
            const float distance2DSquared = delta.x() * delta.x() + delta.y() * delta.y();
            const float distance3DSquared = distance2DSquared + delta.z() * delta.z();
            const float distance3D = std::sqrt(distance3DSquared);
            const float distance2D = std::sqrt(distance2DSquared);
            Computed& value = computed[i];
            value.mAccepted = (options.mFilterBy2D ? distance2D : distance3D) <= options.mMaxDistance;
            if (!value.mAccepted)
                return;
            value.mSample = {
                .mObjectId = target.mObjectId,
                .mSourceIndex = target.mSourceIndex,
                .mDistance3D = distance3D,
                .mDistance2D = distance2D,
                .mHeightDifference = delta.z(),
                .mHasObject = target.mHasObject,
            };
        };
        if (pool)
            pool->parallelFor(count, measure);
        else
            for (std::size_t i = 0; i < count; ++i)
                measure(i);

        auto result = std::make_shared<std::vector<NativeDistanceSample>>();
        result->reserve(computed.size());
        for (const Computed& value : computed)
            if (value.mAccepted)
                result->push_back(value.mSample);
        if (options.mSortByDistance)
        {
            std::stable_sort(result->begin(), result->end(), [](const auto& lhs, const auto& rhs) {
                if (std::isnan(rhs.mDistance3D))
                    return !std::isnan(lhs.mDistance3D);
                return lhs.mDistance3D < rhs.mDistance3D;
            });
        }
        return result;
    }
}
