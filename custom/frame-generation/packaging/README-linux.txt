OpenMW Frame Generation (openmw-fg) v{VERSION} — Linux x86_64
=============================================================

Doubles your framerate in OpenMW by generating an intermediate frame
between every two rendered frames (optical-flow interpolation, like
AMD Fluid Motion Frames — but running natively on OpenMW's OpenGL).

Requires: Linux x86_64, OpenMW 0.48+ (tested on 0.51.0), any GPU with
OpenGL 4.3 (NVIDIA 600-series+, AMD GCN+, Intel Haswell+), X11 session
(on Wayland it runs through XWayland, see notes).

INSTALL
-------
1. Extract libopenmw_fg.so, openmw_fg.ini and openmw-fg.sh into any
   folder (e.g. next to your openmw binary, or ~/openmw-fg/).
2. Launch OpenMW through the wrapper:

       ./openmw-fg.sh                  # uses 'openmw' from PATH
       ./openmw-fg.sh /path/to/openmw  # explicit binary

   Or set the preload yourself:

       LD_PRELOAD=/path/to/libopenmw_fg.so openmw

That's it. The log and ini live next to the .so. Remove the preload
to uninstall — nothing else is touched.

KEYS
----
  F8   - config menu (toggle FG, vsync, tuning; Save writes the ini)
  F9   - fps overlay top-right:  72 -> 144  (game fps -> presented fps)
  End  - quick frame generation on/off

(Rebindable in the F8 menu; stored as SDL scancodes in the ini.)

NOTES
-----
- openmw_fg.log (next to the .so) shows live in/out fps stats.
- X11/GLX only. On a Wayland desktop SDL normally picks XWayland
  automatically; if the log says "no GLX context", launch with
  SDL_VIDEODRIVER=x11 in the environment.
- Flatpak OpenMW: put the files somewhere the sandbox can read and run
  flatpak run --env=LD_PRELOAD=/path/libopenmw_fg.so org.openmw.OpenMW
- Steam Deck: this build is for NATIVE Linux OpenMW. If you run the
  Windows OpenMW under Proton, use the Windows zip instead and add
  WINEDLLOVERRIDES="version=n,b" %command%  to the launch options.
- Fast mouse flicks can smear briefly and thin geometry may shimmer -
  that's inherent to interpolation frame generation. Loading screens
  and teleports are detected and never interpolated.

License: AGPL-3.0 (LICENSE.txt). Uses Dear ImGui (MIT), fetched at
build time. No AMD/NVIDIA SDK code.
