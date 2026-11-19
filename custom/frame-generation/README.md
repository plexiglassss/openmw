# OpenMW Frame Generation (openmw-fg)

Frame generation for [OpenMW](https://openmw.org/) as a drop-in mod — no engine rebuild, no source patch. Doubles presented framerate by interpolating between rendered frames with an FSR3-style optical-flow pipeline running in OpenGL compute shaders. Windows and Linux.

Verified on OpenMW 0.51.0, Windows 11, NVIDIA RTX 4080: game rendering 59 fps → 118 fps presented, zero dropped frames.

## Why not just use DLSS-G / FSR3 / OptiScaler?

OpenMW renders with **OpenGL**. DLSS Frame Generation requires DX12, AMD's FSR3 SDK requires DX12/Vulkan, and injector tools like OptiScaler hook DXGI — none of them can touch a GL swapchain. OpenMW also has no motion vectors, no camera jitter, and no render-scale hook, so the official upscaler SDKs have nothing to consume. This project implements the frame-interpolation technique natively against GL 4.3 compute instead.

## How it works

- **Windows**: ships as a `version.dll` proxy. `SDL2.dll` imports `VERSION.dll`, the loader finds ours in the game directory first, and we forward the real API from `system32`. Classic mod-style injection (same trick as SKSE-era loaders) — delete the DLL to uninstall. MinHook hooks `SwapBuffers`/`wglSwapBuffers` and `wglGetProcAddress`.
- **Linux**: ships as `libopenmw_fg.so` loaded via `LD_PRELOAD`. The dynamic linker binds OpenMW's `SDL_GL_SwapWindow`/`SDL_PollEvent` calls and OSG's `glXGetProcAddressARB`/`glGetIntegerv` lookups to our exported interposers — no code patching at all. X11/GLX (XWayland included).
- The `glBindFramebuffer` the engine resolves is wrapped so every bind of framebuffer 0 is silently redirected into an offscreen texture (with `glDrawBuffer`/`glReadBuffer`/`glGetIntegerv` remapped/spoofed to keep OSG's state cache coherent). The game never touches the real backbuffer again.
- The game's `SwapBuffers` call becomes a non-blocking submit: fence + texture handoff to a **presenter thread** with its own shared GL context. The game thread never waits, so simulation rate is unaffected.
- The presenter computes optical flow between the last two frames (half-res luma pyramid, coarse-to-fine 4×4 block matching, zero-motion bias to keep UI/static geometry solid, 3×3 smoothing) and synthesizes the midpoint frame by bidirectional warping with occlusion fallback. It presents the generated frame, waits half the measured frame interval (EMA-paced), then presents the real frame.
- Scene cuts (loading screens, teleports) are detected with a coarse luma diff and fall back to frame duplication instead of hallucinated interpolation.

## Install

### Windows

1. Copy `version.dll` and `openmw_fg.ini` next to `openmw.exe`.

### Linux

1. Extract `libopenmw_fg.so`, `openmw_fg.ini` and `openmw-fg.sh` anywhere.
2. Launch through the wrapper (`./openmw-fg.sh [/path/to/openmw]`) or set `LD_PRELOAD=/path/to/libopenmw_fg.so` yourself. Log and ini live next to the `.so`.

Steam Deck: native Linux OpenMW (flatpak/AppImage/distro) uses this build. If you run the *Windows* OpenMW under Proton instead, use the Windows zip and add `WINEDLLOVERRIDES="version=n,b" %command%` to the launch options (Wine defaults `version.dll` to its builtin).

### Then

2. Play.
   - **F8** — in-game config menu (Dear ImGui): FG toggle, fps overlay, vsync, scene-cut threshold, search radius, pyramid depth — all hot-applied, `Save settings` writes them back to the ini. Game input is paused and the cursor freed while the menu is open.
   - **F9** — fps overlay top-right: `72 -> 144` (original → presented).
   - **End** — quick FG on/off.
   - Debug: `overlay=1` flashes a small green square bottom-left on generated frames only (useful to confirm interpolated frames are really being presented).
3. Check `openmw_fg.log` in the same folder for `in=... out=...` stats.

Recommended OpenMW settings: leave `vsync mode = 0` (the game's swap is bypassed anyway; `present_vsync` in the ini controls the real one) and cap `framerate limit` to half your refresh rate for the smoothest pacing (e.g. 72 on a 144 Hz display).

**If the game already runs at your monitor's refresh rate, frame generation cannot raise the displayed fps** — the display is the ceiling. Worse, when presents are display-synced (driver vsync, G-Sync/FreeSync, or DWM composition — common even with vsync "off"), presenting two frames per game frame would force the game down to half refresh. The mod detects this automatically (`auto_pause=1`): it briefly probes with interpolation off, and if the base fps jumps back up it idles the interpolation (fps overlay shows `fg idle`) until the game actually drops below half refresh — interiors, dense mod setups, weather — where doubling genuinely helps.

## Config (`openmw_fg.ini`)

| key | default | meaning |
|-----|---------|---------|
| `mode` | `async` | `off` = hooks idle, pure passthrough |
| `hotkey` | End | runtime toggle — VK code on Windows (`0x23`), SDL scancode on Linux (`77`) |
| `menu_key` | F8 | config menu — `0x77` (VK) / `65` (scancode) |
| `fps_key` | F9 | fps overlay toggle — `0x78` (VK) / `66` (scancode) |
| `present_vsync` | `0` | swap interval on the presenter context |
| `overlay` | `0` | debug: green square on generated frames |
| `fps_overlay` | `1` | fps readout top-right (original → presented) |
| `menu_scale` | `1.5` | ImGui font/widget scale |
| `scene_change_threshold` | `0.12` | mean luma delta treated as a scene cut |
| `search_radius` | `2` | block-match search radius per pyramid level |
| `max_levels` | `6` | optical-flow pyramid depth cap |
| `stats_interval_sec` | `5` | log cadence |
| `auto_pause` | `1` | idle interpolation when the game already reaches the display refresh (see above) |
| `display_hz` | `0` | refresh rate used by `auto_pause`; `0` = auto-detect |

## Build

Windows (MSVC):

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Output: `build/Release/version.dll`. MinHook and Dear ImGui are fetched automatically.

Linux (needs `libgl1-mesa-dev`, `libglx-dev`, `libsdl2-dev` headers — SDL2 is not linked, the game provides it):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Output: `build/libopenmw_fg.so`. `./release-linux.sh <version>` builds and packages the release zip.

## License

[AGPL-3.0](LICENSE). Dependencies are fetched at build time and keep their own licenses: [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause), [Dear ImGui](https://github.com/ocornut/imgui) (MIT). No AMD/NVIDIA SDK code is used — the frame interpolation is an original implementation.

## Limitations

- Interpolation-only FG (like AMD Fluid Motion Frames): the HUD is part of the interpolated image, so fast camera pans can wobble UI edges slightly. Engine motion vectors would need OpenMW source changes — the architecture here leaves room to feed depth + camera-derived motion later.
- Generated frames add ~half a frame of latency to real frames (inherent to interpolation FG).
- Window resize / mode change triggers a full re-init (one-frame glitch).
- Requires a GL 4.3+ capable driver (any modern NVIDIA/AMD/Intel). If unavailable, the mod logs it and passes through untouched.
- `openmw-launcher.exe` also loads the DLL (via SDL2) but the hook logic only arms inside `openmw.exe` (same on Linux: the interposers only arm when the process is `openmw`).
- Linux build is X11/GLX only. Wayland desktops work through XWayland (SDL picks it automatically; force `SDL_VIDEODRIVER=x11` if the log reports no GLX context).


### Verification diagnostics (patched build)

Set `diagnostics_overlay=1` or enable **Detailed verification stats** in the F8 menu to show live native/presented FPS, the FG ratio, and cumulative native/generated/presented/dropped counters. The default is off.
