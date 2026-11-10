#ifndef MWLUA_NATIVEQUERYSERVICE_H
#define MWLUA_NATIVEQUERYSERVICE_H

#include <osg/Vec3f>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "object.hpp"

namespace Misc
{
    class NativeJobPool;
}

namespace MWLua
{
    struct NativeDistanceSample
    {
        ObjectId mObjectId;
        std::size_t mSourceIndex = 0;
        float mDistance3D = 0.f;
        float mDistance2D = 0.f;
        float mHeightDifference = 0.f;
        bool mHasObject = false;
    };

    using NativeDistanceSamples = std::shared_ptr<const std::vector<NativeDistanceSample>>;

    template <class ObjectT>
    struct NativeDistanceQuery
    {
        NativeDistanceSamples mSamples;
    };

    struct NativeDistanceTarget
    {
        ObjectId mObjectId;
        osg::Vec3f mPosition;
        std::size_t mSourceIndex = 0;
        bool mHasObject = false;
    };

    struct NativeDistanceOptions
    {
        double mMaxDistance = std::numeric_limits<double>::infinity();
        bool mSortByDistance = false;
        bool mFilterBy2D = false;
    };

    /// Per-call immutable snapshots and coarse native distance kernels shared by
    /// every Lua caller. Live world objects are resolved and copied on the Lua
    /// thread; worker threads receive only values and never touch Ptr, Lua, UI,
    /// physics or scene-graph state.
    class NativeQueryService
    {
    public:
        explicit NativeQueryService(Misc::NativeJobPool& jobs);

        NativeDistanceSamples queryObjects(const ObjectIdList& ids, const osg::Vec3f& origin,
            const NativeDistanceOptions& options = {});
        NativeDistanceSamples queryTargets(std::vector<NativeDistanceTarget> targets, const osg::Vec3f& origin,
            const NativeDistanceOptions& options = {});

    private:
        NativeDistanceSamples querySnapshot(
            const std::vector<NativeDistanceTarget>& targets, const osg::Vec3f& origin, const NativeDistanceOptions& options);

        Misc::NativeJobPool& mJobs;
    };
}

#endif
