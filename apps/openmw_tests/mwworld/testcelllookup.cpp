#include "lookup_test_lua.hpp"

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/readerscache.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace MWWorld
{
    namespace
    {
        class CellLookupTest : public testing::Test
        {
        protected:
            MWBase::Environment mEnvironment;
            ESMStore mStore;
            ESM::ReadersCache mReaders;
            LookupTestLuaManager mLua;
            WorldModel mWorld{ mStore, mReaders };
            std::uint32_t mNextRef = 1;

            void SetUp() override
            {
                MWClass::Static::registerSelf();
                MWClass::Npc::registerSelf();
                mEnvironment.setWorldModel(mWorld);
                mEnvironment.setESMStore(mStore);
                mEnvironment.setLuaManager(mLua);
            }

            CellStore& cell(std::string_view name, bool load = true)
            {
                ESM::Cell record;
                record.blank();
                record.mName = name;
                record.mData.mFlags = ESM::Cell::Interior;
                record.updateId();
                mStore.insertStatic(record);
                return mWorld.getCell(record.mId, load);
            }

            template <class T = ESM::Static>
            Ptr add(CellStore& cell, std::string_view name, bool content = false)
            {
                const ESM::RefId id = ESM::RefId::stringRefId(name);
                const T* base = mStore.get<T>().search(id);
                if (base == nullptr)
                {
                    T record;
                    record.blank();
                    record.mId = id;
                    base = mStore.insertStatic(record);
                }
                ESM::CellRef ref;
                ref.blank();
                ref.mRefID = id;
                ref.mCount = 1;
                if (content)
                    ref.mRefNum = ESM::RefNum{ mNextRef++, 0 };
                LiveCellRef<T> live(ref, base);
                return Ptr(cell.insert(&live), &cell);
            }

            static ConstPtr linear(const CellStore& cell, const ESM::RefId& id)
            {
                ConstPtr result;
                cell.forEachConst([&](const ConstPtr& ptr) {
                    if (ptr.getCellRef().getRefId() != id)
                        return true;
                    result = ptr;
                    return false;
                });
                return result;
            }

            static void count(const Ptr& ptr, int value)
            {
                // Change the live accessibility input without requiring a full
                // World and its unrelated removeRefScript side effect.
                ESM::ObjectState state;
                ptr.getCellRef().writeState(state);
                state.mRef.mCount = value;
                ptr.getCellRef() = CellRef(state.mRef);
            }

            static void check(CellStore& cell, std::string_view name)
            {
                const auto id = ESM::RefId::stringRefId(name);
                const ConstPtr expected = linear(cell, id);
                for (int i = 0; i < 3; ++i)
                {
                    EXPECT_EQ(cell.searchConst(id), expected);
                    EXPECT_EQ(cell.search(id).mRef, expected.mRef);
                    EXPECT_EQ(cell.getPtr(id).mRef, expected.mRef);
                }
            }
        };

        TEST_F(CellLookupTest, unloadedAndPreloadedSearchesDoNotLoad)
        {
            CellStore& room = cell("room", false);
            const auto id = ESM::RefId::stringRefId("missing");
            EXPECT_TRUE(room.search(id).isEmpty());
            EXPECT_TRUE(room.searchConst(id).isEmpty());
            EXPECT_EQ(room.getState(), CellStore::State_Unloaded);
            EXPECT_FALSE(room.hasState());
            room.preload();
            EXPECT_TRUE(room.searchConst(id).isEmpty());
            EXPECT_TRUE(room.getPtr(id).isEmpty());
            EXPECT_EQ(room.getState(), CellStore::State_Preloaded);
            EXPECT_FALSE(room.hasState());
        }

        TEST_F(CellLookupTest, emptyLoadedSearchDoesNotSetState)
        {
            CellStore& room = cell("room");
            check(room, "missing");
            EXPECT_FALSE(room.hasState());
        }

        TEST_F(CellLookupTest, preservesDuplicateOrderAcrossRecordTypesAndCase)
        {
            CellStore& room = cell("room");
            const Ptr firstStatic = add(room, "duplicate");
            add(room, "duplicate");
            const Ptr npc = add<ESM::NPC>(room, "duplicate");
            check(room, "DUPLICATE");
            // NPC comes before Static in the engine's merged traversal, although
            // it was inserted later. Sorting the original merged view is unsafe.
            EXPECT_EQ(room.search(ESM::RefId::stringRefId("duplicate")), npc);
            count(npc, 0);
            check(room, "duplicate");
            EXPECT_EQ(room.search(ESM::RefId::stringRefId("duplicate")), firstStatic);
        }

        TEST_F(CellLookupTest, accessibilityChangesAreCheckedAfterIndexBuild)
        {
            CellStore& room = cell("room");
            const Ptr first = add(room, "duplicate");
            const Ptr second = add(room, "duplicate");
            check(room, "duplicate");
            count(first, 0);
            check(room, "duplicate");
            EXPECT_EQ(room.search(ESM::RefId::stringRefId("duplicate")), second);
            second.getRefData().setDeletedByContentFile(true);
            check(room, "duplicate");
            EXPECT_TRUE(room.search(ESM::RefId::stringRefId("duplicate")).isEmpty());
            count(first, 1);
            first.getRefData().disable();
            check(room, "duplicate");
            EXPECT_EQ(room.search(ESM::RefId::stringRefId("duplicate")), first);
        }

        TEST_F(CellLookupTest, contentReferenceWithZeroCountRemainsAccessible)
        {
            CellStore& room = cell("room");
            const Ptr ref = add(room, "content", true);
            check(room, "content");
            count(ref, 0);
            check(room, "content");
            EXPECT_EQ(room.search(ESM::RefId::stringRefId("content")), ref);
            ref.getRefData().setDeletedByContentFile(true);
            check(room, "content");
            EXPECT_TRUE(room.search(ESM::RefId::stringRefId("content")).isEmpty());
        }

        TEST_F(CellLookupTest, missDoesNotHideLaterInsertion)
        {
            CellStore& room = cell("room");
            add(room, "other");
            check(room, "new");
            const Ptr ref = add(room, "new");
            check(room, "new");
            EXPECT_EQ(room.search(ESM::RefId::stringRefId("new")), ref);
        }

        TEST_F(CellLookupTest, movingBetweenThreeCellsAndReturningInvalidatesBothViews)
        {
            CellStore& a = cell("a");
            CellStore& b = cell("b");
            CellStore& c = cell("c");
            Ptr ref = add(a, "traveller");
            for (CellStore* room : { &a, &b, &c })
                check(*room, "traveller");
            for (CellStore* destination : { &b, &c, &a })
            {
                ref = ref.getCell()->moveTo(ref, destination);
                for (CellStore* room : { &a, &b, &c })
                {
                    check(*room, "traveller");
                    EXPECT_EQ(room->search(ESM::RefId::stringRefId("traveller")).isEmpty(), room != destination);
                }
            }
        }

        TEST_F(CellLookupTest, movedDuplicateKeepsMergedTraversalPrecedence)
        {
            CellStore& a = cell("a");
            CellStore& b = cell("b");
            Ptr moved = add(a, "duplicate");
            const Ptr resident = add(b, "duplicate");
            check(a, "duplicate");
            check(b, "duplicate");
            moved = a.moveTo(moved, &b);
            check(b, "duplicate");
            EXPECT_EQ(b.search(ESM::RefId::stringRefId("duplicate")), resident);
            count(resident, 0);
            check(b, "duplicate");
            EXPECT_EQ(b.search(ESM::RefId::stringRefId("duplicate")), moved);
        }

        TEST_F(CellLookupTest, includeDeletedRebuildDoesNotMakeDeletedObjectsSearchable)
        {
            CellStore& room = cell("room");
            const Ptr ref = add(room, "deleted");
            ref.getRefData().setDeletedByContentFile(true);
            room.forEachConst([](const ConstPtr&) { return true; }, true);
            check(room, "deleted");
            EXPECT_TRUE(room.search(ESM::RefId::stringRefId("deleted")).isEmpty());
        }

        TEST_F(CellLookupTest, matchesLinearTraversalThroughRepeatedMutations)
        {
            CellStore& a = cell("a");
            CellStore& b = cell("b");
            std::vector<Ptr> refs;
            for (int i = 0; i < 128; ++i)
                refs.push_back(add(i % 2 ? a : b, "id_" + std::to_string(i % 23), i % 3 == 0));
            std::mt19937 random(0x5142);
            for (int round = 0; round < 60; ++round)
            {
                Ptr& ref = refs[random() % refs.size()];
                switch (round % 4)
                {
                    case 0:
                        count(ref, random() % 2);
                        break;
                    case 1:
                        ref.getRefData().setDeletedByContentFile(random() % 2 != 0);
                        break;
                    case 2:
                        ref = ref.getCell()->moveTo(ref, ref.getCell() == &a ? &b : &a);
                        break;
                    case 3:
                        refs.push_back(add(a, "id_" + std::to_string(random() % 23)));
                        break;
                }
                for (int id = 0; id < 24; ++id)
                {
                    check(a, "id_" + std::to_string(id));
                    check(b, "id_" + std::to_string(id));
                }
            }
        }

        TEST_F(CellLookupTest, saveBytesAreUnchangedAndRestoreInvalidatesAMiss)
        {
            CellStore& source = cell("source");
            const Ptr original = add(source, "saved");
            mWorld.registerPtr(original);
            const auto save = [&] {
                auto stream = std::make_unique<std::stringstream>();
                ESM::ESMWriter writer;
                writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
                writer.save(*stream);
                writer.startRecord(ESM::REC_CSTA);
                source.writeReferences(writer);
                writer.endRecord(ESM::REC_CSTA);
                writer.close();
                return stream;
            };
            auto before = save();
            check(source, "saved");
            auto after = save();
            EXPECT_EQ(before->str(), after->str());

            CellStore& restored = cell("restored");
            add(restored, "placeholder");
            check(restored, "saved");
            ESM::ESMReader reader;
            reader.open(std::move(after), "cell-lookup-save");
            ASSERT_EQ(reader.getRecName().toInt(), ESM::REC_CSTA);
            reader.getRecHeader();
            restored.readReferences(reader, nullptr);
            check(restored, "saved");
            const Ptr result = restored.search(ESM::RefId::stringRefId("saved"));
            ASSERT_FALSE(result.isEmpty());
            EXPECT_EQ(result.getCellRef().getRefNum(), original.getCellRef().getRefNum());
        }

        TEST_F(CellLookupTest, benchmarkRepeatedHitsAndMisses)
        {
            CellStore& room = cell("benchmark");
            constexpr int count = 4096;
            for (int i = 0; i < count; ++i)
                add(room, "bench_" + std::to_string(i));
            std::vector<ESM::RefId> queries;
            for (int i = 0; i < 20000; ++i)
                queries.push_back(ESM::RefId::stringRefId("bench_" + std::to_string((i * 7919u) % (2 * count))));
            room.count(); // Time lookup, not rebuilding the shared merged view.
            const auto measure = [&](auto&& search) {
                std::uintptr_t checksum = 0;
                const auto start = std::chrono::steady_clock::now();
                for (const auto& id : queries)
                    checksum ^= reinterpret_cast<std::uintptr_t>(search(id).mRef);
                const double ms
                    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                return std::pair(ms, checksum);
            };
            const auto before = measure([&](const ESM::RefId& id) { return linear(room, id); });
            const auto after = measure([&](const ESM::RefId& id) { return room.searchConst(id); });
            EXPECT_EQ(before.second, after.second);
            // Includes the first linear search and initial index construction.
            // Informational benchmark, never an assertion about in-game FPS.
            std::cout << "CELL_LOOKUP_BENCH refs=" << count << " queries=" << queries.size()
                      << " linear_ms=" << before.first << " candidate_ms=" << after.first << '\n';
        }
    }
}
