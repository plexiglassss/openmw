#ifndef OPENMW_MWWORLD_PREPAREDCELL_H
#define OPENMW_MWWORLD_PREPAREDCELL_H

#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/refnum.hpp>
#include <components/vfs/pathutil.hpp>

#include <osg/Group>
#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include <chrono>

namespace Resource { class ResourceSystem; class BulletShapeManager; }
namespace MWRender { class ObjectAnimation; }
namespace MWPhysics { class Object; class PhysicsSystem; }

namespace MWWorld
{
    class ConstPtr;
    class Ptr;

    // Immutable values only: workers never receive a CellStore, LiveCellRef or gameplay Ptr.
    struct StaticPreparationInput
    {
        ESM::RefNum mRefNum;
        ESM::RefId mRecord;
        ESM::Position mPosition;
        float mScale = 1;
        VFS::Path::Normalized mModel;

        static bool eligible(const ConstPtr& ptr);
        static StaticPreparationInput capture(const ConstPtr& ptr);
        bool matches(const Ptr& ptr, VFS::Path::NormalizedView model) const;
    };

    struct PreparedStaticObject
    {
        StaticPreparationInput mInput;
        std::unique_ptr<MWRender::ObjectAnimation> mRendering;
        std::shared_ptr<MWPhysics::Object> mPhysics;
        bool mPrepared = false;
        bool mConsumed = false;
        PreparedStaticObject();
        ~PreparedStaticObject();
        PreparedStaticObject(PreparedStaticObject&&) noexcept;
        PreparedStaticObject& operator=(PreparedStaticObject&&) noexcept;
    };

    // CPU-prepared is deliberately separate from graphics-ready. The caller supplies
    // an explicit compile completion gate before any instance can be consumed.
    class PreparedCell
    {
    public:
        static std::shared_ptr<PreparedCell> prepare(std::vector<StaticPreparationInput> inputs,
            Resource::ResourceSystem& resources, Resource::BulletShapeManager& shapes,
            bool dayNightSwitches, const std::atomic_bool& abort, bool prepareRendering = true);

        osg::Group* compileRoot() const { return mCompileRoot.get(); }
        void setCompileCompletion(std::shared_ptr<std::atomic_bool> completion) { mCompletion = std::move(completion); }
        bool ready() const;
        void retainPreparationCharge(std::shared_ptr<void> charge);
        std::size_t stagePhysics(MWPhysics::PhysicsSystem& physics,
            std::chrono::steady_clock::time_point deadline, std::size_t maximumObjects = 64);
        bool physicsStaged() const { return mStaged == mObjects.size() && !mStagingFailed; }
        bool stagingFailed() const { return mStagingFailed; }
        // The owner calls this once, after graphics preparation, before ordered insertion.
        void detachCompileRoot();
        PreparedStaticObject take(const Ptr& ptr, VFS::Path::NormalizedView model, bool rendering = true);
        std::size_t size() const { return mObjects.size(); }
        std::size_t bytes() const { return mEstimatedBytes; }
        std::size_t renderedUses() const { return mRenderedUses; }
        std::size_t collisionOnlyUses() const { return mCollisionOnlyUses; }
        std::size_t unused() const { return size() - mRenderedUses - mCollisionOnlyUses; }

    private:
        // Keep admission credit until all graph and collision owners have been destroyed.
        std::shared_ptr<void> mPreparationCharge;
        std::map<ESM::RefNum, PreparedStaticObject> mObjects;
        osg::ref_ptr<osg::Group> mCompileRoot = new osg::Group;
        std::shared_ptr<std::atomic_bool> mCompletion;
        std::size_t mEstimatedBytes = 0;
        std::size_t mRenderedUses = 0, mCollisionOnlyUses = 0;
        bool mDetached = false;
        std::size_t mStaged = 0;
        bool mStagingFailed = false;
        std::map<ESM::RefNum, PreparedStaticObject>::iterator mStageCursor;
    };
}
#endif
