# Player setup and terrain-preload diagnostics

This change adds measurements to explain cold-start loading and the first interior-to-exterior transition. It does not claim a further loading-speed improvement. It extends cumulative commit `9d7aca37f84edc556646544f23ef5efd29e62fb6`, retaining the save/transition improvements and automatic post-load frame capture.

The October 25 cold-start log attributed 14.2603 seconds of a 20.9128-second save load to player setup/render/mechanics. The first exterior transition took 38.5118 seconds, including 35.0939 seconds in terrain waiting. Those broad intervals do not establish whether the delay comes from script initialization, model preparation, job queue latency, loading-screen callbacks, I/O, or another wait. This instrumentation splits the boundaries without changing their execution order.

## What the log records

`Save load player detail:` reports a total and these stages in milliseconds:

| Field | Boundary |
| --- | --- |
| `playerWorldSetupMs` | The full `World::setupPlayer()` call. |
| `playerBaseSetupMs` | Player creation/replacement, previous-controller cleanup and rendering setup before the explicit Lua setup call. This is nested within world setup. |
| `playerLuaSetupMs` | `LuaManager::setupPlayer()`, nested within world setup. |
| `playerRenderMs` | `World::renderPlayer()`. |
| `playerUiMs` | The window manager's player update. |
| `playerMechanicsMs` | `MechanicsManager::playerLoaded()`. |
| `playerCameraMs` | Existing vanity/first-person camera restoration. |

The original `player setup, render and mechanics` phase remains available for comparisons. Detailed stage times are inclusive: do not add world setup to its nested base/Lua stages.

`Terrain preload detail:` associates a stable job ID with:

| Field | Boundary |
| --- | --- |
| `enqueueToStartMs` | From just before `addWorkItem()` to entry into the worker measurement. Includes enqueue overhead/locking as well as queue latency. |
| `workMs` | The worker's view reset/preload loop. Ends before the existing `Reporter::complete()` call, excluding that call's mutex acquisition and notification. |
| `mainWaitMs` | Elapsed time inside the main thread's measured Reporter wait. |
| `initialLockMs` | Initial acquisition of the Reporter mutex. |
| `listenerMs` | Progress-range/progress callbacks under the existing Reporter lock, including any work those callbacks trigger. |
| `conditionMs` | Condition-variable waiting and mutex reacquisition. This is not pure sleeping time. |
| `progressUpdates`, `conditionWaits` | Callback-pair and wait-call counts. |
| `viewResetMs` | Reset of reused terrain views. |
| `quadTreeMs` | `ensureQuadTreeBuilt()`. |
| `viewSelectionMs` | View/grid setup and quadtree traversal. |
| `renderNodesMs` | Rendering-node preparation, including nested chunk calls. |
| `terrainChunksMs`, `objectChunksMs`, `groundcoverChunksMs` | Time inside each corresponding chunk manager on the captured worker thread. |
| `progressReportingMs` | Worker calls to `addTotal()` and `addProgress()`, including lock acquisition. Excludes `complete()`. |

Stage call counts include cache hits and early returns; they are not counts of successfully created chunks or cache misses. Repeated synchronization can report the same job ID: do not count it as new work twice. `waitRequested=true` means `isDone()` was false at the check; the job can finish before the actual wait. `abortRequested` records the observed flag, not proof of an early abort. Started/finished/exception flags describe the measurement lifecycle and exception unwinding.

All times are elapsed wall time, including blocking/I/O. Worker phases and main waiting overlap, so they must not be added together. These measurements alone do not establish CPU, disk, GPU or VRAM saturation. Unrelated resource jobs on other threads and asynchronous GPU completion are not individually attributed. A missing stage means no instrumented call was captured, not necessarily that the corresponding subsystem did no work anywhere.

## Execution and overhead

The original unmeasured `Reporter::wait()` remains unchanged. The new measured method preserves its lock ownership, callback order, condition wait and exception propagation. No additional resource-completion waits, thread joins, loading-screen redraws, queue reprioritization, cache invalidation, save serialization changes or gameplay changes are introduced.

Terrain metrics are captured for every terrain-preload job in this diagnostic build, including background preloads. A summary is logged when a job is observed by terrain synchronization; there is no per-chunk logging. Job snapshot locks are never held across loading callbacks or logging. Chunk timers outside a capture session perform no clock reads or mutex operations. Active captures add clock reads and small metric updates; their cost is not yet quantified on the user's Windows installation.

The existing post-load frame capture continues independently, measuring early engine frames after the save-load routine finishes. It does not detect scene presentation, and ordinary door transitions do not rearm it. Its bounded capture window does not limit these new terrain timings. Read `POST_LOAD_DIAGNOSTICS.md` for those separate records and limitations.

## Focused test

1. After the Windows build succeeds, install the complete build into its own folder and use the same configuration and save as before. Inventory Extender, its Bridge and mod plugins need no update for this change. Navmesh regeneration is not required.
2. For the cold-start case, restart Windows and use this build for the first game run. Keep OpenMW in the foreground through the load and transition. Load the same interior save and note when the scene becomes responsive, as well as when the loading screen disappears.
3. After the initial save becomes responsive, walk or turn in the foreground for 15–20 seconds so the post-load capture can complete. Then exit through the normal door into the exterior, let the transition finish, and check that movement and turning respond. The door transition records terrain timings separately. Exit normally and preserve `openmw.log`.
4. A second launch with the same build/save/route is useful to distinguish cold and warmed caches. Preserve its log separately. This does not require an old or unoptimized build, manual cache deletion, or a special console command.

The timings are emitted to the normal log without a timing environment variable or additional collector. A `coc` transition uses the same generic engine paths; no location or command has a special optimization.

## Verification and remaining work

Local GCC C++20 warning-as-error tests pass for the production recorder and Reporter. A separate reviewer independently compiled and ran both, checked engine call order, synchronization, lifecycle and interpretation limits, and found no blocking defect. An initial independent rerun was interrupted by workspace inode exhaustion; a later run using `TMPDIR=/dev/shm` passed without source changes. The Reporter test suppresses only unused-parameter warnings from unchanged default Listener methods.

Linux and Windows CI include both new tests along with all existing regressions. Full engine compilation and live timing results remain pending at source publication. Standalone test success does not establish the user's installed binary or a reduction in real loading time.

The accompanying scoped execution record retains the broader mod audit as open. Its released completion receipt is `INCOMPLETE`; no full installation audit, protected acceptance or live-gameplay pass is claimed. Unrelated geometry, dialogue, HUD-marker and mod compatibility work remains at its previous status.
