#include <gtest/gtest.h>

#include <cmath>

#include "apps/openmw/mwgui/waitdialogtime.hpp"

namespace MWGui
{
    namespace
    {
        void expectTarget(float currentHour, int hours, int hour12, int minute, bool pm, bool nextDay)
        {
            const MWWorld::TimeStamp currentTime(currentHour, 42);
            const RestTimePreview target = getRestTimePreview(currentTime, hours);
            EXPECT_EQ(target.mHour12, hour12);
            EXPECT_EQ(target.mMinute, minute);
            EXPECT_EQ(target.mPm, pm);
            EXPECT_EQ(target.mNextDay, nextDay);
            EXPECT_EQ(currentTime.getDay(), 42);
            EXPECT_EQ(currentTime.getHour(), currentHour);
        }

        TEST(RestTimeTest, PreservesFractionalHours)
        {
            expectTarget(6.5f, 8, 2, 30, true, false);
            expectTarget(20.25f, 9, 5, 15, false, true);
            expectTarget(0.75f, 1, 1, 45, false, false);
        }

        TEST(RestTimeTest, DistinguishesNoonAndMidnight)
        {
            expectTarget(11.f, 1, 12, 0, true, false);
            expectTarget(23.f, 1, 12, 0, false, true);
            expectTarget(0.f, 12, 12, 0, true, false);
            expectTarget(12.f, 12, 12, 0, false, true);
        }

        TEST(RestTimeTest, FullDayRetainsTimeButMarksNextDay)
        {
            expectTarget(0.f, 24, 12, 0, false, true);
            expectTarget(7.5f, 24, 7, 30, false, true);
            expectTarget(23.75f, 24, 11, 45, true, true);
        }

        TEST(RestTimeTest, DoesNotRoundTheLastMinuteUpToMidnight)
        {
            expectTarget(std::nextafter(24.f, 0.f), 0, 11, 59, true, false);
            expectTarget(std::nextafter(24.f, 0.f), 1, 12, 59, false, true);
        }

        TEST(RestTimeTest, UsesTheSameRoundingAsHourlyClockAdvancement)
        {
            // A single 12-hour addition ends just before midnight here, but
            // repeated one-hour advances round to the next day in the game.
            expectTarget(std::nextafter(12.f, 0.f), 12, 12, 0, false, true);
        }
    }
}
