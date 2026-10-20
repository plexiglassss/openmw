OpenMW Frame Generation (openmw-fg) v{VERSION}
============================================

Doubles your framerate in OpenMW by generating an intermediate frame
between every two rendered frames (optical-flow interpolation, like
AMD Fluid Motion Frames — but running natively on OpenMW's OpenGL).

Requires: Windows x64, OpenMW 0.48+ (tested on 0.51.0), any GPU with
OpenGL 4.3 (NVIDIA 600-series+, AMD GCN+, Intel Haswell+).

INSTALL
-------
1. Copy version.dll and openmw_fg.ini into your OpenMW folder
   (next to openmw.exe).
2. Play.

That's it. Delete version.dll to uninstall.

KEYS
----
  F8   - config menu (toggle FG, vsync, tuning; Save writes the ini)
  F9   - fps overlay top-right:  72 -> 144  (game fps -> presented fps)
  End  - quick frame generation on/off

TIPS
----
- openmw_fg.log (same folder) shows live in/out fps stats.
- Fast mouse flicks can smear briefly and thin geometry may shimmer -
  that's inherent to interpolation frame generation. Loading screens
  and teleports are detected and never interpolated.

License: AGPL-3.0 (LICENSE.txt). Uses MinHook (BSD-2) and Dear ImGui
(MIT), fetched at build time. No AMD/NVIDIA SDK code.
