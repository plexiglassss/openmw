# Restore native Options; group only custom settings

The previous grouping replaced all stock tabs and moved their controls. Its
non-stretch wrappers could retain their original narrow width after the window
grew, clipping the Scripts settings panel. Window minimum size could also be
applied after the centering position was calculated.

Preferences, Audio, Controls, Video, Scripts and Language now retain their
original tabs, hierarchy and captions. Existing independent additions such as
Mod Keybindings remain. Unknown replacement-layout pages remain untouched.

Only pages explicitly marked `CustomSettingsPage` are collected into one Custom
tab: Deleveling, Windows and journal, Dialogue colours, Messages, Map icons,
Smart Map and Tooltips. Values and existing event handlers are preserved. The
additional tab uses the existing overflow navigation and does not enlarge the
stock tab-width minimum. The window applies its minimum before centering, and
custom panels follow programmatic and interactive resizing.

The native tests cover unchanged stock widget identity/geometry, original
selection, legacy pages, journal fallback merging, values/click handlers,
scrolling/focus, initial sizing and programmatic resize. The full engine suite
also loads the production XML with real HBox/VBox and LuaAdapter factories and
checks the native Scripts viewport. `verify-layout.py` compares all six native
XML subtrees against the accepted pre-grouping layout and records the preserved
extensions relative to stock RC1. These checks do not claim a screenshot or
gameplay test of a user's installed UI overrides.

Install the complete verified Windows distribution in a separate engine folder.
Keep the executable, DLLs and resources together. Existing user configuration,
mod directories and saves remain in use; no settings reset is needed. Roll back
by launching the previous complete engine directory.

In the game, open Options before resizing it. Check Preferences, every Video
subtab, Language and the Scripts list/detail panel. Open Custom and visit each
section, change a disposable option, close/reopen Options and restore the option.
Check at normal UI scale and after resizing. If an older UI resource override
provides an unmarked custom page, it is intentionally left in its original place.
