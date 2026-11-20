# Native Smart Map with loading-progress improvements

Smart Map is built into this custom OpenMW 0.52 RC1 engine. Open **Options →
Smart Map** to change its behavior. The page scrolls when the available space is
small; use the Options tab arrows if the Smart Map tab is outside the visible row.

The same engine includes the loading-progress and worker-concurrency improvement
from `ffca70e561427c3b54ec3e1972db3ce0fc20a0a3`. See `LOADING-PROGRESS.md` for
its scope and comparison procedure. Existing cumulative engine features remain
part of this build.

## Install

Use the complete Windows distribution from the successful workflow for this
commit. Extract it into a new engine folder and keep the executables, DLLs and
resources together. Launch it using the same configuration and launcher procedure
as your previous custom engine. Keep the previous engine folder for rollback.

No Smart Map ESP, MWSE script, additional data folder or Delta regeneration is
needed. Your saves, plugins, ownership data and actor AI settings are not changed
by Smart Map. Its controls use OpenMW's ordinary settings file.

If you use **Advanced World Map → Override default map**, turn that option off
to see Smart Map's native local/world switching. Advanced World Map can remain
installed and opened separately. Its own canvas and local-map behavior are
controlled by that mod; the engine does not replace or patch them.

## Options

| Control | Default | Behavior |
| --- | --- | --- |
| Enable Smart Map | On | Automatically chooses a view when entering a cell. Turn off to keep manual switching. |
| Use the local map in interiors | On | Selects the local view for interiors, including interiors that behave like exteriors. |
| Choose the map view outdoors | On | Selects local in busy exterior cells and world in other exterior cells. |
| Entrances needed for the local map | 4 | Counts enabled, live doors in the current exterior cell that have a travel destination. Range 1–50. |
| Use the local map near hostile actors | On | Also selects local when a qualifying living actor is nearby; skipped in cells where resting is illegal. |
| Minimum hostile Fight value | 82 | Inclusive AI Fight threshold. Range 78–100. This is a map-selection heuristic, not an actual-combat test. |
| Hostile detection distance | 3500 game units | Inclusive distance for the Fight check. Range 1000–7000. |
| Replace internal map titles with readable names | On | Uses readable exterior/region context for an internal-looking location title when available; changes only the map heading. |

Changes apply immediately and are saved when you close Options. The Reset button
resets only Smart Map's settings. Turning off the master switch disables automatic
selection and title cleanup; it does not lock the manual world/local button.

## How switching works

The engine evaluates the destination cell after a transition. It stops counting
doors once it reaches the configured threshold and only checks nearby actors if
the door count did not already select the local map. It does not repeatedly scan
the world or inventory every frame.

You can still use the ordinary local/world button. Your manual choice stays in
place until the next cell transition or an explicit Smart Map settings change.
Fighting or an actor moving closer within the same cell does not continuously
switch your view. Fast physical movement skips outdoor switching, while teleport
arrivals can still select the appropriate destination view.

This is a native implementation inspired by **abot's Smart Map 1.07**. It uses
OpenMW's map, settings and actor interfaces. The hostile option is functional:
the original MWSE version checked a different configuration key from the one
provided by its settings menu. The native version also excludes dead, disabled
and deleted actors. Readable-title detection and travel handling are generic;
the engine does not hardcode particular mod globals or quest cells.

## Check in your game

1. Open Options → Smart Map and confirm that the master switch is on.
2. Enter an interior while the world map is selected. The native map should switch
   to local. Leave for a quiet exterior and it should select world.
3. Enter a populated exterior cell with at least four enabled teleport doors. It
   should select local at the default entrance threshold.
4. Switch views manually, close and reopen the map in the same cell, and confirm
   the manual view remains. Cross another cell boundary to resume automatic choice.
5. Turn Smart Map off and repeat a transition. It should leave the view you chose.
6. Change the thresholds, close Options, restart the engine and verify they persist.
7. Compare loading and town approaches using the same save, route and graphics
   settings as the prior custom build. Check that scenery is ready as before.

Native policy, settings and real MyGUI form regressions are required by the build
before packaging. They do not substitute for observing the installed modlist in
the game. No measured FPS or loading-time gain is claimed by the package.
