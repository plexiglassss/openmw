# Inventory Extender: custom-engine integration (test build)

Install the matching custom OpenMW engine AND these Lua files. No new plugin or
omwscripts entry is needed. Keep InventoryExtender.omwscripts enabled once.

Add this package's Data_Files directory AFTER the existing Inventory Extender
folder and any previous overrides of these same scripts. Alternatively back up
and copy Data_Files/scripts over the existing mod's scripts directory.
Keep the rest of your installed Inventory Extender, icons, localization and
other mods. This is based on your recovered Inventory Extender (OpenMW)(3).zip,
including your earlier separate-inventory, tooltip, favorites and other fixes.
Those scripts are replacements; don't install a second complete IE version.

Preserve your custom lua51.dll and the separately updated FG version.dll/INI
when copying a new engine distribution (the engine archive supplies its own
standard lua51.dll). Back up the previous engine for rollback.

## World-tooltip option
Settings > Interface > Enhanced world tooltips (mods) independently enables or
disables the world-item tooltip integration. Enabled by default; saved in
settings.cfg as [GUI] enhanced world tooltips. Off restores native world tooltips
and stops the IE bridge from building replacements. Re-enable without restarting.
It does not change inventory tooltips or disable the integrated paper doll.
Without a supplying mod, native tooltips remain in use even when this is enabled.
Object Insight's own separate display resumes its original behaviour when the
bridge is disabled; control that display through Object Insight's own settings.

Test while looking at an item: toggle off/on, check ordinary world tooltips return,
check inventory details and paper doll remain available, then restart to confirm
persistence. This new option has not yet been validated in a live game.

## What changes
- Looking at a carriable item in gameplay uses IE's item-tooltip layout and its
  registered modifiers in OpenMW's normal world-tooltip position. Original
  target selection, activation and owned-object frame indication remain.
  Actors, doors and other non-items retain the normal native tooltip. Full-help
  mode uses the native tooltip. No extra raycast is made by this integration.
- The Inventory window includes the engine's paper doll. The earlier separate
  vanilla-inventory workaround is bypassed on the matching engine. Character
  toggles the panel. Clicking equipment uses IE's existing use/unequip handler;
  dropping a dragged item on the character uses the same route as IE's Use button.
  Container, trade and companion panes keep their existing behavior.
- Settings > Interface > Snap windows to nearby edges toggles snapping. Enabled
  by default, saved in settings.cfg. Windows move independently after snapping.
  Native and Lua windows are targets. Screen edges participate. Resize isn't
  snapped. Threshold is 12 logical GUI pixels, scaling with the interface.

## Object Insight Tooltip adapter
If Object Insight Tooltip is enabled, the included override also moves its Base
DPS, Effective DPS, equipped comparison, armor ratings, enchant used/free and
selected source/debug details into the world tooltip. It uses that mod's existing
calculations and settings. Its separate overlay is hidden while the native item
tooltip owns the target. Other objects retain the original overlay behavior.
The original mod must remain installed/enabled; no new script entry is added.
Place this override after Object Insight Tooltip as well as Inventory Extender.
This adapter supplements world tooltips; it does not change inventory DPS providers.

## Focused in-game checks (not yet passed live)
1. In the inventory, inspect an ordinary weapon, enchanted weapon, poisoned
   weapon, soul gem and an item with a mod description. Put each in the world
   and look at it with menus closed; compare the added details. Confirm owned
   items retain the theft indication and activation still works normally.
2. Look rapidly between items, an NPC and a door. Pick up the targeted item,
   enter a different cell, save/load and toggle full help. Check no stale tooltip
   remains. Dynamic item/provider information refreshes at most four times/sec.
3. Open inventory: one inventory with a character preview. Equip/unequip armor,
   weapon and clothing. Drag an item onto the character. Click worn equipment.
   Repeat while using a container; check barter actions obey IE's existing rules.
   Resize the inventory and collapse/expand Character. Check beast races and
   your custom bodies/armor as those require live rendering validation.
4. Drag map, magic, stats and IE windows near each other in both directions.
   Align tops/bottoms/sides, pull away, try screen edges and resize. Disable
   snapping and confirm free movement; restart to check the setting persists.
5. Verify F8 and normal cursor behavior with your FG mod still installed.

## Limits and rollback
Offline tests cover geometry decisions, Lua parsing and the tooltip bridge's
lifecycle/refresh/error behavior. They do not certify live MyGUI rendering,
NVIDIA rendering, controller menus, or every third-party tooltip provider.
Only details provided by IE's item-tooltip builder/modifier API are reused;
private information or inventory-only provider assumptions may require adaptation.
The collapse state currently lasts for the lifetime of the inventory UI, not
as a new persistent mod setting. Very long mod descriptions retain IE's layout.

For rollback, remove this data-directory entry (or restore backed-up scripts),
then restore the previous engine. Keep the separate FG fix and your custom
lua51.dll. No save conversion is required.
