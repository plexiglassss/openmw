# Combined OpenMW performance candidate

This candidate combines three engine changes and the two SaneMagic adapters in
one complete Windows build. Its parent is
`016577e6e33b16080e9c6828b38a3879526714d7`, preserving the native world-map icons,
native travel/key/cleared-cell tooltips, and every earlier cumulative source gate.

## What changes

| Area | Engine change | Mod changes needed |
| --- | --- | --- |
| Lua UI | Reuse already computed native geometry within one Element action when constraints and dependencies have not changed. Dynamic table reads, callbacks, and text editors use the established traversal. | None. Existing UI callers use this path automatically. |
| Magic queries | Compile effect selections once; read current native stores directly; return the same immutable Lua result when ordered values are unchanged. A native presence check avoids constructing an iterator just to test for effects. | Install the two prepared SaneMagic adapters below. |
| Typed cell queries | Share immutable object-ID snapshots until membership changes; avoid invalidating all object-pointer caches for an identical registration. | None. Existing typed `cell:getAll` callers use this path automatically. |

Gameplay callbacks still run at their existing cadence. Magnitude and effect
queries read the live stores on every call. Cell snapshots track insertion,
movement, deletion/undeletion, content deletion, reference replacement and
registry changes; previously returned lists keep their original membership.
Dynamic object properties remain live. Untyped/player queries retain their
existing path. The native UI cache does not persist geometry across actions.

The candidate does not claim a measured FPS increase. Native benchmarks exercise
specific engine operations with controlled services; they are useful for
measuring work removed, but do not predict FPS on a loaded modlist. The combined
gameplay result still needs one run on the user's machine.

## Install and run once

1. Close OpenMW and keep a separate backup copy of the current engine folder.
   Upgrade using the complete engine package. When retaining a custom Lua
   runtime, preserve **both** `lua51.dll` and its matching
   `resources/lua_libs/content.lua` together. Preserve the engine's existing
   custom `openmw.cfg`, the separate user configuration, and optional FG files.
   The package also contains a complete bundled runtime pair for a fresh folder.
2. Run **Install-SaneMagic-Adapters.cmd**. It checks both existing winning script
   hashes, the reviewed configuration and logged data order, then backs up and
   replaces `npc.lua` and `daesumm_pn.lua` as one transaction. If already installed
   it reports that state. No new modlist lines or `.omwscripts` entries are needed.
   `daesumm_c.lua` and the prior accepted mod fixes remain unchanged.
3. Run **Run-Combined.cmd**. Keep the four mods enabled. Load the usual town save,
   let loading settle, and use the usual short route. Quit normally to finish
   the FrameOnly capture. This is the combined candidate's one gameplay check.

If the adapter installer stops because current sources or configuration differ,
it has not installed a different or partially matching version. Its Source-check
ZIP identifies the mismatch. Send that ZIP with the capture if review is needed.
The new engine paths still work for compatible existing callers, but the native
SaneMagic query path requires the adapters to be installed successfully.

## Same-build fallback and rollback

**Run-Inherited.cmd** is available if a direct comparison is needed. It launches
the same executable with `OPENMW_COMBINED_PERFORMANCE=0`, disabling the new UI
reuse, native query API registration, cell result cache and idempotent pointer
registration. The installed SaneMagic scripts then use their original-code
fallback. Earlier property-reader, timer, lookup and native feature fixes remain.
There is no second engine download or rebuild for this comparison.

The launchers change only their process environment and restore it afterwards.
They record the selected mode in the capture. Launching OpenMW normally enables
the combined paths by default, unless the master environment variable is `0`.
The diagnostic flags `OPENMW_LUA_UI_LEGACY_UPDATES=1` and
`OPENMW_LUA_CELL_QUERY_CACHE=0` can disable individual new paths.

To restore the two mod files, use
`tools/combined-performance/SaneMagic/Restore.cmd`; the existing hash-checked
installer restores their latest complete backup. The engine backup supplies
the full engine rollback.

## Verification included in the package

The build runs the complete native component and engine suites, all four existing
lookup modes, the earlier UI property-reader parity check, and the new combined /
legacy-update / master-off UI parity checks. Native fixtures exercise retained
children, templates, image inheritance, changed constraints, metatable side
effects, text-editor caret/scroll state, immutable effect snapshots, caster
availability, same-update mutations and typed-query membership changes.

`combined-native-tests.json` and `combined-layout-parity.json` bind their results
to the packaged native XML files. `COMBINED-PACKAGE.json` binds the prepared
tools and adapter payloads. Existing source manifests, runtime inventory and
startup bootstrap checks remain required before the complete archive is uploaded.

The adapter parity harness compares the captured originals and modified scripts
using Lua 5.1 and LuaJIT 2.1 with controlled game services. This is script behavior
evidence, not a gameplay performance measurement. CI native tests and startup
checks use the bundled CI Lua runtime; the user's custom replacement runtime
and actual modlist are not exercised in CI.
