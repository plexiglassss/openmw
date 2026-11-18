#include "lookup_test_lua.hpp"
#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/postponedphysics.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"
#include <components/esm3/loadstat.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/readerscache.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <set>

namespace MWWorld
{
    namespace
    {
        class PostponedPhysicsTest : public testing::Test
        {
        protected:
            MWBase::Environment mEnvironment;
            ESMStore mStore;
            ESM::ReadersCache mReaders;
            LookupTestLuaManager mLua;
            WorldModel mWorld{ mStore, mReaders };
            std::set<CellStore*, std::less<>> mActive;
            std::vector<Ptr> mInserted;
            std::size_t mVisited = 0;
            const ESM::Static* mBase = nullptr;

            void SetUp() override
            {
                MWClass::Static::registerSelf();
                mEnvironment.setWorldModel(mWorld);
                mEnvironment.setESMStore(mStore);
                mEnvironment.setLuaManager(mLua);
                ESM::Static base;
                base.blank();
                base.mId = ESM::RefId::stringRefId("postponed fixture");
                mBase = mStore.insertStatic(base);
            }

            CellStore& cell(std::string_view name, bool active = true)
            {
                ESM::Cell record;
                record.blank();
                record.mName = name;
                record.mData.mFlags = ESM::Cell::Interior;
                record.updateId();
                mStore.insert(record);
                auto& result = mWorld.getCell(record.mId);
                if (active)
                    mActive.insert(&result);
                return result;
            }

            Ptr place(CellStore& cell, bool postponed = true, bool enabled = true)
            {
                ESM::CellRef ref;
                ref.blank();
                ref.mRefID = mBase->mId;
                ref.mCount = 1;
                LiveCellRef<ESM::Static> temporary(ref, mBase);
                temporary.mData.mPhysicsPostponed = postponed;
                if (!enabled)
                    temporary.mData.disable();
                // Exercise the real temporary-copy and registration path used by PlaceItem.
                const Ptr ptr(&temporary);
                return ptr.getClass().copyToCell(ptr, cell, 1);
            }

            void drain()
            {
                updatePostponedPhysics(mWorld, mActive, [&](const Ptr& ptr) {
                    ++mVisited;
                    mInserted.push_back(ptr);
                });
            }
        };

        TEST_F(PostponedPhysicsTest, RegisteredCopySurvivesTemporaryAndIsConsumedOnce)
        {
            auto& room = cell("active");
            const auto ptr = place(room);
            mWorld.registerPtr(ptr);
            mWorld.registerPtr(ptr);
            drain(); drain();
            EXPECT_EQ(mVisited, 1u);
            ASSERT_EQ(mInserted.size(), 1u);
            EXPECT_EQ(mInserted.front(), ptr);
            EXPECT_FALSE(ptr.getRefData().mPhysicsPostponed);
        }

        TEST_F(PostponedPhysicsTest, IdempotentRegistrationStillObservesFlagChange)
        {
            auto& room = cell("active");
            const auto ptr = place(room, false);
            ptr.getRefData().mPhysicsPostponed = true;
            mWorld.registerPtr(ptr);
            drain();
            EXPECT_EQ(mInserted.size(), 1u);
        }

        TEST_F(PostponedPhysicsTest, DisabledObjectClearsFlagWithoutAddingCollision)
        {
            const auto ptr = place(cell("active"), true, false);
            drain();
            EXPECT_TRUE(mInserted.empty());
            EXPECT_FALSE(ptr.getRefData().mPhysicsPostponed);
            ptr.getRefData().enable();
            mWorld.registerPtr(ptr);
            drain();
            EXPECT_EQ(mVisited, 0u);
        }

        TEST_F(PostponedPhysicsTest, InactivePlacementAndMovementUseCurrentCell)
        {
            auto& a = cell("active");
            auto& b = cell("inactive", false);
            const auto ptr = place(b);
            drain();
            EXPECT_TRUE(mInserted.empty());
            const auto moved = b.moveTo(ptr, &a);
            drain();
            ASSERT_EQ(mInserted.size(), 1u);
            EXPECT_EQ(mInserted.front().mCell, &a);
            EXPECT_EQ(mInserted.front(), moved);
        }

        TEST_F(PostponedPhysicsTest, ActiveToInactiveAndBackRemainsPending)
        {
            auto& a = cell("active");
            auto& b = cell("inactive", false);
            const auto ptr = place(a);
            const auto moved = a.moveTo(ptr, &b);
            drain();
            EXPECT_TRUE(mInserted.empty());
            b.moveTo(moved, &a);
            drain();
            EXPECT_EQ(mInserted.size(), 1u);
        }

        TEST_F(PostponedPhysicsTest, ActiveCellMoveDoesNotRetainOldLocation)
        {
            auto& a = cell("first"), &b = cell("second");
            const auto ptr = place(a);
            a.moveTo(ptr, &b);
            drain();
            ASSERT_EQ(mInserted.size(), 1u);
            EXPECT_EQ(mInserted.front().mCell, &b);
        }

