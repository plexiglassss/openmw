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

Use `Run-Combined.cmd` for the enabled mode. For a comparison on the same executable, `Run-Previous-Spell-Reads.cmd` copies the details on every read. Both use the earlier town, HUD and native-job improvements. With frame capture enabled, `openmw-stats.log` records spell scans, empty scans, yielded entries, lookups, copied native effects, reuse and effect tables exported to Lua for each frame. The paired native benchmark is in `active-spell-reads.json`.
