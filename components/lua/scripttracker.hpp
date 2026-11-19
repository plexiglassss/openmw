#ifndef COMPONENTS_LUA_SCRIPTTRACKER_H
#define COMPONENTS_LUA_SCRIPTTRACKER_H

#include <cstdint>
#include <memory>
#include <queue>
#include <utility>

#include "scriptscontainer.hpp"

namespace LuaUtil
{
    class ScriptTracker
    {
        using Frame = unsigned int;
        struct TrackedScriptContainer
        {
            ScriptsContainerWeakPtr mPtr;
            Frame mTtl;
            double mRetainUntil;
        };
        std::queue<TrackedScriptContainer> mLoadedScripts;
        Frame mFrame = 0;
        double mElapsedSeconds = 0;
        double mInactiveCacheDuration;
        std::uint64_t mCacheMemoryThreshold;

    public:
        // The threshold applies to total Lua memory. It is an eviction trigger, not a hard memory cap.
        // A zero duration or threshold preserves the ordinary frame-based unloading policy.
        explicit ScriptTracker(double inactiveCacheDuration = 0,
            std::uint64_t cacheMemoryThreshold = std::uint64_t{ 2 } * 1024 * 1024 * 1024);

        void unloadInactiveScripts(LuaView& lua, double frameDuration = 0);

        void onLoad(ScriptsContainer& container);

        std::size_t size() const { return mLoadedScripts.size(); }
    };
}

#endif // COMPONENTS_LUA_SCRIPTTRACKER_H