        TEST_F(PostponedPhysicsTest, DeregisteredReferencesAreNotVisited)
        {
            const auto ptr = place(cell("active"));
            mWorld.deregisterLiveCellRef(*ptr.mRef);
            drain();
            EXPECT_EQ(mVisited, 0u);
        }

        TEST_F(PostponedPhysicsTest, OldReferenceRemovalDoesNotEraseReplacement)
        {
            auto& room = cell("active");
            const auto old = place(room);
            const auto replacement = old.getClass().moveToCell(old, room);
            mWorld.deregisterLiveCellRef(*old.mRef);
            drain();
            ASSERT_EQ(mInserted.size(), 1u);
            EXPECT_EQ(mInserted.front(), replacement);
        }

        TEST_F(PostponedPhysicsTest, DynamicCountZeroMatchesCellAccessibility)
        {
            const auto ptr = place(cell("active"));
            ESM::ObjectState state;
            ptr.getCellRef().writeState(state);
            state.mRef.mCount = 0;
            ptr.getCellRef() = CellRef(state.mRef);
            drain();
            EXPECT_TRUE(mInserted.empty());
            EXPECT_TRUE(ptr.getRefData().mPhysicsPostponed);
            ptr.getCellRef().setCount(1);
            drain();
            EXPECT_EQ(mInserted.size(), 1u);
        }

        TEST_F(PostponedPhysicsTest, ContentCountZeroClearsFlagWithoutCollision)
        {
            const auto ptr = place(cell("active"));
            mWorld.deregisterLiveCellRef(*ptr.mRef);
            ESM::ObjectState state;
            ptr.getCellRef().writeState(state);
            state.mRef.mRefNum = { 42, 0 };
            state.mRef.mCount = 0;
            ptr.getCellRef() = CellRef(state.mRef);
            mWorld.registerPtr(ptr);
            drain();
            EXPECT_TRUE(mInserted.empty());
            EXPECT_FALSE(ptr.getRefData().mPhysicsPostponed);
        }

        TEST_F(PostponedPhysicsTest, ContentDeletionIsSkippedUntilRestored)
        {
            const auto ptr = place(cell("active"));
            ptr.getRefData().setDeletedByContentFile(true);
            drain();
            EXPECT_TRUE(mInserted.empty());
            ptr.getRefData().setDeletedByContentFile(false);
            drain();
            EXPECT_EQ(mInserted.size(), 1u);
        }

        TEST_F(PostponedPhysicsTest, ExceptionDoesNotLoseUnprocessedObjects)
        {
            auto& room = cell("active");
            place(room); place(room); place(room);
            EXPECT_THROW(mWorld.forEachPostponedPhysics([](const Ptr& ptr) -> bool {
                ptr.getRefData().mPhysicsPostponed = false;
                throw std::runtime_error("insertion failure");
            }), std::runtime_error);
            drain();
            EXPECT_EQ(mInserted.size(), 2u);
        }

        TEST_F(PostponedPhysicsTest, CallbackMayClearRegistryWithoutInvalidatingIteration)
        {
            auto& room = cell("active");
            place(room); place(room);
            std::size_t calls = 0;
            mWorld.forEachPostponedPhysics([&](const Ptr&) {
                ++calls;
                mWorld.clear();
                return true;
            });
            EXPECT_EQ(calls, 1u);
            drain();
            EXPECT_EQ(mVisited, 0u);
        }

        TEST_F(PostponedPhysicsTest, ResetDoesNotCarryPendingWorkIntoNewWorld)
        {
            place(cell("old"));
            mActive.clear();
            mWorld.clear();
            drain();
            EXPECT_EQ(mVisited, 0u);
            place(cell("new"), false);
            drain();
            EXPECT_EQ(mVisited, 0u);
        }

        TEST_F(PostponedPhysicsTest, DrainCostDependsOnPendingObjectsNotWholeGrid)
        {
            auto& room = cell("dense");
            for (int i = 0; i < 10000; ++i)
                place(room, false);
            for (int i = 0; i < 3; ++i)
                place(room);
            std::size_t enumerated = 0;
            mWorld.forEachPostponedPhysics([&](const Ptr&) { ++enumerated; return false; });
            EXPECT_EQ(enumerated, Misc::useSparsePostponedPhysics() ? 3u : 0u);
            testing::Test::RecordProperty("pending_enumerated", static_cast<int>(enumerated));
            testing::Test::RecordProperty("sparse_enabled", Misc::useSparsePostponedPhysics() ? 1 : 0);
            const auto start = std::chrono::steady_clock::now();
            drain();
            const auto first = std::chrono::steady_clock::now();
            for (int i = 0; i < 1000; ++i)
                drain();
            std::cout << "POSTPONED_PHYSICS_BENCH sparse=" << Misc::useSparsePostponedPhysics()
                      << " registered=10003 pending_enumerated=" << enumerated << " inserted=" << mVisited
                      << " first_ms=" << std::chrono::duration<double, std::milli>(first-start).count()
                      << " empty_1000_ms="
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-first).count() << '\n';
            EXPECT_EQ(mVisited, 3u);
            EXPECT_EQ(mInserted.size(), 3u);
            testing::Test::RecordProperty("inserted", static_cast<int>(mInserted.size()));
        }
    }
}
