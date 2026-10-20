# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A frame-generation injector for OpenMW (Windows, x64). Builds a single DLL named `version.dll` that is dropped next to `openmw.exe`; it doubles the presented framerate by interpolating between rendered frames with optical-flow compute shaders. No OpenMW source changes — everything happens through API hooking inside the game process. OpenMW source (for reference reading only) lives at `G:\Modding\_Github\openmw-stuff\openmw`.

## Build & test

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Output: `build/Release/version.dll`. MinHook and Dear ImGui are pinned via FetchContent (network needed on first configure). Static CRT (`MultiThreaded`) — do not add dependencies that force the dynamic runtime.

There are no unit tests; verification is behavioral:

1. Copy `build/Release/version.dll` (+ `openmw_fg.ini` if absent) into `G:\Modding\Morrowind\OpenMW 0.51.0\` (the copy fails while the game is running — that's the user playing, wait).
2. Launch `openmw.exe` from that directory, let it run ~20 s, kill it.
3. Read `openmw_fg.log` there. Healthy startup sequence: `hooks: installed` → `fg: GL_VERSION=...` → `overlay: ImGui ready` → `fg: presenter ready` → `fg: framebuffer redirect ACTIVE` → periodic `fg: in=X fps out=2X fps ... dropped=0`. `out ≈ 2×in` is the pass criterion.

## Architecture

Two threads, two GL contexts, one lie:

- **Injection**: `version.def`/`version_proxy.cpp` export the real version.dll API (forwarded from system32). SDL2.dll's static `VERSION.dll` import loads us because the game dir precedes system32 in DLL search order (works only because version.dll is not a KnownDLL — same reason opengl32.dll proxying was rejected). `dllmain.cpp` arms hooks only when the host exe is `openmw.exe`; the launcher also loads the DLL but stays passthrough.
- **Hook layer** (`hooks.cpp`): MinHook on `gdi32!SwapBuffers`, `opengl32!wglSwapBuffers` (TLS reentry guard — treat them as aliases), `wglGetProcAddress` (returns wrappers for `glBindFramebuffer`/`EXT` so OSG's resolved pointer is ours), `glGetIntegerv`/`glDrawBuffer`/`glReadBuffer` (state-cache lies, below), `SetCursorPos`/`ClipCursor` (suppressed while the ImGui menu is open). Hooks install at process start from a DllMain-spawned thread — before OpenMW creates its GL context, which is what makes the `wglGetProcAddress` interception land.
- **The lie** (`framegen.cpp` remap/spoof functions): once redirect is armed, every game bind of framebuffer 0 is remapped to a replacement FBO whose color attachment is one of 4 shared full-res textures. `glGetIntegerv(GL_*_FRAMEBUFFER_BINDING)` reports 0 for it and `glDrawBuffer/glReadBuffer(GL_BACK)` map to `GL_COLOR_ATTACHMENT0`, so OSG's lazy state cache never desyncs. All remaps check `wglGetCurrentContext() == gameCtx` so the presenter context is unaffected.
- **Game thread** (`fg::OnSwap`): swap becomes fence + submit + rotate to a texture not in `{pending, presenterCur, presenterPrev}` (4 textures make one always free), then returns without presenting — the game never blocks, sim rate is untouched. Single-slot mailbox: a still-pending submission is dropped (fence deleted, `dropCount++`).
- **Presenter thread** (`Presenter::run`, own shared GL context on the same window): waits on the submit event, `glWaitSync`s the fence, runs the pipeline, presents interpolated frame, busy-waits until `submitTime + ema/2`, presents the real frame. Presents are fullscreen-triangle *draws*, never `glBlitFramebuffer` — the default framebuffer may be multisampled (`antialiasing > 0`) and non-MSAA→MSAA blits are GL errors.
- **Pipeline** (`shaders.hpp`, all GLSL 430 compute): half-res luma pyramid → coarse-to-fine 4×4-block SAD matching (flow in *level-local pixel units*, ×2 per upsample; explicit zero-motion candidate with a bonus keeps UI/static solid) → 3×3 smooth → bidirectional warp at t=0.5 with occlusion fallback to the real frame. A coarsest-level luma diff in an SSBO gates scene cuts (loading screens) into frame duplication — GPU-side, no CPU readback.
- **Overlay** (`overlay.cpp`): ImGui lives entirely on the presenter thread/context. Input arrives via a `WH_GETMESSAGE` hook on the game window's thread through a locked event queue; while the menu is open the hook nulls input messages (game input-paused) and the cursor hooks stop SDL's relative-mouse recentering. F8 menu, F9 fps readout, End FG toggle.

## Conventions & gotchas

- **Two kinds of GL pointers.** `glFoo_` (from `gl_min.hpp`'s X-macro loader) are raw driver entry points loaded through the *unhooked* `real_wglGetProcAddress` — internal calls bypass our own remapping. `real_*` (from `hooks.hpp`) are MinHook trampolines for the hooked 1.1 exports. Never call plain `glBindFramebuffer`-style hooked functions from FG code; use the raw/trampoline pointers or you'll recurse through your own remap.
- GL enums/typedefs are hand-declared in `gl_min.hpp` (no glext.h dependency). Watch for name collisions with `GL/gl.h` legacy enums — `GL_LOAD` (accumulation buffer) already bit once.
- Resize, minimize, and presenter failure all funnel through `teardownOnGameThread()` + full lazy re-init on a later swap; there is deliberately no in-place resize path.
- Objects and ownership: textures/programs/sync objects are shared across contexts, FBOs and VAOs are not — the replacement FBO lives in the game context, the present VAO and sized resources in the presenter context. Fences are created on the game thread, consumed and deleted on the presenter.
- Config (`config.cpp`) is plain globals read every frame — most settings hot-apply for free; `present_vsync` and `max_levels` are applied by presenter-loop change detection. `ConfigSave()` rewrites the ini (menu "Save settings").
- Logging is the only observability: `Log()` → `openmw_fg.log` next to the DLL, timestamped with thread ids. When touching threading, log thread ids.
- Deployed ini at the game dir is the user's live config — don't clobber it when redeploying, only the DLL.
