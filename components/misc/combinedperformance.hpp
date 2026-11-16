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

    inline bool useCellRenderPreparation()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_CELL_RENDER_PREPARATION");
        return useCombinedPerformance() && enabled;
    }

    inline bool useAsyncObjectPaging()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_ASYNC_OBJECT_PAGING");
        return useCombinedPerformance() && enabled;
    }

    inline bool useActorResourcePreload()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_ACTOR_RESOURCE_PRELOAD");
        return useCombinedPerformance() && enabled;
    }

    inline bool useSharedSandboxPackages()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_SHARED_SANDBOX_PACKAGES");
        return useCombinedPerformance() && enabled;
    }

    inline bool useResponsiveArrivalCancellation()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_RESPONSIVE_ARRIVAL_CANCELLATION");
        return useCombinedPerformance() && enabled;
    }

    inline bool useArrivalCompilePriority()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_ARRIVAL_COMPILE_PRIORITY");
        return useCombinedPerformance() && enabled;
    }

    inline bool useSharedPositionReads()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_SHARED_POSITION_READS");
        return useNativeJobs() && enabled;
    }

    inline bool useActorSpatialQueries()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_ACTOR_SPATIAL_QUERIES");
        return useCombinedPerformance() && enabled;
    }

    inline bool useSpatialDistanceQueries()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_SPATIAL_DISTANCE_QUERIES");
        return useNativeJobs() && enabled;
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
