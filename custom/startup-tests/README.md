# Early Windows startup screen

No extra mod or configuration entry is required. Install the matching engine.

A native, non-activating status panel is created at application entry before
configuration loading and SDL/graphics setup. It paints on its own UI thread
using Windows fonts and GDI, so it requires no game archives, fonts or textures.
It shows elapsed time, an activity indicator and the current initialization stage.
The indicator is not a percentage or evidence of progress inside a blocked stage.
The existing startup work stays on its original threads and in its original order.

The panel is centered on the pointer's monitor and remains above the game during
startup. It does not take keyboard focus. This first implementation is a compact
startup panel, not a full-screen modded wallpaper. OS executable/DLL loading before
application entry cannot display this panel. Slow initialization stages retain
their last label while the panel continues painting.

The panel closes before a configured company video, or after requesting the first
normal loading-screen draw when no video is used. It uses the same entry point
whether launched directly or through the launcher. RAII closes/joins its thread
on exceptions and normal early returns. Window creation/thread-launch failure
leaves the original startup path available. --help/--version (-h/-v) suppress it.
Other platforms retain their existing startup presentation.

No change to frame generation, rendering contexts, navmesh, physics, script order,
or the previously delivered performance/UI features. The panel cannot make startup
work complete faster. Its native UI may add a small startup-only painting cost.

Validation: Linux compilation exercises only the no-op portability path.
The Windows CI gate compiles native code and tests immediate shutdown, disabled
mode, concurrent status updates, HWND creation/destruction, repeated finish and
exception cleanup before building the full engine. CI is not a visual game test.

Live checklist: launch directly and through Launcher; verify panel appears before
archive indexing, elapsed seconds update, and normal loading/menu replaces it.
Check fullscreen/windowed, multiple monitors, high DPI, enabled/disabled company
video, FG enabled, and a failed-startup configuration. Verify no stray window or
process remains on exit. Compare startup duration with the same cache conditions.
