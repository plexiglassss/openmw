# OpenMW parallel native jobs

This Windows build combines the `5e66b560` native-jobs engine and its `a0395b81` distribution with configurable location rendering profiles.

## Horror Mod lighting

The bundled profile is an editable file at `resources/vfs/shader_profiles/horror_mod.yaml`. Extracting the full package makes it available automatically. With `Horror Mod.esp` active and **Options → Video → Lights → Location Rendering Profiles** enabled, the listed interiors use the four shader values in the file. Other cells use your normal settings. The switch is on by default.

To customize the interiors or shader values, edit the YAML while OpenMW is closed and restart the game. A copy at `shader_profiles/horror_mod.yaml` in a higher-priority mod folder overrides the bundled copy and can be kept across engine upgrades. Set `enabled: false` to disable this profile alone.

## Using the engine

1. Extract the complete engine into a separate folder. If you use the Rubic0n Lua runtime, copy your matching `lua51.dll` and `resources/lua_libs/content.lua` into it together. Carry over your custom engine configuration and optional frame-generation files if you normally use them.
2. Launching `openmw.exe` enables the candidate by default. To record the usual town route and a cell transition, use `Run-Combined.cmd`. Close OpenMW normally to complete the capture.
3. To compare this actor and HUD change with the last native-jobs release, use `Run-Previous-Frame-Updates.cmd` with the same save and route. `Run-Inherited.cmd` is the older comparison with both the native jobs and the new frame updates disabled for that launch.
4. Each optional adapter has its own `Install.cmd` under `tools/combined-performance`. If you use all four mods, `Install-Native-Job-Adapters.cmd` runs them in sequence. Each component verifies the winning script hashes and captured installation context and backs up its originals. Components already installed are reported. Use each component's `Restore.cmd` for rollback. All four are optional for engine operation.

## Engine work

OpenMW checks which actors could collide before running the existing avoidance tests. A sorted position snapshot excludes distant actors and returns possible neighbours in their original order. OpenMW reads movement and applies corrections on the mechanics thread. Crowded groups fall back to the full scan. This path follows the existing NPC collision avoidance setting and is used in TES3 towns.

During object paging, the resource system can decode batches of uncached NIF files on dedicated workers. The paging thread selects the final model paths, publishes results and constructs rendering objects. Failed reads follow the existing template/error-marker path. The worker pool runs bounded chunks and waits before returning to the owner. Small batches and systems with no available workers execute synchronously.

Lua distance queries take a fresh copy of positions for each call. Multi-type cell queries share the existing typed snapshots and their invalidation rules. Active spell source queries can reuse a result when the spell, caster registry and reference revisions still match. Magnitudes are read live.

Four workers are configured by default. In `settings.cfg`, the `[General]` entries `native jobs enabled`, `native jobs threads`, and `native jobs minimum items` control activation, worker count (0–16), and the grain for lightweight distance arithmetic. A count of zero gives the synchronous kernels. Heavy NIF and actor-neighbour jobs use their own grain. Worker totals are recorded at shutdown.

## Optional scripts

| Mod | Behavior |
| --- | --- |
| Proximity Tool | Computes 3D distance, planar distance and signed height in one native call at the existing marker method. Selection runs once at its original point. |
| Advanced World Map | Batches distance filtering for known teleport doors and combines typed Door/NPC retrieval. |
| Quest Guider Lite | Combines typed lists during synchronous quest-giver scans. Yielding background scans fetch their next group after the checkpoint. |
| Sane Magic | Uses the two previously accepted effect-query scripts. Repeated reads can use the engine's new revision checks. |

The capture launchers use the same executable. A measured town FPS increase and end-to-end cell-transition improvement require a comparison on the actual modlist.

## Town frame updates

Run `Run-Combined.cmd` from the extracted engine folder. This enables prepared onUpdate callbacks, direct reads of modified actor stats, reuse of repeated actor type checks and retention of stable HUD widget attachments. Each works with ordinary scripts. Existing mod adapters are optional.

For a same-executable comparison with the previous town update code, run `Run-Previous-Frame-Updates.cmd` with the same save and route. Both launchers keep native jobs enabled. Each capture records both switches. Open inventory twice in Seyda Neen if comparing the menu hitches as well.

## Sparse HUD updates

The engine speeds up ordinary property reads and avoids repeating a native alpha update for a widget whose inheritance flag already matches. Mods can also pass the actual changed inline layout tables to `Element:updateProperties`. The engine reapplies their properties, recalculates enclosing sizes, and performs one final root layout for each queued element. Full requests supersede narrower requests.

For the repaired Proximity Tool setup, add `tools/combined-performance/Proximity Tool - Sparse HUD` after the current Proximity scripts in Data Directories. The supplied menu uses this API when only displayed distance labels or direction arrows change. Use `Run-Combined.cmd` for the default behavior and `Run-Previous-HUD.cmd` for the same executable with the previous HUD path. Keep the overlay enabled in both runs.

## Ordinary active-spell reads

The standard Lua actor API shares immutable spell names and effect lists across equivalent reads. Each result keeps the values it saw, including the duration and magnitude of its effects. Each access to `spell.effects` produces a fresh Lua table. Mods use their existing scripts and calls.

Use `Run-Combined.cmd` for the enabled mode. For a comparison on the same executable, `Run-Previous-Spell-Reads.cmd` copies the details on every read. Both use the earlier town, HUD and native-job improvements. The earlier HUD and town comparison launchers keep spell sharing enabled, so those comparisons still isolate their original settings. The inherited mode disables it. With frame capture enabled, `openmw-stats.log` records spell scans, empty scans, yielded entries, lookups, copied native effects, reuse and effect tables exported to Lua for each frame. The paired native benchmark is in `active-spell-reads.json`.

