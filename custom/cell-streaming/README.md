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
Keep the installed UIToolkit QuickLoot Sticky v3.6 and Object Insight UI v3.7 overlays;
this engine update does not replace those companion mods with an older bundle.

## Prepare scenery before it is needed

Required camera tiles now contain their complete object sections before traversal publishes the
frame. The earlier terrain-only pending frames and four-per-frame arrival attachment quota are
removed. A required request only checks existing preparation; it never starts a speculative job
and immediately duplicates that job on the foreground path. When preparation is incomplete,
normal `getChunk`/draw semantics finish required scenery. Cached CPU-only preload nodes are reused
without pretending that a graphics completion callback has fired. Active cells, exact rays and
compatibility paths retain immediate loading.

A per-camera motion predictor plans at most ten times per second. It forecasts two seconds of
smoothed motion, bounded to one exterior cell, then selects actual future quadtree LOD/chunk
identities and an additional one-cell ring around the current view. Teleports, reversed clocks,
long sample gaps and recycled view records reset prediction. Stationary views warm the outer
ring without inventing motion. The nearest 256 distinct future candidates are offered to the
existing bounded queue. Terrain stitching flags do not create duplicate object identities.
Unsupported graphics contexts skip prediction planning. Planning does not activate cells, scripts, actors, collision or navigation. Quadtree selection
still scales with the configured view distance; these are admission limits, not timing guarantees.

Two CPU/graphics preparations may be outstanding, with 64 retained request entries and a 1 GiB
admission estimate. Each unknown job reserves 64 MiB; estimates include geometry arrays and a
4 MiB allowance per texture. Arrival retention uses the same texture allowance without reading mutable image slots.
Actual assets can exceed estimates; this is not a hard RAM/VRAM cap.
Unused pending jobs expire after one second and ready request records after a quarter second.
A required miss cancels queued speculation before normal loading; already-running factories
observe cooperative cancellation. Cancellation does not join a worker on the camera thread.
Cancelled graphs retire through the worker unref queue, and detached callbacks cannot publish
into replacement requests. The existing resource-cache and scene-node lifetimes remain in use.

Completed graphics receipts are retained separately for up to 512 exact current nodes using
weak references. They survive request retirement and unaffected selection changes without
keeping scene graphs alive. Recycled addresses and replacements cannot inherit receipts. VFS,
model settings, graphics-cache releases, compile-operation/context changes and shader revisions
invalidate receipts. Completion means the independent OSG compile-operation callback has run
for all captured contexts; it is not proof of successful driver shader linking or a hardware fence.

Cold exterior arrivals keep CPU preparation for the initial terrain view behind the loading
screen until both its near and distant passes finish. The wait does not depend on graphics
callbacks, and failures or cancellation release the normal demand path. A different first-camera
position or LOD may still need additional work; that work is required before the frame is shown.
Initial exterior loading can consequently take longer than the previous early-reveal build.
A missed prediction can still stall movement or first drawing. This revision removes deliberate
late scenery as a scheduling policy; it does not promise stall-free unseen assets or eliminate
ordinary distance/LOD appearance changes.

The normal log reports `Scenery preparation` with prepared hits, required fallback count/time,
ahead planning passes/time and deadline cancellations. Generation changes report retired requests.
Cull and resource timings remain inclusive and must not be added together. These observations
support same-save, same-route comparisons of first visits and reversals. Native tests cover
prediction, lifecycle, cancellation and graphics readiness. Production-body fixtures cover
required-frame routing, future selection, duplicate exclusion and full-arrival CPU completion.
Visual acceptance and transition timings with the user's installation remain to be measured.

`OPENMW_ASYNC_OBJECT_PAGING=0` disables future object preparation for a process-local comparison.
Both capture scripts record this and the actor/resource-preparation flags. No process-startup
preload sweep, installation-specific content rule or gameplay API change is introduced.
