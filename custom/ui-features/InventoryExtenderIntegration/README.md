# OpenMW Bridge v1.2.6 — UI and lifecycle repair test build

ACTION: Replace the separate OpenMW Bridge folder with this version, install the matching new engine, and install the new Frame Generation version.dll from its separate build. Fully close OpenMW first. Keep your Restored Base folder and provider mods; keep InventoryExtender.omwscripts enabled once. The bridge Data_Files directory must remain after the base. Do not copy it into the base.

## Changes

- Paper-doll action now uses left-button press coordinates, rather than a later release/current-pointer lookup. Portrait pointer events are consumed locally so they cannot initiate a parent window drag. Cursor pickup/equip logic remains. This is a candidate fix: the supplied log has no click trace establishing which stage failed. A short Bridge 1.2.6 press line now reports the equipment slot and UI mode on each click; rejected bound/ownership checks are reported too.
- Item tooltip headers stretch to the detail width, center the icon/name and use the existing MWUI horizontal separator between icon and name. Provider-added content is retained.
- The world tooltip can display the ingredient inside a native graphic-herbalism plant with one ingredient stack, while retaining the plant as the target for ownership/activation. Empty plants, ordinary containers and mixed/multiple contents retain native tooltips. Script-only activators without native harvest support are not guessed from their name.
- Icon-pack enumeration now deduplicates names case-insensitively and uses a strict comparator. It excludes the bridge's custom image directory from selectable themes. This fixes the settings-script startup error recorded in the supplied log.
- The matching engine captures a loading background once per loading episode. The prior callback set its one-shot flag but did not check it, allowing repeated capture of historical framebuffer images with frame generation.
- The matching Windows FG DLL hooks destruction of its own game GL context and stops/joins its presenter before destruction, outside DllMain. It temporarily acquires/restores the appropriate context when needed. The previous Windows hooks never called the available shutdown routine. An acquisition failure is logged rather than deleting GPU resources under the wrong context.

## Evidence and limits

The supplied openmw(20261023-214700).zip contains engine revision d204c15520. Inventory Extender's menu settings failed at iconPack.lua:41 with invalid order function for sorting. The log reaches Quitting peacefully, roughly 18 seconds after settings/storage saving starts; the user reports the process survives the closed window. The log contains no Frame Generation DLL identifier, so the exact installed DLL version is not established.

Offline checks cover Lua syntax and inventory/provider/protection regressions, plant-target separation, icon-pack enumeration, one-shot loading capture and WGL shutdown ownership/context restoration. Windows builds compile the complete engine and DLL separately. No live OpenMW/NVIDIA test was performed. Do not treat a source-level candidate as an in-game pass.

## Focused test

1. Open Inventory Extender settings: no icon-pack sorting error should appear.
2. Click equipped gear on the doll, then click the doll again. Expect cursor pickup then equip. If it still does nothing, send the new log; the Bridge 1.2.6 press trace identifies the next stage without guessing.
3. Inspect several short/long item tooltips and an owned item: centered header, standard separator, one outer frame and ownership background.
4. Inspect unharvested hypha facia on a tree, then harvest it. Confirm ingredient details before harvest and no stale ingredient details afterward.
5. Walk the same cell boundaries with frame generation, then quit. Confirm no back-and-forth loading background and that openmw.exe disappears. If it survives, include openmw_fg.log alongside openmw.log; only the latter was supplied this time.