## Parallel rendering queries and Lua work

Run `Run-Combined.cmd` for all changes. `Run-Previous-Parallel-Queries.cmd` runs the same executable with all three new features disabled and the previous shared spell reads, HUD and town changes enabled. `Run-Without-Ray-Batching.cmd`, `Run-Without-Async-Distances.cmd` and `Run-Without-Lua-Workers.cmd` each disable only the named feature. All five create the existing frame capture. With frame generation installed, keep its setting the same between runs. The capture includes engine-side FPS and counts of submitted worker jobs, visual rays, shared traversals and saved traversals.

Existing `nearby.asyncCastRenderingRay` callers benefit automatically when adjacent requests share a scene state, eye position and ignore list. Equal endpoints use one intersection. Requests across other delayed engine actions remain separate. Rendering runs on the scene owner. The callbacks run in their original order.

### Optional gameplay integrations

Under `tools/combined-performance/Parallel Query Adapters` are two independent data folders. Put each folder after the corresponding installed mod and its native-job adapter in the OpenMW data directory list. Each contains the complete replacement scripts for the files it overrides. Keep both original mods and their data enabled. Removing the additional directory restores the previous scripts. The two upgrade folders target the Advanced World Map and Quest Guider Lite scripts in this cumulative series.

* Advanced World Map checks groups of 24 or more nearby teleport doors on a worker at its periodic discovery tick. It keeps the sampled origin and radius, applies eligible discoveries on the owner, and resolves pending work when opening the map or saving. Immediate interactions and smaller lists use the existing path. Test by approaching a previously undiscovered destination, then opening the map or saving immediately. A different cell discards an outstanding sample from the old cell.
* Quest Guider Lite sorts groups totalling at least 128 candidate positions when filling quest details. Only distances and row indices cross the thread boundary. The menu and request ID still select the result. Small requests or a full queue use the original comparator on the scripting thread. Open a quest with many candidate positions, change selection, and close/reopen the journal. Markers and distance order should match the comparison run.

### Generic script interfaces

`nearby.snapshotPositions(objectsOrVectors)` copies live positions once. `nearby.queryDistancesAsync(snapshotOrTargets, origin, options)` submits owned data. Maximum input is 16,384 targets per snapshot. The existing `maxDistance`, `filterBy2D` and `sortByDistance` options apply. `nearby.queryDistances(snapshot, origin, options)` evaluates the same retained sample immediately. Captured positions remain historical if the underlying objects move. Existing object handles keep their normal validity rules.

`core.loadWorker("scripts.my_mod.worker")` reads a named Lua module through the active VFS on the owner. `core.defineWorker(text)` accepts equivalent text directly. The module returns a function. The returned submit function takes one dense array of at most 16,384 numbers. Each accepted job runs in a fresh, independent Lua state on the native pool. Its function returns one dense number array. Available library calls are `table.sort`, `math.abs`, `math.ceil`, `math.floor`, `math.huge`, `math.max`, `math.min` and `math.sqrt`. Source is limited to 32 KiB, VM allocation to 8 MiB and execution to approximately two million Lua instructions. Parallelism applies to opted-in data processing. Ordinary engine callbacks, objects and mutable game state stay with their owners.

Both submit functions return a ticket or `nil` when capacity is unavailable. `ticket:poll()` returns `pending`, `ready` plus its one-shot result, `error` plus its message, `cancelled`, or `consumed`. Call `ticket:cancel()` to abandon work. Up to 16 outstanding results share the bounded service. Publication follows submission order. Starting/loading a game or reloading scripts invalidates previous tickets and position snapshots. Missing API functions and a declined job both allow scripts to use their synchronous implementation.


## Town frame times, exterior loading and the first frame after a save

Launch `openmw.exe` to play. Hidden script settings groups now build their widgets when first opened. Static Lua labels reuse their text layout when the caption, font and line settings still match. Foreground graphics compilation uses the configured frame target and prepares at most two background GL objects per pass. The loading screen keeps its existing higher allowance. Background terrain composite textures can resume between layers. Visible textures retain priority.

Model preparation reuses the material shader on drawables that inherit a single unaliased state owner. Each geometry still prepares its own UVs and tangents. Automatic texture discovery caches the full file name and configured suffix, including missing companions, until the VFS index changes. A shader lookup can inspect the full define map without copying it on a hit.

Whole-world Lua record lookups preserve the original cell and reference order. For an unloaded TES3 cell with no possible matching placement or live reference, the engine can answer without rereading its content. Saved runtime references, unindexed cells and other game formats retain their existing lookup paths.

For one follow-up with script and frame timings, run `Run-Detailed-Town.cmd`, load the same save, take the same route and press Ctrl+Alt+5 on inventory open. Quit normally to finish the capture. The OSG log records the time spent in the GL incremental compile operation, in seconds. Detailed callback timing adds overhead; use `Run-Combined.cmd` for comparable FPS.

### Exterior transitions and loading a save

The terrain preloader prepares distinct NIF models on the native workers while the terrain thread assembles the scene in reference order. The regular model request owns cache publication and graphics compilation. Cancelling a destination discards unfinished chunks. Foreground traversal retains its existing NIF decoding path.

Use `Run-Combined.cmd` on the same save and route for before/after comparison. In `openmw.log`, compare the whole exterior transition and `mainWaitMs`. The `pagedBatchWallMs` timer includes ordered consumption and overlapping preparation. The `pagedModelCpuMs`, `pagedModelLoadCpuMs` and `pagedModelShaderCpuMs` timers sum helper work. They can overlap, and adding them to the wall time double-counts work. The save-loading player detail reports the time spent building the inventory list and preview, updating spells, and applying magic effects.
