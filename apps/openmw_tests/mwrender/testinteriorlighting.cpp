#include "apps/openmw/mwrender/interiorlighting.hpp"

#include <components/sceneutil/stateupdater.hpp>

#include <gtest/gtest.h>

#include <osg/StateSet>
#include <osg/Uniform>
#include <osg/ref_ptr>

#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace MWRender
{
    namespace
    {
        constexpr std::string_view ValidDocument = R"(
format: 1
name: horror-classic
mode: classic
required-content:
  - HorrorFixture.esp
interiors:
  - "Horror, Old Cave"
  - "Horror, Oblivion"
  - "Horror, Pandemonium"
  - "Horror, Oblivion, Shrine of Adramelech"
  - "Horror, Oblivion, Adramelech's Tomb"
)";

        InteriorLightingProfile parse(std::string_view text)
        {
            std::istringstream stream{ std::string(text) };
            return parseInteriorLightingProfile(stream);
        }

        std::string changedDocument(std::string_view original, std::string_view replacement)
        {
            std::string text(ValidDocument);
            const std::size_t offset = text.find(original);
            if (offset == std::string::npos)
                throw std::logic_error("Invalid test document replacement");
            text.replace(offset, original.size(), replacement);
            return text;
        }

        InteriorLightingProfile fixture()
        {
            return parse(ValidDocument);
        }

        TEST(InteriorLightingProfileTest, parsesExplicitClassicAllowlistWithoutChangingNames)
        {
            const InteriorLightingProfile profile = fixture();
            EXPECT_EQ(profile.mName, "horror-classic");
            EXPECT_EQ(profile.mRequiredContent, (std::vector<std::string>{ "HorrorFixture.esp" }));
            EXPECT_EQ(profile.mInteriors,
                (std::vector<std::string>{ "Horror, Old Cave", "Horror, Oblivion", "Horror, Pandemonium",
                    "Horror, Oblivion, Shrine of Adramelech", "Horror, Oblivion, Adramelech's Tomb" }));
        }

        TEST(InteriorLightingProfileTest, rejectsMissingUnknownAndRepeatedFields)
        {
            for (const std::string_view field : { "format: 1\n", "name: horror-classic\n", "mode: classic\n",
                     "required-content:\n  - HorrorFixture.esp\n" })
            {
                SCOPED_TRACE(field);
                EXPECT_THROW(parse(changedDocument(field, "")), std::exception);
            }
            EXPECT_THROW(parse(changedDocument("interiors:\n", "unknown-interiors:\n")), std::exception);
            EXPECT_THROW(parse(std::string(ValidDocument) + "unknown-setting: true\n"), std::exception);
            EXPECT_THROW(parse(std::string(ValidDocument) + "name: another-profile\n"), std::exception);
            EXPECT_THROW(parse(std::string(ValidDocument) + "format: 1\n"), std::exception);
            EXPECT_THROW(parse(std::string(ValidDocument) + "mode: classic\n"), std::exception);
        }

        TEST(InteriorLightingProfileTest, rejectsMalformedRootsAndUnsupportedFormatsOrModes)
        {
            for (const std::string_view text : { "", "null", "classic", "[]", "- format: 1\n", "format: [\n" })
            {
                SCOPED_TRACE(text);
                EXPECT_THROW(parse(text), std::exception);
            }
            for (const std::string_view version : { "format: 0", "format: 2", "format: unsupported", "format: []" })
            {
                SCOPED_TRACE(version);
                EXPECT_THROW(parse(changedDocument("format: 1", version)), std::exception);
            }
            for (const std::string_view mode :
                { "mode: modern", "mode: Classic", "mode: null", "mode: []", "mode: {}" })
            {
                SCOPED_TRACE(mode);
                EXPECT_THROW(parse(changedDocument("mode: classic", mode)), std::exception);
            }
        }

        TEST(InteriorLightingProfileTest, rejectsEmptyNamesAndWronglyTypedLists)
        {
            for (const std::string_view name : { "name: ''", "name: '   '", "name: null", "name: []", "name: {}" })
            {
                SCOPED_TRACE(name);
                EXPECT_THROW(parse(changedDocument("name: horror-classic", name)), std::exception);
            }
            for (const std::string_view required : { "required-content: []\n", "required-content: null\n",
                     "required-content: HorrorFixture.esp\n", "required-content: {}\n",
                     "required-content: [null]\n", "required-content: ['']\n", "required-content: ['   ']\n",
                     "required-content: [{}]\n", "required-content: [[]]\n" })
            {
                SCOPED_TRACE(required);
                EXPECT_THROW(parse(changedDocument("required-content:\n  - HorrorFixture.esp\n", required)),
                    std::exception);
            }
            for (const std::string_view interiors :
                { "[]", "null", "Horror, Old Cave", "{}", "[null]", "['']", "['   ']", "[{}]", "[[]]" })
            {
                SCOPED_TRACE(interiors);
                const std::string text = "format: 1\nname: horror-classic\nmode: classic\n"
                                         "required-content: [HorrorFixture.esp]\ninteriors: "
                    + std::string(interiors) + "\n";
                EXPECT_THROW(parse(text), std::exception);
            }
        }

        TEST(InteriorLightingProfileTest, rejectsContentPathsAndEmbeddedLineBreaks)
        {
            for (const std::string_view content :
                { "/mods/HorrorFixture.esp", "../HorrorFixture.esp", "mods\\HorrorFixture.esp",
                    "F:\\Mods\\HorrorFixture.esp", "C:HorrorFixture.esp", ".", ".." })
            {
                SCOPED_TRACE(content);
                const std::string replacement = "required-content:\n  - '" + std::string(content) + "'\n";
                EXPECT_THROW(parse(changedDocument("required-content:\n  - HorrorFixture.esp\n", replacement)),
                    std::exception);
            }
            EXPECT_THROW(parse(changedDocument("name: horror-classic", "name: \"horror\\nclassic\"")), std::exception);
            EXPECT_THROW(parse(changedDocument("HorrorFixture.esp", "\"Horror\\rFixture.esp\"")), std::exception);
            EXPECT_THROW(parse(changedDocument("\"Horror, Old Cave\"", "\"Horror,\\nOld Cave\"")), std::exception);
        }

        TEST(InteriorLightingProfileTest, matchesOnlyExactConfiguredInteriorsWithAsciiCaseFolding)
        {
            InteriorLightingProfiles profiles;
            ASSERT_TRUE(profiles.add(fixture(), { "horrorfixture.ESP" }));
            EXPECT_EQ(profiles.size(), 5u);
            EXPECT_EQ(profiles.find("Horror, Old Cave", false), "horror-classic");
            EXPECT_EQ(profiles.find("HORROR, OLD CAVE", false), "horror-classic");
            EXPECT_EQ(profiles.find("horror, oblivion, adramelech's tomb", false), "horror-classic");
            EXPECT_TRUE(profiles.find("Horror, Old Cave Annex", false).empty());
            EXPECT_TRUE(profiles.find("Horror, Old Cave ", false).empty());
            EXPECT_TRUE(profiles.find(" Horror, Old Cave", false).empty());
            EXPECT_TRUE(profiles.find("Horror, Old Cav", false).empty());
        }

        TEST(InteriorLightingProfileTest, requiresEveryContentFileAndAcceptsActiveBasenamesAcrossPlatforms)
        {
            InteriorLightingProfile profile = fixture();
            profile.mRequiredContent.push_back("Required Master.esm");
            InteriorLightingProfiles profiles;
            EXPECT_FALSE(profiles.add(profile, { "HorrorFixture.esp", "An unrelated mod.esp" }));
            EXPECT_EQ(profiles.size(), 0u);
            EXPECT_FALSE(profiles.add(profile, { "Required Master.esm", "HorrorFixture.esp.disabled" }));
            EXPECT_TRUE(profiles.find("Horror, Old Cave", false).empty());

            ASSERT_TRUE(profiles.add(profile, { "F:\\Mods\\HORRORFIXTURE.ESP", "/data/Required Master.ESM" }));
            EXPECT_EQ(profiles.size(), 5u);
            EXPECT_EQ(profiles.find("Horror, Old Cave", false), "horror-classic");
        }

        TEST(InteriorLightingProfileTest, inactiveContentCannotPartiallyInstallOrRemoveAProfile)
        {
            InteriorLightingProfiles profiles;
            const InteriorLightingProfile existing{ "another-profile", { "Other.esp" }, { "Other, Interior" } };
            ASSERT_TRUE(profiles.add(existing, { "Other.esp" }));
            EXPECT_FALSE(profiles.add(fixture(), {}));
            EXPECT_FALSE(profiles.add(fixture(), { "HorrorFixture.esp.old" }));
            EXPECT_EQ(profiles.size(), 1u);
            EXPECT_EQ(profiles.find("Other, Interior", false), "another-profile");
            EXPECT_TRUE(profiles.find("Horror, Old Cave", false).empty());
        }

        TEST(InteriorLightingProfileTest, excludesExteriorsObsoleteNamesAndUnlistedSharedVenues)
        {
            InteriorLightingProfiles profiles;
            ASSERT_TRUE(profiles.add(fixture(), { "HorrorFixture.esp" }));
            EXPECT_TRUE(profiles.find("Horror, Old Cave", true).empty());
            EXPECT_TRUE(profiles.find("Horror, Pandemonium", true).empty());
            for (const std::string_view name : { "", "Old Cave", "Oblivion", "Pandemonium",
                     "Oblivion, Shrine of Adramelech", "Oblivion, Adramelech's Tomb", "Shrine of Adramelech",
                     "Adramelech's Tomb", "Balmora, Mages Guild", "Suran, Desele's House of Earthly Delights",
                     "Vivec, Foreign Quarter Lower Waistworks", "Horror, Unlisted Interior" })
            {
                SCOPED_TRACE(name);
                EXPECT_TRUE(profiles.find(name, false).empty());
            }
            EXPECT_EQ(profiles.find("Horror, Old Cave", false), "horror-classic");
        }

        TEST(InteriorLightingProfileTest, doesNotInterpretWildcardOrRegularExpressionCellNames)
        {
            InteriorLightingProfiles profiles;
            const InteriorLightingProfile literal{ "literal-profile", { "Fixture.esp" },
                { "Horror, .*", "Horror, *", "^Horror, Old Cave$", "Horror, [A-Z]+" } };
            ASSERT_TRUE(profiles.add(literal, { "Fixture.esp" }));
            EXPECT_EQ(profiles.find("Horror, .*", false), "literal-profile");
            EXPECT_EQ(profiles.find("Horror, *", false), "literal-profile");
            EXPECT_EQ(profiles.find("^Horror, Old Cave$", false), "literal-profile");
            EXPECT_TRUE(profiles.find("Horror, Old Cave", false).empty());
            EXPECT_TRUE(profiles.find("Horror, NEW", false).empty());
        }

        TEST(InteriorLightingProfileTest, foldsAsciiWithoutChangingNonAsciiNameBytes)
        {
            InteriorLightingProfiles profiles;
            const InteriorLightingProfile accented{ "accented-profile", { "Fixture.esp" },
                { "Horror, \xC3\x89" "tage" } };
            ASSERT_TRUE(profiles.add(accented, { "Fixture.esp" }));
            EXPECT_EQ(profiles.find("HORROR, \xC3\x89" "TAGE", false), "accented-profile");
            EXPECT_TRUE(profiles.find("horror, \xC3\xA9" "tage", false).empty());
        }

        TEST(InteriorLightingProfileTest, sameNamedProfilesMergeAndRepeatedCellsAreIdempotent)
        {
            InteriorLightingProfiles profiles;
            ASSERT_TRUE(profiles.add(fixture(), { "HorrorFixture.esp" }));
            const InteriorLightingProfile extension{ "horror-classic", { "HorrorFixture.esp" },
                { "HORROR, OLD CAVE", "Horror, Extra Chamber", "horror, extra chamber" } };
            ASSERT_TRUE(profiles.add(extension, { "HorrorFixture.esp" }));
            EXPECT_EQ(profiles.size(), 6u);
            ASSERT_TRUE(profiles.add(extension, { "HorrorFixture.esp" }));
            EXPECT_EQ(profiles.size(), 6u);
            EXPECT_EQ(profiles.find("Horror, Extra Chamber", false), "horror-classic");
            EXPECT_EQ(profiles.find("Horror, Oblivion", false), "horror-classic");
        }

        TEST(InteriorLightingProfileTest, overlappingDifferentProfilesAreRejectedAtomically)
        {
            InteriorLightingProfiles profiles;
            ASSERT_TRUE(profiles.add(fixture(), { "HorrorFixture.esp" }));
            const InteriorLightingProfile overlap{ "conflicting-profile", { "HorrorFixture.esp" },
                { "New cell before overlap", "HORROR, OLD CAVE", "New cell after overlap" } };
            EXPECT_THROW(profiles.add(overlap, { "HorrorFixture.esp" }), std::exception);
            EXPECT_EQ(profiles.size(), 5u);
            EXPECT_TRUE(profiles.find("New cell before overlap", false).empty());
            EXPECT_TRUE(profiles.find("New cell after overlap", false).empty());
            EXPECT_EQ(profiles.find("Horror, Old Cave", false), "horror-classic");
        }

        TEST(InteriorLightingProfileTest, invalidDirectProfilesCannotPartiallyRegisterCells)
        {
            InteriorLightingProfiles profiles;
            ASSERT_TRUE(profiles.add(fixture(), { "HorrorFixture.esp" }));
            const std::vector<InteriorLightingProfile> invalid{
                { "", { "HorrorFixture.esp" }, { "New cell" } },
                { "invalid", {}, { "New cell" } },
                { "invalid", { "" }, { "New cell" } },
                { "invalid", { "mods/HorrorFixture.esp" }, { "New cell" } },
                { "invalid", { "HorrorFixture.esp" }, {} },
                { "invalid", { "HorrorFixture.esp" }, { "New cell", "" } },
                { "invalid", { "HorrorFixture.esp" }, { "New cell", "  " } },
                { "invalid", { "HorrorFixture.esp" }, { "New cell", "Bad\ncell" } },
            };
            for (const InteriorLightingProfile& profile : invalid)
            {
                SCOPED_TRACE(profile.mName);
                EXPECT_THROW(profiles.add(profile, { "HorrorFixture.esp" }), std::exception);
                EXPECT_EQ(profiles.size(), 5u);
                EXPECT_TRUE(profiles.find("New cell", false).empty());
                EXPECT_EQ(profiles.find("Horror, Old Cave", false), "horror-classic");
            }
        }

        TEST(InteriorLightingProfileTest, clearRemovesAllBindingsAndAllowsFreshContentSelection)
        {
            InteriorLightingProfiles profiles;
            ASSERT_TRUE(profiles.add(fixture(), { "HorrorFixture.esp" }));
            profiles.clear();
            EXPECT_EQ(profiles.size(), 0u);
            EXPECT_TRUE(profiles.find("Horror, Old Cave", false).empty());
            EXPECT_FALSE(profiles.add(fixture(), { "Unrelated.esp" }));
            const InteriorLightingProfile replacement{ "replacement", { "Replacement.esp" }, { "Horror, Old Cave" } };
            ASSERT_TRUE(profiles.add(replacement, { "Replacement.esp" }));
            EXPECT_EQ(profiles.find("Horror, Old Cave", false), "replacement");
            EXPECT_EQ(profiles.size(), 1u);
        }

        TEST(InteriorLightingProfileTest, parseFailureLeavesPreviouslyRegisteredProfilesIntact)
        {
            InteriorLightingProfiles profiles;
            ASSERT_TRUE(profiles.add(fixture(), { "HorrorFixture.esp" }));
            EXPECT_THROW(profiles.add(parse(changedDocument("mode: classic", "mode: unsupported")),
                             { "HorrorFixture.esp" }),
                std::exception);
            EXPECT_EQ(profiles.size(), 5u);
            EXPECT_EQ(profiles.find("Horror, Old Cave", false), "horror-classic");
        }

        TEST(InteriorLightingProfileTest, shippedHorrorProfileRequiresItsContentAndOnlySelectsDedicatedInteriors)
        {
            const std::filesystem::path path = std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR)
                / "custom/horror-lighting/profile/shader_profiles/horror_mod.yaml";
            std::ifstream stream(path);
            ASSERT_TRUE(stream.is_open());
            const InteriorLightingProfile profile = parseInteriorLightingProfile(stream);
            EXPECT_EQ(profile.mName, "Horror Mod");
            EXPECT_EQ(profile.mRequiredContent, (std::vector<std::string>{ "Horror Mod.esp" }));
            InteriorLightingProfiles profiles;
            EXPECT_FALSE(profiles.add(profile, { "Morrowind.esm", "S3LightFixes.omwaddon" }));
            EXPECT_EQ(profiles.size(), 0u);
            ASSERT_TRUE(profiles.add(profile, { "Morrowind.esm", "HORROR MOD.ESP" }));
            EXPECT_EQ(profiles.size(), 63u);
            for (const std::string_view name :
                { "Horror, Old Cave", "Lower Depths", "Horror, Pandemonium", "Gubrez, Sector C", "HorrorVoid" })
            {
                SCOPED_TRACE(name);
                EXPECT_EQ(profiles.find(name, false), "Horror Mod");
                EXPECT_TRUE(profiles.find(name, true).empty());
            }
            for (const std::string_view name : { "Old Cave", "Oblivion", "Pandemonium",
                     "Oblivion, Shrine of Adramelech", "Oblivion, Adramelech's Tomb", "Suran, Suran Tradehouse",
                     "Suran, Temple", "Suran, Desele's House of Earthly Delights", "Suran, Oran Manor" })
            {
                SCOPED_TRACE(name);
                EXPECT_TRUE(profiles.find(name, false).empty());
            }
        }

        TEST(InteriorLightingProfileTest, stateUpdaterPublishesClassicUniformAcrossOnOffOnAndReset)
        {
            SceneUtil::StateUpdater updater;
            const osg::ref_ptr<osg::StateSet> first = new osg::StateSet;
            const osg::ref_ptr<osg::StateSet> second = new osg::StateSet;
            auto expectClassic = [](osg::StateSet* state, bool expected) {
                osg::Uniform* uniform = state->getUniform("classicInteriorLighting");
                ASSERT_NE(uniform, nullptr);
                EXPECT_EQ(uniform->getType(), osg::Uniform::BOOL);
                bool value = !expected;
                ASSERT_TRUE(uniform->get(value));
                EXPECT_EQ(value, expected);
            };

            updater.setDefaults(first.get());
            expectClassic(first.get(), false);
            updater.setClassicInteriorLighting(true);
            updater.apply(first.get(), nullptr);
            expectClassic(first.get(), true);
            updater.setClassicInteriorLighting(false);
            updater.apply(first.get(), nullptr);
            expectClassic(first.get(), false);

            updater.setClassicInteriorLighting(true);
            updater.reset();
            updater.setDefaults(second.get());
            expectClassic(second.get(), true);
            updater.apply(first.get(), nullptr);
            updater.apply(second.get(), nullptr);
            expectClassic(first.get(), true);
            expectClassic(second.get(), true);

            updater.setClassicInteriorLighting(false);
            updater.apply(first.get(), nullptr);
            updater.apply(second.get(), nullptr);
            expectClassic(first.get(), false);
            expectClassic(second.get(), false);
        }
    }
}

