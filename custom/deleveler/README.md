# Native creature and item deleveling

OpenMW can now make every existing entry in an enabled leveled-list category
eligible from player level 1. The feature is built into the native list selector
and has separate controls for creature lists and item lists.

## Turn it on

Open **Options → Gameplay** and enable either or both of:

- **Delevel creature lists**
- **Delevel item lists**

Both options default to **Off**. They are saved with the normal OpenMW settings.
Changes apply to future list selections immediately; restarting is not required.
Creatures already spawned and inventory already generated are not rerolled.

The equivalent `settings.cfg` entries are:

```ini
[Game]
delevel creatures = true
delevel items = true
```

These settings belong in the user settings file, not in `openmw.cfg` or a plugin.
With both settings false, native list selection retains its previous behavior.

## What changes

For the enabled category, the engine uses an effective required level of 1 when
finding the highest eligible tier and collecting eligible entries. The actual
selection level and the stored list records are unchanged. This allows strong
creatures and valuable items already present in a list to appear early.

Creature strength, actor statistics, hand-placed objects and list membership are
unchanged. Duplicate entries keep their weighting. Nested lists, their separate
creature/item settings, chance-none rolls, and item-count rules use the existing
native selection path. The change does not scan, delete or replace live actors.

The effective-level policy also applies to future native selections from lists
modified during play or restored from a save. Scripts that add or remove entries
using stored IDs and levels continue to see the final loaded record values.
Lua mods that independently filter raw records instead of calling the native
selector retain their own behavior.

The gameplay goal follows OperatorJack and RedFurryDemon's Morrowind deleveler:
all list thresholds act as level 1. This implementation changes selection rather
than rewriting those thresholds in the stored records.

## Existing mods and merged lists

Continue using the normal leveled-list merge. This feature does not merge plugins
or recover contributions lost to record overrides. No new ESP, ESM, Lua mod or
generated deleveling patch is required, and installing this engine feature alone
does not require rebuilding a merge.

Other deleveling mods may already have changed list values. Turning this option
off restores normal selection against the current loaded records; it does not
undo other mods or reconstruct vanilla lists. Existing compatibility patches
should be retained until their own contents have been reviewed.

## Install the complete Windows engine package

1. Close OpenMW and back up the current engine folder.
2. Extract the complete verified engine archive into that folder, replacing the
   supplied executables, defaults and resources together.
3. If using a replacement Rubic0n runtime, preserve and restore its matching
   `lua51.dll` and `resources/lua_libs/content.lua` together. The package supplies
   the standard matched pair for a fresh installation.
4. Keep optional frame-generation `version.dll` and `openmw_fg.ini` if used; they
   remain separate from the engine package.
5. Launch OpenMW and enable the desired Options → Gameplay switches.

The `openmw.cfg` beside the executable is the engine bootstrap and remains in
the complete package. It is separate from the user modlist configuration.

To roll back, close OpenMW and restore the previous complete engine folder.
The feature does not introduce a save-format change. Already generated results
remain part of a save, just as with normal leveled-list selections.

## Validation

The release workflow compiles the actual Windows engine and runs the native
component and engine suites before packaging. The deleveling tests exercise the
production selector, including independent switches, highest-tier and all-level
lists, duplicates, nested lists, chance-none, explicit level zero, unchanged
stored records, and later list changes. Disabled selection is compared with the
pre-change algorithm using identical random-generator states.

Exact source checks preserve the accepted combined engine at
`91cf944a0fe8a3d2333b3ae97ccb17376c577565`, including rest/wait finish time and
Continue. Earlier menu and cumulative gates run unchanged at their own accepted
commits. The final compiled source permits only the existing lookup build patch.
The complete package must contain the source-matched defaults, Options layout,
localization, engine bootstrap, runtime, and successful native test evidence.

The release report identifies which checks actually passed for its specific
commit. Automated tests and configuration startup do not establish gameplay or
appearance in an individual installation or certify an entire modlist.

## In-game checks

1. Open Options → Gameplay. Check that both switches are visible and can be
   toggled independently with the normal input controls.
2. Set the desired options, close Options and reopen it. Restart OpenMW once to
   confirm the choices remain saved.
3. With creature deleveling enabled, observe newly generated encounters. The
   creatures retain their usual strength. A particular powerful creature is not
   guaranteed: chance-none, duplicates and random selection still apply.
4. With item deleveling enabled, check newly generated inventories or containers.
   Previously generated inventories are expected to retain their contents.
5. Turn a category off. Subsequent selections in that category again follow the
   current modlist's stored level restrictions.

Use fresh selections when comparing behavior. Revisiting an already resolved
spawn or inventory does not force a new draw.
