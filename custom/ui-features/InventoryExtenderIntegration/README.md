# OpenMW Bridge v1.2.8 — recent markers and plant rollback

Replace the separate OpenMW Bridge folder with this version. Keep the restored Inventory Extender base unchanged and keep the bridge Data_Files after it in data order. Update the cumulative engine to the matching build to remove the engine-side plant resolver. No new game or save conversion is needed.

The Frame Generation startup repair is a separate DLL update. Close OpenMW, replace version.dll next to openmw.exe with the matching Frame Generation build, and keep openmw_fg.ini and your existing lua51.dll. The bridge does not install either DLL.

## Changes

- Clear new now invalidates cached table labels when their marker changes. It was clearing the stored flags, but the label's cache-comparison data omitted the marker. Grid labels already included it. The control now explicitly uses the mod's button border assets and a visible background, including when Interface Reimagined normally suppresses those borders.
- New markers have no timer. They are saved with the game and remain until cleared or until none of that item record remains in the observed inventory. They mark increased totals per record ID, not individual copies. Splitting/merging stacks does not count as acquisition. A later acquisition can mark the item again.
- Removed the unharvested-plant resolver, container scanning and hover diagnostic logging. Plants retain native tooltips. Loose/harvested ingredients and equipment keep enhanced tooltips, including the existing Alchemy skill filtering.
- Preserves previous paper-doll, comparison, ownership, window-snapping, protection and cumulative engine work.

## Validation

Lua syntax and the existing tooltip/protection/paper-doll/layout regressions pass locally. The new regression executes the shipped item-name renderer: it fails with the old cache metadata and passes with this change. No live OpenMW UI pass is claimed. Full Windows engine compilation is a separate Actions result.

## Focused in-game check

1. Acquire an item, see its marker in table view, then click the bordered Clear new button. The marker should disappear immediately. Close/reopen inventory and save/reload: it should remain cleared. Acquire more of that type: it should reappear. Repeat in grid view.
2. Look at an unharvested plant: expect the original tooltip. Harvest it or inspect a loose ingredient: expect enhanced details with skill-gated effects.
3. With the separate new FG DLL, check startup wallpaper, the previously fixed cell transitions, F12, save/load, resize and Alt+Tab. The startup repair is an offline-tested candidate, not yet confirmed on your Windows/GPU combination.
4. Load your usual save and send openmw.log. New Save load phase/records lines separate record restoration, progress display, player setup, initial cell and Lua startup. This is investigation, not a claimed loading speedup.
5. For Win+G, compare a restart with version.dll renamed/removed against a restart with it installed. The End hotkey disables interpolation but retains the presentation hook. New SDL input mode log lines record keyboard/mouse grabs without logging key presses. Win+G is not claimed fixed.
