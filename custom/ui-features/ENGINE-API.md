# UI extensions, first custom test build

All prior engine optimizations and the FG loading-continuity source are retained.

## World tooltip
Player ui.getWorldTooltipObject() returns the engine-selected world object or
nil outside gameplay/without a target. It does not cast a ray.
ui.setWorldTooltip(object, element) queues attachment of a Lua UI Element into
the native world tooltip. Pass nil,nil to detach. Caller owns/destroys the
Element; create with layer='' so it has no independent display layer. One
active provider owns the slot; Inventory Extender aggregates its providers.
The replacement is used only for the matching target, outside GUI mode, and
not in full-help mode. No callback into Lua occurs during native tooltip draw.
Native ownership framing, focus, enabled and visibility gates remain active.

## PaperDoll widget
ui.TYPE.PaperDoll is a player-facing Lua layout widget backed by the existing
InventoryPreview and its RTT texture; no second preview scene is created.
It supports standard position/size/visibility properties. paperDollClick emits
{slot=N}; N is the engine's zero-based equipment slot or -1 for empty space.
Lua owns use/drag decisions and can query types.Actor.getEquipment(player,N).
The integration routes actions through IE's existing provider/use logic.

## Snapping
GUI/window snapping is a native bool setting, default true, exposed under
Settings > Interface. Shared native/Lua registration excludes non-window
children. Pure snapRect chooses the nearest eligible edge on each axis within
12 GUI pixels, requiring nearby/overlapping projections on the other axis.
Raw drag movement remains independent of snapped display placement so users
can pull away. No docking groups or saved inter-window dependencies exist.

## Verification status
Local C++ geometry assertions and Lua 5.4 bridge/parse tests passed. XML parse
and Windows full-engine build are CI gates. Live OpenMW validation is required.
This is an implementation/test-build checkpoint, not a claim all UI cases pass.
