#include "preparedcell.hpp"

#include "class.hpp"
#include "ptr.hpp"
#include "../mwphysics/object.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwrender/animation.hpp"
#include "../mwrender/vismask.hpp"

#include <components/debug/debuglog.hpp>
#include <components/diagnostics/timing.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/scenefootprint.hpp>
#include <components/sceneutil/userdata.hpp>

#include <algorithm>
#include <cmath>

namespace MWWorld
{
    bool StaticPreparationInput::eligible(const ConstPtr& ptr)
    {
        if (ptr.isEmpty() || ptr.getType() != ESM::REC_STAT || !ptr.isInCell()
            || !ptr.getCellRef().getRefNum().hasContentFile() || !ptr.getRefData().isEnabled()
            || ptr.getRefData().isDeletedByContentFile() || ptr.getCellRef().getCount() <= 0
            || Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId()))
            return false;
        const auto& position = ptr.getRefData().getPosition();
        return std::isfinite(ptr.getCellRef().getScale()) && ptr.getCellRef().getScale() > 0
            && std::ranges::all_of(position.pos, [](float v) { return std::isfinite(v); })
            && std::ranges::all_of(position.rot, [](float v) { return std::isfinite(v); });
    }

    StaticPreparationInput StaticPreparationInput::capture(const ConstPtr& ptr)
    {
        return { ptr.getCellRef().getRefNum(), ptr.getCellRef().getRefId(), ptr.getRefData().getPosition(),
            ptr.getCellRef().getScale(), ptr.getClass().getCorrectedModel(ptr) };
    }

    bool StaticPreparationInput::matches(const Ptr& ptr, VFS::Path::NormalizedView model) const
    {
        if (!eligible(ptr))
            return false;
        const auto& position = ptr.getRefData().getPosition();
        return mRefNum == ptr.getCellRef().getRefNum() && mRecord == ptr.getCellRef().getRefId()
            && mModel == model && mScale == ptr.getCellRef().getScale()
            && std::ranges::equal(mPosition.pos, position.pos) && std::ranges::equal(mPosition.rot, position.rot);
    }

    PreparedStaticObject::PreparedStaticObject() = default;
    PreparedStaticObject::~PreparedStaticObject() = default;
    PreparedStaticObject::PreparedStaticObject(PreparedStaticObject&&) noexcept = default;
    PreparedStaticObject& PreparedStaticObject::operator=(PreparedStaticObject&&) noexcept = default;

    std::shared_ptr<PreparedCell> PreparedCell::prepare(std::vector<StaticPreparationInput> inputs,
        Resource::ResourceSystem& resources, Resource::BulletShapeManager& shapes,
        bool dayNightSwitches, const std::atomic_bool& abort, bool prepareRendering)
    {
        Diagnostics::Scope measured("prepared-cell", "statics", "constructDetached");
        auto result = std::make_shared<PreparedCell>();
        SceneUtil::SceneFootprint footprint;
        // A partial packet is permitted, but never changes the fallback for missing entries.
        constexpr std::size_t maximumObjects = 2048;
        constexpr std::size_t maximumBytes = 64ull * 1024 * 1024;
        for (const auto& input : inputs)
        {
            if (abort || result->size() >= maximumObjects || result->bytes() >= maximumBytes)
                break;
            if (input.mModel.empty())
                continue;
            try
            {
                PreparedStaticObject object;
                object.mInput = input;
                object.mPrepared = true;
                osg::ref_ptr<SceneUtil::PositionAttitudeTransform> root;
                if (prepareRendering)
                {
                    root = new SceneUtil::PositionAttitudeTransform;
                    root->setPosition(input.mPosition.asVec3());
                    root->setAttitude(Misc::Convert::makeOsgQuat(input.mPosition));
                    root->setScale(osg::Vec3f(input.mScale, input.mScale, input.mScale));
                    root->setNodeMask(MWRender::Mask_Static);
                    SceneUtil::addUserData(*root, Ptr{});
                    object.mRendering = std::make_unique<MWRender::ObjectAnimation>(
                        root, std::string(input.mModel.value()), &resources, dayNightSwitches);
                }
                auto shape = shapes.getInstance(input.mModel, input.mScale);
                if (shape && shape->mCollisionShape)
                    object.mPhysics = std::make_shared<MWPhysics::Object>(std::move(shape),
                        input.mPosition.asVec3(), Misc::Convert::makeOsgQuat(input.mPosition), input.mScale);
                // Estimate shared assets once; collision allowance is admission accounting, not measured VRAM.
                result->mEstimatedBytes += (root ? footprint.measure(*root) : 0) + 4096;
                if (result->mObjects.emplace(input.mRefNum, std::move(object)).second && root)
                    result->mCompileRoot->addChild(root);
            }
            catch (const std::exception& error)
            {
                Log(Debug::Warning) << "Detached static preparation failed for " << input.mRecord << ": " << error.what();
            }
        }
        return result;
    }

    bool PreparedCell::ready() const
    {
        return mCompletion && mCompletion->load(std::memory_order_acquire);
    }

    void PreparedCell::retainPreparationCharge(std::shared_ptr<void> charge)
    {
        mPreparationCharge = std::move(charge);
        for (const auto& [ref, object] : mObjects)
            if (object.mPhysics) object.mPhysics->retainPreparationCharge(mPreparationCharge);
    }

    std::size_t PreparedCell::stagePhysics(MWPhysics::PhysicsSystem& physics,
        std::chrono::steady_clock::time_point deadline, std::size_t maximumObjects)
    {
        if (!ready() || mDetached || mStagingFailed || physicsStaged()) return 0;
        if (mStaged == 0) mStageCursor = mObjects.begin();
        std::size_t count = 0;
        try
        {
            while (mStageCursor != mObjects.end() && count < maximumObjects
                && std::chrono::steady_clock::now() < deadline)
            {
                if (!physics.stagePreparedObject(mStageCursor->second.mPhysics)) break;
                ++mStageCursor; ++mStaged; ++count;
            }
        }
        catch (const std::exception& error)
        {
            mStagingFailed = true;
            Log(Debug::Warning) << "Dormant collision staging failed: " << error.what();
        }
        return count;
    }

    void PreparedCell::detachCompileRoot()
    {
        if (!ready() || mDetached)
            throw std::logic_error("Prepared cell is not ready for a unique handover");
        mCompileRoot->removeChildren(0, mCompileRoot->getNumChildren());
        mDetached = true;
    }

    PreparedStaticObject PreparedCell::take(const Ptr& ptr, VFS::Path::NormalizedView model, bool rendering)
    {
        if (!mDetached || !ready() || ptr.isEmpty())
            return {};
        const auto found = mObjects.find(ptr.getCellRef().getRefNum());
        if (found == mObjects.end() || found->second.mConsumed || !found->second.mInput.matches(ptr, model))
            return {};
        // Keep the empty slot until worker-side retirement; no large graph destructor on a mismatch.
        auto& source = found->second;
        PreparedStaticObject result;
        if (rendering)
            result.mRendering = std::move(source.mRendering);
        if (result.mRendering)
            ++mRenderedUses;
        else
            ++mCollisionOnlyUses;
        result.mPhysics = std::move(source.mPhysics);
        result.mPrepared = true;
        source.mConsumed = true;
        return result;
    }
}
