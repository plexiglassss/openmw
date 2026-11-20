#include <gtest/gtest.h>

#include <apps/openmw/mwgui/smartmappolicy.hpp>

#include <limits>

namespace
{
    using namespace MWGui::SmartMap;

    struct Scenario
    {
        Options mOptions{ true, true, true, 4, true };
        Entry mEntry{ true, false, 0.f, false };
        int mDoors = 0;
        bool mHostile = false;
        int mDoorQueries = 0;
        int mHostileQueries = 0;
        int mRequestedThreshold = 0;

        View evaluate()
        {
            return chooseView(mOptions, mEntry,
                [&](int threshold) {
                    ++mDoorQueries;
                    mRequestedThreshold = threshold;
                    return mDoors;
                },
                [&] {
                    ++mHostileQueries;
                    return mHostile;
                });
        }
    };

    TEST(SmartMapPolicyTest, DisabledMasterLeavesViewAndDoesNotQueryWorld)
    {
        Scenario s;
        s.mOptions.mEnabled = false;
        s.mDoors = 50;
        s.mHostile = true;
        EXPECT_EQ(s.evaluate(), View::Unchanged);
        s.mEntry.mExterior = false;
        EXPECT_EQ(s.evaluate(), View::Unchanged);
        EXPECT_EQ(s.mDoorQueries, 0);
        EXPECT_EQ(s.mHostileQueries, 0);
    }

    TEST(SmartMapPolicyTest, InteriorSelectionDoesNotDependOnExteriorRulesOrSpeed)
    {
        Scenario s;
        s.mEntry = { false, false, 1000000.f, true };
        s.mOptions.mExteriors = false;
        EXPECT_EQ(s.evaluate(), View::Local);
        s.mOptions.mInteriors = false;
        EXPECT_EQ(s.evaluate(), View::Unchanged);
        EXPECT_EQ(s.mDoorQueries, 0);
        EXPECT_EQ(s.mHostileQueries, 0);
    }

    TEST(SmartMapPolicyTest, ExteriorRuleCanBeDisabledIndependently)
    {
        Scenario s;
        s.mOptions.mExteriors = false;
        EXPECT_EQ(s.evaluate(), View::Unchanged);
        EXPECT_EQ(s.mDoorQueries, 0);
        EXPECT_EQ(s.mHostileQueries, 0);
        s.mEntry.mExterior = false;
        EXPECT_EQ(s.evaluate(), View::Local);
    }

    TEST(SmartMapPolicyTest, DoorThresholdIsInclusiveAndStopsBeforeActorQuery)
    {
        Scenario s;
        s.mDoors = 3;
        EXPECT_EQ(s.evaluate(), View::World);
        EXPECT_EQ(s.mHostileQueries, 1);
        s.mDoors = 4;
        EXPECT_EQ(s.evaluate(), View::Local);
        EXPECT_EQ(s.mRequestedThreshold, 4);
        EXPECT_EQ(s.mHostileQueries, 1);
        s.mDoors = 50;
        EXPECT_EQ(s.evaluate(), View::Local);
        EXPECT_EQ(s.mHostileQueries, 1);
    }

    TEST(SmartMapPolicyTest, DoorSliderEndpointsReachTheReferenceQuery)
    {
        Scenario s;
        s.mOptions.mDoorThreshold = 1;
        s.mDoors = 1;
        EXPECT_EQ(s.evaluate(), View::Local);
        EXPECT_EQ(s.mRequestedThreshold, 1);
        s.mOptions.mDoorThreshold = 50;
        s.mDoors = 49;
        EXPECT_EQ(s.evaluate(), View::World);
        EXPECT_EQ(s.mRequestedThreshold, 50);
        s.mDoors = 50;
        EXPECT_EQ(s.evaluate(), View::Local);
    }

    TEST(SmartMapPolicyTest, EnabledHostileRuleSelectsLocalInSparseExterior)
    {
        Scenario s;
        s.mHostile = true;
        EXPECT_EQ(s.evaluate(), View::Local);
        EXPECT_EQ(s.mHostileQueries, 1);
        s.mOptions.mHostiles = false;
        EXPECT_EQ(s.evaluate(), View::World);
        EXPECT_EQ(s.mHostileQueries, 1);
    }

    TEST(SmartMapPolicyTest, RestForbiddenSkipsHostileQueryButPreservesDoorRule)
    {
        Scenario s;
        s.mEntry.mRestForbidden = true;
        s.mHostile = true;
        EXPECT_EQ(s.evaluate(), View::World);
        EXPECT_EQ(s.mHostileQueries, 0);
        s.mDoors = 4;
        EXPECT_EQ(s.evaluate(), View::Local);
        EXPECT_EQ(s.mHostileQueries, 0);
    }

