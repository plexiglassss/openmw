#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/door.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"
#include "lookup_test_lua.hpp"
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/sceneutil/preloadplancache.hpp>
#include <gtest/gtest.h>

namespace MWWorld
{
    namespace
    {
        class DoorPreloadPlanTest : public testing::Test
        {
        protected:
            MWBase::Environment environment;
            ESMStore store;
            ESM::ReadersCache readers;
            LookupTestLuaManager lua;
            WorldModel world{ store, readers };
            using Cache = SceneUtil::PreloadPlanCache<const CellStore*, std::pair<std::uint64_t, std::uint64_t>,
                std::vector<ConstPtr>>;
            Cache plans{ 4 };
            void SetUp() override
            {
                MWClass::Door::registerSelf();
                environment.setWorldModel(world);
                environment.setESMStore(store);
                environment.setLuaManager(lua);
            }
            CellStore& cell(std::string_view name)
            {
                ESM::Cell record;
                record.blank();
                record.mName = name;
                record.mData.mFlags = ESM::Cell::Interior;
                record.updateId();
                store.insert(record);
                return world.getCell(record.mId);
            }
            Ptr door(CellStore& cell)
            {
                ESM::Door base;
                base.blank();
                base.mId = ESM::RefId::stringRefId("door fixture");
                ESM::CellRef ref;
                ref.blank();
                ref.mRefID = base.mId;
                ref.mCount = 1;
                LiveCellRef<ESM::Door> temporary(ref, store.insertStatic(base));
                return Ptr(cell.insert(&temporary), &cell);
            }
            const std::vector<ConstPtr>& snapshot(const CellStore& cell)
            {
                return plans.get(&cell, { cell.getReferenceListRevision(), getReferenceQueryRevision() }, [&] {
                    std::vector<ConstPtr> result;
                    for (const auto& ref : cell.getReadOnlyDoors().mList)
                        result.emplace_back(&ref, &cell);
                    return result;
                });
            }
        };

        TEST_F(DoorPreloadPlanTest, InsertionInvalidatesEvenWhenPreviousSnapshotWasEmpty)
        {
            auto& a = cell("a");
            EXPECT_TRUE(snapshot(a).empty());
            const auto ptr = door(a);
            ASSERT_EQ(snapshot(a).size(), 1u);
            EXPECT_EQ(snapshot(a).front().mRef, ptr.mRef);
            const auto builds = plans.builds();
            snapshot(a);
            EXPECT_EQ(plans.builds(), builds);
        }

        TEST_F(DoorPreloadPlanTest, StateReplacementAndPositionRemainVisible)
        {
            auto& a = cell("a");
            const auto ptr = door(a);
            ASSERT_EQ(snapshot(a).size(), 1u);
            EXPECT_FALSE(snapshot(a).front().getCellRef().getTeleport());
            ESM::CellRef replacement;
            replacement.blank();
            replacement.mRefID = ptr.getCellRef().getRefId();
            replacement.mCount = 1;
            replacement.mTeleport = true;
            replacement.mDestCell = "changed destination";
            ptr.getCellRef() = CellRef(replacement);
            EXPECT_TRUE(snapshot(a).front().getCellRef().getTeleport());
            EXPECT_EQ(snapshot(a).front().getCellRef().getDestCell(), ESM::RefId::stringRefId("changed destination"));
            auto position = ptr.getRefData().getPosition();
            position.pos[0] = 1234.f;
            ptr.getRefData().setPosition(position);
            EXPECT_EQ(snapshot(a).front().getRefData().getPosition().pos[0], 1234.f);
        }

        TEST_F(DoorPreloadPlanTest, MovingReferencesAdvancesBothMembershipRevisions)
        {
            auto& a = cell("a");
            auto& b = cell("b");
            const auto ptr = door(a);
            world.registerPtr(ptr);
            const auto oldA = a.getReferenceListRevision(), oldB = b.getReferenceListRevision();
            const auto moved = a.moveTo(ptr, &b);
            EXPECT_GT(a.getReferenceListRevision(), oldA);
            EXPECT_GT(b.getReferenceListRevision(), oldB);
            EXPECT_EQ(moved.mRef, ptr.mRef);
            plans.erase(&a);
            plans.clear();
            EXPECT_EQ(plans.size(), 0u);
        }
    }
}
