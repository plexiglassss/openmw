# Rest/wait finish time and Continue

This build adds two native menu conveniences to the complete cumulative custom
OpenMW engine. It starts from the accepted engine and the Windows packaging
repair. No ESP, ESM, Lua mod, load-order change or new setting is required.

## Rest and wait

Open the rest/wait menu with the assigned key and change the hours slider. A
new line below the selected hours shows the expected finish time, including
minutes and AM/PM. It adds **(next day)** when the selected duration crosses
midnight. Mouse, keyboard and controller adjustments use the same update path.

For example, selecting eight hours at 10:30 p.m. shows **Ends at: 6:30 a.m.
(next day)**. A full 24 hours keeps the clock time and marks the next day.

The preview describes the selected slider duration. Until Healed still uses its
existing separate duration calculation. A rest interrupted by an encounter can
end earlier. The preview does not advance the clock, change regeneration, alter
the 1–24-hour range or change autosaves.

The remembered MWSE feature is in NullCascade's
[UI Expansion](https://www.nexusmods.com/morrowind/mods/46071?tab=logs), introduced
in version 1.5. This is a native OpenMW implementation with minutes and a next-day
indicator added to the original concept.

## Continue

The title screen shows **Continue** immediately above **New Game** whenever the
engine has discovered a save for the current game. The new entry uses the game
font, menu colours, hover/pressed feedback and a borderless row aligned with the
existing menu. Keyboard and controller focus starts on Continue when available.

Continue loads the save with the newest saved-file timestamp across all
recognized characters. Manual saves, quicksaves and autosaves are eligible. A
stable file-path ordering resolves equal timestamps. Normal load handling and
missing-content warnings still apply; a recognized header is not a promise that
the entire save is readable. Invalid headers and saves for a different game are
excluded by the existing save discovery.

The option is hidden when no saves exist. Deleting the newest save makes the next
newest one eligible; deleting the last save hides Continue. During a running
game, the existing **Return** entry still resumes that game. Continue is a title
screen action and cannot replace a running game through a stale click.

## Install the Windows build

1. Close OpenMW and keep a separate backup copy of the current engine folder.
2. Extract the complete engine archive into the engine folder. Replace the
   executables **and** the supplied resources together. In particular, the new
   rest dialog layout, menu skin and English localization belong with the new
   executable.
3. Keep the separate user configuration and mod folders. There is no plugin to
   enable and nothing to regenerate.
4. If using the custom Rubic0n runtime, preserve/reapply the existing matched
   `lua51.dll` and `resources/lua_libs/content.lua` together. The complete archive
   also contains a matched standard runtime pair for a fresh engine installation.
5. Keep the existing optional frame-generation `version.dll` and its
   configuration if using them. They remain separate from the engine archive.

The included `openmw.cfg` next to the executable is the required engine
bootstrap. It is separate from the user's modlist configuration. The complete
archive must retain this file, `lua51.dll` and the internal Lua content helper.

For rollback, close OpenMW and restore the previous engine folder from the
backup, keeping its executable and resources together. These menu additions do
not change the save format.

## Quick checks in the game

* Launch with saves present. Continue should be directly above New Game, with
  working mouse highlighting and keyboard/controller selection. Select it and
  check that the newest save opens.
* Press Escape during play. Return should still resume the current game.
* Open the rest/wait menu and move the slider. Check that the finish time changes
  immediately, that midnight/noon are correct, and that 24 hours shows the same
  clock time on the next day. Rest/wait normally and compare the resulting game
  time if it is not interrupted.
* With no saves for the selected game, Continue should be absent. Save deletion
  is covered by automated temporary-file tests; no personal save needs deleting
  to test this build.

## Verification and limits

The focused native regression suites are `RestTimeTest` and `ContinueGameTest`.
They exercise production time normalization and real serialized save-header
discovery, including noon, midnight, fractional hours, 24 hours, all save types,
multiple characters, overwrite ordering, deletion, tied timestamps and invalid
or wrong-game headers.

CI compiles the actual menu implementation and runs the complete existing
component/engine suites. It retains the lookup, notification, installer,
frame-generation and collector checks. The historical cumulative source gate
runs unchanged at the exact preserved baseline. A separate source manifest binds
every added/changed feature file and verifies that all other source bytes remain
unchanged, before and after the existing lookup-only build patch.

Packaging verifies the complete bootstrap/runtime files, exact compiled defaults
and test evidence. The packaged executable must pass the existing configuration
startup check. The final package carries its exact commit and verification
records. These automated checks are distinct from seeing the menus in the user's
game installation; gameplay and the user's replacement Lua runtime require
their own observations.
