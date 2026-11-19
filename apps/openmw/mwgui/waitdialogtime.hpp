#ifndef MWGUI_WAIT_DIALOG_TIME_H
#define MWGUI_WAIT_DIALOG_TIME_H

#include "../mwworld/timestamp.hpp"

namespace MWGui
{
    struct RestTimePreview
    {
        int mHour12;
        int mMinute;
        bool mPm;
        bool mNextDay;
    };

    inline RestTimePreview getRestTimePreview(const MWWorld::TimeStamp& currentTime, int hours)
    {
        MWWorld::TimeStamp endTime = currentTime;
        // Waiting advances the game clock one hour at a time. Preserve that
        // normalization and float rounding rather than adding the whole duration.
        for (int i = 0; i < hours; ++i)
            endTime += 1;

        const int minutes = static_cast<int>(endTime.getHour() * 60.f);
        const int hour = minutes / 60;
        return { hour % 12 == 0 ? 12 : hour % 12, minutes % 60, hour >= 12, endTime.getDay() > currentTime.getDay() };
    }
}

#endif
