#include "apps/openmw/mwworld/preloaddependencies.hpp"

#include <components/esm3/loadcrea.hpp>
#include <components/misc/resourcehelpers.hpp>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <string>

namespace MWWorld
{
    namespace
    {
        ESM::RefId id(std::string_view name)
        {
            return ESM::RefId::stringRefId(name);
        }

        class PreloadDependenciesTest : public testing::Test
        {
        protected:
            ESMStore mStore;
            PreloadDependencyOptions mOptions;

            template <class T>
            T record(std::string_view name)
            {
                T value;
                value.blank();
                value.mId = id(name);
                return value;
            }

            void SetUp() override
            {
                for (const auto name : { "a", "b", "c" })
                {
                    auto value = record<ESM::Weapon>(name);
                    value.mModel = std::string(name) + ".nif";
                    mStore.insertStatic(value);
                }
                mOptions.mLevel = 10;
            }

            template <class T = ESM::ItemLevList>
            void list(std::string_view name, std::initializer_list<ESM::LevelledListBase::LevelItem> entries,
                int flags = 0, unsigned char chanceNone = 0)
            {
                auto value = record<T>(name);
                value.mFlags = flags;
                value.mChanceNone = chanceNone;
                value.mList = entries;
                mStore.insertStatic(value);
            }

            std::vector<ESM::RefId> candidates(std::initializer_list<ESM::RefId> roots)
            {
                return collectPreloadCandidates(mStore, std::span(roots.begin(), roots.size()), mOptions);
            }

            std::vector<std::string> equipment(const ESM::InventoryList& inventory, bool female)
            {
                std::vector<VFS::Path::NormalizedView> models;
                appendEquipmentPreloadModels(mStore, inventory, female, mOptions, models);
                std::vector<std::string> result;
                for (const auto path : models)
                    result.emplace_back(path.value());
                return result;
            }
        };

        TEST_F(PreloadDependenciesTest, HighestEligibleLevelAndDuplicates)
        {
            list("items", { { id("a"), 1 }, { id("b"), 10 }, { id("b"), 10 }, { id("c"), 11 } });
            EXPECT_EQ(candidates({ id("items"), id("items"), id("missing") }), std::vector{ id("b") });
            mOptions.mLevel = 0;
            EXPECT_TRUE(candidates({ id("items") }).empty());
        }

        TEST_F(PreloadDependenciesTest, EachListKindUsesItsOwnAllLevelsFlag)
        {
            list("items", { { id("a"), 1 }, { id("b"), 10 } }, ESM::ItemLevList::AllLevels);
            list<ESM::CreatureLevList>(
                "creatures", { { id("a"), 1 }, { id("b"), 10 } }, ESM::CreatureLevList::AllLevels);
            EXPECT_EQ(candidates({ id("items") }), (std::vector{ id("a"), id("b") }));
            EXPECT_EQ(candidates({ id("creatures") }), (std::vector{ id("a"), id("b") }));
            list("each", { { id("a"), 1 }, { id("b"), 10 } }, ESM::ItemLevList::Each);
            EXPECT_EQ(candidates({ id("each") }), std::vector{ id("b") });
        }

        TEST_F(PreloadDependenciesTest, DelevelOptionsAreIndependentAndDoNotMutateRecords)
        {
            list("items", { { id("a"), 1 }, { id("b"), 20 } });
            list<ESM::CreatureLevList>("creatures", { { id("b"), 20 }, { id("c"), 1 } });
            mOptions.mDelevelItems = true;
            EXPECT_EQ(candidates({ id("items") }), (std::vector{ id("a"), id("b") }));
            EXPECT_EQ(candidates({ id("creatures") }), std::vector{ id("c") });
            mOptions.mDelevelItems = false;
            mOptions.mDelevelCreatures = true;
            EXPECT_EQ(candidates({ id("items") }), std::vector{ id("a") });
            EXPECT_EQ(candidates({ id("creatures") }), (std::vector{ id("b"), id("c") }));
            EXPECT_EQ(mStore.get<ESM::ItemLevList>().find(id("items"))->mList[1].mLevel, 20);
            EXPECT_EQ(mStore.get<ESM::CreatureLevList>().find(id("creatures"))->mList[0].mLevel, 20);
        }

