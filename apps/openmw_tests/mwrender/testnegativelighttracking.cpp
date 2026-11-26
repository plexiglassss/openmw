#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/light.hpp"
#include "apps/openmw/mwrender/animation.hpp"
#include "apps/openmw/mwrender/negativelighttracker.hpp"
#include "apps/openmw/mwrender/objects.hpp"
#include "apps/openmw/mwrender/renderinginterface.hpp"
#include "apps/openmw/mwrender/vismask.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"
#include "apps/openmw_tests/mwworld/lookup_test_lua.hpp"

#include <components/esm3/loadligh.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/lightcontroller.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/lightutil.hpp>
#include <components/sceneutil/unrefqueue.hpp>

#include <gtest/gtest.h>
#include <osg/FrameStamp>
#include <osg/Group>
#include <osg/NodeVisitor>

#include <memory>
#include <vector>

namespace
{
    // Exercise the same base Animation attachment operations used by equipped
    // lights, without needing an NPC skeleton or external game assets.
    class LightAttachmentAnimation : public MWRender::Animation
    {
        osg::ref_ptr<osg::Group> mPart;

    public:
        explicit LightAttachmentAnimation(const MWWorld::Ptr& ptr)
            : Animation(ptr, new osg::Group, nullptr)
        {
        }

        void equip(const ESM::Light& light)
        {
            unequip();
            mPart = new osg::Group;
            getOrCreateObjectRoot()->addChild(mPart);
            addExtraLight(mPart, SceneUtil::LightCommon(light));
        }

        void unequip()
        {
            removeExtraLight();
            if (mPart)
                getOrCreateObjectRoot()->removeChild(mPart);
            mPart = nullptr;
        }

        osg::ref_ptr<SceneUtil::LightSource> attachAdditional(const ESM::Light& light)
        {
            const SceneUtil::LightCommon common(light);
            auto source = SceneUtil::addLight(getOrCreateObjectRoot(), common, MWRender::Mask_Lighting, false);
            trackNegativeLight(source.get(), common);
            return source;
        }

        void detachAdditional(SceneUtil::LightSource* source)
        {
            getOrCreateObjectRoot()->removeChild(source);
            untrackNegativeLight(source);
        }

        SceneUtil::LightSource* equippedSource() { return mExtraLightSource.get(); }
    };

    class NegativeLightLifecycleTest : public testing::Test, public MWRender::RenderingInterface
    {
    protected:
        MWBase::Environment mEnvironment;
        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        MWWorld::LookupTestLuaManager mLua;
        MWWorld::WorldModel mWorld{ mStore, mReaders };
        osg::ref_ptr<osg::Group> mRoot = new osg::Group;
        std::shared_ptr<MWRender::NegativeLightTracker> mTracker = std::make_shared<MWRender::NegativeLightTracker>();
        SceneUtil::UnrefQueue mUnref;
        std::unique_ptr<MWRender::Objects> mObjects;
        const ESM::Light* mBase = nullptr;

        void SetUp() override
        {
            MWClass::Light::registerSelf();
            mEnvironment.setWorldModel(mWorld);
            mEnvironment.setESMStore(mStore);
            mEnvironment.setLuaManager(mLua);
            ESM::Light base;
            base.blank();
            base.mId = ESM::RefId::stringRefId("negative lighting fixture");
            base.mData.mFlags = ESM::Light::Negative;
            base.mData.mColor = 0x010203;
            base.mData.mRadius = 128;
            mBase = mStore.insertStatic(base);
            mObjects = std::make_unique<MWRender::Objects>(nullptr, mRoot, mUnref, mTracker);
        }

        void TearDown() override
        {
            // The production renderer disables its callback before its scene
            // members are destroyed, too. Test callbacks may refer to locals.
            mTracker->setCallback({});
            mObjects.reset();
        }

        MWRender::Objects& getObjects() override { return *mObjects; }

        MWWorld::CellStore& cell(std::string_view name)
        {
            ESM::Cell record;
            record.blank();
            record.mName = name;
            record.mData.mFlags = ESM::Cell::Interior;
            record.updateId();
            mStore.insert(record);
            return mWorld.getCell(record.mId);
        }

