# Interior map icon visibility repair

Dragging the local map repeatedly requested door discovery. Exterior discovery
was cached by cell, but each interior request appended another complete set of
door widgets. New or restyled ImageBox textures could also enter the render list
after fog, until the periodic magic-marker redraw restored widget depth order.

The repair keeps the current interior's door widgets after its first deferred
discovery, including interiors with no doors. Changing cells resets that state
and retains the existing widget recycling and post-script discovery timing.
Viewport movement therefore preserves marker identity and mouse capture.
Actual door creation and artwork refresh restore draw order immediately, so
ordinary doors stay below fog while custom notes and magic markers stay above it.

This changes the native map implementation in the cumulative
`openmw-0.52-rc1-custom` branch. It adds no settings, plugins or cell-specific rules.
Use the complete Windows distribution from the successful workflow for the
repair commit, keeping its executables, DLLs and resources together. Existing
mod directories, configuration and saves can be used as before.

## Automated verification

`python custom/map-icon-visibility/test-door-refresh.py --output result.json`
compiles and executes production method bodies with counted world/widget
boundaries. The same harness can run an earlier revision with `--source-ref`.
Its receipt records the exact source identity and the executed assertions.
It checks the marker lifecycle; it does not emulate a full game or renderer.

`MapIconVisibilityTest.DoorArtworkRefreshKeepsFogAboveDoorsAndNotesAboveFog`
runs real MyGUI widget and render-item ordering with synthetic platform textures.
The full Windows workflow requires that test alongside the existing native map,
Smart Map, controls and cumulative engine tests before packaging.

## In-game check

1. Load the same interior and leave an exit under unexplored fog. Open the local
   map and keep the left mouse button pressed while dragging continuously over
   that area. The exit icon should remain obscured during the entire drag.
2. Explore the exit normally, then drag starting on its visible icon. The drag
   should continue until you release the button; the icon and its normal tooltip
   should still work after release.
3. Zoom, close and reopen the map, change interiors, and return. Hidden and
   explored icons should retain the same behavior. Controller panning uses the
   same interior marker lifecycle.

Compilation, executable fixture results and package identity are separate from
confirmation in the user's running game. This repair does not certify the whole
installed modlist.
