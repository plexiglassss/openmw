# Navigation geometry preparation

Extends cumulative build 80135007, including its exterior prediction and graphics
preparation changes, tooltip/QuickLoot fixes, HBFS god-mode repair and all tools.

Overlapping requests for the same navigation tile now share its source geometry
construction. Navigation meshes for different actor sizes still have their own
Recast generation and pathfinding rules. A tile snapshot reserves object storage
for its spatial-query matches instead of every object in the active world.

Tile invalidation, leaving the navigation range and world changes detach obsolete
builds from the cache. An older completion cannot overwrite a newer build. Its
original callers still receive their captured geometry; it is not misreported as
an empty tile. Failed builds wake their waiting callers and allow a later retry.
Snapshot owners keep collision resources alive during preparation.

No navigation worker count, active radius, scenery visibility, cache file format,
startup scan, or mod API is changed. The default navigation worker count is one:
sharing avoids work only when calls overlap, usually with multiple configured
navigation workers and different actor sizes. The allocation change applies with
one worker too. The user's effective worker setting has not been verified.

## Comparison

Use `Run-Combined.cmd` for the new default. `Run-Previous-Navigation.cmd` disables
only the new sharing and proportional-allocation optimizations for that launch,
leaving the publication/lifetime safeguards and previous improvements enabled.
It sets the process-local `OPENMW_SHARED_NAVIGATION_GEOMETRY=0` switch. Restart
between modes. The cumulative master switch also disables the optimization.

Keep the save, settings, view distance, mod order and Frame Generation state fixed.
Compare first and return traversals separately. Character Speed 2000 is a useful
stress test, but does not imply a particular measured world speed. Prefer an
unrecorded or FrameOnly comparison before Detailed timing. No new mod patch is
needed. If using a custom Lua runtime, keep its `lua51.dll` and matching
`resources/lua_libs/content.lua` together in the separate test engine folder.

## Evidence and limits

Native tests count real triangle traversals under controlled overlapping requests,
compare resulting geometry, check independent tiles, source ownership, exceptions,
same-tile changes, world exit/reentry and range exit/reentry. They also verify the
uncached navmeshtool path and actual snapshot capacity in a 1-local/1024-distant
object fixture. Both optimized and comparison modes must pass.

Diagnostic counters expose current pending tiles, joined requests and the last
snapshot's object count/capacity. Detailed timing distinguishes geometry snapshot,
build and shared wait. These are evidence of avoided work and correctness on the
test fixtures, not a measured gameplay gain. In-game frame pacing, loading screens
and behavior on the user's modlist remain to be tested.
