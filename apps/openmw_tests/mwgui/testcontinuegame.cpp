#include <apps/openmw/mwstate/charactermanager.hpp>
#include <apps/openmw/mwstate/saveselection.hpp>

#include <components/esm3/esmwriter.hpp>
#include <components/testing/util.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>

namespace
{
    class ContinueGameTest : public ::testing::Test
    {
    protected:
        const std::filesystem::path mSaves = TestingOpenMW::currentTestDirPath() / "saves";
        const std::filesystem::file_time_type mBaseTime
            = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);

        void SetUp() override { std::filesystem::create_directories(mSaves); }

        std::filesystem::path writeSave(const std::string& character, const std::string& filename, int seconds,
            const std::string& game = "Morrowind.esm", int level = 1)
        {
            const std::filesystem::path path = mSaves / character / filename;
            std::filesystem::create_directories(path.parent_path());

            // Use real save headers so the same discovery, game filtering and slot timestamps run as in the engine.
            ESM::SavedGame profile{};
            profile.mContentFiles = { game };
            profile.mPlayerName = character;
            profile.mPlayerLevel = level;
            profile.mPlayerClassName = "Adventurer";
            profile.mPlayerCellName = "Seyda Neen";
            profile.mDescription = filename;
            profile.mInGameTime.mDay = 1;
            profile.mInGameTime.mYear = 427;

            std::ofstream stream(path, std::ios::binary);
            ESM::ESMWriter writer;
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
            writer.save(stream);
            writer.startRecord(ESM::REC_SAVE);
            profile.save(writer);
            writer.endRecord(ESM::REC_SAVE);
            writer.close();
            stream.close();
            std::filesystem::last_write_time(path, mBaseTime + std::chrono::seconds(seconds));
            return path;
        }
    };

    TEST_F(ContinueGameTest, EmptyAndUnsavedCharactersHaveNoContinueTarget)
    {
        MWState::CharacterManager characters(mSaves, { "Morrowind.esm" });
        auto selected = MWState::selectLatestSave(characters.begin(), characters.end());
        EXPECT_EQ(selected.mCharacter, nullptr);
        EXPECT_EQ(selected.mSlot, nullptr);

        characters.createCharacter("Unsaved Character");
        selected = MWState::selectLatestSave(characters.begin(), characters.end());
        EXPECT_EQ(selected.mCharacter, nullptr);
        EXPECT_EQ(selected.mSlot, nullptr);
    }

    TEST_F(ContinueGameTest, LatestManualQuickAndAutoSavesAreAllEligible)
    {
        writeSave("First Character", "Manual.omwsave", 10);
        writeSave("Second Character", "Quicksave.omwsave", 20);
        writeSave("Third Character", "Autosave.omwsave", 30);

        for (const auto& [character, filename, seconds] : {
                 std::tuple{ "Third Character", "Autosave.omwsave", 30 },
                 std::tuple{ "Second Character", "Quicksave.omwsave", 40 },
                 std::tuple{ "First Character", "Manual.omwsave", 50 },
             })
        {
            const auto expected = writeSave(character, filename, seconds);
            MWState::CharacterManager characters(mSaves, { "Morrowind.esm" });
            const auto selected = MWState::selectLatestSave(characters.begin(), characters.end());
            ASSERT_NE(selected.mSlot, nullptr);
            ASSERT_NE(selected.mCharacter, nullptr);
            EXPECT_EQ(selected.mSlot->mPath, expected);
            EXPECT_EQ(selected.mCharacter->getPath(), expected.parent_path());
        }
    }

    TEST_F(ContinueGameTest, SelectionUsesSaveTimeRatherThanPlayerLevelOrCurrentCharacter)
    {
        writeSave("Veteran", "Manual.omwsave", 10, "Morrowind.esm", 70);
        const auto expected = writeSave("Beginner", "Quicksave.omwsave", 20, "Morrowind.esm", 1);
        MWState::CharacterManager characters(mSaves, { "Morrowind.esm" });
        const MWState::Character* veteran = nullptr;
        for (const auto& character : characters)
            if (character.getPath().filename() == "Veteran")
                veteran = &character;
        ASSERT_NE(veteran, nullptr);
        characters.setCurrentCharacter(veteran);

        const auto selected = MWState::selectLatestSave(characters.begin(), characters.end());
        ASSERT_NE(selected.mSlot, nullptr);
        EXPECT_EQ(selected.mSlot->mPath, expected);
        EXPECT_EQ(characters.getCurrentCharacter(), veteran);
    }

    TEST_F(ContinueGameTest, OverwritingAnOlderCharacterSaveDoesNotDependOnCharacterListOrder)
    {
        const auto expected = writeSave("Older Character", "Manual.omwsave", 10);
        writeSave("Newer Character", "Quicksave.omwsave", 20);
        MWState::CharacterManager characters(mSaves, { "Morrowind.esm" });
        ASSERT_EQ(characters.begin()->getPath().filename(), "Newer Character");

        const auto older = std::next(characters.begin());
        characters.setCurrentCharacter(&*older);
        MWState::Character* current = characters.getCurrentCharacter();
        const ESM::SavedGame profile = current->begin()->mProfile;
        current->updateSlot(&*current->begin(), profile);

        ASSERT_EQ(characters.begin()->getPath().filename(), "Newer Character");
        const auto selected = MWState::selectLatestSave(characters.begin(), characters.end());
        ASSERT_NE(selected.mSlot, nullptr);
        EXPECT_EQ(selected.mSlot->mPath, expected);
    }

    TEST_F(ContinueGameTest, DeletingTheLatestSaveReconsidersOtherCharactersAndThenHidesContinue)
    {
        const auto fallback = writeSave("First Character", "Manual.omwsave", 10);
        writeSave("Second Character", "Autosave.omwsave", 20);
        MWState::CharacterManager characters(mSaves, { "Morrowind.esm" });

        auto selected = MWState::selectLatestSave(characters.begin(), characters.end());
        ASSERT_NE(selected.mSlot, nullptr);
        characters.deleteSlot(selected.mSlot, selected.mCharacter);

        selected = MWState::selectLatestSave(characters.begin(), characters.end());
        ASSERT_NE(selected.mSlot, nullptr);
        EXPECT_EQ(selected.mSlot->mPath, fallback);
        characters.deleteSlot(selected.mSlot, selected.mCharacter);

        selected = MWState::selectLatestSave(characters.begin(), characters.end());
        EXPECT_EQ(selected.mCharacter, nullptr);
        EXPECT_EQ(selected.mSlot, nullptr);
    }

    TEST_F(ContinueGameTest, EqualTimestampsUseAStablePathOrderAcrossCharactersAndSlots)
    {
        writeSave("B Character", "A.omwsave", 20);
        writeSave("A Character", "Z.omwsave", 20);
        const auto expected = writeSave("A Character", "A.omwsave", 20);
        MWState::CharacterManager characters(mSaves, { "Morrowind.esm" });

        const auto selected = MWState::selectLatestSave(characters.begin(), characters.end());
        ASSERT_NE(selected.mSlot, nullptr);
        EXPECT_EQ(selected.mSlot->mPath, expected);
    }

    TEST_F(ContinueGameTest, DiscoveryExcludesInvalidHeadersAndSavesForOtherGames)
    {
        const auto expected = writeSave("Morrowind Character", "Manual.omwsave", 10);
        writeSave("Different Game", "Autosave.omwsave", 50, "AnotherGame.omwgame");
        const auto invalid = mSaves / "Morrowind Character" / "Broken.omwsave";
        std::ofstream(invalid, std::ios::binary) << "This is not an OpenMW save";
        std::filesystem::last_write_time(invalid, mBaseTime + std::chrono::seconds(100));

        MWState::CharacterManager characters(mSaves, { "Morrowind.esm" });
        const auto selected = MWState::selectLatestSave(characters.begin(), characters.end());
        ASSERT_NE(selected.mSlot, nullptr);
        EXPECT_EQ(selected.mSlot->mPath, expected);
    }
}
