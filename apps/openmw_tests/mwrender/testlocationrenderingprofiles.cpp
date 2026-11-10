#include <apps/openmw/mwrender/locationrenderingprofiles.hpp>

#include <components/sceneutil/stateupdater.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>

#include <gtest/gtest.h>

#include <osg/StateSet>
#include <osg/Uniform>
#include <osg/ref_ptr>

#include <exception>
#include <cstdlib>
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
    using MWRender::parseLocationRenderingProfile;

    LocationRenderingProfile parse(std::string_view text)
    {
        std::istringstream stream{ std::string(text) };
        return parseLocationRenderingProfile(stream);
    }

    constexpr std::string_view ProfileA = R"(
format: 2
id: example-a
name: Example A
enabled: true
priority: 10
required-content:
  - Example.esp
match:
  type: interior
  cells:
    - Example, Cave
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: true
)";

    TEST(LocationRenderingProfileTest, parsesAllFourShaderValues)
    {
        const LocationRenderingProfile profile = parse(ProfileA);
        EXPECT_EQ(profile.mId, "example-a");
        EXPECT_EQ(profile.mPriority, 10);
        ASSERT_EQ(profile.mInteriors.size(), 1);
        EXPECT_EQ(profile.mInteriors.front(), "Example, Cave");
        EXPECT_TRUE(profile.mShaders.mClampLighting);
        EXPECT_FALSE(profile.mShaders.mForcePerPixelLighting);
        EXPECT_FALSE(profile.mShaders.mAutoUseObjectNormalMaps);
        EXPECT_TRUE(profile.mShaders.mAutoUseObjectSpecularMaps);
    }

    TEST(LocationRenderingProfileTest, matchesExactInteriorOnlyWhenRequiredContentIsActive)
    {
        LocationRenderingProfiles profiles;
        EXPECT_FALSE(profiles.add(parse(ProfileA), { "Other.esp" }));
        EXPECT_EQ(profiles.find("Example, Cave", false), nullptr);

        EXPECT_TRUE(profiles.add(parse(ProfileA), { "C:\\Mods\\Example.ESP" }));
        EXPECT_NE(profiles.find("example, cave", false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave Annex", false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave", true), nullptr);
    }

    TEST(LocationRenderingProfileTest, bundledProfileIsDiscoveredAndCanBeReplacedThroughTheVfs)
    {
        const char* installed = std::getenv("OPENMW_TEST_PROFILE_DATA");
        const auto data = installed ? std::filesystem::path(installed)
                                    : std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR) / "files/data";
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<VFS::FileSystemArchive>(data));
        vfs.buildIndex();
        const auto source = vfs.get(VFS::Path::NormalizedView("shader_profiles/horror_mod.yaml"));
        const LocationRenderingProfile bundled = MWRender::parseLocationRenderingProfile(*source);
        ASSERT_EQ(bundled.mInteriors.size(), 63u);
        ASSERT_EQ(bundled.mRequiredContent.size(), 1u);
        LocationRenderingProfiles profiles;
        profiles.load(vfs, {});
        EXPECT_EQ(profiles.size(), 0u);
        profiles.load(vfs, bundled.mRequiredContent);
        ASSERT_EQ(profiles.size(), bundled.mInteriors.size());
        for (const std::string& cell : bundled.mInteriors)
        {
            const auto* active = profiles.find(cell, false);
            ASSERT_NE(active, nullptr) << cell;
            EXPECT_EQ(active->mId, bundled.mId);
            EXPECT_EQ(active->mShaders, bundled.mShaders);
            EXPECT_EQ(profiles.find(cell, true), nullptr);
        }
        EXPECT_EQ(profiles.find("Unlisted fixture interior", false), nullptr);
        RecordProperty("selected_interiors", static_cast<int>(profiles.size()));

        const auto overrideDir = TestingOpenMW::outputDirPath("profile-override");
        std::filesystem::create_directories(overrideDir / "shader_profiles");
        {
            std::ofstream edited(overrideDir / "shader_profiles/horror_mod.yaml");
            ASSERT_TRUE(edited << ProfileA);
        }
        vfs.addArchive(std::make_unique<VFS::FileSystemArchive>(overrideDir));
        vfs.buildIndex();
        profiles.load(vfs, { "Example.esp" });
        ASSERT_NE(profiles.find("Example, Cave", false), nullptr);
        EXPECT_EQ(profiles.find(bundled.mInteriors.front(), false), nullptr);
        profiles.load(vfs, bundled.mRequiredContent);
        EXPECT_EQ(profiles.size(), 0u);
    }

    TEST(LocationRenderingProfileTest, higherPriorityDoesNotHideConflictsAtLowerPriorities)
    {
        auto lower = parse(ProfileA);
        lower.mRequiredContent.clear();
        auto higher = lower;
        higher.mId = "higher";
        higher.mPriority++;
        auto conflict = lower;
        conflict.mId = "conflict";
        conflict.mInteriors = { "New cell", lower.mInteriors.front() };
        for (bool highFirst : { false, true })
        {
            LocationRenderingProfiles profiles;
            ASSERT_TRUE(profiles.add(highFirst ? higher : lower, {}));
            ASSERT_TRUE(profiles.add(highFirst ? lower : higher, {}));
            EXPECT_THROW(profiles.add(conflict, {}), std::runtime_error);
            EXPECT_EQ(profiles.find("New cell", false), nullptr);
            ASSERT_NE(profiles.find(lower.mInteriors.front(), false), nullptr);
            EXPECT_EQ(profiles.find(lower.mInteriors.front(), false)->mId, higher.mId);
        }
    }

    TEST(LocationRenderingProfileTest, disabledProfileDoesNotPublishCells)
    {
        const std::string text = std::string(ProfileA).replace(std::string(ProfileA).find("enabled: true"),
            std::string("enabled: true").size(), "enabled: false");
        LocationRenderingProfiles profiles;
        EXPECT_FALSE(profiles.add(parse(text), { "Example.esp" }));
        EXPECT_EQ(profiles.size(), 0);
    }

    TEST(LocationRenderingProfileTest, higherPriorityProfileWinsIndependentlyOfLoadOrder)
    {
        const auto high = parse(R"(
format: 2
id: high
name: High
priority: 50
match: { type: interior, cells: [Shared Cell] }
shaders:
  clamp-lighting: false
  force-per-pixel-lighting: true
  auto-use-object-normal-maps: true
  auto-use-object-specular-maps: false
)");
        const auto low = parse(R"(
format: 2
id: low
name: Low
priority: -5
match: { type: interior, cells: [Shared Cell] }
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: true
)");

        LocationRenderingProfiles profiles;
        EXPECT_TRUE(profiles.add(high, {}));
        EXPECT_TRUE(profiles.add(low, {}));
        ASSERT_NE(profiles.find("Shared Cell", false), nullptr);
        EXPECT_EQ(profiles.find("Shared Cell", false)->mId, "high");

        profiles.clear();
        EXPECT_TRUE(profiles.add(low, {}));
        EXPECT_TRUE(profiles.add(high, {}));
        ASSERT_NE(profiles.find("Shared Cell", false), nullptr);
        EXPECT_EQ(profiles.find("Shared Cell", false)->mId, "high");
    }

    TEST(LocationRenderingProfileTest, equalPriorityConflictIsRejectedAtomically)
    {
        const auto first = parse(ProfileA);
        const auto conflict = parse(R"(
format: 2
id: example-b
name: Example B
priority: 10
match: { type: interior, cells: ["Example, Cave", "Other Cave"] }
shaders:
  clamp-lighting: false
  force-per-pixel-lighting: true
  auto-use-object-normal-maps: true
  auto-use-object-specular-maps: true
)");
        LocationRenderingProfiles profiles;
        ASSERT_TRUE(profiles.add(first, { "Example.esp" }));
        EXPECT_THROW(profiles.add(conflict, {}), std::runtime_error);
        EXPECT_EQ(profiles.find("Other Cave", false), nullptr);
        ASSERT_NE(profiles.find("Example, Cave", false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave", false)->mId, "example-a");
    }

    TEST(LocationRenderingProfileTest, malformedAndUnknownFieldsAreRejected)
    {
        EXPECT_THROW(parse(R"(
format: 2
id: bad
name: Bad
match: { type: exterior, cells: [Somewhere] }
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: false
)"), std::runtime_error);

        EXPECT_THROW(parse(R"(
format: 2
id: bad
name: Bad
unexpected: true
match: { type: interior, cells: [Somewhere] }
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: false
)"), std::runtime_error);
    }

    TEST(LocationRenderingProfileTest, rejectsInvalidIdsPathsTypesAndRepeatedEntries)
    {
        for (const std::string_view id : { "", "bad id", "bad/id", "bad:id", "bad\\id" })
        {
            SCOPED_TRACE(id);
            std::string text(ProfileA);
            const std::size_t start = text.find("id: example-a");
            ASSERT_NE(start, std::string::npos);
            text.replace(start, std::string("id: example-a").size(), "id: '" + std::string(id) + "'");
            EXPECT_THROW(parse(text), std::exception);
        }

        for (const std::string_view content : { "/mods/Example.esp", "../Example.esp", "mods\\Example.esp",
                 "F:\\Mods\\Example.esp", "C:Example.esp", ".", ".." })
        {
            SCOPED_TRACE(content);
            std::string text(ProfileA);
            const std::size_t start = text.find("  - Example.esp");
            ASSERT_NE(start, std::string::npos);
            text.replace(start, std::string("  - Example.esp").size(), "  - '" + std::string(content) + "'");
            EXPECT_THROW(parse(text), std::exception);
        }

        EXPECT_THROW(parse(R"(
format: 2
id: repeated-cells
name: Repeated cells
match: { type: interior, cells: [Cell One, cell one] }
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: false
)"), std::exception);

        EXPECT_THROW(parse(R"(
format: 2
id: bad-bool
name: Bad boolean
match: { type: interior, cells: [Cell One] }
shaders:
  clamp-lighting: [true]
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: false
)"), std::exception);
    }

    TEST(LocationRenderingProfileTest, wildcardLookingCellNamesRemainLiteral)
    {
        const auto literal = parse(R"(
format: 2
id: literal
name: Literal
match:
  type: interior
  cells: ["Example, .*", "Example, *", "^Example, Cave$"]
shaders:
  clamp-lighting: true
  force-per-pixel-lighting: false
  auto-use-object-normal-maps: false
  auto-use-object-specular-maps: false
)");
        LocationRenderingProfiles profiles;
        ASSERT_TRUE(profiles.add(literal, {}));
        EXPECT_NE(profiles.find("Example, .*", false), nullptr);
        EXPECT_NE(profiles.find("Example, *", false), nullptr);
        EXPECT_NE(profiles.find("^Example, Cave$", false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave", false), nullptr);
    }

    TEST(LocationRenderingProfileTest, duplicateIdsAreRejectedWithoutChangingPublishedCells)
    {
        LocationRenderingProfiles profiles;
        ASSERT_TRUE(profiles.add(parse(ProfileA), { "Example.esp" }));
        const auto duplicate = parse(R"(
format: 2
id: EXAMPLE-A
name: Duplicate
priority: 100
match: { type: interior, cells: [Other Cell] }
shaders:
  clamp-lighting: false
  force-per-pixel-lighting: true
  auto-use-object-normal-maps: true
  auto-use-object-specular-maps: true
)");
        EXPECT_THROW(profiles.add(duplicate, {}), std::exception);
        EXPECT_EQ(profiles.find("Other Cell", false), nullptr);
        ASSERT_NE(profiles.find("Example, Cave", false), nullptr);
        EXPECT_EQ(profiles.find("Example, Cave", false)->mId, "example-a");
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
