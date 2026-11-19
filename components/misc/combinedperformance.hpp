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

    inline bool useSparsePostponedPhysics()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_SPARSE_POSTPONED_PHYSICS");
        return useCombinedPerformance() && enabled;
    }

    inline bool usePreparedCollisionScale()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_PREPARED_COLLISION_SCALE");
        return useCombinedPerformance() && enabled;
    }

    inline bool useCellObjectIndex()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_CELL_OBJECT_INDEX");
        return useCombinedPerformance() && enabled;
    }

    inline bool usePreloadPlanCache()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_PRELOAD_PLAN_CACHE");
        return useCombinedPerformance() && enabled;
    }

    inline bool usePreloadRetention()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_PRELOAD_RETENTION");
        return useCombinedPerformance() && enabled;
    }

    inline bool useSlicedCleanup()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_SLICED_CLEANUP");
        return useCombinedPerformance() && enabled;
    }

    inline bool useCostAwarePreload()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_COST_AWARE_PRELOAD");
        return useCellRenderPreparation() && enabled;
    }

    inline bool useGranularCompile()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_GRANULAR_COMPILE");
        return useCombinedPerformance() && enabled;
    }

    inline bool useAsyncObjectPaging()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_ASYNC_OBJECT_PAGING");
        return useCombinedPerformance() && enabled;
    }

    // Experimental unmerged cover regressed dense scenery. Keep it available
    // for diagnostics only; normal preparation uses complete optimized chunks.
    inline bool useSceneryCoverage()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_SCENERY_COVERAGE");
            return value && std::string_view(value) == "1";
        }();
        return useAsyncObjectPaging() && enabled;
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

    inline bool useLuaPrototypeCache()
    {
        static const bool enabled = enabledUnlessZero("OPENMW_LUA_PROTOTYPE_CACHE");
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
