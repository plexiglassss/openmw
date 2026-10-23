# Border-style follow-up v1.2.3

Replaced the tinted white-image divider with I.MWUI.templates.verticalLine, the
vertical counterpart of the horizontalLine template already used by inventory.
Reparented it from the body to the window-content level. The template's relative
height now spans the full inner window, eliminating the body/header offset at the
top in full-height mode. Native border textures and thickness are retained. The
below-filter alternative starts below its filters to avoid drawing through them.
Layout and syntax checks pass; live appearance remains to be verified.

# Toolbar toggle follow-up v1.2.2

Added the paper-doll visibility action as a character icon after Search and before
the wrench. Only the player's Inventory gets this button. Hiding collapses the
portrait column to zero and hides its separator; showing restores the layout.
Compact filters account for the extra button width. The top-left text button is
still removed. Replace only the integration when already on the v1.2 engine.

# Layout follow-up v1.2.1

Replace the integration package only if already using the v1.2 engine. Removed
the Character button and its 30-pixel reserved strip; top-aligned the portrait and
added a thin gold separator with pointer events disabled. The separator follows
both layout modes and window resizing. The layout regression now checks the top
position and divider extent. Live visual verification is pending.

# UI integration repair audit — 23 October 2026

Action required: update both the cumulative engine and the v1.2 integration files.
These repair defects in our earlier integration, not defects blamed on your setup.

## Evidence

Inspected both supplied screenshots directly. The inventory image shows literal
NEW prefixes. The world image shows an owned-colour tooltip frame without text.
The paper-doll timing/resizing and snapping failures were reported by the user;
those behaviors cannot be established from a still image alone.

## Findings and corrections

1. Recent items: table/grid renderers inserted localized text. Replaced that with
   a private 32x32 transparent gold-sparkle PNG, displayed at badge size. No font
   glyph or globally shared icon is replaced. Tracking/protection are unchanged.
2. Preview refresh: the custom widget only assigned the shared texture when its
   Lua properties updated and relied on native inventory notifications to redraw.
   Asynchronous Lua equipment changes could outlast that refresh. A visible widget
   now synchronizes against the 19 actual engine slots at the GUI frame boundary.
   A changed slot requests one preview update; unchanged slots do not. This also
   handles unequip, rather than treating clicks as proof an equip already happened.
   The existing shared renderer is retained; no second avatar renderer is created.
3. Sizing: the Lua layout imposed a 192-pixel portrait-column cap and sampled the
   viewport size of the hidden native inventory. Removed the cap and update the
   shared viewport from the current visible portrait size. UV and item selection
   use that viewport. Native inventory sizing resets the custom viewport state.
4. Layout: added a persisted Window-options checkbox. Full-height mode allocates
   the left column independently of filters and the bottom information bar. Normal
   mode places the portrait below filters. Proportions are preserved at narrow sizes.
5. Empty tooltip: Lua WidgetExtension::updateVisible bakes inherited visibility
   into each widget. The adapter attached under a hidden host and never traversed
   those descendants after the host became visible. Added an adapter refresh after
   showing the host. Also removed unconditional re-showing of the ordinary dynamic
   frame in the world-tooltip path. Fallback stays available.
6. Snapping: the original integration covered native Windows and LuaWindow drag
   handlers, but IE uses a generic widget and its own Lua mouseMove calculations.
   Added an opt-in windowSnapTarget property with registration cleanup and a
   snapWindow API. IE marks its actual window roots and calls this only on moving,
   preserving the unsnapped pointer origin. Targets use inherited visibility and
   both drag directions share the existing engine toggle and rectangle algorithm.

## Validation

- Existing shipped Lua syntax, equipment-handler, protection/recent tracking and
  world-comparison regression tests pass.
- test-integration-layout.py executes the shipped updateSize and custom drag
  callback: resizing, full-height option, hidden portrait, small windows, engine
  snapping call, disabled behavior, old-API fallback, and resize exclusion.
- test-native-integration.py compiles the actual syncPaperDoll, adapter refresh and
  tooltip-display code against stateful doubles. It reproduces hidden descendants
  remaining hidden after only showing the outer box, then checks the correction.
  It covers delayed equip, unequip, unchanged-frame redraw suppression, viewport
  growth/caps and missing-player guards.
- Both new tests are CI gates; the native method harness runs with GCC and MSVC.
- These tests do not render a real MyGUI/OSG scene. Earlier mocked tests missed the
  independent IE drag path, inherited visibility and asynchronous equipment timing.
  New coverage narrows those gaps but does not substitute for the README live test.
- No global shared assets, quest records, save formats or navigation data changed.
