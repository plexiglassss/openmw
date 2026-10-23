# Inventory Extender — OpenMW Bridge v1.2.5

ACTION: Keep this bridge in its own data folder AFTER the restored Inventory Extender base. Restart OpenMW completely. Use engine d204c155208371a3955e8e01e9929ad445c38410 or a later cumulative build. No new engine compilation is required for v1.2.5.

## Separate installation

The restoration ZIP contains two independent mod folders:

1. Inventory Extender - Restored Base/Data_Files
2. Inventory Extender - OpenMW Bridge/Data_Files

Add both Data_Files folders to OpenMW's data directory list, in that order. The bridge must have higher file priority. Do not copy bridge scripts into the base directory. Disable the old contaminated base data entry and earlier bridge/integration data entries; keep their files as a backup outside the active data list. The restored base already includes the October Focus Fix, so its old separate data entry is unnecessary.

Keep InventoryExtender.omwscripts enabled once. There is no additional bridge .omwscripts or ESP to enable. Keep your other tooltip-provider mods and their scripts enabled as before; this package does not reinstall Arms and Armor Compare, Heft, requirements mods or Object Insight Tooltip. Existing provider ordering remains unchanged.

## Cursor behaviour

Click equipped gear on the paper doll with an empty cursor: the ordinary IE pickup action unequips it and attaches the item icon to the cursor. Click the paper doll again to equip that held item. It uses the same asynchronous move and returned-object handling as inventory pickup, including protection tracking. An equipped stack is picked up as a stack. Bound items and provider-rejected actions remain blocked; pending purchases/sales cannot be changed by the paper doll.

All earlier bridge features remain: paper-doll toggle and full-height divider, resize/drag snapping, enhanced world tooltips and ownership frame, equipment comparisons, recent-item icons and item protection.

## Base provenance

The newest full uploaded Inventory Extender found was Inventory Extender (OpenMW)(3).zip (10 September 2026). Its 74 files exactly match the Inventory Extender folder in Inventory_Extender_v46_Stale_Tooltip_Fix_Aligned.zip. The upstream CHANGELOG still says 1.1.0; v46 is our custom revision, not an upstream version.

The restored base contains those 74 files, with only ui/templates/itemTable.lua replaced by the verified Inventory_Extender_Focus_Fix_v1.0.zip correction from 14 October. The Focus Fix's archived original matches the uploaded file exactly. Bridge modifications are stored only in the separate bridge folder.

## Validation and in-game check

Offline: actual cursor/equipment handler checks pass for Interface, Container, Companion and Barter modes; provider veto and bound restrictions pass; existing trade, tooltip, protection, Lua syntax and layout regressions pass. ZIP entries are byte-compared and CRC checked. Live OpenMW testing remains pending.

In game: click equipped armour or a weapon, confirm the portrait updates and the icon stays on the cursor, then click the doll again and confirm it equips and the cursor clears. Also return it to the inventory grid, try a bound item, and repeat while a container or barter window is open. Confirm pending trade reservations and protection remain respected.
