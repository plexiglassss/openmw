# Version 1.1 — world equipment comparison

This cumulative package replaces version 1.0 and earlier custom integration files.
Keep the original Inventory Extender installed; load this package's Data_Files
AFTER it so these scripts win. Do not install both integration versions.
Requires our custom OpenMW build with the world-tooltip bridge; this update
itself changes Lua only, so an existing compatible custom build needs no rebuild.

With Enhanced world tooltips enabled in the engine Interface settings, the
Inventory Extender Compare equipped item setting now also applies to items
under the crosshair in the world. Hold ALT compares immediately; releasing ALT
restores the single tooltip. Always and Never are respected. The same equipped
slot selection and tooltip providers are used as in Inventory Extender,
including both ring slots. Unsupported items keep their ordinary tooltip.
These are side-by-side item details, not newly calculated numeric deltas.
Object Insight's additional supplement remains for the targeted item only.

Target and ALT changes update immediately. Condition/enchantment and equipment
changes refresh at most four times per second while a target remains selected.
No extra raycast or per-frame inventory scan was added. A comparison-provider
failure falls back to the single-item tooltip; if that also fails, the native
engine tooltip remains available.

Offline Lua syntax and regression tests passed. Live OpenMW visual testing is
still required, particularly wide tooltips at your resolution/UI scale.
Test: equip a weapon, look at another on a table, hold/release ALT without moving
the crosshair. Repeat with armor and rings (two equipped), then an ordinary
miscellaneous item. Check Always/Never, change targets, open/close inventory,
and disable Enhanced world tooltips. Confirm panels fit, show correct equipped
items and vanish without stale text. Check mod-supplied stats in both panes.

---

# Inventory Extender quality-of-life update — offline test build

Replace your previous custom integration folder with this package. Keep the
original Inventory Extender installed and its .omwscripts entry enabled once.
No new plugin, script-list entry, or save conversion is needed.

Equipment comparison ALREADY EXISTS in your installed Inventory Extender:
Settings > Scripts > Inventory Extender > Window options > Compare equipped item.
Select Hold ALT (or Always). Existing saved Never preferences are preserved;
new installations now default to Hold ALT. Hover an unequipped weapon, armor or
clothing item to compare its tooltip with the equipped item(s) for those slots.
This uses the existing tooltip providers, not a new damage/balance calculation.

New settings under Inventory Extender > Tweaks (enabled by default):
- Show recent-item marks: NEW in table and grid views. Clear NEW acknowledges
  all current marks. Marks and count history are stored in the save, not in
  permanent Lua storage. No inventory polling loop was added.
- Protect favourite items: blocks outgoing sales, transfers and drops through
  Inventory Extender. This extends existing protection to previously unguarded
  drag paths and bulk transfers; disabling it permits these operations.
- Protect equipped items: independent option covering the same actions. Unequip
  first to move an item, or disable the option. Equipment use/unequip still works.

The protection checks retain the original status while dragging, including when
stack splitting changes identity or moving into the cursor unequips an item.
Returning an item to your own inventory is allowed. Barter checks again when
confirming an offer, so enabling protection mid-transaction cannot bypass it.
Quest scripts, consumption, theft confiscation and other mods' independent UIs
are outside these safeguards; this does not make items globally immovable.

NEW is an inventory-count indication by record type, not a timestamped loot log.
A net count increase since the previous IE refresh marks that item type; all
copies of that record share the mark. Existing inventory starts unmarked.
Splits/merges alone do not mark items. An acquisition offset by consumption or
removal before the next refresh cannot be detected. Marks persist until cleared
or all copies leave the inventory. Pending purchases are not marked as owned.

Replace the entire previous integration folder, retaining the same data path.
Its Data_Files directory must load after original IE and Object Insight Tooltip
(if installed), as before. This package includes the earlier world-tooltip,
paper-doll and barter fixes; use our matching custom engine for those features.

## Validation
Lua syntax, existing world-tooltip lifecycle tests, paper-doll tests, and new
protection/recent-item regression tests passed offline. New tests exercise
individual and bulk transfers, drops, own-inventory returns, toggles, changed
cursor identity, drag unequipping, baseline counts, stack gains/splits/merges,
clear, save/load and reacquisition. Live UI/rendering and complete modlist
compatibility remain unverified.

## Focused in-game checks
1. Set Compare equipped item to Hold ALT. Hover a spare weapon, armor piece and
   ring; compare the panes with the equipped items. Try Always and Never too.
2. Pick up an item, open inventory: NEW should appear. Add to an existing stack;
   check its mark. Press Clear NEW; close/reopen and confirm marks stay cleared.
3. Acquire another item and save/reload: its mark should remain. Move an entire
   marked record out of inventory, then acquire it again: it should be NEW.
4. Favourite an item (default F). Try selling, direct transfer, dragging into a
   container/companion and dropping in the world: all should be blocked while
   protection is on. Return the dragged item to your own inventory successfully.
5. Repeat with equipped gear, then unequip it and retry. Check paper-doll use
   still works. Disable each protection option independently and retest.
6. Stage an ordinary sale, then enable its applicable protection before Offer;
   confirm the sale is refused, with no money/disposition change from that offer.
7. Check both table and grid views, small/resized windows, barter and companions.

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

## Paper-doll barter correction (2026-10-23)
Paper-doll clicks and drops now request equipment actions explicitly, bypassing
Inventory Extender's ordinary barter-row routing. Click visible equipment to
unequip it into your inventory; drop an available player item onto the doll to
use/equip it. No need to target a particular body slot when dropping.
During barter, merchant-owned items, uncommitted purchases, and stacks with any
pending sale are rejected without ending the drag or changing the transaction.
Complete or cancel the trade first. Bound equipment cannot be removed through
this interaction, and existing equipment-provider veto hooks remain active.
Ordinary inventory-row trading is unchanged. Container/companion behavior still
uses the existing IE item-use route, including its source/pickpocket context.

Offline regression tests execute the shipped equipment handler in all four UI
modes with mocked services; they cover equip/unequip, provider veto, bound items,
merchant ownership, pending transactions, cancelled sales and ordinary barter
routing. The old handler fails this regression. This is not live-game validation.

In game: open barter with an equippable item you own; equip via the doll, then
click it to unequip. Confirm merchant gold and the offer list do not change.
Queue an item for sale and try the doll; cancel the sale and retry. Try an
unpaid purchase on the doll and confirm it cannot be used before completing the
trade. Check regular buy/sell, bound equipment, containers and companions too.
