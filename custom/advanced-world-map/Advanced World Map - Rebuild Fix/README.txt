Advanced World Map 1.18.0 - Rebuild Fix 1.0.0
For the cumulative OpenMW custom build, 20 November 2026

Requires Advanced World Map v1.18.0 (data handler v11). Keep its normal
.omwscripts entry enabled. Advanced World Map - Tracking v1.2.0 stays installed
unchanged. This is a script override, with no new plugin or .omwscripts entry.

INSTALL
1. Extract the "Advanced World Map - Rebuild Fix" folder as a separate mod.
2. Add that folder as an OpenMW data directory AFTER Advanced World Map and
   any older Advanced World Map compatibility/async overrides. It must win for
   scripts/advanced_world_map. Keep obsolete v1.17 initialization overrides
   disabled. Do not reinstall an older performance adapter over this folder.
3. Start the matching cumulative engine normally or use tools/run-combined.cmd
   for a benchmark. The expensive crash-capture launcher is not required.
4. Existing complete map data remains usable. To exercise the repair, use
   AWM's existing rebuild option once, then allow the rebuild to finish.

CHANGES
- Rebuild traversal uses explicit depth-first frames instead of recursive
  processCell calls; very deep door chains no longer exhaust the LuaJIT stack.
- Rebuilding advances on global updates, including paused updates. A soft
  two-millisecond / 128-checkpoint allowance spreads work across updates.
  Individual native queries, sorting and final storage/serialization can
  exceed that allowance; this is not a guarantee of stutter-free rebuilding.
- Map data is prepared privately. A failed/cancelled/stale result does not
  replace the last complete published map. Storage-write failures attempt
  rollback and do not announce successful completion.
- Fresh request tokens reject saved, duplicated or superseded replies. Both
  orderings of delayed safe-init menu starts are covered by tests.
- Preserves v11 map/transport features and the captured combined NPC/door
  query optimization. Does not change map discovery, view distance or detail.

ROLLBACK
Disable/remove this override data directory; the original v1.18.0 scripts
become active again. No save edits are required. Keep a normal pre-test save.
Do not delete player storage or existing map data to install this repair.

VALIDATION
The actual builders were compared with v11 on controlled graph, rectangle,
clustering and transport fixtures in Lua5.4 and LuaJIT2.1. Deep/cyclic graphs,
cancellation, late results, delayed menu starts and storage failure were also
checked. Native game APIs are mocked in these tests. In-game performance and
map behavior on the complete installed modlist still require a gameplay run.
