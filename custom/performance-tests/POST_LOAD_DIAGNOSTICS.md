# Automatic post-load freeze investigation

The user reports faster save restoration with a longer frozen scene afterwards. This change records early post-load engine work during a bounded window. It does not claim to fix the freeze or to improve performance by itself.

Base: `plexiglassss/openmw`, `diagnostic-timing-2829ae6d`, commit `9b5235c11f13f2c99495b08c1aa64bb27804eb8d`. This is cumulative with the previous save-load and transition changes. No mod, save format, Inventory Extender, bridge, or frame-generation files change.

## Test after the Windows build succeeds

1. Install the resulting engine build using the normal replacement procedure.
2. Start OpenMW normally and load the save that exhibits the frozen scene. No timing environment variables or external profiler are needed.
3. Keep the game in the foreground. Once the freeze ends, move/look around normally for another 15–20 seconds. Avoid a second load during this capture.
4. Quit and supply the ordinary `openmw.log`. Mention approximately how long the visible freeze lasted and whether the camera responded immediately afterwards. A short video is optional.

One reproduced run can identify the dominant recorded phase; an unchanged-build repeat can check consistency. An unoptimized baseline is not required for this investigation. Installed runtime, hardware, mods and cache state still matter when comparing timings.

## What the new log measures

Sampling arms at the successful end of `StateManager::loadGame`, immediately after the existing `Save load total` line and before the function's remaining local destructors. It covers the following engine frames, including a partial frame when loading completes during input or state handling. It does not measure the exact disappearance of the loading screen. Frame observations include incomplete/minimized frames, explicitly marked `completed=0`.

The window ends at a frame boundary after at least ten elapsed seconds and five frame observations, with a hard limit of 10,000 observations. A first frame lasting twenty seconds is retained and followed by four more observations. The actual elapsed duration and termination reason are logged. The first five observations and subsequent frames of at least 100 ms get detail lines, capped at forty detail lines total, plus the first-completed-frame line and final summary. Cleanup cancels an open capture; the next successful load starts a new sample epoch.

The recorded stages are GC wait, input, sound, Lua synchronized actions, state changes, MWScript/time advancement/recharge, mechanics, physics, world/preloading, GUI, resource maintenance/unref queue/stereo settings, scene-graph traversals, focus, Lua scheduling, rendering traversal, Lua completion and GC scheduling. Actual Lua update execution is also measured in the worker, or on the main thread when Lua threading is disabled.

`cellCalls` counts invocations of the nonloaded branch of `CellStore::load`, attributed to the current phase. It counts attempted instantiations, including scopes unwound on failure; already loaded cells do not count. `outerCellMs` times only outermost nested loads per thread. Nested loads count individually, but their elapsed time is included only in the outer load's phase. This measures deferred cell-reference construction, not all mesh/texture loading. No per-cell names or per-call log lines are emitted.

Interpretation rules:

- Phase durations are inclusive. Cell durations are contained in their parent phase. Never add both as independent costs.
- Lua execution may overlap rendering. `luaFinish` is the main thread's completion call: with a worker it measures waiting; without a worker it contains Lua execution. `gcStart` similarly includes synchronous GC when threading is disabled.
- `frameMs` is engine-frame wall time; `sinceLoadEndMs` includes inter-frame gaps, frame limiting and logging overhead. Phase totals do not include every small unscoped operation, all off-thread work or work outside `Engine::frame`.
- “First completed engine frame” means the engine returned through its normal frame-completion path. It does not prove the GPU displayed a fresh image or that input was responsive. Exceptions caught by the existing engine may still allow that path to complete.
- Scopes opened under a previous active sample are discarded after a reload. Their tail may therefore be unattributed in the new partial frame, but old load/worker costs cannot contaminate the new sample. A scope opened while sampling was inactive counts only its time after arming.
- Missing log messages and gaps between unrelated script messages are not freeze measurements or evidence against the adjacent mod.

## Verification and limits

The production recorder tests exercise inactive sampling, mid-phase arming, reload epochs, cancellation, partial/incomplete frames, long first-frame stalls, window and output bounds, nested cell loads, and simultaneous worker updates. A second test compiles the actual engine logging adapter and checks its output plus successful-load/cleanup integration. Both are wired into Linux and Windows CI. Local GCC tests passed with warnings treated as errors. The full Windows engine compilation and the user's live capture are separate checks.

Code inspection found no changed execution order, new engine wait, deferred-work policy, script behavior or save serialization. Sampling uses fixed arrays, brief metric mutex sections, and bounded log formatting; engine and logger calls occur outside the metric mutex. When inactive, scopes use atomic checks and phase bookkeeping. This has not been benchmarked in the user's installation.

Independent review: `/root/review_postload_findings` reviewed the design and implementation. Its findings prompted explicit attempted-cell-load wording and an added maintenance phase. Final review details are stored with the evidence.

## Evidence and remaining work

The prior logs show `Save load total` falling from 15,959.2 ms on `ec015282bb` to 4,717.64 ms on `275731d678`, and CSTA restoration from 10,104.5 ms to 720.563 ms. They are different save states, not a controlled A/B. These values support reduced restoration cost, not a measured total time to responsive gameplay. The user's overall improvement and increased post-load freeze remain the runtime observations.

The next engineering decision depends on the new capture: deferred cell loading in an engine query, script work, Lua actions, rendering, or a wait require different remedies. Generic engine optimization remains the scope. No location, individual MWScript, quest or console-command special case is added.

Governance is OMW-MCP-2.0.0, baseline 2026-10-24-r18, read from current authority and release-hash verified. This is a bounded engine diagnostic change, not a completed installation-wide mod audit. The retained execution ledger and completion receipt keep the broader audit open; protected acceptance is unavailable. Runtime freeze cause, responsive-gameplay timing, and performance improvement from any subsequent remedy remain unresolved. Earlier mod and geometry audit statuses are unchanged.