    TEST(SmartMapPolicyTest, FastExteriorMovementIsSuppressedAtInclusiveBoundary)
    {
        Scenario s;
        s.mEntry.mSpeedSquared = std::nextafter(300.f * 300.f, 0.f);
        EXPECT_EQ(s.evaluate(), View::World);
        s.mEntry.mSpeedSquared = 300.f * 300.f;
        EXPECT_EQ(s.evaluate(), View::Unchanged);
        s.mEntry.mSpeedSquared = 1000000.f;
        EXPECT_EQ(s.evaluate(), View::Unchanged);
        EXPECT_EQ(s.mDoorQueries, 1);
        EXPECT_EQ(s.mHostileQueries, 1);
    }

    TEST(SmartMapPolicyTest, TeleportArrivalDoesNotInheritMovementSuppression)
    {
        Scenario s;
        s.mEntry = { true, true, 1000000.f, false };
        s.mDoors = 4;
        EXPECT_EQ(s.evaluate(), View::Local);
        EXPECT_EQ(s.mDoorQueries, 1);
        s.mDoors = 0;
        EXPECT_EQ(s.evaluate(), View::World);
    }

    TEST(SmartMapPolicyTest, HostileFightAndDistanceThresholdsAreInclusive)
    {
        constexpr float boundary = 3500.f * 3500.f;
        EXPECT_TRUE(withinHostileThreshold(82, 82, boundary, 3500));
        EXPECT_FALSE(withinHostileThreshold(81, 82, boundary, 3500));
        EXPECT_FALSE(withinHostileThreshold(100, 82, std::nextafter(boundary, INFINITY), 3500));
        EXPECT_TRUE(withinHostileThreshold(100, 100, 0.f, 1000));
        EXPECT_TRUE(withinHostileThreshold(78, 78, 7000.f * 7000.f, 7000));
    }

    TEST(SmartMapPolicyTest, InvalidActorDistanceCannotTriggerLocalMap)
    {
        EXPECT_FALSE(withinHostileThreshold(100, 82, -1.f, 3500));
        EXPECT_FALSE(withinHostileThreshold(100, 82, std::numeric_limits<float>::infinity(), 3500));
        EXPECT_FALSE(withinHostileThreshold(100, 82, std::numeric_limits<float>::quiet_NaN(), 3500));
    }

    TEST(SmartMapLifecycleTest, CellDecisionWaitsForNextScriptFrameAndRunsOnce)
    {
        PendingUpdate pending;
        EXPECT_FALSE(pending.nextFrame());
        pending.queue();
        EXPECT_FALSE(pending.nextFrame());
        EXPECT_TRUE(pending.nextFrame());
        for (int frame = 0; frame < 1000; ++frame)
            EXPECT_FALSE(pending.nextFrame());
    }

    TEST(SmartMapLifecycleTest, NewArrivalReplacesDeferredDecision)
    {
        PendingUpdate pending;
        pending.queue();
        EXPECT_FALSE(pending.nextFrame());
        pending.queue();
        EXPECT_FALSE(pending.nextFrame());
        EXPECT_TRUE(pending.nextFrame());
        EXPECT_FALSE(pending.nextFrame());
    }

    TEST(SmartMapLifecycleTest, ManualSelectionCancelsPendingAutomaticSwitch)
    {
        PendingUpdate pending;
        pending.queue();
        EXPECT_FALSE(pending.nextFrame());
        pending.cancel();
        EXPECT_FALSE(pending.nextFrame());
        EXPECT_FALSE(pending.nextFrame());
        pending.queue();
        EXPECT_FALSE(pending.nextFrame());
        EXPECT_TRUE(pending.nextFrame());
    }

    TEST(SmartMapLifecycleTest, ResetDropsOldSaveDecisionAndAcceptsNewArrival)
    {
        PendingUpdate pending;
        pending.queue();
        pending.cancel();
        EXPECT_FALSE(pending.nextFrame());
        pending.queue();
        EXPECT_FALSE(pending.nextFrame());
        EXPECT_TRUE(pending.nextFrame());
    }

    TEST(SmartMapLocationNameTest, RecognizesGenericInternalPrefixesWithoutModNames)
    {
        EXPECT_TRUE(isInternalLocationName("AB_Preview_001"));
        EXPECT_TRUE(isInternalLocationName("CD23_Unfinished Interior"));
        EXPECT_TRUE(isInternalLocationName("ABCDEFG8_placeholder"));
    }

    TEST(SmartMapLocationNameTest, PreservesReadableNamesAndAmbiguousIdentifiers)
    {
        for (std::string_view name : { "", "Balmora", "Balmora, Guild of Mages", "A_Building", "ab_preview",
                 "12_House", "AB_", "ABCDEFGHI_Name", "Temple_Ancestral", "Temple AB_Name", "\xC3\x89_Name" })
            EXPECT_FALSE(isInternalLocationName(name)) << name;
    }
}
