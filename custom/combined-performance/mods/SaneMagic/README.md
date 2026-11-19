# Sane Magic native-query adapters

These two scripts use the combined engine's native queries to reduce the work
spent reading active spells and bound-weapon effects. They retain the installed
Sane Magic gameplay logic and the earlier live-view, empty-effect, and combat
notification fixes.

## Install the packaged component

1. Close OpenMW and its launcher.
2. Open `tools/combined-performance/SaneMagic` in the combined build and run
   `Install.cmd`.
3. Start the combined engine using the package's normal launch instructions.

The installer finds the active loose-file providers from the last OpenMW log.
It checks both existing script hashes, both replacement hashes, the reviewed
engine revision, the logged data-directory order, and the current `openmw.cfg`
hashes. It then backs up and replaces these two files together:

- `scripts/SaneMagic/npc.lua`
- `scripts/SaneMagic/daesumm_pn.lua`

No new data-directory or `.omwscripts` entries are needed. `daesumm_c.lua` and
the other Sane Magic scripts keep their existing contents. The installer uses
the existing mod folders, so mod-manager ordering does not need to change.

The checks are bound to the captured November 9 installation. A later mod
update, a changed mod order, or changed `openmw.cfg` contents can cause the
installer to stop before replacing anything. It prints the reason and, when
possible, creates a source-check ZIP for review. Keep the existing configuration
when installing this component; the installer does not revise configuration or
weaken a failed check.

For a dry run from PowerShell, use:

```powershell
& .\Install-Fix.ps1 -CheckOnly
```

The optional `-LogPath` argument selects a nondefault OpenMW log. The installer
records its backup location and transaction receipt. If a replacement fails,
it attempts to restore every file already replaced and reports the recovery
result.

## Restore

Close OpenMW and its launcher, then run `Restore.cmd`. It restores the latest
successful backup for this component. Restoration checks the current files and
the backup hashes, and stops if restoring would overwrite a later change.

## Runtime behavior

The NPC script compiles a query for applied effects from temporary spells cast
by players. The engine filters native records before creating Lua results and
reuses immutable ID/caster arrays when those selected values are unchanged.
Effect order, duplicates, caster identity, first observations, and removals
remain available to the original gameplay checks.

The summon script batches its ordered bound-weapon magnitude reads and uses a
native empty-effect check. It recreates its selection if the source list is
changed in place. Every update still reads current native effects, and every
gameplay check keeps its original cadence. Query results do not replace checks
of settings, AI state, pause state, sneak, or combat.

Both scripts detect the new methods at runtime. On a compatible older engine,
or when the combined engine starts with `OPENMW_COMBINED_PERFORMANCE=0`, they
use the captured scripts' original query paths. The same installed scripts can
therefore participate in the combined package's two launch modes.

## Source and packaging details

`manifest.json` contains the captured source hashes, replacement hashes, engine
compatibility list, configuration hashes, and logged data-order hash. It
contains no private configuration contents or installation paths. The package
build adds its exact candidate revision to the compatibility list.

The release packaging step copies the unchanged
`custom/gameplay-responsiveness/Install-Fix.ps1` into this component, copies the
two scripts to `Data Files/scripts/SaneMagic`, and includes this README, the
manifest, and the two command wrappers. Preserve script bytes when copying;
the manifest verifies the packaged payload before any installation.

The adapter parity harness is
`custom/combined-performance/tests/test_sanemagic_adapters.py`. It compares the
actual captured originals and replacements under Lua 5.1 and LuaJIT 2.1, with
native queries present and absent. The engine tests separately exercise real
query reads, snapshots, mutation visibility, actor/caster validity, and
ordered selections. These checks verify behavior and the implemented query
path; they do not establish a measured in-game frame-rate gain. The default
baseline fixtures use deterministic gzip compression and are checked against
the exact original script hashes after decompression. An explicit baseline
directory can instead supply the raw captured Lua files.