        TEST_F(PreloadDependenciesTest, CrossKindCyclesAndMissingRecordsTerminate)
        {
            list("items", { { id("creatures"), 1 }, { id("a"), 1 }, { id("missing"), 1 } });
            list<ESM::CreatureLevList>("creatures", { { id("items"), 1 }, { id("b"), 1 } });
            EXPECT_EQ(candidates({ id("items") }), (std::vector{ id("a"), id("b") }));
        }

        TEST_F(PreloadDependenciesTest, ChanceNoneAndEmptyLists)
        {
            list("none", { { id("a"), 1 } }, 0, 100);
            list("sometimes", { { id("b"), 1 } }, 0, 99);
            list("empty", {});
            EXPECT_EQ(candidates({ id("none"), id("sometimes"), id("empty") }), std::vector{ id("b") });
        }

        TEST_F(PreloadDependenciesTest, CandidateRecordAndEntryBudgets)
        {
            list("items", { { id("a"), 1 }, { id("b"), 1 }, { id("c"), 1 } });
            mOptions.mMaxCandidates = 1;
            EXPECT_EQ(candidates({ id("items") }), std::vector{ id("a") });
            mOptions.mMaxCandidates = 128;
            mOptions.mMaxRecords = 1;
            EXPECT_TRUE(candidates({ id("items") }).empty());
            mOptions.mMaxRecords = 512;
            mOptions.mMaxEntries = 2;
            EXPECT_TRUE(candidates({ id("items") }).empty());
            mOptions.mMaxEntries = 3;
            EXPECT_EQ(candidates({ id("items") }), (std::vector{ id("a"), id("b"), id("c") }));
            mOptions.mMaxCandidates = 0;
            EXPECT_TRUE(candidates({ id("items") }).empty());
        }

        TEST_F(PreloadDependenciesTest, InventorySkipsZeroAndIncludesRestockingAndNestedItems)
        {
            list("items", { { id("c"), 1 } });
            ESM::InventoryList inventory;
            inventory.mList = { { 0, id("a") }, { -1, id("b") }, { 1, id("items") } };
            EXPECT_EQ(equipment(inventory, false), (std::vector<std::string>{ "b.nif", "c.nif" }));
            EXPECT_EQ(inventory.mList[1].mCount, -1);
            EXPECT_EQ(inventory.mList[2].mItem, id("items"));
        }

        TEST_F(PreloadDependenciesTest, EquipmentUsesGenderedPartsAndMissingFemaleFallback)
        {
            auto male = record<ESM::BodyPart>("male");
            male.mModel = "male.nif";
            mStore.insertStatic(male);
            auto female = record<ESM::BodyPart>("female");
            female.mModel = "female.nif";
            mStore.insertStatic(female);
            auto armor = record<ESM::Armor>("armor");
            armor.mParts.mParts = { { ESM::PRT_Cuirass, male.mId, female.mId } };
            mStore.insertStatic(armor);
            auto clothing = record<ESM::Clothing>("clothing");
            clothing.mParts.mParts = { { ESM::PRT_Groin, male.mId, id("missing") } };
            mStore.insertStatic(clothing);
            ESM::InventoryList inventory;
            inventory.mList = { { 1, armor.mId }, { 1, clothing.mId } };
            EXPECT_EQ(equipment(inventory, false), (std::vector<std::string>{ "male.nif", "male.nif" }));
            EXPECT_EQ(equipment(inventory, true), (std::vector<std::string>{ "female.nif", "male.nif" }));
        }

        TEST_F(PreloadDependenciesTest, LightAndEmptyModelFallback)
        {
            auto light = record<ESM::Light>("light");
            light.mModel = "light.nif";
            mStore.insertStatic(light);
            auto empty = record<ESM::Weapon>("empty");
            mStore.insertStatic(empty);
            ESM::InventoryList inventory;
            inventory.mList = { { 1, light.mId }, { 1, empty.mId }, { 1, id("missing") } };
            EXPECT_EQ(equipment(inventory, false), std::vector<std::string>{ "light.nif" });
        }