        const ESM::Light* variant(std::string_view id, int flags, unsigned int colour, int radius = 128)
        {
            auto record = *mBase;
            record.mId = ESM::RefId::stringRefId(id);
            record.mData.mFlags = flags;
            record.mData.mColor = colour;
            record.mData.mRadius = radius;
            return mStore.insertStatic(record);
        }

        MWWorld::Ptr place(MWWorld::CellStore& room, const ESM::Light* base = nullptr, bool render = true)
        {
            if (!base)
                base = mBase;
            ESM::CellRef ref;
            ref.blank();
            ref.mRefID = base->mId;
            ref.mCount = 1;
            MWWorld::LiveCellRef<ESM::Light> temporary(ref, base);
            const MWWorld::Ptr original(&temporary);
            auto ptr = original.getClass().copyToCell(original, room, 1);
            if (render)
                ptr.getClass().insertObjectRendering(ptr, "", *this);
            return ptr;
        }
    };

    TEST_F(NegativeLightLifecycleTest, PlacedSourcesRemainUntilTheLastRemoval)
    {
        auto& room = cell("dynamic room");
        std::vector<const void*> changes;
        mTracker->setCallback([&](const void* changed) { changes.push_back(changed); });
        const auto first = place(room);
        const auto second = place(room);
        EXPECT_EQ(mTracker->count(room.getCell()), 2u);
        ASSERT_EQ(changes.size(), 1u);
        EXPECT_EQ(changes.front(), room.getCell());
        mObjects->getAnimation(first)->setNegativeLightTracker(mTracker);
        EXPECT_EQ(mTracker->count(room.getCell()), 2u);
        EXPECT_TRUE(mObjects->removeObject(first));
        EXPECT_EQ(mTracker->count(room.getCell()), 1u);
        EXPECT_EQ(changes.size(), 1u);
        EXPECT_TRUE(mObjects->removeObject(second));
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);
        EXPECT_EQ(changes.size(), 2u);
        // Membership is removed on the scene thread, before deferred deletion.
        EXPECT_EQ(mUnref.getSize(), 2u);
        EXPECT_TRUE(mObjects->removeObject(second));
        EXPECT_EQ(changes.size(), 2u);
    }

    TEST_F(NegativeLightLifecycleTest, ActiveMoveTransfersMembershipBeforeOldCellUnload)
    {
        auto& first = cell("first");
        auto& second = cell("second");
        const auto ptr = place(first);
        const auto moved = first.moveTo(ptr, &second);
        mObjects->updatePtr(ptr, moved);
        EXPECT_EQ(mTracker->count(first.getCell()), 0u);
        EXPECT_EQ(mTracker->count(second.getCell()), 1u);
        mObjects->removeCell(&first);
        EXPECT_EQ(mTracker->count(second.getCell()), 1u);
        mObjects->removeCell(&second);
        EXPECT_EQ(mTracker->count(second.getCell()), 0u);
    }

    TEST_F(NegativeLightLifecycleTest, OffDefaultAndBlackRecordsDoNotActivateButZeroRadiusDoes)
    {
        auto& room = cell("controls");
        // Call the real MWClass::Light insertion policy, not a test-side
        // approximation of its OffDefault rule.
        place(room, variant("off", ESM::Light::Negative | ESM::Light::OffDefault, 0x010203));
        place(room, variant("black", ESM::Light::Negative, 0xff000000));
        place(room, variant("positive", 0, 0x010203));
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);
        const auto active = place(room, variant("zero radius", ESM::Light::Negative, 0x010203, 0));
        EXPECT_EQ(mTracker->count(room.getCell()), 1u);
        mObjects->removeObject(active);
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);
    }

    TEST_F(NegativeLightLifecycleTest, DisableReenableAndClearedWorldRebuildMembership)
    {
        auto& first = cell("old scene");
        const auto ptr = place(first);
        EXPECT_EQ(mTracker->count(first.getCell()), 1u);
        // World disable/enable uses these removal/insertion paths.
        mObjects->removeObject(ptr);
        EXPECT_EQ(mTracker->count(first.getCell()), 0u);
        ptr.getClass().insertObjectRendering(ptr, "", *this);
        EXPECT_EQ(mTracker->count(first.getCell()), 1u);

        // Save loading clears the rendering summary before destroying the old
        // cells. No registry entry or deferred destructor may retain that cell.
        const void* oldIdentity = first.getCell();
        mTracker->clear();
        mObjects->removeCell(&first);
        mWorld.clear();
        EXPECT_EQ(mTracker->count(oldIdentity), 0u);
        auto& elsewhere = cell("loaded elsewhere");
        EXPECT_EQ(mTracker->count(elsewhere.getCell()), 0u);
        place(elsewhere);
        EXPECT_EQ(mTracker->count(elsewhere.getCell()), 1u);
        mObjects->removeCell(&elsewhere);
        EXPECT_EQ(mTracker->count(elsewhere.getCell()), 0u);
    }

    TEST_F(NegativeLightLifecycleTest, InventoryReferenceAndPreviewCannotActivateTheWorld)
    {
        auto& room = cell("preview room");
        const auto ptr = place(room, nullptr, false);
        // The preview contains a genuine native negative source, but is never
        // registered with the world tracker.
        LightAttachmentAnimation preview(ptr);
        preview.equip(*mBase);
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);

        ESM::CellRef ref;
        ref.blank();
        ref.mRefID = mBase->mId;
        ref.mCount = 1;
        MWWorld::LiveCellRef<ESM::Light> inventory(ref, mBase);
        LightAttachmentAnimation inventoryOnly{ MWWorld::Ptr(&inventory) };
        inventoryOnly.setNegativeLightTracker(mTracker);
        inventoryOnly.equip(*mBase);
        EXPECT_EQ(mTracker->count(nullptr), 0u);
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);
    }

    TEST_F(NegativeLightLifecycleTest, EquippedAndAdditionalSourcesFollowActualAttachment)
    {
        auto& room = cell("equipment");
        const auto ptr = place(room, nullptr, false);
        LightAttachmentAnimation animation(ptr);
        animation.setNegativeLightTracker(mTracker);
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);
        // Equipped lights really emit even when the placed record is OffDefault.
        const auto* carried
            = variant("carried", ESM::Light::Negative | ESM::Light::Carry | ESM::Light::OffDefault, 0x010203);
        animation.equip(*carried);
        EXPECT_EQ(mTracker->count(room.getCell()), 1u);
        const auto hidden = animation.attachAdditional(*mBase);
        EXPECT_EQ(mTracker->count(room.getCell()), 2u);
        animation.unequip();
        EXPECT_EQ(mTracker->count(room.getCell()), 1u);
        animation.detachAdditional(hidden);
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);
        animation.equip(*carried);
        animation.removeFromScene();
        EXPECT_EQ(mTracker->count(room.getCell()), 0u);
    }

    TEST_F(NegativeLightLifecycleTest, FlickerPulseFadeAndCameraMasksDoNotToggleMembership)
    {
        auto& room = cell("animated lighting");
        const auto ptr = place(room, nullptr, false);
        for (int flags : { ESM::Light::Flicker, ESM::Light::FlickerSlow, ESM::Light::Pulse, ESM::Light::PulseSlow })
        {
            auto record = *mBase;
            record.mData.mFlags |= flags;
            LightAttachmentAnimation animation(ptr);
            animation.setNegativeLightTracker(mTracker);
            animation.equip(record);
            auto* source = animation.equippedSource();
            ASSERT_NE(source, nullptr);
            auto* callback = source->getUpdateCallback();
            while (callback && !dynamic_cast<SceneUtil::LightController*>(callback))
                callback = callback->getNestedCallback();
            auto* controller = dynamic_cast<SceneUtil::LightController*>(callback);
            ASSERT_NE(controller, nullptr);
            const osg::ref_ptr<osg::FrameStamp> stamp = new osg::FrameStamp;
            osg::NodeVisitor visitor;
            visitor.setFrameStamp(stamp);
            // Run the actual native controller at zero actor fade. Eligibility
            // remains tied to authored RGB, not the instantaneous diffuse value.
            source->setActorFade(0.f);
            source->setNodeMask(0);
            for (unsigned int frame = 0; frame < 4; ++frame)
            {
                visitor.setTraversalNumber(frame);
                stamp->setSimulationTime(1.0 + frame * 0.25);
                (*controller)(source, &visitor);
                EXPECT_EQ(source->getLight(frame)->getDiffuse().r(), 0.f);
                EXPECT_EQ(mTracker->count(room.getCell()), 1u);
            }
            animation.unequip();
            EXPECT_EQ(mTracker->count(room.getCell()), 0u);
        }
    }
}
