# Diagnostic timing build

User action: launch this build with OPENMW_TIMING_FILE set to an absolute CSV path.
Base: 2829ae6de450cd8ed6082bb7090af87587ce06bd. Existing performance/GC,
launcher, dialogue and Frame Generation source changes are retained.

This optional recorder does not enable the Lua instruction/memory profiler and
works with `lua profiler = false`, preserving the runtime's allocator choice.
Keep lua debug false. No mods, content entries or saves are edited.

## Capture

Keep the previous build. Extract the diagnostic runtime into a separate folder.
Retain the installed Rubic0n lua51.dll and the intended FG version.dll/INI;
the engine CI package includes the standard lua51.dll and no FG version.dll.
Keep all other settings fixed. Set OPENMW_TIMING_FILE alongside the usual
OPENMW_OSG_STATS_FILE and OPENMW_OSG_STATS_LIST=times;resource variables in
the PowerShell process that starts openmw.exe. Close existing game processes first.
Use a unique directory per run. Check that timing CSV and stats are nonempty after
normal exit. Copy the matching openmw.log and settings.cfg before another launch.

Record separately: stationary after loading settles; a repeatable walking route;
first conversation with the BQC NPC followed by a repeat conversation. Record the
approximate wall-clock times and actions. This CSV uses elapsed steady-clock
microseconds, not wall-clock timestamps; frame IDs join it to engine stats.

## Columns and interpretation

- thread: recorder thread ID, not Windows TID; frame: engine frame ID, -1 before assignment.
- category: lua, lua-load, mwscript, action, gc, phase, wait, recorder.
- source: script VFS path, legacy script ID, queued-action origin, or engine phase.
- callback: engine/event name, lifecycle name, timer, or generic call.
- calls, total_us, self_us, max_us: aggregated per thread/frame/category/source/callback.
- first_us, last_us: first start and last finish relative to recorder initialization.

Elapsed time is NOT CPU time. Inclusive times overlap parent phases and callbacks;
do not sum them. Self time subtracts instrumented child scopes only. Native calls
and uninstrumented Lua interface/helper calls stay charged to their caller. Queued
actions retain the instrumented origin at enqueue time; no traceback is collected.
Actions without a known origin remain unattributed. Multiple actor instances of
the same script are aggregated. Legacy timings include script-manager work, not
opcode-level attribution. Worker GC is tagged with the worker's most recent frame;
it can overlap the main thread starting the next frame. Nonthreaded finishUpdate
includes execution, rather than just waiting. Last partial frames flush at thread exit.

Recorder flush rows measure aggregation formatting, lock wait and disk flush;
per-call timer/map/string bookkeeping also costs time and is not fully measured by
those rows. Compare an otherwise identical run with OPENMW_TIMING_FILE unset to
estimate total overhead. This is diagnostic instrumentation, not an FPS optimization.
Output failures can produce incomplete files. A crash may lose the current frame.
Normal exit and checking output integrity are required. Keep captures short initially.
Frames retain separate rows, but output writes are now batched per thread until
1 MiB or one second at a frame boundary. Thread exit flushes pending data.
An abnormal exit can therefore lose buffered frames; no background writer is added.

## Validation

custom/timing-tests/smoke.cpp and verify.py check nesting/exclusive accounting,
aggregation, exceptions, CSV quoting, two threads, frame IDs and final partial flush.
The dedicated Windows workflow runs these before the complete engine build/tests.
No live game, Rubic0n DLL or NVIDIA validation is implied by CI success.

## 22 October combined diagnostics and responsiveness change

- Windows freeze warnings wait for 60 continuous seconds of reported window
  unresponsiveness. Recovery resets the timer. The Windows hung-window detection
  delay and monitor polling are additional. Crash notification/dump handling and
  its handshake timeouts are unchanged. The warning still permits Abort.
- Timing CSV writes are batched per thread (1 MiB or one second, evaluated at
  frame boundaries), preserving separate frame IDs and flushing on thread exit.
  The recorder measurement includes formatting and writes when performed; it
  still does not measure all per-scope bookkeeping overhead.
- Standard activation records the object ID and separates creation of its action
  from execution. Teleport, dialogue opening, scene load/unload and exterior-grid
  transitions have nested scopes. Async rendering-ray actions now have a name.
  This is attribution, not an unverified change to activation or queue behaviour.
- Start-Profiling.ps1 captures stderr (including a checkpoint after application
  wrapper cleanup) and copies fresh openmw*.dmp files from configured/user/temp/
  runtime roots after abnormal exit. Dumps can be large. Existing unrelated or
  older dumps are not copied. No dump being present is not proof of clean exit.
- Both prior captures exited with -1073740791 after the engine destructor's
  peaceful-quit message. No crash stack was supplied, so the cause is unresolved;
  this revision does not claim a shutdown crash fix.

The smoke test now checks the 60-second boundary and recovery reset without
sleeping, plus preservation of 128 buffered frames and final thread flushing.
