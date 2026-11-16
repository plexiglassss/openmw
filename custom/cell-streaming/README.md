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
