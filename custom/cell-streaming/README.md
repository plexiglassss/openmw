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

## Continuous distant-object preparation

A distant paged-object miss can now return pending immediately to camera traversal. The
terrain remains drawable while worker jobs prepare missing object sections. Nearby gameplay
cells and exact ray queries retain their existing immediate semantics. Before a prepared
section becomes visible, an independent completion callback covers the captured graphics
contexts; an existing shared "queued" marker is never treated as completed work. This is an
OSG compile-operation completion contract, not proof of successful driver shader linking or
a hardware GPU fence. Absent or uninitialized graphics contexts use the compatibility path.

Admission prefers the nearest missing sections, with two outstanding CPU/graphics preparations,
64 retained request entries and a 1 GiB admission estimate. Each unknown job reserves 64 MiB;
completed estimates include geometry arrays and a 4 MiB allowance per texture. Actual assets
can exceed these estimates. This is not a hard process RAM/VRAM cap. Unused pending jobs expire
after one second; ready request records expire after a quarter second. Visible scene nodes
and normal resource caches have their existing lifetimes. Cancelled graphs are retired through
the worker unref queue; detached callbacks cannot publish into a replacement request.

Camera and ray views have separate readiness semantics. A CPU-complete ray view cannot bypass
the camera graphics gate. Selection, model/VFS/cache and graphics-context changes invalidate
requests and reused views; stale builders cannot repopulate a cleared chunk cache. Size estimates
used to reject distant objects carry the same resource epoch. Required detached cell clones now
finish before optional animation-directory scanning.

`OPENMW_ASYNC_OBJECT_PAGING=0` selects the previous camera path for a process-local comparison.
Both capture scripts record this flag and the actor/resource-preparation flags. The normal log
reports whether distant streaming is enabled. No startup preload sweep is added.

This moves distant object preparation out of camera culling. Terrain and groundcover creation,
active-cell insertion, exact rays, Lua activation and driver work can still stall. Distant objects
may appear after their terrain while preparation finishes. Native/body tests validate lifecycle
and fallback behavior; a same-save, same-route game recording is still required to measure the
actual improvement and check visual behavior in the full installation.

## Preserve completed scenery across view rebuilds

The user reports better transition performance with the continuous-streaming build, but the
16 November evening recording shows distant scenery repeatedly disappearing and returning
on the same short route. Completed request records retire after 0.25 seconds; a cell-grid or
LOD view rebuild could therefore queue graphics preparation again for an unchanged cached
object node that had already been displayed.

Object paging now remembers up to 512 completed nodes using weak lifetime references, outside
the heavy request queue. A rebuilt view can immediately reuse the exact current cached node
when its graphics completion is already known. These receipts retain no scene graphs and do
not increase worker concurrency or the asset admission budget. Recycled addresses cannot
inherit a previous node's readiness. Least recently used receipts are evicted at the limit.

Selective object edits still invalidate affected CPU chunks and all pending requests; only
unchanged current nodes can reuse their receipt. VFS, model settings, graphics-cache releases,
compile-operation changes, graphics-context changes and shader source/template/GL revisions
invalidate all receipts. This includes live lighting defines and shader hot reload. Pending and
replacement nodes still pass through the completion gate. Active cells and exact rays retain
their existing paths. There is no startup sweep or installation-specific content rule.

Native tests cover weak ownership, address reuse, bounded eviction, invalidation and retention
after request expiry. Production-body fixtures exercise reuse without queueing, replacement
and pending nodes, selection changes, every graphics generation and compatibility fallback.
This addresses avoidable repeat pop-in; new LOD/chunk shapes, resource-cache eviction and
first-time distant detail can still arrive late. A new in-game recording is needed to confirm
how much of the visible popping this removes in the user's installation.