        TEST_F(PreloadDependenciesTest, AnimationDirectoriesMatchNestedModelPaths)
        {
            using Misc::ResourceHelpers::additionalAnimationPath;
            const auto directory = additionalAnimationPath(VFS::Path::NormalizedView("meshes/custom/xactor.nif"));
            ASSERT_TRUE(directory);
            EXPECT_EQ(directory->value(), "animations/custom/xactor/");
            EXPECT_FALSE(additionalAnimationPath(VFS::Path::NormalizedView("xactor.nif")));
            EXPECT_FALSE(additionalAnimationPath(VFS::Path::NormalizedView("meshes/xactor")));
        }

        TEST_F(PreloadDependenciesTest, SnapshotOwnsPathsAfterSourceMutationAndDestruction)
        {
            std::vector<VFS::Path::Normalized> snapshot;
            {
                VFS::Path::Normalized source("meshes/original.nif");
                const std::array<VFS::Path::NormalizedView, 2> views{ source, source };
                snapshot = snapshotPreloadModels(views);
                source = VFS::Path::Normalized(std::string(200, 'a') + ".nif");
            }
            ASSERT_EQ(snapshot.size(), 2);
            EXPECT_EQ(snapshot[0].value(), "meshes/original.nif");
            EXPECT_EQ(snapshot[1].value(), "meshes/original.nif");
        }

        TEST_F(PreloadDependenciesTest, NpcAnimationRootsIncludeGenderBeastAndCustomSources)
        {
            const ActorPreloadModels paths{ VFS::Path::NormalizedView("common.nif"),
                VFS::Path::NormalizedView("male.nif"), VFS::Path::NormalizedView("female.nif"),
                VFS::Path::NormalizedView("beast.nif"), VFS::Path::NormalizedView("swim.nif") };
            auto npc = record<ESM::NPC>("npc");
            auto race = record<ESM::Race>("race");
            std::vector<VFS::Path::NormalizedView> models;
            npc.mFlags = ESM::NPC::Female;
            appendActorAnimationPreloadModels(npc, race, paths, models);
            ASSERT_EQ(models.size(), 2);
            EXPECT_EQ(models[0], paths.mCommon);
            EXPECT_EQ(models[1], paths.mFemale);
            models.clear();
            npc.mFlags = 0;
            appendActorAnimationPreloadModels(npc, race, paths, models);
            EXPECT_EQ(models[1], paths.mMale);
            models.clear();
            npc.mRace = id("custom_argonian");
            npc.mModel = "custom.nif";
            race.mData.mFlags = ESM::Race::Beast;
            appendActorAnimationPreloadModels(npc, race, paths, models);
            ASSERT_EQ(models.size(), 4);
            EXPECT_EQ(models[1], paths.mBeast);
            EXPECT_EQ(models[2].value(), "custom.nif");
            EXPECT_EQ(models[3], paths.mArgonianSwim);
        }

        TEST_F(PreloadDependenciesTest, CreatureAnimationRootsIncludeBipedalCommonSource)
        {
            const ActorPreloadModels paths{ VFS::Path::NormalizedView("common.nif"), {}, {}, {}, {} };
            auto creature = record<ESM::Creature>("creature");
            creature.mModel = "creature.nif";
            std::vector<VFS::Path::NormalizedView> models;
            appendActorAnimationPreloadModels(creature, paths, models);
            ASSERT_EQ(models.size(), 1);
            EXPECT_EQ(models[0].value(), "creature.nif");
            models.clear();
            creature.mFlags = ESM::Creature::Bipedal;
            appendActorAnimationPreloadModels(creature, paths, models);
            ASSERT_EQ(models.size(), 2);
            EXPECT_EQ(models[0], paths.mCommon);
            EXPECT_EQ(models[1].value(), "creature.nif");
        }
    }
}
