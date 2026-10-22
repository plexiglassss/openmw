#ifndef OPENMW_COMPONENTS_CRASHCATCHER_FREEZEWARNINGTIMER_HPP
#define OPENMW_COMPONENTS_CRASHCATCHER_FREEZEWARNINGTIMER_HPP

#include <chrono>
#include <optional>

namespace Crash
{
    class FreezeWarningTimer
    {
        using Clock = std::chrono::steady_clock;
        std::optional<Clock::time_point> mSince;

    public:
        bool update(bool unresponsive, Clock::time_point now)
        {
            if (!unresponsive)
            {
                mSince.reset();
                return false;
            }
            if (!mSince)
                mSince = now;
            return now - *mSince >= std::chrono::seconds(60);
        }
    };
}
#endif
