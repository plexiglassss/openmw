# v1.2.4 — resize snapping and tooltip fixes

Requires the new v1.2.4 engine commit for resize snapping and paper-doll hover. Copy this cumulative Data_Files integration over the previous version, retaining the original Inventory Extender installation. Do not stack multiple integration versions.

- Inventory Extender and native windows snap their resized edges to nearby registered windows and screen edges, using the existing Window Snapping option. Opposite edges stay fixed; minimum sizes are respected.
- Hovering equipment on the paper doll uses the native equipment hit test and Inventory Extender item-tooltip providers. Empty areas, dragging, leaving the portrait and clicking dismiss the tooltip.
- World item tooltips remove IE's opaque outer box only. The engine supplies one frame and its normal ownership background, according to Show Owned settings. Internal progress-bar borders remain.
- The full-height native divider and toolbar character toggle from v1.2.3 remain.

Offline checks pass; Windows engine compilation and live rendering remain separate checks. Test each resize edge/corner with snapping enabled and disabled, hover equipped gear and empty portrait areas, unequip by clicking, then look at owned/unowned items and compare equipment. Confirm one outside border and the normal red ownership background.

The screenshot shows Spanish text for light_com_candle_08_64 in both tooltip systems. No name translation was added by this integration. The exact winning plugin supplying that name has not been established; no item records are changed here.

---

# Inventory Extender custom integration v1.2.3 — offline test build

Action required: replace the previous custom Inventory Extender integration
package. This layout update uses the same engine as v1.2; no newer engine is needed. Keep the original Inventory
Extender installed. No plugin change, new .omwscripts entry, save conversion or
navmesh regeneration is required.

## Installation

1. Install the cumulative Windows engine build containing the v1.2 UI repairs.
   The preceding performance build 5bef26f does NOT contain these repairs.
2. Replace your previous custom integration folder with this package. Do not keep
   v1.0/v1.1 as additional active integration folders.
3. Keep the original Inventory Extender and its existing script entry enabled.
   Add this package's Data_Files directory AFTER original Inventory Extender and
   any older overrides, so its scripts and private icon win.
4. Keep Object Insight Tooltip installed if you use its extra statistics; the
   optional bridge is included. Inventory Extender comparison works without it.
5. Enable Enhanced world tooltips and Window snapping under the engine's Interface
   settings. For equipment comparisons, choose Inventory Extender > Window options
   > Compare equipped item > Hold ALT or Always. Existing Never preferences remain.

## Changes in v1.2.3

The divider now uses the same MWUI vertical border template and installed border
textures as the other UI borders. No custom color, transparency or white-texture
line is used. In full-height mode it attaches at the window-content level and
runs from the top inner border to the bottom inner border, including the header
space that previously left a gap. It still resizes and hides with the portrait.
The alternate below-filter layout keeps the divider below its full-width filters.
This is a cumulative integration-only update; keep the v1.2 engine.

## Changes retained from v1.2.2

A character silhouette button sits immediately after the search field and before
the wrench. Click it to hide/show the paper doll. Its tooltip changes between
Hide character and Show character. Hiding also removes the separator and gives
the inventory the freed width. The button remains accessible while hidden.

The former Character text/button and reserved portrait header strip remain gone.
The portrait starts at the top, with a thin gold vertical separator. Both layout
modes are retained. This is a cumulative replacement for v1.2/v1.2.1; it needs no
engine update beyond the v1.2 engine.

## Changes retained from v1.2

- Newly acquired item records display a small gold sparkle image instead of NEW,
  in both table and grid views. Existing recent-item tracking and Clear NEW remain.
- The visible paper doll watches the actual equipped slots. It refreshes after an
  equip or unequip has taken effect, including clicking the doll, without waiting
  for another item action. Unchanged equipment does not request repeated redraws.
- Resizing the inventory resizes the portrait and its rendering viewport, keeping
  the character's aspect ratio. The old 192-pixel width cap is removed.
- Full-height character column is enabled by default under Inventory Extender >
  Window options. Filters and the information bar start to its right. Disable it
  for the portrait below full-width filters. The column uses the available height; very narrow windows
  preserve character proportions rather than stretching the image.
- World tooltip contents recover their visibility after attachment to the native
  hidden tooltip host. The normal empty frame is no longer re-enabled over them.
- Inventory Extender's custom title drag handler now uses engine snapping, and its
  windows register as targets for native windows too. Hidden targets are ignored.
  Snapping remains controlled by the engine option; resizing is not snapped.

## Previous features retained

The package includes enhanced world item tooltips, equipped-item comparison,
paper-doll equipment handling, recent-item marks and independent protection for
favourite and equipped items. Comparison reuses Inventory Extender's providers and
slot rules, including both rings. ALT switching is immediate. Ordinary unchanged
tooltip content refresh remains throttled. Unsupported or failed providers retain
single-item/native fallback. Object Insight supplements the targeted item only.

Recent marks track net inventory-count gains by record ID using existing refreshes.
All copies of that record share the mark; this is not a timestamped acquisition
log. Marks persist in saves until cleared. Acquisition and removal between two
refreshes can cancel out. No permanent Lua storage or new inventory polling loop
was introduced for recent items.

Protection covers outgoing transfers, sales and drops through Inventory Extender,
including drag and bulk paths. Equipping/unequipping and returning to your own
inventory remain allowed. Other mods' independent UIs, quest scripts, consumption
and confiscation are outside these protections.

## Focused in-game test

1. Acquire an item: check the gold sparkle in table and grid views, then Clear NEW.
2. Equip one weapon with inventory open. The doll must show it without another
   action. Click the weapon on the doll to unequip: it must disappear immediately
   after the action completes. Repeat with armor and in barter; ensure no sale.
3. Resize taller, wider and smaller. Check character proportions and item clicks.
4. Toggle Full-height character column off/on. Verify filters and information bar
   move to the expected side and no controls overlap the portrait.
5. Look at the lantern or another item in the world. Verify actual text, not an
   empty box; change targets repeatedly. Hold/release ALT over equipment. Check
   owned and unowned items, and disable Enhanced world tooltips to verify fallback.
6. Drag Inventory Extender near the map/statistics/spell windows and another IE
   window. Check alignment within 12 logical UI pixels. Also move a native window
   towards IE, then disable Window snapping and verify free movement.

Offline tests pass. These repairs have NOT yet been verified in a live OpenMW
session at your UI scale. Full Windows compilation is a separate CI gate.
