#include "components/misc/strings/conversion.hpp"
#include "components/settings/parser.hpp"
#include "components/settings/values.hpp"
#include "components/testing/util.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>

#ifndef OPENMW_PROJECT_SOURCE_DIR
#define OPENMW_PROJECT_SOURCE_DIR "."
#endif

namespace Settings
{
    namespace
    {
        using namespace testing;

        struct SettingsValuesTest : Test
        {
            const std::filesystem::path mSettingsDefaultPath = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR }
                / "files" / Misc::StringUtils::stringToU8String("settings-default.cfg");

            SettingsValuesTest()
            {
                Manager::mDefaultSettings.clear();
                Manager::mUserSettings.clear();
                Manager::mChangedSettings.clear();
                SettingsFileParser parser;
                parser.loadSettingsFile(mSettingsDefaultPath, Manager::mDefaultSettings);
            }
        };

        TEST_F(SettingsValuesTest, shouldLoadFromSettingsManager)
        {
            Index index;
            Values values(index);
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 60);
        }

        TEST_F(SettingsValuesTest, shouldFillIndexOnLoad)
        {
            Index index;
            Values values(index);
            EXPECT_EQ(index.get<float>("Camera", "field of view").get(), 60);
        }

        TEST_F(SettingsValuesTest, constructorShouldThrowExceptionOnMissingSetting)
        {
            Manager::mDefaultSettings.erase({ "Camera", "field of view" });
            Index index;
            EXPECT_THROW([&] { Values values(index); }(), std::runtime_error);
        }

        TEST_F(SettingsValuesTest, constructorShouldSanitize)
        {
            Manager::mUserSettings[std::make_pair("Camera", "field of view")] = "-1";
            Index index;
            Values values(index);
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 1);
        }

        TEST_F(SettingsValuesTest, constructorWithDefaultShouldDoLookup)
        {
            Manager::mUserSettings[std::make_pair("category", "value")] = "13";
            Index index;
            SettingValue<int> value{ index, "category", "value", 42 };
            EXPECT_EQ(value.get(), 13);
            value.reset();
            EXPECT_EQ(value.get(), 42);
        }

        TEST_F(SettingsValuesTest, constructorWithDefaultShouldSanitize)
        {
            Manager::mUserSettings[std::make_pair("category", "value")] = "2";
            Index index;
            SettingValue<int> value{ index, "category", "value", -1, Settings::makeClampSanitizerInt(0, 1) };
            EXPECT_EQ(value.get(), 1);
            value.reset();
            EXPECT_EQ(value.get(), 0);
        }

        TEST_F(SettingsValuesTest, constructorWithDefaultShouldFallbackToDefault)
        {
            Index index;
            const SettingValue<int> value{ index, "category", "value", 42 };
            EXPECT_EQ(value.get(), 42);
        }

        TEST_F(SettingsValuesTest, moveConstructorShouldSetDefaults)
        {
            Index index;
            Values defaultValues(index);
            Manager::mUserSettings.emplace(std::make_pair("Camera", "field of view"), "61");
            Values values(std::move(defaultValues));
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 61);
            values.mCamera.mFieldOfView.reset();
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 60);
        }

        TEST_F(SettingsValuesTest, moveConstructorShouldSanitize)
        {
            Index index;
            Values defaultValues(index);
            Manager::mUserSettings[std::make_pair("Camera", "field of view")] = "-1";
            Values values(std::move(defaultValues));
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 1);
        }

        TEST_F(SettingsValuesTest, moveConstructorShouldThrowOnMissingSetting)
        {
            Index index;
            SettingValue<int> defaultValue{ index, "category", "value", 42 };
            EXPECT_THROW([&] { SettingValue<int> value(std::move(defaultValue)); }(), std::runtime_error);
        }

        TEST_F(SettingsValuesTest, findShouldThrowExceptionOnTypeMismatch)
        {
            Index index;
            Values values(index);
            EXPECT_THROW(index.find<int>("Camera", "field of view"), std::invalid_argument);
        }

        TEST_F(SettingsValuesTest, findShouldReturnNullptrForAbsentSetting)
        {
            Index index;
            Values values(index);
            EXPECT_EQ(index.find<int>("foo", "bar"), nullptr);
        }

        TEST_F(SettingsValuesTest, getShouldThrowExceptionForAbsentSetting)
        {
            Index index;
            Values values(index);
            EXPECT_THROW(index.get<int>("foo", "bar").get(), std::invalid_argument);
        }

        TEST_F(SettingsValuesTest, setShouldChangeManagerUserSettings)
        {
            Index index;
            Values values(index);
            values.mCamera.mFieldOfView.set(42);
            EXPECT_EQ(Manager::mUserSettings.at({ "Camera", "field of view" }), "42");
            EXPECT_THAT(Manager::mChangedSettings, ElementsAre(std::make_pair("Camera", "field of view")));
        }

        TEST_F(SettingsValuesTest, setShouldNotChangeManagerChangedSettingsForNoChange)
        {
            Index index;
            Values values(index);
            values.mCamera.mFieldOfView.set(values.mCamera.mFieldOfView.get());
            EXPECT_THAT(Manager::mChangedSettings, ElementsAre());
        }

        TEST_F(SettingsValuesTest, journalTypographyDefaultsPreserveExistingAppearance)
        {
            Index index;
            Values values(index);
            EXPECT_EQ(values.mGUI.mJournalFontSize.get(), 0);
            EXPECT_EQ(values.mGUI.mJournalTextAlignment.get(), 0);
            EXPECT_EQ(values.mGUI.mJournalPageNumberFontSize.get(), 0);
            EXPECT_EQ(values.mGUI.mJournalButtonScale.get(), 100);
        }

        TEST_F(SettingsValuesTest, dialogueColoursKeepTheThemeForNormalDialogueByDefault)
        {
            Index index;
            Values values(index);
            EXPECT_TRUE(values.mGUI.mDialogueColouring.get());
            EXPECT_TRUE(values.mGUI.mDialogueUseSystemColour.get());
            EXPECT_TRUE(values.mGUI.mDialogueClassifyNarration.get());
            EXPECT_TRUE(values.mGUI.mJournalColourNarration.get());
            EXPECT_EQ(values.mGUI.mColorDialogueTopic.get(), MyGUI::Colour(0.55f, 0.85f, 0.85f));
            const auto& journalNotice = values.mGUI.mColorDialogueJournal.get();
            EXPECT_FLOAT_EQ(journalNotice.red, journalNotice.green);
            EXPECT_FLOAT_EQ(journalNotice.green, journalNotice.blue);
            EXPECT_LT(values.mGUI.mColorJournalNarration.get().red, values.mGUI.mColorDialogueNarration.get().red);
        }

        TEST_F(SettingsValuesTest, dialogueColoursRoundTripAllRolesWithoutChangingOtherInterfaceSettings)
        {
            const auto path = TestingOpenMW::outputFilePath("dialogue-colour-settings.cfg");
            const std::array<std::pair<std::string_view, MyGUI::Colour>, 7> colours{ {
                { "color dialogue normal", MyGUI::Colour(37.f / 255.f, 121.f / 255.f, 231.f / 255.f) },
                { "color dialogue topic", MyGUI::Colour(0.17f, 0.63f, 0.89f) },
                { "color dialogue inventory", MyGUI::Colour(0.13f, 0.37f, 0.61f) },
                { "color dialogue stat", MyGUI::Colour(0.71f, 0.19f, 0.83f) },
                { "color dialogue narration", MyGUI::Colour(0.21f, 0.51f, 0.81f) },
                { "color dialogue journal", MyGUI::Colour(0.31f, 0.41f, 0.51f) },
                { "color journal narration", MyGUI::Colour(0.11f, 0.22f, 0.33f) },
            } };
            MyGUI::Colour topicColour;
            {
                Index index;
                Values values(index);
                topicColour = values.mGUI.mColorTopicSpecific.get();
                values.mGUI.mDialogueColouring.set(false);
                values.mGUI.mDialogueUseSystemColour.set(false);
                values.mGUI.mDialogueClassifyNarration.set(false);
                values.mGUI.mJournalColourNarration.set(false);
                for (const auto& [name, colour] : colours)
                    index.get<MyGUI::Colour>("GUI", name).set(colour);
                SettingsFileParser parser;
                parser.saveSettingsFile(path, Manager::mUserSettings);
            }

            Manager::mUserSettings.clear();
            SettingsFileParser parser;
            parser.loadSettingsFile(path, Manager::mUserSettings);
            Index index;
            Values values(index);
            EXPECT_FALSE(values.mGUI.mDialogueColouring.get());
            EXPECT_FALSE(values.mGUI.mDialogueUseSystemColour.get());
            EXPECT_FALSE(values.mGUI.mDialogueClassifyNarration.get());
            EXPECT_FALSE(values.mGUI.mJournalColourNarration.get());
            for (const auto& [name, expected] : colours)
            {
                SCOPED_TRACE(name);
                const auto& actual = index.get<MyGUI::Colour>("GUI", name).get();
                EXPECT_NEAR(actual.red, expected.red, 0.000001f);
                EXPECT_NEAR(actual.green, expected.green, 0.000001f);
                EXPECT_NEAR(actual.blue, expected.blue, 0.000001f);
                EXPECT_FLOAT_EQ(actual.alpha, expected.alpha);
            }
            EXPECT_EQ(values.mGUI.mColorTopicSpecific.get(), topicColour);
            EXPECT_EQ(values.mGUI.mJournalFontSize.get(), 0);
            EXPECT_EQ(values.mGUI.mJournalTextAlignment.get(), 0);
            EXPECT_EQ(values.mGUI.mFontSize.get(), 16);
            EXPECT_EQ(values.mGUI.mScalingFactor.get(), 1.f);
        }

        TEST_F(SettingsValuesTest, resettingOneDialogueColourKeepsOtherColoursAndThemeChoice)
        {
            Index index;
            // Mirror StaticValues::initDefaults()/init(): the move captures the
            // default palette before reading the saved user settings.
            Values defaultValues(index);
            const auto defaultStatColour = defaultValues.mGUI.mColorDialogueStat.get();
            const auto defaultTopicColour = defaultValues.mGUI.mColorDialogueTopic.get();
            const MyGUI::Colour customNormal(0.123456f, 0.345678f, 0.567891f);
            const MyGUI::Colour customInventory(0.15f, 0.35f, 0.75f);
            const MyGUI::Colour customTopic(0.17f, 0.63f, 0.89f);
            Manager::mUserSettings[{ "GUI", "color dialogue normal" }] = "0.123456 0.345678 0.567891 1";
            Manager::mUserSettings[{ "GUI", "color dialogue inventory" }] = "0.15 0.35 0.75 1";
            Manager::mUserSettings[{ "GUI", "color dialogue topic" }] = "0.17 0.63 0.89 1";
            Manager::mUserSettings[{ "GUI", "color dialogue stat" }] = "0.9 0.8 0.7 1";
            Manager::mUserSettings[{ "GUI", "dialogue use system colour" }] = "false";
            Values values(std::move(defaultValues));
            EXPECT_EQ(values.mGUI.mColorDialogueNormal.get(), customNormal);
            EXPECT_EQ(values.mGUI.mColorDialogueInventory.get(), customInventory);
            EXPECT_EQ(values.mGUI.mColorDialogueStat.get(), MyGUI::Colour(0.9f, 0.8f, 0.7f));
            EXPECT_EQ(values.mGUI.mColorDialogueTopic.get(), customTopic);
            EXPECT_FALSE(values.mGUI.mDialogueUseSystemColour.get());
            values.mGUI.mColorDialogueStat.set(MyGUI::Colour::Black);
            index.get<MyGUI::Colour>("GUI", "color dialogue stat").reset();
            EXPECT_EQ(values.mGUI.mColorDialogueStat.get(), defaultStatColour);
            EXPECT_EQ(values.mGUI.mColorDialogueTopic.get(), customTopic);
            EXPECT_EQ(values.mGUI.mColorDialogueNormal.get(), customNormal);
            EXPECT_EQ(values.mGUI.mColorDialogueInventory.get(), customInventory);
            EXPECT_FALSE(values.mGUI.mDialogueUseSystemColour.get());

            index.get<MyGUI::Colour>("GUI", "color dialogue topic").reset();
            EXPECT_EQ(values.mGUI.mColorDialogueTopic.get(), defaultTopicColour);
            EXPECT_EQ(values.mGUI.mColorDialogueNormal.get(), customNormal);
            EXPECT_EQ(values.mGUI.mColorDialogueInventory.get(), customInventory);
            EXPECT_FALSE(values.mGUI.mDialogueUseSystemColour.get());
        }

        TEST_F(SettingsValuesTest, journalTypographyRoundTripsWithoutChangingGlobalFontOrScale)
        {
            const auto path = TestingOpenMW::outputFilePath("journal-typography-settings.cfg");
            int globalFontSize = 0;
            float globalScale = 0;
            {
                Index index;
                Values values(index);
                globalFontSize = values.mGUI.mFontSize.get();
                globalScale = values.mGUI.mScalingFactor.get();
                values.mGUI.mJournalFontSize.set(12);
                values.mGUI.mJournalTextAlignment.set(3);
                values.mGUI.mJournalPageNumberFontSize.set(14);
                values.mGUI.mJournalButtonScale.set(75);
                SettingsFileParser parser;
                parser.saveSettingsFile(path, Manager::mUserSettings);
            }

            Manager::mUserSettings.clear();
            SettingsFileParser parser;
            parser.loadSettingsFile(path, Manager::mUserSettings);
            Index index;
            Values values(index);
            EXPECT_EQ(values.mGUI.mJournalFontSize.get(), 12);
            EXPECT_EQ(values.mGUI.mJournalTextAlignment.get(), 3);
            EXPECT_EQ(values.mGUI.mJournalPageNumberFontSize.get(), 14);
            EXPECT_EQ(values.mGUI.mJournalButtonScale.get(), 75);
            EXPECT_EQ(values.mGUI.mFontSize.get(), globalFontSize);
            EXPECT_EQ(values.mGUI.mScalingFactor.get(), globalScale);
        }

        TEST_F(SettingsValuesTest, journalTypographyAcceptsEveryOfferedChoice)
        {
            Index index;
            Values values(index);
            for (int size = 10; size <= 24; ++size)
            {
                values.mGUI.mJournalFontSize.set(size);
                EXPECT_EQ(values.mGUI.mJournalFontSize.get(), size);
                values.mGUI.mJournalPageNumberFontSize.set(size);
                EXPECT_EQ(values.mGUI.mJournalPageNumberFontSize.get(), size);
            }
            values.mGUI.mJournalFontSize.set(0);
            EXPECT_EQ(values.mGUI.mJournalFontSize.get(), 0);
            values.mGUI.mJournalPageNumberFontSize.set(0);
            EXPECT_EQ(values.mGUI.mJournalPageNumberFontSize.get(), 0);
            for (int percent = 50; percent <= 100; ++percent)
            {
                values.mGUI.mJournalButtonScale.set(percent);
                EXPECT_EQ(values.mGUI.mJournalButtonScale.get(), percent);
            }
            for (int alignment = 0; alignment < 4; ++alignment)
            {
                values.mGUI.mJournalTextAlignment.set(alignment);
                EXPECT_EQ(values.mGUI.mJournalTextAlignment.get(), alignment);
            }
        }

        TEST_F(SettingsValuesTest, journalTypographyRejectsUnsupportedValues)
        {
            Index index;
            Values values(index);
            for (int size : { -1, 1, 9, 25, 1000 })
            {
                EXPECT_THROW(values.mGUI.mJournalFontSize.set(size), std::runtime_error);
                EXPECT_THROW(values.mGUI.mJournalPageNumberFontSize.set(size), std::runtime_error);
            }
            for (int alignment : { -1, 4, 1000 })
                EXPECT_THROW(values.mGUI.mJournalTextAlignment.set(alignment), std::runtime_error);
            EXPECT_EQ(values.mGUI.mJournalFontSize.get(), 0);
            EXPECT_EQ(values.mGUI.mJournalTextAlignment.get(), 0);
            EXPECT_EQ(values.mGUI.mJournalPageNumberFontSize.get(), 0);
        }

        TEST_F(SettingsValuesTest, journalTypographyClampsButtonScaleWithoutChangingTextSettings)
        {
            Index index;
            Values values(index);
            const int globalFontSize = values.mGUI.mFontSize.get();
            const float globalScale = values.mGUI.mScalingFactor.get();
            values.mGUI.mJournalFontSize.set(12);
            values.mGUI.mJournalTextAlignment.set(3);
            values.mGUI.mJournalPageNumberFontSize.set(14);

            for (int percent : { -1000, 0, 49 })
            {
                values.mGUI.mJournalButtonScale.set(percent);
                EXPECT_EQ(values.mGUI.mJournalButtonScale.get(), 50);
            }
            for (int percent : { 101, 1000 })
            {
                values.mGUI.mJournalButtonScale.set(percent);
                EXPECT_EQ(values.mGUI.mJournalButtonScale.get(), 100);
            }

            EXPECT_EQ(values.mGUI.mJournalFontSize.get(), 12);
            EXPECT_EQ(values.mGUI.mJournalTextAlignment.get(), 3);
            EXPECT_EQ(values.mGUI.mJournalPageNumberFontSize.get(), 14);
            EXPECT_EQ(values.mGUI.mFontSize.get(), globalFontSize);
            EXPECT_EQ(values.mGUI.mScalingFactor.get(), globalScale);
        }
    }
}
