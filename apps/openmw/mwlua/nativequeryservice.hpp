#ifndef MWLUA_NATIVEQUERYSERVICE_H
#define MWLUA_NATIVEQUERYSERVICE_H

#include <osg/Vec3f>
#include <components/misc/ownedjobs.hpp>
#include <components/misc/spatialindex.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
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

    using NativePositions = std::shared_ptr<const std::vector<NativeDistanceTarget>>;
    struct PositionSnapshot
    {
        NativePositions mValues;
        std::weak_ptr<const int> mSession;
    };

    struct NativeDistanceOptions
    {
        double mMaxDistance = std::numeric_limits<double>::infinity();
        bool mSortByDistance = false;
        bool mFilterBy2D = false;
    };

    /// Scoped immutable snapshots and coarse native distance kernels shared by
    /// every Lua caller. Live world objects are resolved and copied on the Lua
    /// thread; worker threads receive only values and never touch Ptr, Lua, UI,
    /// physics or scene-graph state.
    class NativeQueryService
    {
    public:
        explicit NativeQueryService(Misc::NativeJobPool& jobs);

        class PositionReadScope
        {
        public:
            explicit PositionReadScope(NativeQueryService& service);
            ~PositionReadScope();
            PositionReadScope(const PositionReadScope&) = delete;
            PositionReadScope& operator=(const PositionReadScope&) = delete;
        private:
            NativeQueryService& mService;
        };

        NativePositions snapshotObjects(const ObjectIdList& ids);
        std::optional<osg::Vec3f> objectPosition(ObjectId id);
        void invalidatePositions();

        NativeDistanceSamples queryObjects(const ObjectIdList& ids, const osg::Vec3f& origin,
            const NativeDistanceOptions& options = {});
        NativeDistanceSamples queryTargets(std::vector<NativeDistanceTarget> targets, const osg::Vec3f& origin,
            const NativeDistanceOptions& options = {});
        NativeDistanceSamples querySnapshot(const NativePositions& positions, const osg::Vec3f& origin,
            const NativeDistanceOptions& options = {});
        // Called only on the owning Lua thread. Returned immutable indices may
        // be retained independently by workers across eviction and session reset.
        std::shared_ptr<const Misc::SpatialIndex> prepareDistanceIndex(const NativePositions& positions,
            const osg::Vec3f& origin, const NativeDistanceOptions& options);

        static std::vector<NativeDistanceTarget> copyObjects(const ObjectIdList& ids);
        static NativeDistanceSamples compute(const std::vector<NativeDistanceTarget>& targets,
            const osg::Vec3f& origin, const NativeDistanceOptions& options, Misc::NativeJobPool* pool = nullptr,
            const Misc::SpatialIndex* index = nullptr);

        Misc::OwnedJobs mAsync;
        std::shared_ptr<const int> mSession = std::make_shared<const int>(0);
        std::size_t mDistanceSubmitted = 0;
        std::size_t mLuaSubmitted = 0;
        std::size_t mPositionsResolved = 0;
        std::size_t mPositionsReused = 0;
        std::size_t mPositionListsReused = 0;
        std::size_t mPositionListsBuilt = 0;
        std::size_t mDistanceIndicesBuilt = 0;
        std::size_t mDistanceIndicesReused = 0;
        void reset();

    private:
        Misc::NativeJobPool& mJobs;
        void checkPositionRevision();
        bool sharePositions() const;
        std::size_t mPositionReadDepth = 0;
        std::size_t mPositionRevision = 0;
        std::size_t mSnapshotTargets = 0;
        struct CachedList
        {
            ObjectIdList mIds; // Retain identity so a different list cannot reuse its address.
            NativePositions mValues;
        };
        std::unordered_map<ObjectId, std::optional<osg::Vec3f>> mPositionReads;
        std::unordered_map<const std::vector<ObjectId>*, CachedList> mPositionLists;
        struct CachedIndex
        {
            NativePositions mPositions;
            std::shared_ptr<const Misc::SpatialIndex> mIndex;
            std::size_t mUses = 0;
        };
        std::unordered_map<const std::vector<NativeDistanceTarget>*, CachedIndex> mDistanceIndices;
        std::deque<const std::vector<NativeDistanceTarget>*> mDistanceIndexOrder;
        std::size_t mIndexedTargets = 0;
    };
}

#endif
