# Startup loading progress repair — 4 November 2026

## Symptom and cause

With the FG DLL present, the startup loading screen could keep showing the first
captured plugin name and progress-bar position while OpenMW continued loading.
The supplied affected recording shows `Morrowind.esm` through approximately
21.5 seconds and reaches the menu around 22 seconds. The comparison recording
shows successive plugin names and also reaches the menu. These are phone
recordings, so their timings are observations, not performance benchmarks.

The redirect activation path seeded the first FG texture from the native back
buffer, then enabled interception of future framebuffer-0 binds. Both texture
attachment and seeding deliberately restored the original framebuffer bindings.
Consequently, a context already drawing to framebuffer 0 stayed there until the
engine explicitly bound it again. Startup's plain GUI camera can render without
another framebuffer bind: OSG only applies a framebuffer object when a render
stage owns one. The engine could therefore draw changing loading text into the
native back buffer while the FG presenter repeatedly received the seeded image.

Relevant source:

- `src/fg/framegen.cpp`: `OnSwapShared`, initial redirect activation.
- `src/fg/frame_contents.hpp`: seeding, activation binding, and ring preservation.
- OpenMW `components/myguiplatform/myguirendermanager.cpp`: plain GUI camera.
- [OSG 3.6.5 RenderStage.cpp](https://github.com/openscenegraph/OpenSceneGraph/blob/OpenSceneGraph-3.6.5/src/osgUtil/RenderStage.cpp): conditional framebuffer application in `drawInner`.

## Change

Immediately after seeding, the game thread reads the physical draw and read
framebuffer bindings through `real_glGetIntegerv`. Each target still bound to
native framebuffer 0 is rebound to the FG framebuffer using the raw driver entry
point. Nonzero engine-owned targets are preserved independently. The redirect
flag is then enabled, so future engine binds continue through the existing hook.

The activation swap still presents the native frame once. Later loading draws
reach the FG texture even when the engine never repeats `glBindFramebuffer(0)`.
This adds two state queries and at most two binds per initialization or resize;
it adds no per-frame copy, CPU readback, wait, interpolation rule, or engine patch.

The existing native-frame seeding, four-texture contents preservation, GPU reuse
fences, screenshot helper suppression, timeline reset, overlay/input handling,
resize recovery, and shutdown repair remain in place. The source base is the
user's cumulative journal branch commit
`cbdf7a0adfac4c33f0d5f5bc62174f3bd3146fd0`.

## Validation and limits

`tests/frame_contents_gl.py` compiles the production helper header and uses real
Mesa OpenGL through EGL. It includes:

- A negative control: after seeding alone, another loading draw changes the
  native default framebuffer while the texture submitted to FG stays stale.
- Immediate activation followed by successive partial loading draws without
  another engine framebuffer bind, including rotation through the four textures.
- Separate default/nondefault read and draw bindings, preserving engine FBOs.
- The earlier retained-image, partial-GUI, producer-overtaking, full-render-resume,
  native-default seeding, MSAA resolve, GL state, and error checks.
- Source integration checks for seed -> bind current targets -> enable redirect,
  and destination GPU fence -> preserve -> attach ordering.

The Windows workflow builds x64 Release with MSVC and runs the DLL-load/version-
export-forwarding smoke test before packaging. `BUILD-ID.txt` identifies the
exact source commit and workflow run; that run records the actual test results.
These checks do not exercise the user's NVIDIA driver, monitor presentation, or
complete running OpenMW installation. Live startup verification remains required.

This is a standalone FG DLL change. It changes no native plugin, load order,
engine executable, save, or Lua runtime. The scoped change was prepared against
OMW-MCP4.2.0, baseline `2026-11-03-r22`; this does not close unrelated modlist holds.

## Install and verify

1. Close OpenMW and back up the existing `version.dll` beside `openmw.exe`.
2. Replace only that `version.dll` with this build. Keep the existing
   `openmw_fg.ini`, engine files, and `lua51.dll`.
3. Launch with FG loaded and watch startup: displayed plugin names and the
   progress bar should advance through the loading process.
4. If the display still sticks, retain `openmw_fg.log` from that launch and record
   the stuck label. Restore the backed-up DLL to roll back.

The End hotkey disables interpolation but retains the hook and presenter. For a
native-presentation comparison, close OpenMW and rename/remove `version.dll`.
