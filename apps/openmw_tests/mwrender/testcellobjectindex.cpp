#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwrender/objects.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"
#include "apps/openmw_tests/mwworld/lookup_test_lua.hpp"
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <gtest/gtest.h>
#include <osg/Group>
#include <osg/Stats>

namespace
{
    class RendererCellIndexTest : public testing::Test
    {
    protected:
        MWBase::Environment mEnvironment;
        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        MWWorld::LookupTestLuaManager mLua;
        MWWorld::WorldModel mWorld{ mStore, mReaders };
        osg::ref_ptr<osg::Group> mRoot = new osg::Group;
        SceneUtil::UnrefQueue mUnref;
        std::unique_ptr<MWRender::Objects> mObjects;
        const ESM::Static* mBase = nullptr;

        void SetUp() override
        {
            MWClass::Static::registerSelf();
            mEnvironment.setWorldModel(mWorld);
            mEnvironment.setESMStore(mStore);
            mEnvironment.setLuaManager(mLua);
            ESM::Static base;
            base.blank();
            base.mId = ESM::RefId::stringRefId("cell renderer fixture");
            mBase = mStore.insertStatic(base);
            // Empty models still exercise real Animation/scene graph ownership
            // without requiring an OpenGL context or external game assets.
            mObjects = std::make_unique<MWRender::Objects>(nullptr, mRoot, mUnref);
        }

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
        MWWorld::Ptr place(MWWorld::CellStore& room)
        {
            ESM::CellRef ref;
            ref.blank();
            ref.mRefID = mBase->mId;
            ref.mCount = 1;
            MWWorld::LiveCellRef<ESM::Static> temporary(ref, mBase);
            const MWWorld::Ptr original(&temporary);
            auto ptr = original.getClass().copyToCell(original, room, 1);
            mObjects->insertModel(ptr, "", false);
            return ptr;
        }
    };

    TEST_F(RendererCellIndexTest, ActiveMoveSurvivesUnloadingOldCell)
    {
        auto& a = cell("a");
        auto& b = cell("b");
        const auto ptr = place(a);
        auto* animation = mObjects->getAnimation(ptr);
        const auto moved = a.moveTo(ptr, &b);
        mObjects->updatePtr(ptr, moved);
        mObjects->removeCell(&a);
        EXPECT_EQ(mObjects->getAnimation(moved), animation);
        EXPECT_EQ(mRoot->getNumChildren(), 1u);
        mObjects->removeCell(&b);
        EXPECT_EQ(mObjects->getAnimation(moved), nullptr);
        EXPECT_EQ(mRoot->getNumChildren(), 0u);
        mObjects->removeCell(&b);
    }

    TEST_F(RendererCellIndexTest, RemovedObjectAndReinsertedObjectHaveNoStaleMembership)
    {
        auto& a = cell("a");
        const auto ptr = place(a);
        EXPECT_TRUE(mObjects->removeObject(ptr));
        EXPECT_EQ(ptr.getRefData().getBaseNode(), nullptr);
        mObjects->insertModel(ptr, "", false);
        mObjects->removeCell(&a);
        EXPECT_EQ(mObjects->getAnimation(ptr), nullptr);
        EXPECT_EQ(mRoot->getNumChildren(), 0u);
    }

    TEST_F(RendererCellIndexTest, SmallCellUnloadVisitsOnlyItsAnimations)
    {
        auto& other = cell("other");
        auto& leaving = cell("leaving");
        for (int i = 0; i < 1000; ++i)
            place(other);
        std::vector<MWWorld::Ptr> targets;
        for (int i = 0; i < 3; ++i)
            targets.push_back(place(leaving));
        mObjects->removeCell(&leaving);
        for (const auto& ptr : targets)
            EXPECT_EQ(mObjects->getAnimation(ptr), nullptr);
        osg::ref_ptr<osg::Stats> stats = new osg::Stats("cell renderer test");
        mObjects->reportStats(1, *stats);
        double visited = 0, removed = 0;
        ASSERT_TRUE(stats->getAttribute(1, "Cell Unload Objects Visited", visited));
        ASSERT_TRUE(stats->getAttribute(1, "Cell Unload Objects Removed", removed));
        EXPECT_EQ(visited, Misc::useCellObjectIndex() ? 3 : 1003);
        EXPECT_EQ(removed, 3);
        RecordProperty("visited", static_cast<int>(visited));
        RecordProperty("removed", static_cast<int>(removed));
    }
}
