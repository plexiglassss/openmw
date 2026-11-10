# OpenMW parallel native jobs

This Windows build continues the `12e5b050` native-jobs engine and its corrected `3db8987d` distribution.

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
