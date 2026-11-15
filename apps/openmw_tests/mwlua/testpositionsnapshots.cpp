#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwlua/context.hpp"
#include "apps/openmw/mwlua/nativequeryservice.hpp"
#include "apps/openmw/mwlua/nearbybindings.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <components/esm3/loadnpc.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/misc/nativejobpool.hpp>
#include <gtest/gtest.h>

namespace
{
    class PositionSnapshots : public testing::Test
    {
    protected:
        MWBase::Environment mEnvironment;
        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        MWWorld::WorldModel mWorld{ mStore, mReaders };
        Misc::NativeJobPool mJobs{ 2, 8 };
        MWLua::NativeQueryService mService{ mJobs };
        ESM::NPC mNpc;
        std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::NPC>>> mRefs;
        void SetUp() override
        {
            MWClass::Npc::registerSelf();
            mEnvironment.setWorldModel(mWorld);
            mEnvironment.setESMStore(mStore);
            mNpc.blank();
            mNpc.mId = ESM::RefId::stringRefId("position_fixture");
        }
        MWWorld::Ptr object(float x, ESM::RefNum id = {})
        {
            ESM::CellRef ref;
            ref.blank();
            ref.mRefID = mNpc.mId;
            ref.mRefNum = id.isSet() ? id : ESM::RefNum{ static_cast<unsigned>(mRefs.size() + 1), 0 };
            auto live = std::make_unique<MWWorld::LiveCellRef<ESM::NPC>>(ref, &mNpc);
            MWWorld::Ptr ptr(live.get());
            move(ptr, x);
            mWorld.registerPtr(ptr);
            mRefs.push_back(std::move(live));
            return ptr;
        }
        static void move(const MWWorld::Ptr& ptr, float x)
        {
            auto position = ptr.getRefData().getPosition();
            position.pos[0] = x;
            position.pos[1] = position.pos[2] = 0;
            ptr.getRefData().setPosition(position);
        }
    };

    TEST_F(PositionSnapshots, ReusesWholeListsAndOverlappingObjectsWithinOneReadPhase)
    {
        const auto a = object(3), b = object(4);
        auto ids = std::make_shared<std::vector<MWLua::ObjectId>>(
            std::initializer_list<MWLua::ObjectId>{ MWLua::getId(a), MWLua::getId(b), MWLua::getId(a) });
        MWLua::NativeQueryService::PositionReadScope reads(mService);
        auto first = mService.snapshotObjects(ids);
        auto second = mService.snapshotObjects(ids);
        auto overlap = std::make_shared<std::vector<MWLua::ObjectId>>(*ids);
        auto third = mService.snapshotObjects(overlap);
        ASSERT_EQ(first->size(), 3u);
        EXPECT_EQ((*first)[2].mSourceIndex, 3u);
        EXPECT_EQ((*third)[2].mPosition, (*first)[2].mPosition);
        if (Misc::useSharedPositionReads())
        {
            EXPECT_EQ(first, second);
            EXPECT_EQ(mService.mPositionsResolved, 2u);
            EXPECT_EQ(mService.mPositionListsReused, 1u);
        }
        const auto baseline = MWLua::NativeQueryService::copyObjects(ids);
        for (std::size_t i = 0; i < baseline.size(); ++i)
            EXPECT_EQ(baseline[i].mPosition, (*first)[i].mPosition);
    }

    TEST_F(PositionSnapshots, MovementBetweenPhasesAndRegistryReplacementInvalidateAutomaticReuse)
    {
        auto a = object(3);
        auto ids = std::make_shared<std::vector<MWLua::ObjectId>>(1, MWLua::getId(a));
        MWLua::NativePositions retained;
        {
            MWLua::NativeQueryService::PositionReadScope reads(mService);
            retained = mService.snapshotObjects(ids);
        }
        move(a, 9);
        {
            MWLua::NativeQueryService::PositionReadScope reads(mService);
            EXPECT_EQ(mService.snapshotObjects(ids)->front().mPosition.x(), 9);
            object(12, MWLua::getId(a));
            EXPECT_EQ(mService.snapshotObjects(ids)->front().mPosition.x(), 12);
        }
        EXPECT_EQ(retained->front().mPosition.x(), 3);
        EXPECT_EQ(mService.snapshotObjects(ids)->front().mPosition.x(), 12);
    }

