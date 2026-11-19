# Transition preload changes

Install the resulting engine build only. No Inventory Extender or Frame Generation update. Existing preload settings remain authoritative; no quality, draw distance, collision, navigation or script functionality is disabled.

## Findings and changes

Exterior grid activation scheduled terrain work but did not explicitly schedule destination cell mesh/shape preparation before waiting for terrain and inserting objects. Predictive door preloads might already cover those cells; direct teleports often have no predictive opportunity. The new path queues the sorted destination cells through the existing bounded CellPreloader before the terrain wait. Existing cache hits only refresh timestamps. New jobs use the established resource workers; CellStore access, rendering/physics insertion, respawn and scripts remain on the main thread. No new resource synchronization scheme is introduced. This may overlap resource work with the existing terrain job; the actual gain depends on cache state and the user's transition bottleneck.

Door/fast-travel surroundings previously truncated their desired cell list to remaining cache capacity. When full, the list became empty, preventing refresh of cells already present and allowing relevant entries to expire. The list now uses total configured capacity, and existing CellPreloader admission/eviction rules enforce the limit. A zero-capacity guard prevents selecting an oldest entry from an empty cache. No unbounded cache growth.

Ordinary openmw.log now records exterior/interior transition totals, old-cell unload, cell-record access, terrain setup/wait, resource scheduling, paged physics, cell insertion, navigation and player setup. Object insertion records collection, rendering/physics and navigation phases. Timers are per phase, not per object. These totals are nested and should not be summed together. They do not include later asynchronous work or script handlers executed after the transition function returns.

## Validation

`python3 custom/performance-tests/test-transition-preload.py` compiles and executes production preload admission and surroundings scheduling bodies with counted doubles. Covers a full nine-cell cache, repeat timestamp refresh with no new jobs, refusal of new destinations while entries are recent, old-entry eviction, small and zero capacity, and interior behavior. The original surroundings implementation reproduces the failed timestamp assertion. Source-order checks confirm resource scheduling precedes terrain wait and scene insertion and respects preload enablement. Existing resource-preload tests remain in CI.

This is not an OSG/Bullet concurrency test or an in-game benchmark. Full Windows compilation and user runtime tests are required. Potential cost: scheduling new jobs itself scans destination model lists and may contend with existing work. The new log separates scheduling cost from terrain wait and insertion so regressions are visible. Actual scene/quest/navmesh behavior must be checked live.

## Focused test

1. Load your usual early save. Leave Seyda Neen's Census and Excise Office through the normal door.
2. Re-enter and leave again. First and repeat transitions can differ because caches warm up.
3. Use the same distant `coc` destination that normally stalls; note the exact destination. Repeat once.
4. Check ground collision, nearby NPC movement, doors and normal scripts at the destination.
5. Exit and send openmw.log. No separate profiler or old build run is required. Report whether the pause happens before the loading display, during it, or just after control returns.

The previously confirmed metadata-only save-load optimization remains included. No live speedup has been measured for this transition change at commit time.
