#include <apps/openmw/mwrender/locationrenderingprofiles.hpp>
#include <apps/openmw/mwrender/negativelighttracker.hpp>

#include <components/esm3/loadligh.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/stateupdater.hpp>
#include <components/testing/util.hpp>

#include <gtest/gtest.h>
#include <osg/StateSet>
#include <osg/Uniform>
#include <osg/ref_ptr>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using MWRender::LocationRenderingProfile;
    using MWRender::LocationRenderingProfiles;
    using MWRender::NegativeLightTracker;

    LocationRenderingProfile parse(std::string_view text)
    {
        std::istringstream stream{ std::string(text) };
        return MWRender::parseLocationRenderingProfile(stream);
    }

    constexpr std::string_view ProfileA = R"(
id: example-a
name: Example A
enabled: true
priority: 10
required-content: [Example.esp]
match:
  type: interior
  cells: ["Example, Cave"]
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: true
)";

    constexpr std::string_view Automatic = R"(
id: automatic-negative-light-interiors
name: Negative-light compatibility
priority: 10
match: {type: interior, condition: contains-negative-light}
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: false
)";

    std::string document(const std::vector<std::string_view>& profiles)
    {
        std::ostringstream output;
        output << "format: 3\nprofiles:" << (profiles.empty() ? " []\n" : "\n");
        for (std::string_view profile : profiles)
        {
            bool first = true;
            std::istringstream lines{ std::string(profile) };
            for (std::string line; std::getline(lines, line);)
            {
                if (line.empty())
                    continue;
                output << (first ? "  - " : "    ") << line << '\n';
                first = false;
            }
        }
        return output.str();
    }

    std::filesystem::path writeShared(std::string_view content)
    {
        const auto directory = TestingOpenMW::outputDirPath(
            std::string("shared-profiles-") + testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::create_directories(directory);
        const auto file = directory / "location-rendering.yaml";
        std::ofstream output(file);
        output << content;
        output.close();
        if (!output)
            throw std::runtime_error("Could not write shared profile fixture");
        return file;
    }

    TEST(LocationRenderingProfileTest, parsesAllFourShaderValues)
    {
        const auto profile = parse(ProfileA);
        EXPECT_EQ(profile.mId, "example-a");
        EXPECT_EQ(profile.mPriority, 10);
        ASSERT_EQ(profile.mInteriors.size(), 1);
        EXPECT_EQ(profile.mInteriors.front(), "Example, Cave");
        EXPECT_FALSE(profile.mContainsNegativeLight);
        EXPECT_TRUE(profile.mShaders.mClampLighting);
        EXPECT_FALSE(profile.mShaders.mForcePerPixelLighting);
        EXPECT_FALSE(profile.mShaders.mAutoUseObjectNormalMaps);
        EXPECT_TRUE(profile.mShaders.mAutoUseObjectSpecularMaps);
    }

    TEST(LocationRenderingProfileTest, parsesGenericConditionWithoutAnyModOrCellList)
    {
        const auto profile = parse(Automatic);
        EXPECT_TRUE(profile.mContainsNegativeLight);
        EXPECT_TRUE(profile.mRequiredContent.empty());
        EXPECT_TRUE(profile.mInteriors.empty());
        LocationRenderingProfiles profiles;
        ASSERT_TRUE(profiles.add(profile, {}));
        EXPECT_EQ(profiles.find("Unseen new mod room", false, false), nullptr);
        EXPECT_NE(profiles.find("Unseen new mod room", false, true), nullptr);
        EXPECT_EQ(profiles.find("Unseen new mod room", true, true), nullptr);
    }

    TEST(LocationRenderingProfileTest, matchesExactInteriorOnlyWhenAllRequiredContentIsActive)
    {
        auto profile = parse(ProfileA);
        profile.mRequiredContent.push_back("Dependency.esm");
        LocationRenderingProfiles profiles;
        EXPECT_FALSE(profiles.add(profile, { "Example.esp" }));
        EXPECT_TRUE(profiles.add(profile, { "C:\\Mods\\Example.ESP", "Dependency.esm" }));
        EXPECT_NE(profiles.find("example, cave", false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave Annex", false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave", true), nullptr);
    }

    TEST(LocationRenderingProfileTest, loadsOnlySharedFileAndDoesNotFallBackToLegacyProfiles)
    {
        const auto file = writeShared(document({ ProfileA, Automatic }));
        const auto legacy = file.parent_path() / "shader_profiles";
        std::filesystem::create_directories(legacy);
        std::ofstream(legacy / "old-profile.yaml") << "format: 2\n" << ProfileA;
        LocationRenderingProfiles profiles;
        profiles.load(file, { "Example.esp" });
        ASSERT_EQ(profiles.profiles().size(), 2u);
        EXPECT_NE(profiles.find("Other room", false, true), nullptr);
        profiles.load(file.parent_path() / "absent.yaml", { "Example.esp" });
        EXPECT_TRUE(profiles.profiles().empty());
        EXPECT_EQ(profiles.find("Example, Cave", false, true), nullptr);
        std::ofstream(file) << document({});
        profiles.load(file, { "Example.esp" });
        EXPECT_TRUE(profiles.profiles().empty());
    }

    TEST(LocationRenderingProfileTest, invalidRuleDoesNotDisableOtherValidRules)
    {
        const auto file = writeShared(document({ "id: incomplete\n", Automatic, ProfileA }));
        LocationRenderingProfiles profiles;
        profiles.load(file, { "Example.esp" });
        EXPECT_EQ(profiles.profiles().size(), 2u);
        EXPECT_NE(profiles.find("New room", false, true), nullptr);
    }

    TEST(LocationRenderingProfileTest, badDocumentNeverUsesPreviouslyLoadedOrLegacyProfiles)
    {
        const std::vector<std::string> invalid = { "format: 2\nprofiles: []\n", "format: 3\nprofiles: [\n",
            "format: 3\nprofiles: {}\n", "format: 3\nprofiles: []\nunrecognised: true\n",
            "format: 3\nformat: 3\nprofiles: []\n", "format: 3\nprofiles: []\n---\nformat: 3\nprofiles: []\n" };
        for (const auto& text : invalid)
        {
            SCOPED_TRACE(text);
            LocationRenderingProfiles profiles;
            ASSERT_TRUE(profiles.add(parse(Automatic), {}));
            profiles.load(writeShared(text), {});
            EXPECT_TRUE(profiles.profiles().empty());
            EXPECT_EQ(profiles.find("Any room", false, true), nullptr);
        }
    }

    TEST(LocationRenderingProfileTest, duplicateIdsInSharedFileRejectAllOccurrences)
    {
        std::string duplicate(ProfileA);
        duplicate.replace(duplicate.find("id: example-a"), 13, "id: EXAMPLE-A");
        for (bool reversed : { false, true })
        {
            const auto file = writeShared(
                reversed ? document({ duplicate, ProfileA, Automatic }) : document({ ProfileA, duplicate, Automatic }));
            LocationRenderingProfiles profiles;
            profiles.load(file, { "Example.esp" });
            ASSERT_EQ(profiles.profiles().size(), 1u);
            EXPECT_EQ(profiles.profiles().front().mId, "automatic-negative-light-interiors");
        }
    }

    TEST(LocationRenderingProfileTest, disabledProfileDoesNotPublishAnyMatch)
    {
        auto profile = parse(Automatic);
        profile.mEnabled = false;
        LocationRenderingProfiles profiles;
        EXPECT_FALSE(profiles.add(profile, {}));
        EXPECT_EQ(profiles.find("Room", false, true), nullptr);
    }

    TEST(LocationRenderingProfileTest, explicitHigherPriorityWinsOverAutomaticConditionInEitherOrder)
    {
        auto custom = parse(ProfileA);
        custom.mPriority = 100;
        for (bool reversed : { false, true })
        {
            LocationRenderingProfiles profiles;
            ASSERT_TRUE(profiles.add(reversed ? custom : parse(Automatic), { "Example.esp" }));
            ASSERT_TRUE(profiles.add(reversed ? parse(Automatic) : custom, { "Example.esp" }));
            ASSERT_NE(profiles.find("Example, Cave", false, true), nullptr);
            EXPECT_EQ(profiles.find("Example, Cave", false, true)->mId, "example-a");
            EXPECT_EQ(profiles.find("Other", false, false), nullptr);
            EXPECT_NE(profiles.find("Other", false, true), nullptr);
        }
    }

    TEST(LocationRenderingProfileTest, conflictingPriorityUsesGlobalsOnlyInAffectedMatches)
    {
        auto a = parse(ProfileA);
        a.mRequiredContent.clear();
        auto b = a;
        b.mId = "example-b";
        b.mInteriors.push_back("Other Cave");
        b.mShaders.mClampLighting = false;
        for (bool reversed : { false, true })
        {
            LocationRenderingProfiles profiles;
            ASSERT_TRUE(profiles.add(reversed ? a : b, {}));
            ASSERT_TRUE(profiles.add(reversed ? b : a, {}));
            EXPECT_EQ(profiles.find("Example, Cave", false), nullptr);
            ASSERT_NE(profiles.find("Other Cave", false), nullptr);
            EXPECT_EQ(profiles.find("Other Cave", false)->mId, "example-b");
            auto high = a;
            high.mId = "higher";
            high.mPriority++;
            ASSERT_TRUE(profiles.add(high, {}));
            EXPECT_EQ(profiles.find("Example, Cave", false)->mId, "higher");
        }
    }

    TEST(LocationRenderingProfileTest, identicalEqualPriorityMatchesAreDeterministic)
    {
        auto a = parse(ProfileA);
        a.mRequiredContent.clear();
        auto b = a;
        b.mId = "example-b";
        for (bool reversed : { false, true })
        {
            LocationRenderingProfiles profiles;
            ASSERT_TRUE(profiles.add(reversed ? a : b, {}));
            ASSERT_TRUE(profiles.add(reversed ? b : a, {}));
            EXPECT_EQ(profiles.find("Example, Cave", false)->mId, "example-a");
        }
    }

    TEST(LocationRenderingProfileTest, conditionConflictDoesNotAffectLocationsWithoutNegativeLight)
    {
        LocationRenderingProfiles profiles;
        ASSERT_TRUE(profiles.add(parse(ProfileA), { "Example.esp" }));
        ASSERT_TRUE(profiles.add(parse(Automatic), {}));
        EXPECT_NE(profiles.find("Example, Cave", false, false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave", false, true), nullptr);
        EXPECT_NE(profiles.find("Other", false, true), nullptr);
    }

    TEST(LocationRenderingProfileTest, rejectsUnknownAndAmbiguousMatchForms)
    {
        for (std::string_view match : { "{type: interior}", "{type: exterior, condition: contains-negative-light}",
                 "{type: interior, condition: unknown}",
                 "{type: interior, cells: [Room], condition: contains-negative-light}", "{type: interior, cells: []}",
                 "{type: interior, cells: [Room, room]}" })
        {
            SCOPED_TRACE(match);
            std::string text(Automatic);
            const auto first = text.find("match:");
            const auto last = text.find("\nshaders:", first);
            text.replace(first, last - first, "match: " + std::string(match));
            EXPECT_THROW(parse(text), std::exception);
        }
        EXPECT_THROW(parse(std::string(Automatic) + "unexpected: true\n"), std::exception);
        EXPECT_THROW(parse("format: 2\n" + std::string(ProfileA)), std::exception);
    }

    TEST(LocationRenderingProfileTest, requiresCompleteShaderValuesAndValidIdentifiers)
    {
        for (std::string_view key : { "clamp-lighting", "force-per-pixel-lighting", "auto-use-object-normal-maps",
                 "auto-use-object-specular-maps" })
        {
            std::string text(Automatic);
            const auto first = text.find("  " + std::string(key));
            text.erase(first, text.find('\n', first) - first + 1);
            EXPECT_THROW(parse(text), std::exception);
        }
        for (std::string_view id : { "bad id", "../bad", "bad/id", "bad:id", "bad\\id", "" })
        {
            std::string text(ProfileA);
            text.replace(text.find("id: example-a"), 13, "id: '" + std::string(id) + "'");
            EXPECT_THROW(parse(text), std::exception);
        }
        for (std::string_view content : { "../Example.esp", "C:Example.esp", "mods/Example.esp", "mods\\Example.esp",
                 "F:\\Mods\\Example.esp", ".", ".." })
        {
            std::string text(ProfileA);
            text.replace(text.find("[Example.esp]"), 13, "['" + std::string(content) + "']");
            EXPECT_THROW(parse(text), std::exception);
        }
        for (std::string_view boolean : { "[true]", "42", "null", "invalid" })
        {
            std::string text(Automatic);
            const std::string field = "clamp-lighting: true";
            text.replace(text.find(field), field.size(), "clamp-lighting: " + std::string(boolean));
            EXPECT_THROW(parse(text), std::exception);
        }
    }

    TEST(LocationRenderingProfileTest, wildcardLookingCellNamesRemainLiteral)
    {
        auto profile = parse(ProfileA);
        profile.mRequiredContent.clear();
        profile.mInteriors = { "Room *", "Room .*" };
        LocationRenderingProfiles profiles;
        ASSERT_TRUE(profiles.add(profile, {}));
        EXPECT_NE(profiles.find("room *", false), nullptr);
        EXPECT_EQ(profiles.find("Room One", false), nullptr);
    }

    TEST(LocationRenderingProfileTest, qualifyingColourIgnoresAlphaRadiusAndAnimationFlags)
    {
        ESM::Light record;
        record.blank();
        record.mData.mFlags = ESM::Light::Negative;
        record.mData.mRadius = 0;
        record.mData.mColor = 0x010203;
        EXPECT_TRUE(MWRender::isQualifyingNegativeLight(SceneUtil::LightCommon(record)));
        for (int animation : { ESM::Light::Flicker, ESM::Light::FlickerSlow, ESM::Light::Pulse, ESM::Light::PulseSlow })
        {
            record.mData.mFlags = ESM::Light::Negative | animation;
            EXPECT_TRUE(MWRender::isQualifyingNegativeLight(SceneUtil::LightCommon(record)));
        }
        record.mData.mColor = 0xff000000;
        EXPECT_FALSE(MWRender::isQualifyingNegativeLight(SceneUtil::LightCommon(record)));
        record.mData.mColor = 0x010203;
        record.mData.mFlags = 0;
        EXPECT_FALSE(MWRender::isQualifyingNegativeLight(SceneUtil::LightCommon(record)));
    }

    TEST(LocationRenderingProfileTest, trackerOnlyNotifiesZeroCrossingsAndMovesAtomically)
    {
        NegativeLightTracker tracker;
        int roomA, roomB, ownerA, ownerB;
        std::vector<const void*> notifications;
        tracker.setCallback([&](const void* cell) { notifications.push_back(cell); });
        tracker.set(&ownerA, &roomA, 1);
        tracker.set(&ownerA, &roomA, 1);
        tracker.set(&ownerA, &roomA, 2);
        tracker.set(&ownerB, &roomA, 1);
        EXPECT_EQ(tracker.count(&roomA), 3u);
        ASSERT_EQ(notifications.size(), 1u);
        tracker.erase(&ownerA);
        EXPECT_EQ(tracker.count(&roomA), 1u);
        EXPECT_EQ(notifications.size(), 1u);
        tracker.set(&ownerB, &roomB, 1);
        ASSERT_EQ(notifications.size(), 3u);
        EXPECT_EQ(notifications[1], &roomA);
        EXPECT_EQ(notifications[2], &roomB);
        tracker.clear();
        EXPECT_EQ(tracker.count(&roomB), 0u);
        ASSERT_EQ(notifications.size(), 4u);
        tracker.erase(&ownerB);
        EXPECT_EQ(notifications.size(), 4u);
        tracker.setCallback({});
        tracker.set(&ownerA, &roomA, 1);
        tracker.clear();
        EXPECT_EQ(notifications.size(), 4u);
    }

    TEST(LocationRenderingProfileTest, trackerAndConditionalProfileRestoreOrdinaryUniforms)
    {
        LocationRenderingProfiles profiles;
        profiles.load(writeShared(document({ Automatic })), {});
        NegativeLightTracker tracker;
        SceneUtil::StateUpdater updater;
        osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
        updater.setDefaults(state.get());
        int room, elsewhere, light;
        const void* currentCell = &room;
        bool masterEnabled = true;
        bool exterior = false;
        const auto apply = [&] {
            const auto* profile = masterEnabled && currentCell
                ? profiles.find("Unlisted dynamic room", exterior, tracker.count(currentCell) != 0)
                : nullptr;
            if (profile)
                updater.setLocationRenderingProfile(true, profile->mShaders.mClampLighting,
                    profile->mShaders.mForcePerPixelLighting, profile->mShaders.mAutoUseObjectNormalMaps,
                    profile->mShaders.mAutoUseObjectSpecularMaps);
            else
                updater.setLocationRenderingProfile(false, false, false, true, true);
            updater.apply(state.get(), nullptr);
        };
        tracker.setCallback([&](const void*) { apply(); });
        const auto active = [&] {
            bool result = false;
            state->getUniform("locationRenderingProfileActive")->get(result);
            return result;
        };
        apply();
        EXPECT_FALSE(active());
        tracker.set(&light, &room, 1);
        EXPECT_TRUE(active());
        tracker.erase(&light);
        EXPECT_FALSE(active());
        tracker.set(&light, &room, 1);
        EXPECT_TRUE(active());
        masterEnabled = false;
        apply();
        EXPECT_FALSE(active());
        masterEnabled = true;
        apply();
        EXPECT_TRUE(active());
        currentCell = &elsewhere;
        apply();
        EXPECT_FALSE(active());
        currentCell = &room;
        exterior = true;
        apply();
        EXPECT_FALSE(active());
        exterior = false;
        apply();
        EXPECT_TRUE(active());
        tracker.clear();
        EXPECT_FALSE(active());
    }

    TEST(LocationRenderingProfileTest, stateUpdaterPublishesProfileAndOrdinaryMapUniforms)
    {
        SceneUtil::StateUpdater updater;
        const osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
        updater.setOrdinaryAutomaticObjectMaps(true, false);
        updater.setLocationRenderingProfile(true, true, false, false, true);
        updater.setDefaults(state.get());
        updater.apply(state.get(), nullptr);

        auto expectBool = [state](const char* name, bool expected) {
            osg::Uniform* uniform = state->getUniform(name);
            ASSERT_NE(uniform, nullptr) << name;
            EXPECT_EQ(uniform->getType(), osg::Uniform::BOOL) << name;
            bool value = !expected;
            ASSERT_TRUE(uniform->get(value)) << name;
            EXPECT_EQ(value, expected) << name;
        };
        expectBool("locationRenderingProfileActive", true);
        expectBool("locationProfileClampLighting", true);
        expectBool("locationProfileForcePerPixelLighting", false);
        expectBool("locationProfileAutoUseObjectNormalMaps", false);
        expectBool("locationProfileAutoUseObjectSpecularMaps", true);
        expectBool("ordinaryAutoUseObjectNormalMaps", true);
        expectBool("ordinaryAutoUseObjectSpecularMaps", false);

        updater.setLocationRenderingProfile(false, false, false, true, true);
        updater.setOrdinaryAutomaticObjectMaps(false, true);
        updater.apply(state.get(), nullptr);
        expectBool("locationRenderingProfileActive", false);
        expectBool("ordinaryAutoUseObjectNormalMaps", false);
        expectBool("ordinaryAutoUseObjectSpecularMaps", true);
    }

}
