#ifndef OPENMW_COMPONENTS_SCENEUTIL_PRELOADRETENTION_H
#define OPENMW_COMPONENTS_SCENEUTIL_PRELOADRETENTION_H

#include <algorithm>
#include <cmath>

namespace SceneUtil
{
    // Recency remains the reuse estimate. Completed preparation earns at most
    // two seconds of protection, so expensive obsolete cells cannot stay pinned.
    // The caller still enforces the original expiry and hard entry-count bound.
    inline double preloadRetentionBonus(double successfulWorkSeconds)
    {
        return std::isfinite(successfulWorkSeconds) ? std::clamp(successfulWorkSeconds * 4.0, 0.0, 2.0) : 0.0;
    }

    template <class Iterator, class LastUse, class Cost>
    Iterator selectPreloadVictim(Iterator begin, Iterator end, double now, bool costAware, LastUse lastUse, Cost cost)
    {
        auto victim = end;
        double oldest = 0;
        for (auto it = begin; it != end; ++it)
        {
            const double used = lastUse(*it);
            if (!std::isfinite(used) || !(used + 1.0 < now))
                continue;
            const double score = used + (costAware ? preloadRetentionBonus(cost(*it)) : 0.0);
            if (victim == end || score < oldest)
            {
                victim = it;
                oldest = score;
            }
        }
        return victim;
    }
}

#endif
