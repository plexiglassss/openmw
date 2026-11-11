#ifndef OPENMW_COMPONENTS_MISC_COMBINEDPERFORMANCE_H
#define OPENMW_COMPONENTS_MISC_COMBINEDPERFORMANCE_H

#include <cstdlib>
#include <string_view>

namespace Misc
{
    // Process-local rollback/comparison switch for the combined native work.
    // Earlier accepted engine features and performance changes are unaffected.
    inline bool useCombinedPerformance()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_COMBINED_PERFORMANCE");
            return value == nullptr || std::string_view(value) != "0";
        }();
        return enabled;
    }
    inline bool useNativeJobs()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_NATIVE_JOBS");
            return value == nullptr || std::string_view(value) != "0";
        }();
        return enabled && useCombinedPerformance();
    }

    inline bool useTownFrameUpdates()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_TOWN_FRAME_UPDATES");
            return useCombinedPerformance() && (value == nullptr || std::string_view(value) != "0");
        }();
        return enabled;
    }

    inline bool useSharedActiveSpellReads()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_SHARED_ACTIVE_SPELL_READS");
            return useCombinedPerformance() && (value == nullptr || std::string_view(value) != "0");
        }();
        return enabled;
    }

    inline bool enabledUnlessZero(const char* name)
    {
        const char* value = std::getenv(name);
        return !value || std::string_view(value) != "0";
    }

    inline bool useBatchedVisualRays()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_BATCHED_VISUAL_RAYS");
        return useCombinedPerformance() && enabled;
    }

    inline bool useAsyncDistanceJobs()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_ASYNC_DISTANCE_JOBS");
        return useNativeJobs() && enabled;
    }

    inline bool useIsolatedLuaWorkers()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_ISOLATED_LUA_WORKERS");
        return useNativeJobs() && enabled;
    }

    inline bool useSparseUiUpdates()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_SPARSE_UI_UPDATES");
            return useTownFrameUpdates() && (value == nullptr || std::string_view(value) != "0");
        }();
        return enabled;
    }
}

#endif
