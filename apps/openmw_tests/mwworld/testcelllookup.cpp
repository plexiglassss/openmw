#include "lookup_test_lua.hpp"

#include "apps/openmw/mwlua/cellbindings.hpp"
#include "apps/openmw/mwlua/context.hpp"
#include "apps/openmw/mwworld/queryrevision.hpp"

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <components/esm3/cellstate.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/readerscache.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
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
                mStore.insert(record);
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

        TEST_F(CellLookupTest, luaQueryRejectsUnknownTypeAfterLoading)
        {
            CellStore& room = cell("unknown Lua type", false);
            ASSERT_EQ(room.getState(), CellStore::State_Unloaded);
            LuaUtil::LuaState bindingState{ nullptr, nullptr };
            MWLua::Context context{ MWLua::Context::Global };
            context.mLua = &bindingState;
            // Use the real userdata binding and its empty type-package map.
            // No World service is installed: treating an unknown table as the
            // zero-valued Player type would enter the wrong service branch.
            MWLua::initCellBindingsForGlobalScripts(context);
            sol::state_view lua = bindingState.unsafeState();
            lua["testedCell"] = MWLua::GCell{ &room };
            const auto result = lua.safe_script(R"(
                assert(not testedCell.hasBeenLoaded)
                local formattedAfterLoad = false
                local unknownType = setmetatable({}, {
                    __tostring = function()
                        formattedAfterLoad = testedCell.hasBeenLoaded
                        return 'unregistered fixture type'
                    end,
                })
                local ok, message = pcall(function() return testedCell:getAll(unknownType) end)
                assert(not ok, 'An unknown type table must be rejected')
                assert(type(message) == 'string')
                assert(string.find(message,
                    'Incorrect type argument in cell:getAll: unregistered fixture type', 1, true), message)
                assert(formattedAfterLoad, 'The cell must load before formatting the type error')
                assert(testedCell.hasBeenLoaded, 'Type rejection must preserve the preceding cell load')
            )",
                sol::script_pass_on_error);
            if (!result.valid())
            {
                const auto error = result.get<sol::error>();
                FAIL() << error.what();
            }
            EXPECT_EQ(room.getState(), CellStore::State_Loaded);
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

        TEST_F(CellLookupTest, typedQuerySharesSnapshotsWithoutInvalidatingRegisteredPointers)
        {
            CellStore& room = cell("query room");
            add(room, "first");
            add(room, "second");
            const auto first = MWLua::queryCellObjects(room, ESM::REC_STAT);
            const auto registry = mWorld.getPtrRegistryRevision();
            const auto second = MWLua::queryCellObjects(room, ESM::REC_STAT);
            ASSERT_EQ(first->size(), 2u);
            EXPECT_EQ(first, second);
            EXPECT_EQ(registry, mWorld.getPtrRegistryRevision());
            const auto uncached = MWLua::queryCellObjects(room, ESM::REC_STAT, false);
            EXPECT_NE(first, uncached);
            EXPECT_EQ(*first, *uncached);
            EXPECT_EQ(registry, mWorld.getPtrRegistryRevision());
        }

        TEST_F(CellLookupTest, typedQueryObservesDeletionAndUndeletionInSameUpdate)
        {
            CellStore& room = cell("query room");
            const Ptr ref = add(room, "temporary");
            const auto before = MWLua::queryCellObjects(room, ESM::REC_STAT);
            ASSERT_EQ(before->size(), 1u);
            count(ref, 0); // Saved/live state replacement, with no frame advance.
            const auto deleted = MWLua::queryCellObjects(room, ESM::REC_STAT);
            EXPECT_TRUE(deleted->empty());
            EXPECT_EQ(before->size(), 1u); // Previously returned snapshots are immutable.
            ref.getCellRef().setCount(1); // Direct zero-to-nonzero setter path.
            const auto restored = MWLua::queryCellObjects(room, ESM::REC_STAT);
            EXPECT_EQ(*restored, *before);
            EXPECT_TRUE(deleted->empty());
            EXPECT_EQ(restored, MWLua::queryCellObjects(room, ESM::REC_STAT));
        }

        TEST_F(CellLookupTest, typedQueryDoesNotTrustAnOldEmptyMergedViewAfterUndelete)
        {
            CellStore& room = cell("query room");
            const Ptr ref = add(room, "temporary");
            count(ref, 0);
            const auto empty = MWLua::queryCellObjects(room, ESM::REC_STAT);
            ASSERT_TRUE(empty->empty());
            ref.getCellRef().setCount(1);
            const auto result = MWLua::queryCellObjects(room, ESM::REC_STAT);
            ASSERT_EQ(result->size(), 1u);
            EXPECT_EQ(result->front(), ref.getCellRef().getRefNum());
        }

        TEST_F(CellLookupTest, typedQueryTracksContentDeletionAndRefDataReplacement)
        {
            CellStore& room = cell("query room");
            const Ptr ref = add(room, "content", true);
            ASSERT_EQ(MWLua::queryCellObjects(room, ESM::REC_STAT)->size(), 1u);
            ref.getRefData().setDeletedByContentFile(true);
            EXPECT_TRUE(MWLua::queryCellObjects(room, ESM::REC_STAT)->empty());
            RefData restored;
            ref.getRefData() = restored;
            ASSERT_EQ(MWLua::queryCellObjects(room, ESM::REC_STAT)->size(), 1u);
            RefData deleted;
            deleted.setDeletedByContentFile(true);
            ref.getRefData() = std::move(deleted);
            EXPECT_TRUE(MWLua::queryCellObjects(room, ESM::REC_STAT)->empty());
            ref.getRefData().setDeletedByContentFile(false);
            EXPECT_EQ(MWLua::queryCellObjects(room, ESM::REC_STAT)->size(), 1u);
        }

        TEST_F(CellLookupTest, typedQueryPreservesMoveMembershipAndOrder)
        {
            CellStore& a = cell("query a");
            CellStore& b = cell("query b");
            CellStore& c = cell("query c");
            const Ptr first = add(a, "first");
            const Ptr stay = add(a, "stay");
            const Ptr local = add(b, "local");
            const auto original = MWLua::queryCellObjects(a, ESM::REC_STAT);
            MWLua::queryCellObjects(b, ESM::REC_STAT);
            Ptr moved = a.moveTo(first, &b);
            const auto inA = MWLua::queryCellObjects(a, ESM::REC_STAT);
            const auto inB = MWLua::queryCellObjects(b, ESM::REC_STAT);
            EXPECT_EQ(*inA, (std::vector<ESM::RefNum>{ stay.getCellRef().getRefNum() }));
            EXPECT_EQ(
                *inB, (std::vector<ESM::RefNum>{ local.getCellRef().getRefNum(), first.getCellRef().getRefNum() }));
            moved = b.moveTo(moved, &c);
            EXPECT_EQ(MWLua::queryCellObjects(b, ESM::REC_STAT)->size(), 1u);
            EXPECT_EQ(MWLua::queryCellObjects(c, ESM::REC_STAT)->size(), 1u);
            c.moveTo(moved, &a);
            EXPECT_EQ(*MWLua::queryCellObjects(a, ESM::REC_STAT), *original);
            EXPECT_EQ(original->size(), 2u);
            EXPECT_EQ(inA->size(), 1u);
        }

        TEST_F(CellLookupTest, typedQueryRefreshesAfterInsertionAndKeepsOldEmptySnapshot)
        {
            CellStore& room = cell("query room");
            const auto empty = MWLua::queryCellObjects(room, ESM::REC_STAT);
            ASSERT_TRUE(empty->empty());
            add(room, "new reference");
            EXPECT_EQ(MWLua::queryCellObjects(room, ESM::REC_STAT)->size(), 1u);
            EXPECT_TRUE(empty->empty());
        }

        TEST_F(CellLookupTest, typedQueryFiltersPlayerAndHiddenMarkers)
        {
            CellStore& room = cell("query room");
            add<ESM::NPC>(room, "player");
            const Ptr npc = add<ESM::NPC>(room, "ordinary npc");
            add(room, "prisonmarker");
            const Ptr visible = add(room, "ordinary static");
            const auto npcs = MWLua::queryCellObjects(room, ESM::REC_NPC_);
            const auto statics = MWLua::queryCellObjects(room, ESM::REC_STAT);
            EXPECT_EQ(*npcs, (std::vector<ESM::RefNum>{ npc.getCellRef().getRefNum() }));
            EXPECT_EQ(*statics, (std::vector<ESM::RefNum>{ visible.getCellRef().getRefNum() }));
        }

        TEST_F(CellLookupTest, typedQueryRefreshesIdentityAndSpecialTypeAfterReplacement)
        {
            CellStore& room = cell("query room");
            const Ptr ref = add(room, "ordinary static");
            const auto first = MWLua::queryCellObjects(room, ESM::REC_STAT);
            const ESM::RefNum replacement{ 9000, 3 };
            ref.getCellRef().setRefNum(replacement);
            const auto changed = MWLua::queryCellObjects(room, ESM::REC_STAT);
            ASSERT_EQ(changed->size(), 1u);
            EXPECT_EQ(changed->front(), replacement);
            EXPECT_NE(first->front(), replacement);
            ESM::ObjectState state;
            ref.getCellRef().writeState(state);
            state.mRef.mRefID = ESM::RefId::stringRefId("prisonmarker");
            ref.getCellRef() = CellRef(state.mRef);
            EXPECT_TRUE(MWLua::queryCellObjects(room, ESM::REC_STAT)->empty());
            EXPECT_EQ(changed->size(), 1u);
        }

        TEST_F(CellLookupTest, typedQueryKeepsMembershipWhileDynamicPropertiesChange)
        {
            CellStore& room = cell("query room");
            const Ptr ref = add(room, "ordinary static");
            const auto first = MWLua::queryCellObjects(room, ESM::REC_STAT);
            ref.getCellRef().setCount(-3); // Nonzero negative counts remain live.
            ref.getRefData().disable();
            ESM::Position position = ref.getRefData().getPosition();
            position.pos[0] += 500;
            ref.getRefData().setPosition(position);
            const auto second = MWLua::queryCellObjects(room, ESM::REC_STAT);
            EXPECT_EQ(first, second);
            EXPECT_FALSE(mWorld.getPtr(first->front()).getRefData().isEnabled());
            EXPECT_EQ(mWorld.getPtr(first->front()).getRefData().getPosition().pos[0], position.pos[0]);
        }

        TEST_F(CellLookupTest, registryAvoidsOnlyExactlyIdempotentRegistration)
        {
            CellStore& a = cell("registry a");
            CellStore& b = cell("registry b");
            const Ptr ref = add(a, "registered");
            mWorld.registerPtr(ref);
            const auto initial = mWorld.getPtrRegistryRevision();
            mWorld.registerPtr(ref);
            EXPECT_EQ(mWorld.getPtrRegistryRevision(), initial);
            Ptr changed(ref.mRef, &b);
            mWorld.registerPtr(changed);
            EXPECT_GT(mWorld.getPtrRegistryRevision(), initial);
            EXPECT_EQ(mWorld.getPtr(ref.getCellRef().getRefNum()).mCell, &b);
            const auto afterCell = mWorld.getPtrRegistryRevision();
            std::max_align_t storage;
            changed.mCell = nullptr;
            changed.mContainerStore = reinterpret_cast<ContainerStore*>(&storage);
            mWorld.registerPtr(changed);
            EXPECT_GT(mWorld.getPtrRegistryRevision(), afterCell);
            EXPECT_EQ(mWorld.getPtr(ref.getCellRef().getRefNum()).mContainerStore, changed.mContainerStore);
            mWorld.registerPtr(ref);
        }

        TEST_F(CellLookupTest, typedQueryRefreshesAfterRegistryRemoval)
        {
            CellStore& room = cell("query room");
            const Ptr ref = add(room, "ordinary static");
            const auto first = MWLua::queryCellObjects(room, ESM::REC_STAT);
            mWorld.deregisterLiveCellRef(*ref.mRef);
            EXPECT_TRUE(mWorld.getPtr(first->front()).isEmpty());
            const auto refreshed = MWLua::queryCellObjects(room, ESM::REC_STAT);
            EXPECT_NE(first, refreshed);
            EXPECT_EQ(*first, *refreshed);
            EXPECT_EQ(mWorld.getPtr(refreshed->front()).mRef, ref.mRef);
        }

        TEST_F(CellLookupTest, typedQueryRetainsEmptyCellShortcutAndRejectsUnsupportedTypes)
        {
            CellStore& room = cell("query empty", false);
            // The conservative shortcut requires the completed content index,
            // just as it does in a running game. An unindexed cell is unknown.
            EXPECT_TRUE(room.mayHaveReferenceOfType(ESM::REC_NPC_));
            ESM::Class cls;
            cls.blank();
            cls.mId = ESM::RefId::stringRefId("class");
            mStore.insertStatic(cls);
            ESM::Race race;
            race.blank();
            race.mId = ESM::RefId::stringRefId("race");
            mStore.insertStatic(race);
            mStore.setUp();
            mStore.validateRecords(mReaders);
            ASSERT_FALSE(room.mayHaveReferenceOfType(ESM::REC_NPC_));
            ASSERT_FALSE(room.mayHaveReferenceOfType(ESM::REC_DOOR));
            const auto result = MWLua::queryCellObjects(room, ESM::REC_NPC_);
            EXPECT_TRUE(result->empty());
            EXPECT_EQ(room.getState(), CellStore::State_Unloaded);
            EXPECT_TRUE(MWLua::queryCellObjects(room, ESM::REC_DOOR)->empty());
            EXPECT_EQ(room.getState(), CellStore::State_Unloaded);
            EXPECT_FALSE(MWLua::queryCellObjects(room, ESM::REC_GLOB));
            EXPECT_EQ(room.getState(), CellStore::State_Loaded);
        }

        TEST_F(CellLookupTest, typedQueryBenchmark)
        {
            CellStore& room = cell("query benchmark");
            for (int i = 0; i < 1024; ++i)
                add(room, "reference " + std::to_string(i));
            const auto expected = MWLua::queryCellObjects(room, ESM::REC_STAT, false);
            std::vector<double> cached, uncached;
            std::uint64_t checksum = 0;
            const auto measure = [&](bool cache) {
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < 1000; ++i)
                {
                    const auto result = MWLua::queryCellObjects(room, ESM::REC_STAT, cache);
                    checksum += result->size();
                    std::atomic_signal_fence(std::memory_order_seq_cst);
                }
                return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count()
                    / 1000;
            };
            for (int round = 0; round < 7; ++round)
            {
                if (round % 2 == 0)
                {
                    uncached.push_back(measure(false));
                    cached.push_back(measure(true));
                }
                else
                {
                    cached.push_back(measure(true));
                    uncached.push_back(measure(false));
                }
            }
            EXPECT_EQ(*MWLua::queryCellObjects(room, ESM::REC_STAT), *expected);
            EXPECT_EQ(checksum, 14u * 1000u * 1024u);
            std::sort(cached.begin(), cached.end());
            std::sort(uncached.begin(), uncached.end());
            RecordProperty("references", 1024);
            RecordProperty("rounds", 7);
            RecordProperty("queries_per_round", 1000);
            RecordProperty("uncached_median_ns_per_query", std::to_string(uncached[3]));
            RecordProperty("cached_median_ns_per_query", std::to_string(cached[3]));
            RecordProperty("scope", "native typed cell query; unchanged membership; no Lua dispatch or game FPS");
            std::cout << "CELL_QUERY_BENCHMARK references=1024 rounds=7 queries_per_round=1000 uncached_ns="
                      << uncached[3] << " cached_ns=" << cached[3] << '\n';
        }

        class WorldCellLookupTest : public CellLookupTest
        {
        protected:
            Ptr find(std::string_view name) { return mWorld.getPtrByRefId(ESM::RefId::stringRefId(name)); }

            CellStore& exterior(int x, int y)
            {
                // Supply an existing cell, just as cell() does for interiors.
                // Generating a new exterior intentionally notifies Lua, which
                // is outside these lookup tests and rejected by the strict stub.
                ESM::Cell record;
                record.blank();
                record.mData.mFlags = ESM::Cell::HasWater;
                record.mData.mX = x;
                record.mData.mY = y;
                record.updateId();
                mStore.insert(record);
                return mWorld.getExterior(ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId));
            }
        };

        TEST_F(WorldCellLookupTest, missDoesNotHideInsertionOrNewCell)
        {
            CellStore& a = cell("a");
            add(a, "other");
            EXPECT_TRUE(find("new").isEmpty());
            const Ptr inserted = add(a, "new");
            EXPECT_EQ(find("NEW"), inserted);
            EXPECT_TRUE(find("later").isEmpty());
            const Ptr later = add(cell("later cell"), "later");
            EXPECT_EQ(find("later"), later);
        }

        TEST_F(WorldCellLookupTest, followsMovementAndReturnAfterCachedHit)
        {
            CellStore& a = cell("a");
            CellStore& b = cell("b");
            CellStore& c = exterior(3, 4);
            Ptr ref = add(a, "traveller");
            EXPECT_EQ(find("traveller"), ref);
            for (CellStore* destination : { &b, &c, &a })
            {
                ref = ref.getCell()->moveTo(ref, destination);
                EXPECT_EQ(find("traveller"), ref);
                EXPECT_EQ(find("traveller").getCell(), destination);
            }
        }

        TEST_F(WorldCellLookupTest, preservesExteriorAndCaseInsensitiveInteriorOrder)
        {
            add(cell("beta"), "inside");
            const Ptr firstInterior = add(cell("Alpha"), "inside");
            EXPECT_EQ(find("inside"), firstInterior);
            add(cell("another interior"), "chargen_plank");
            add(exterior(-22, 16), "chargen_plank");
            const Ptr plank = add(exterior(-2, -9), "chargen_plank");
            EXPECT_EQ(find("chargen_plank"), plank);
        }

        TEST_F(WorldCellLookupTest, preservesCachePrecedenceAndChecksAccessibilityLive)
        {
            CellStore& a = cell("a");
            CellStore& b = cell("b");
            const Ptr cached = add(b, "duplicate");
            EXPECT_EQ(find("duplicate"), cached);
            const Ptr earlier = add(a, "duplicate");
            EXPECT_EQ(find("duplicate"), cached);
            cached.getRefData().setDeletedByContentFile(true);
            EXPECT_EQ(find("duplicate"), earlier);
            count(earlier, 0);
            EXPECT_TRUE(find("duplicate").isEmpty());
            count(earlier, 1);
            earlier.getRefData().disable();
            EXPECT_EQ(find("duplicate"), earlier);
        }

        TEST_F(WorldCellLookupTest, inaccessibleReferenceIsNotOmittedFromCandidateIndex)
        {
            CellStore& a = cell("a");
            const Ptr ref = add(a, "restored");
            a.count(); // Keep the pre-existing merged-view accessibility behavior identical.
            count(ref, 0);
            EXPECT_TRUE(find("unrelated").isEmpty());
            count(ref, 1);
            EXPECT_EQ(find("restored"), ref);
            const Ptr content = add(a, "content", true);
            count(content, 0);
            EXPECT_EQ(find("content"), content);
        }

        TEST_F(WorldCellLookupTest, unloadedAndPreloadedCellsRemainSearchableAfterInsertion)
        {
            CellStore& a = cell("a", false);
            CellStore& b = cell("b", false);
            b.preload();
            EXPECT_TRUE(find("missing").isEmpty());

            // With indexed candidates enabled, a negative lookup must leave an
            // unrelated unloaded cell untouched. The opt-out and original
            // traversal deliberately retain the prior eager-preload behavior.
            const char* worldIndex = std::getenv("OPENMW_WORLD_CELL_INDEX");
            const char* candidateIndex = std::getenv("OPENMW_WORLD_CELL_CANDIDATES");
            const bool lazyCandidates = (worldIndex == nullptr || std::string_view(worldIndex) != "0")
                && (candidateIndex == nullptr || std::string_view(candidateIndex) != "0");
            EXPECT_EQ(a.getState(), lazyCandidates ? CellStore::State_Unloaded : CellStore::State_Preloaded);
            EXPECT_EQ(b.getState(), CellStore::State_Preloaded);
            a.load();
            b.load();
            const Ptr first = add(a, "first");
            const Ptr second = add(b, "second");
            EXPECT_EQ(find("first"), first);
            EXPECT_EQ(find("second"), second);
        }

        TEST_F(WorldCellLookupTest, metadataOnlySavedCellsAreNotPreloadedOnMiss)
        {
            // A real modded save can contain tens of thousands of CSTA entries
            // without OBJE/MVRF references. loadState() sets hasState(), but
            // none of these cells contain runtime additions.
            std::vector<CellStore*> saved;
            for (unsigned i = 0; i != 64; ++i)
            {
                CellStore& room = cell("metadata " + std::to_string(i), false);
                ESM::CellState state{};
                state.mId = room.getCell()->getId();
                state.mIsInterior = true;
                room.loadState(state);
                EXPECT_TRUE(room.hasState());
                EXPECT_FALSE(room.hasUnloadedLiveReferences());
                saved.push_back(&room);
            }

            EXPECT_TRUE(find("definitely absent").isEmpty());

            const char* worldIndex = std::getenv("OPENMW_WORLD_CELL_INDEX");
            const char* candidateIndex = std::getenv("OPENMW_WORLD_CELL_CANDIDATES");
            const bool lazyCandidates = (worldIndex == nullptr || std::string_view(worldIndex) != "0")
                && (candidateIndex == nullptr || std::string_view(candidateIndex) != "0");
            for (CellStore* room : saved)
                EXPECT_EQ(room->getState(), lazyCandidates ? CellStore::State_Unloaded : CellStore::State_Preloaded);

            // An unloaded cell holding a runtime-created reference is not
            // metadata-only and must remain an eligible search candidate.
            CellStore& runtime = cell("runtime additions", false);
            EXPECT_FALSE(runtime.hasUnloadedLiveReferences());
            add(runtime, "created reference");
            EXPECT_TRUE(runtime.hasUnloadedLiveReferences());
        }

        TEST_F(WorldCellLookupTest, draftCellDoesNotJoinNormalSearch)
        {
            add(mWorld.getDraftCell(), "draft only");
            EXPECT_TRUE(find("draft only").isEmpty());
        }

        TEST_F(WorldCellLookupTest, clearDiscardsCandidatesAndDirtyCells)
        {
            add(cell("a"), "old");
            EXPECT_FALSE(find("old").isEmpty());
            add(mWorld.getInterior("a"), "dirty");
            mWorld.clear();
            EXPECT_TRUE(find("old").isEmpty());
            EXPECT_TRUE(find("dirty").isEmpty());
            const Ptr replacement = add(mWorld.getInterior("a"), "old");
            EXPECT_EQ(find("old"), replacement);
        }

        TEST_F(WorldCellLookupTest, saveRestoreRefreshesCandidatesWithoutChangingSaveBytes)
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
            EXPECT_EQ(find("saved"), original);
            EXPECT_EQ(save()->str(), before->str());
            mWorld.clear();
            CellStore& restored = mWorld.getInterior("source");
            EXPECT_TRUE(find("saved").isEmpty());
            ESM::ESMReader reader;
            reader.open(std::move(before), "world-cell-lookup-save");
            ASSERT_EQ(reader.getRecName().toInt(), ESM::REC_CSTA);
            reader.getRecHeader();
            restored.readReferences(reader, nullptr);
            const Ptr result = find("saved");
            ASSERT_FALSE(result.isEmpty());
            EXPECT_EQ(result.getCell(), &restored);
        }

        TEST_F(WorldCellLookupTest, refreshRemovesMovedCandidateAndPreservesOtherDuplicates)
        {
            CellStore& a = cell("a");
            CellStore& b = cell("b");
            CellStore& c = cell("c");
            Ptr first = add(a, "shared");
            const Ptr second = add(b, "shared");
            EXPECT_TRUE(find("build index").isEmpty());
            first = a.moveTo(first, &c);
            EXPECT_EQ(find("shared"), second);
            count(second, 0);
            EXPECT_EQ(find("shared"), first);
            const Ptr added = add(a, "new id");
            EXPECT_EQ(find("new id"), added);
        }

        TEST_F(WorldCellLookupTest, benchmarkDistinctIdsAndSharedCandidates)
        {
            constexpr int cells = 2000;
            constexpr int uniquePerCell = 64;
            for (int i = 0; i < cells; ++i)
            {
                CellStore& room = cell("distinct room " + std::to_string(i));
                add(room, "shared scenery");
                for (int j = 0; j < uniquePerCell; ++j)
                    add(room, "unique " + std::to_string(i) + ":" + std::to_string(j));
            }
            const auto start = std::chrono::steady_clock::now();
            EXPECT_TRUE(find("initial absent").isEmpty());
            const auto built = std::chrono::steady_clock::now();
            for (int i = 0; i < 100; ++i)
                EXPECT_TRUE(find("absent " + std::to_string(i)).isEmpty());
            const auto finish = std::chrono::steady_clock::now();
            std::cout << "WORLD_DISTINCT_BENCH cells=" << cells << " unique_per_cell=" << uniquePerCell
                      << " first_ms=" << std::chrono::duration<double, std::milli>(built - start).count()
                      << " repeated_ms=" << std::chrono::duration<double, std::milli>(finish - built).count() << '\n';
        }

        TEST_F(WorldCellLookupTest, benchmarkKnownCellMisses)
        {
            constexpr int cells = 1000;
            constexpr int refsPerCell = 32;
            for (int i = 0; i < cells; ++i)
            {
                CellStore& room = cell("bench room " + std::to_string(i));
                for (int j = 0; j < refsPerCell; ++j)
                    add(room, "bench item " + std::to_string(j));
            }
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 100; ++i)
                EXPECT_TRUE(find("absent " + std::to_string(i)).isEmpty());
            const double ms
                = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "WORLD_CELL_LOOKUP_BENCH cells=" << cells << " refs_per_cell=" << refsPerCell
                      << " queries=100 including_index_build_ms=" << ms << '\n';
        }
    }
}
