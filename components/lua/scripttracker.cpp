#include "scripttracker.hpp"

#include <cmath>

namespace LuaUtil
{
    namespace
    {
        constexpr unsigned sMinLoadedFrames = 50;
        constexpr unsigned sMaxLoadedFrames = 600;
        constexpr unsigned sUsageFrameGrowth = 10;
        constexpr std::size_t sMinToProcess = 1;
        constexpr std::size_t sToProcessDiv = 20; // 5%
    }

    ScriptTracker::ScriptTracker(double inactiveCacheDuration, std::uint64_t cacheMemoryThreshold)
        : mInactiveCacheDuration(
            std::isfinite(inactiveCacheDuration) && inactiveCacheDuration > 0 ? inactiveCacheDuration : 0)
        , mCacheMemoryThreshold(cacheMemoryThreshold)
    {
    }

    void ScriptTracker::onLoad(ScriptsContainer& container)
    {
        mLoadedScripts.push(
            { container.getWeakPointer(), sMinLoadedFrames + mFrame, mElapsedSeconds + mInactiveCacheDuration });
    }

    void ScriptTracker::unloadInactiveScripts(LuaView& lua, double frameDuration)
    {
        if (std::isfinite(frameDuration) && frameDuration > 0)
            mElapsedSeconds += frameDuration;

        // memory_used uses the Lua GC counters, so this also works with the Lua profiler disabled.
        // Under pressure we resume the existing policy, without evicting active scripts or forcing GC.
        const bool retainInactive = mInactiveCacheDuration > 0 && mCacheMemoryThreshold > 0
            && lua.sol().memory_used() < mCacheMemoryThreshold;

        // This code is technically incorrect if mFrame overflows... but at 300fps that takes about half a year
        std::size_t toProcess = std::max(mLoadedScripts.size() / sToProcessDiv, sMinToProcess);
        while (toProcess && !mLoadedScripts.empty())
        {
            --toProcess;
            auto [ptr, ttl, retainUntil] = std::move(mLoadedScripts.front());
            mLoadedScripts.pop();
            ScriptsContainer* container = *ptr;
            // Object no longer exists, cease tracking
            if (!container)
                continue;
            // Ignore activity of local scripts in the active grid
            if (container->isActive())
            {
                ttl = std::max(ttl, mFrame + sMinLoadedFrames);
                retainUntil = mElapsedSeconds + mInactiveCacheDuration;
            }
            else
            {
                bool activeSinceLastPop = container->mRequiredLoading;
                if (activeSinceLastPop)
                {
                    container->mRequiredLoading = false;
                    ttl = std::min(ttl + sUsageFrameGrowth, mFrame + sMaxLoadedFrames);
                    retainUntil = mElapsedSeconds + mInactiveCacheDuration;
                }
                else if (ttl < mFrame && (!retainInactive || retainUntil <= mElapsedSeconds))
                {
                    container->ensureUnloaded(lua);
                    continue;
                }
            }
            mLoadedScripts.push({ std::move(ptr), ttl, retainUntil });
        }
        ++mFrame;
    }
}
