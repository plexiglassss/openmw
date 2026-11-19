# Unified mod controls and barter/save feedback

Based on cumulative engine 520a8bcdff36f519b2fe63a2872de132856a1b90.

Options → Controls → Mod Keybindings discovers active registered inputBinding settings,
plus formats exposed by installed ModKeybindings adapters. Open an existing save to include
player/global groups. Newly enabled mods appear after the engine loads their scripts; no
source scan or manually generated installed-mod list is needed. Changes go to the original
setting storage; inputBinding settings retain their binding ID and update OMWInputBindings.
Global settings use OMWSettingsGlobalSet. Settings changes invalidate the page; idle frames
perform no registry scans. The page supports search, independently scrolling mod and
action panels, multiple bindings for adapters that support them, and clearing/canceling.

Reset controls asks for confirmation for the selected mod, or all listed mods when
All mods is selected. Search/conflict filters do not narrow that stated reset scope.
It restores the registered setting defaults and leaves unrelated settings/native
controls alone. Built-in inputBinding defaults are binding IDs, not physical keys;
reset clears their physical mappings to the format's unassigned default. An adapter
may provide `reset(value, argument, default, set)` for mappings stored separately;
otherwise the registered default is written through the original setting store.
Global defaults still use the existing global-event writeback path. Failed resets
are reported rather than counted as restored. A built-in binding ID shared with a
shortcut outside the confirmed reset is left unchanged and reported, so resetting
one mod cannot silently clear another mod's shared mapping.

The RC1 input repair forwards paragraph-label clicks and bridges unconsumed Lua
wheel events to their native scroll host. Native scrollbars and overflowing text
editors retain their own wheel handling. Native regression tests inject real
coordinates and both wheel phases, including search, rebinding, scrolling, reset,
and the diagnostic legacy layout/update modes.

A red binding has overlapping declared contexts and modifier matching. Amber means a
possible conflict whose context or modifier rules are not declared. Native controls are
compared by physical device/code, not their translated labels. Unknown custom formats are
listed; hardcoded/private controls and runtime conditions cannot be inferred universally.
Controller axes/mouse wheels are not represented by this initial button-binding adapter.
The base Keyboard/Controller tabs remain the editors for native controls.

The companion UIToolkit adapter supports BindCustom, including per-setting device limits;
Inventory Extender Redone supplies its menu contexts. These are separate mod updates.
The engine contains no inventory-mod name or custom-format parser.

The recorder owns a press through release before GUI text/navigation and script events.
Release cleanup still reaches native held-state tracking. Escape cancels without clearing
the old binding. Closing settings cancels recording.

Barter uses the configured keyboard Activate key. A quantity modal receives one confirmation;
the same press cannot also submit the offer. Search/text focus, unrelated native modals and
modified/repeated keys keep their normal handling. Lua replacements opt in through
ui.registerUiActionHandler(id, async:callback(function(action) ... return handled end)).
An actual true claims the action; a callback exception is logged and consumes it as well,
preventing an unintended fallback trade. This callback runs in the same synchronous UI phase
as mouse clicks; callbacks must not assume a simulated gameplay frame has run. Game UI
cleanup drops registrations, so mods register again onInit/onLoad. The included IER update
claims only its own open Trade window, blocks unrelated toolkit popups, and submits each
quantity once. Existing controller controls are preserved.

Removal notices have a separate default red colour, configurable in Dialogue colours.
Additions retain their existing green. Native RemoveItem and localized removal templates
use the semantic removal category; English fallback classification remains conservative.

Saving presents a forced frame after capturing the save thumbnail, before serialization.
The progress range reserves space for map/UI serialization and file flush. Saving no longer
uses the delayed important-label toast fallback. No extra game-loading wait stage is added.
The historical source proof reverses only the exact reviewed save function change before
checking the previous map-restore/load implementation.

Native merchant stack collection buckets candidates by RefId and retains the exact existing
symmetric stacks predicate and first-seen ordering. No cross-frame item cache is introduced.
IER also retains tooltip v3.8's removal of redundant quarter-second full inventory refreshes;
merchant equipped checks are memoized only within one inventory snapshot.

Validation: actual Lua module tests, exclusive input-capture/classification tests, differential
old/new stack-loop fixtures, existing native dialogue/UI tests and the cumulative Windows
build/test gates. These do not replace testing the user's installed modlist in game. Report
Windows CI status separately; do not label an uncompleted build as delivered.

The F3 lower history trace is supplied by OSG 3.6.5 StatsHandler: it appends one point per
rendered frame and scrolls when its history width is full. Its fixed logical chart width is
also not the whole width of every aspect ratio. Upper bars represent elapsed time. Shorter
traces/bars alone are not evidence of a Lua engine fault; no cosmetic stretching is applied.
The engine timing history previously used a maximum of 10000 for values stored in seconds,
making it effectively flat. It now uses the same 0.016-second vertical range as OSG's built-in
traversal traces. This corrects the vertical scale, not the history width.
