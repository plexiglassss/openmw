#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/creature.hpp"
#include "apps/openmw/mwclass/creaturelevlist.hpp"
#include "apps/openmw/mwclass/itemlevlist.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwmechanics/levelledlist.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/settings/values.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace MWMechanics
{
    namespace
    {
        class LevelledListTest : public testing::Test
        {
        protected:
            MWBase::Environment mEnvironment;
            MWWorld::ESMStore mStore;
            const bool mOldCreatures = Settings::game().mDelevelCreatures;
            const bool mOldItems = Settings::game().mDelevelItems;
            const Settings::CategorySettingVector mOldChanges = Settings::Manager::mChangedSettings;
            const std::array<ESM::RefId, 3> mItems{ ESM::RefId::stringRefId("item_a"),
                ESM::RefId::stringRefId("item_b"), ESM::RefId::stringRefId("item_c") };
            const std::array<ESM::RefId, 3> mCreatures{ ESM::RefId::stringRefId("creature_a"),
                ESM::RefId::stringRefId("creature_b"), ESM::RefId::stringRefId("creature_c") };

            void SetUp() override
            {
                MWClass::Miscellaneous::registerSelf();
                MWClass::Creature::registerSelf();
                MWClass::ItemLevList::registerSelf();
                MWClass::CreatureLevList::registerSelf();
                mEnvironment.setESMStore(mStore);
                Settings::game().mDelevelCreatures.set(false);
                Settings::game().mDelevelItems.set(false);
                for (const auto& id : mItems)
                {
                    ESM::Miscellaneous item;
                    item.blank();
                    item.mId = id;
                    mStore.insertStatic(item);
                }
                for (const auto& id : mCreatures)
                {
                    ESM::Creature creature;
                    creature.blank();
                    creature.mId = id;
                    mStore.insertStatic(creature);
                }
            }

            void TearDown() override
            {
                Settings::game().mDelevelCreatures.set(mOldCreatures);
                Settings::game().mDelevelItems.set(mOldItems);
                Settings::Manager::mChangedSettings = mOldChanges;
            }

            static void enable(bool creature, bool value)
            {
                if (creature)
                    Settings::game().mDelevelCreatures.set(value);
                else
                    Settings::game().mDelevelItems.set(value);
            }

            template <class T = ESM::LevelledListBase>
            static T makeList(std::string_view name, std::initializer_list<ESM::LevelledListBase::LevelItem> entries)
            {
                T list;
                list.blank();
                list.mId = ESM::RefId::stringRefId(name);
                list.mList = entries;
                return list;
            }

            // These pools are specified by each test, independently of the production eligibility code.
            // Use the native random distributions so this also checks every consumed RNG draw on MSVC and GCC.
            static void expectRolls(const ESM::LevelledListBase& list, bool creature, int level,
                const std::vector<ESM::RefId>& pool)
            {
                Misc::Rng::Generator actual(0x1a2b3c);
                Misc::Rng::Generator expected = actual;
                for (int draw = 0; draw < 64; ++draw)
                {
                    SCOPED_TRACE(draw);
                    ESM::RefId result;
                    if (Misc::Rng::roll0to99(expected) >= list.mChanceNone && !pool.empty())
                        result = pool[Misc::Rng::rollDice(pool.size(), expected)];
                    EXPECT_EQ(getLevelledItem(&list, creature, actual, level), result);
                    EXPECT_EQ(actual, expected);
                }
            }

            static void expectUnchanged(const ESM::LevelledListBase& actual, const ESM::LevelledListBase& expected)
            {
                EXPECT_EQ(actual.mId, expected.mId);
                EXPECT_EQ(actual.mFlags, expected.mFlags);
                EXPECT_EQ(actual.mRecordFlags, expected.mRecordFlags);
                EXPECT_EQ(actual.mChanceNone, expected.mChanceNone);
                ASSERT_EQ(actual.mList.size(), expected.mList.size());
                for (std::size_t i = 0; i < actual.mList.size(); ++i)
                {
                    EXPECT_EQ(actual.mList[i].mId, expected.mList[i].mId);
                    EXPECT_EQ(actual.mList[i].mLevel, expected.mList[i].mLevel);
                }
            }
        };

        TEST_F(LevelledListTest, DisabledMatchesNativeCandidatePoolsAndRandomState)
        {
            for (bool creature : { false, true })
            {
                SCOPED_TRACE(creature);
                const auto& ids = creature ? mCreatures : mItems;
                auto list = makeList("disabled", { { ids[0], 0 }, { ids[1], 5 }, { ids[0], 1 },
                                                    { ids[2], 5 }, { ids[1], 80 } });
                list.mChanceNone = 37;
                expectRolls(list, creature, 0, { ids[0] });
                expectRolls(list, creature, 1, { ids[0] });
                expectRolls(list, creature, 5, { ids[1], ids[2] });
                expectRolls(list, creature, 100, { ids[1] });

                // Item Each and the unused creature flag must not be mistaken for AllLevels.
                list.mFlags = creature ? 2 : ESM::ItemLevList::Each;
                expectRolls(list, creature, 5, { ids[1], ids[2] });
                list.mFlags = creature ? static_cast<int>(ESM::CreatureLevList::AllLevels)
                                      : static_cast<int>(ESM::ItemLevList::AllLevels);
                expectRolls(list, creature, 5, { ids[0], ids[1], ids[0], ids[2] });
            }
        }

        TEST_F(LevelledListTest, EnabledTreatsAllThresholdsAsOneWithoutChangingRecords)
        {
            for (bool creature : { false, true })
            {
                SCOPED_TRACE(creature);
                enable(creature, true);
                const auto& ids = creature ? mCreatures : mItems;
                auto list = makeList("enabled", { { ids[0], 0 }, { ids[1], 5 }, { ids[0], 1 },
                                                   { ids[2], 65535 }, { ids[1], 80 } });
                list.mRecordFlags = 0x40;
                list.mChanceNone = 19;
                for (int flags : { 0, 1, 2, 3 })
                {
                    SCOPED_TRACE(flags);
                    list.mFlags = flags;
                    const auto original = list;
                    expectRolls(list, creature, 1, { ids[0], ids[1], ids[0], ids[2], ids[1] });
                    expectRolls(list, creature, 100, { ids[0], ids[1], ids[0], ids[2], ids[1] });
                    expectUnchanged(list, original);
                }
                enable(creature, false);
            }
        }

        TEST_F(LevelledListTest, TogglesAreIndependentAndApplyToFutureSelections)
        {
            for (bool creature : { false, true })
            {
                SCOPED_TRACE(creature);
                const auto& ids = creature ? mCreatures : mItems;
                auto list = makeList("toggle", { { ids[0], 80 } });
                enable(!creature, true);
                expectRolls(list, creature, 1, {});
                enable(creature, true);
                expectRolls(list, creature, 1, { ids[0] });
                enable(creature, false);
                expectRolls(list, creature, 1, {});
                enable(!creature, false);
                EXPECT_EQ(list.mList.front().mLevel, 80);
            }
        }

        TEST_F(LevelledListTest, ChanceNoneEmptyListsAndExplicitLevelZeroKeepNativeRandomCalls)
        {
            for (bool creature : { false, true })
            {
                SCOPED_TRACE(creature);
                const auto& ids = creature ? mCreatures : mItems;
                for (bool enabled : { false, true })
                {
                    SCOPED_TRACE(enabled);
                    enable(creature, enabled);
                    auto list = makeList("none", { { ids[0], 0 }, { ids[1], 40 } });
                    list.mChanceNone = 100;
                    expectRolls(list, creature, 50, { ids[1] });
                    list.mChanceNone = 0;
                    expectRolls(list, creature, 0, enabled ? std::vector<ESM::RefId>{}
                                                         : std::vector<ESM::RefId>{ ids[0] });
                    list.mList.clear();
                    expectRolls(list, creature, 50, {});
                }
                enable(creature, false);
            }
        }

        TEST_F(LevelledListTest, MissingEntriesKeepTheirWeightAndReturnNothing)
        {
            const auto missing = ESM::RefId::stringRefId("missing_record");
            auto list = makeList("missing", { { mItems[0], 1 }, { missing, 1 }, { missing, 80 } });
            expectRolls(list, false, 1, { mItems[0], {} });
            enable(false, true);
            expectRolls(list, false, 1, { mItems[0], {}, {} });
        }

        TEST_F(LevelledListTest, NestedListsMatchRecordsDelevelledByTheReferenceMod)
        {
            auto child = makeList<ESM::ItemLevList>(
                "nested_item", { { mItems[0], 0 }, { mItems[1], 90 }, { mItems[0], 90 } });
            child.mChanceNone = 25;
            auto parent = makeList<ESM::CreatureLevList>(
                "nested_creature", { { child.mId, 20 }, { mCreatures[0], 80 }, { child.mId, 40 } });
            parent.mChanceNone = 15;
            const auto originalParent = parent;

            // The supplied MWSE mod sets every threshold to one. Use the actual native selector
            // over those records as the reference, including nested chance-none and duplicate slots.
            auto flattenedChild = child;
            auto flattenedParent = parent;
            for (auto& entry : flattenedChild.mList)
                entry.mLevel = 1;
            for (auto& entry : flattenedParent.mList)
                entry.mLevel = 1;
            mStore.insertStatic(flattenedChild);
            Misc::Rng::Generator expected(24681357);
            std::vector<std::pair<ESM::RefId, Misc::Rng::Generator>> reference;
            for (int i = 0; i < 64; ++i)
            {
                auto result = getLevelledItem(&flattenedParent, true, expected, 1);
                reference.emplace_back(result, expected);
            }

            mStore.overrideRecord(child);
            enable(false, true);
            enable(true, true);
            Misc::Rng::Generator actual(24681357);
            for (const auto& [result, state] : reference)
            {
                EXPECT_EQ(getLevelledItem(&parent, true, actual, 1), result);
                EXPECT_EQ(actual, state);
            }
            expectUnchanged(parent, originalParent);
            expectUnchanged(*mStore.get<ESM::ItemLevList>().find(child.mId), child);
        }

        TEST_F(LevelledListTest, NestedListTypeAndSelectionLevelArePreserved)
        {
            const auto itemChild = makeList<ESM::ItemLevList>("item_child", { { mItems[0], 5 } });
            const auto creatureChild = makeList<ESM::CreatureLevList>("creature_child", { { mCreatures[0], 5 } });
            mStore.insertStatic(itemChild);
            mStore.insertStatic(creatureChild);
            for (bool creature : { false, true })
            {
                SCOPED_TRACE(creature);
                const auto parent = makeList(
                    "mixed_parent", { { creature ? itemChild.mId : creatureChild.mId, 20 } });
                const auto result = creature ? mItems[0] : mCreatures[0];
                enable(creature, true);
                enable(!creature, false);
                Misc::Rng::Generator rng(12345);
                EXPECT_TRUE(getLevelledItem(&parent, creature, rng, 1).empty());
                EXPECT_EQ(getLevelledItem(&parent, creature, rng, 6), result);
                enable(!creature, true);
                EXPECT_EQ(getLevelledItem(&parent, creature, rng, 1), result);
                enable(creature, false);
                enable(!creature, false);
            }
        }

        TEST_F(LevelledListTest, RuntimeOverridesAndSavedRecordsUseCurrentPolicy)
        {
            auto list = makeList<ESM::ItemLevList>("dynamic_items", { { mItems[0], 20 } });
            mStore.insertStatic(list);
            enable(false, true);
            expectRolls(*mStore.get<ESM::ItemLevList>().find(list.mId), false, 1, { mItems[0] });

            // MWScript changes lists through overrideRecord, retaining the levels used to match removals.
            list.mList.push_back({ mItems[1], 100 });
            mStore.overrideRecord(list);
            expectRolls(*mStore.get<ESM::ItemLevList>().find(list.mId), false, 1, { mItems[0], mItems[1] });
            const auto saved = *mStore.get<ESM::ItemLevList>().find(list.mId);
            ESM::ESMWriter writer;
            auto stream = std::make_unique<std::stringstream>();
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
            writer.save(*stream);
            writer.startRecord(ESM::ItemLevList::sRecordId, saved.mRecordFlags);
            saved.save(writer);
            writer.endRecord(ESM::ItemLevList::sRecordId);

            const auto added = std::find_if(list.mList.begin(), list.mList.end(), [&](const auto& entry) {
                return entry.mId == mItems[1] && entry.mLevel == 100;
            });
            ASSERT_NE(added, list.mList.end());
            list.mList.erase(added);
            mStore.overrideRecord(list);
            expectRolls(*mStore.get<ESM::ItemLevList>().find(list.mId), false, 1, { mItems[0] });

            ESM::ESMReader reader;
            reader.open(std::move(stream), "saved-levelled-list");
            ASSERT_EQ(reader.getRecName().toInt(), ESM::ItemLevList::sRecordId);
            reader.getRecHeader();
            ASSERT_TRUE(mStore.readRecord(reader, ESM::ItemLevList::sRecordId));
            const auto* restored = mStore.get<ESM::ItemLevList>().find(list.mId);
            expectUnchanged(*restored, saved);
            expectRolls(*restored, false, 1, { mItems[0], mItems[1] });
            enable(false, false);
            expectRolls(*restored, false, 1, {});
            expectRolls(*restored, false, 100, { mItems[1] });
        }
    }
}
