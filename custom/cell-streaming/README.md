# Exterior cell preparation

This cumulative build prepares detached rendering instances during normal cell preloading,
prioritizes the likely next exterior cells, and shares immutable Lua package bindings across
scripts belonging to the same container. All preceding cumulative engine features are retained.

Rendering preparation honors the existing `[Cells] preload instances` option. Required
templates and collision shapes are warmed first. Exterior statics are excluded from speculative
render copies because ObjectPaging normally owns their rendering. Actors still assemble and
activate on the owner thread; this does not pre-run scripts, respawn cells, or publish collision early.

The one-use render cache is capped at 512 entries including work in progress, 32 per template,
and 128 MiB of estimated clone data; individual estimates above 16 MiB retain the demand path.
The estimate includes internal rig/morph arrays but is not an exact process-memory limit.
Particles and unsupported third-party dynamic drawable layouts retain the normal demand path.
Cache keys include the exact template, model-settings revision and VFS revision. Expired tickets
are discarded. Consumed live scene nodes are not retained by preload tickets. A cache miss never
waits for speculative cloning. Particle initialization uses the current settings at consumption.
`NodeInstance` resource statistics expose cache size, gets, hits and expirations.

Exterior candidates are admitted nearest first and promoted in reverse order so the nearest
queued request runs first. Repeated route predictions refresh normal cache retention. Cancellation
removes waiting cell jobs; running work exits at its next cancellation check. Gameplay activation
and navigation safety waits keep their existing order.

Lua scripts retain fresh environments, module results, bytecode-loaded closures, and package-factory
results. Only immutable package bindings are shared, with snapshots rebuilt on configuration changes.
Factory order and engine-event timing are unchanged. Module-name resolution is cached with a bounded
4096-entry map, and VFS rebuilds invalidate both resolved paths and compiled bytecode.

For a process-local comparison, set `OPENMW_CELL_RENDER_PREPARATION=0` to disable detached rendering
preparation, or `OPENMW_SHARED_SANDBOX_PACKAGES=0` to restore per-sandbox package-table population.
Normal operation needs neither variable. These do not disable earlier GPU skinning or spatial-query work.

Native tests cover cache handoff, limits, cancellation/clear during cloning, stale revisions,
ownership, queue priority, Lua isolation, package override order and VFS invalidation. Windows CI
builds the full editor and companion tools and runs the component and engine suites.
The user's measured capture identifies cell insertion as the principal stall; it does not isolate
the cost of each construction substage. This build is a tested optimization candidate. Reduction
of the user's transition times, visual correctness with the full modlist and fully seamless
crossings require an in-game comparison and are not claimed by the native test results.

## Actor resources and transition follow-up

The resource predictor now reads initial NPC/creature inventory records even before runtime
inventories exist, and includes eligible actors behind leveled spawn lists. It warms gendered
equipment parts, weapons, lights, additional keyframes and custom bone NIFs. Prediction never
selects a spawn, rolls the game RNG, auto-equips, or executes scripts. Runtime gameplay and
actor assembly retain their original paths. New predicted assets warm templates and collision
resources without reserving extra whole-model rendering clones.

List traversal is bounded to 128 candidates, 512 records and 8192 list entries per query;
spawn prediction also has a 128-candidate cell budget. Additional animation scanning stops at
4096 unique entries per cell job and checks cancellation between files. These are entry limits,
not byte or time guarantees; snapshot work still scales with the number of cell references. Missing, invalid and
unpredicted resources still use demand loading. Prediction adds no process-wide negative cache.
`OPENMW_ACTOR_RESOURCE_PRELOAD=0` disables this follow-up independently for comparison.

Slow cell insertions now record model resolution, rendering, mechanics, physics, Lua notification,
loading-screen progress, existing resource subphases and at most three slow object IDs in the
ordinary log. Terrain culls over 50 ms report preparation versus traversal and resource phases.
These measurements are inclusive and can nest; their totals must not be added together.

The 16 November capture confirms 33 grid transitions, 58.727 seconds total and 98.27% in broad
object insertion. Separate multi-second culls also occur. It does not identify the exact costly
asset or prove that navmesh generation dominates; it does not establish a before/after speedup.
The latest native QuickLoot empty-tooltip suppression and HBFS god-mode accounting are retained.
The matching QuickLoot/Inventory Extender/UIToolkit/Object Insight v3.3 mod overlay is supplied
separately from engine binaries and must win over earlier UI overlays.
