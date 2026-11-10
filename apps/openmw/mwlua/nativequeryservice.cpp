#include "nativequeryservice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <components/misc/nativejobpool.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/worldmodel.hpp"

namespace MWLua
{
    NativeQueryService::NativeQueryService(Misc::NativeJobPool& jobs)
        : mJobs(jobs)
    {
    }

    NativeDistanceSamples NativeQueryService::queryObjects(
        const ObjectIdList& ids, const osg::Vec3f& origin, const NativeDistanceOptions& options)
    {
        if (!ids)
            return querySnapshot({}, origin, options);
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
        return querySnapshot(targets, origin, options);
    }

    NativeDistanceSamples NativeQueryService::queryTargets(
        std::vector<NativeDistanceTarget> targets, const osg::Vec3f& origin, const NativeDistanceOptions& options)
    {
        return querySnapshot(targets, origin, options);
    }

    NativeDistanceSamples NativeQueryService::querySnapshot(const std::vector<NativeDistanceTarget>& targets,
        const osg::Vec3f& origin, const NativeDistanceOptions& options)
    {
        struct Computed
        {
            NativeDistanceSample mSample;
            bool mAccepted = false;
        };

        std::vector<Computed> computed(targets.size());
        mJobs.parallelFor(targets.size(), [&](std::size_t i) {
            const NativeDistanceTarget& target = targets[i];
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
        });

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