    TEST_F(PositionSnapshots, InvalidatedRemovalAndChangedMembershipDoNotChangeRetainedSnapshot)
    {
        auto a = object(3), b = object(4);
        auto ids = std::make_shared<std::vector<MWLua::ObjectId>>(
            std::initializer_list<MWLua::ObjectId>{ MWLua::getId(a), MWLua::getId(b) });
        MWLua::NativeQueryService::PositionReadScope reads(mService);
        auto retained = mService.snapshotObjects(ids);
        a.getCellRef().setCount(0);
        mService.invalidatePositions(); // Queued world actions invalidate before the next callback.
        auto current = mService.snapshotObjects(ids);
        ASSERT_EQ(current->size(), 1u);
        EXPECT_EQ(current->front().mSourceIndex, 2u);
        EXPECT_EQ(retained->size(), 2u);
        ids->assign(1, MWLua::getId(b));
        mService.invalidatePositions();
        EXPECT_EQ(mService.snapshotObjects(ids)->front().mSourceIndex, 1u);
        mService.reset();
        EXPECT_EQ(retained->size(), 2u);
    }

    TEST_F(PositionSnapshots, PublicBindingsShareObjectListsButObserveMutableArrayValues)
    {
        auto a = object(3);
        auto ids = std::make_shared<std::vector<MWLua::ObjectId>>(1, MWLua::getId(a));
        LuaUtil::LuaState runtime(nullptr, nullptr);
        MWLua::Context context{ MWLua::Context::Local };
        context.mLua = &runtime;
        context.mNativeQueryService = &mService;
        sol::state_view lua(runtime.unsafeState());
        sol::table api(lua, sol::create);
        MWLua::addNearbyDistanceBindings(api, context);
        lua["nearby"] = api;
        lua["objects"] = MWLua::LObjectList{ ids };
        lua["obj"] = MWLua::LObject(MWLua::getId(a));
        lua["zero"] = osg::Vec3f();
        lua["far"] = osg::Vec3f(9, 0, 0);
        if (!api["snapshotPositions"].valid())
            GTEST_SKIP() << "Asynchronous distance API disabled for this comparison";
        const auto run = [&](const char* script) {
            LuaUtil::LuaState::throwIfError(lua.safe_script(script, sol::script_pass_on_error));
        };
        {
            MWLua::NativeQueryService::PositionReadScope reads(mService);
            run("s=nearby.snapshotPositions(objects); assert(nearby.queryDistances(objects,zero):distance(1)==3); "
                "assert(nearby.queryDistances(s,zero):distance(1)==3); "
                "a={zero}; assert(nearby.queryDistances(a,zero):distance(1)==0); "
                "a[1]=far; assert(nearby.queryDistances(a,zero):distance(1)==9); "
                "mixed={obj,obj,far}; r=nearby.queryDistances(mixed,zero); "
                "assert(#r==3 and r:distance(1)==3 and r:distance(2)==3 and r:sourceIndex(3)==3); "
                "ticket=assert(nearby.queryDistancesAsync(objects,zero))");
            if (Misc::useSharedPositionReads())
                EXPECT_EQ(mService.mPositionListsReused, 2u);
            a.getCellRef().setCount(0);
            mService.invalidatePositions();
            run("r=nearby.queryDistances(mixed,zero); assert(#r==1 and r:sourceIndex(1)==3); "
                "assert(nearby.queryDistances(s,zero):distance(1)==3)");
            a.getCellRef().setCount(1);
            mService.invalidatePositions();
        }
        move(a, 12);
        run("assert(nearby.queryDistances(objects,zero):distance(1)==12); "
            "assert(nearby.queryDistances(s,zero):distance(1)==3)");
        mService.reset();
        run("assert(ticket:poll()=='cancelled'); assert(not pcall(nearby.queryDistances,s,zero))");
    }
}
