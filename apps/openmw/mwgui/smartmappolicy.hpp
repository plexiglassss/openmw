#ifndef OPENMW_MWGUI_SMARTMAPPOLICY_HPP
#define OPENMW_MWGUI_SMARTMAPPOLICY_HPP

#include <cmath>
#include <string_view>

namespace MWGui::SmartMap
{
    enum class View
    {
        Unchanged,
        Local,
        World,
    };

    struct Options
    {
        bool mEnabled;
        bool mInteriors;
        bool mExteriors;
        int mDoorThreshold;
        bool mHostiles;
    };

    struct Entry
    {
        bool mExterior;
        bool mTeleported;
        float mSpeedSquared;
        bool mRestForbidden;
    };

    // Queries are lazy: interiors, disabled rules and fast exterior travel do
    // not enumerate references. A sufficient door count avoids the actor query.
    template <class CountDoors, class NearbyHostile>
    View chooseView(const Options& options, const Entry& entry, CountDoors&& countDoors, NearbyHostile&& nearbyHostile)
    {
        if (!options.mEnabled)
            return View::Unchanged;
        if (!entry.mExterior)
            return options.mInteriors ? View::Local : View::Unchanged;
        if (!options.mExteriors || (!entry.mTeleported && entry.mSpeedSquared >= 300.f * 300.f))
            return View::Unchanged;
        if (countDoors(options.mDoorThreshold) >= options.mDoorThreshold)
            return View::Local;
        if (options.mHostiles && !entry.mRestForbidden && nearbyHostile())
            return View::Local;
        return View::World;
    }

    inline bool withinHostileThreshold(int fight, int minimumFight, float distanceSquared, int maximumDistance)
    {
        const float maximumDistanceSquared = static_cast<float>(maximumDistance) * maximumDistance;
        return fight >= minimumFight && std::isfinite(distanceSquared) && distanceSquared >= 0
            && distanceSquared <= maximumDistanceSquared;
    }

    // A generic, conservative identifier shape. This does not name a mod or
    // alter any CELL record; readable names and lowercase prefixes survive.
    inline bool isInternalLocationName(std::string_view name)
    {
        const auto separator = name.find('_');
        if (separator < 2 || separator > 8 || separator + 1 >= name.size())
            return false;
        bool hasLetter = false;
        for (const char c : name.substr(0, separator))
        {
            if (c >= 'A' && c <= 'Z')
                hasLetter = true;
            else if (c < '0' || c > '9')
                return false;
        }
        return hasLetter;
    }

    // Cell changes can precede player-pointer replacement and local scripts.
    // Wait one GUI update, then consume once even while the map is hidden.
    class PendingUpdate
    {
    public:
        void queue()
        {
            mPending = true;
            mDefer = true;
        }
        void cancel()
        {
            mPending = false;
            mDefer = false;
        }
        bool nextFrame()
        {
            if (!mPending)
                return false;
            if (mDefer)
            {
                mDefer = false;
                return false;
            }
            mPending = false;
            return true;
        }

    private:
        bool mPending = false;
        bool mDefer = false;
    };
}

#endif
