OPENMW RESPONSIVENESS CAPTURE - 4 NOVEMBER 2026

Start with one capture of the way you currently play. This uses the diagnostics
already present in your current custom engine. No engine rebuild is required.

1. Extract this folder anywhere writable. Close OpenMW and its launcher.
2. Double-click START_CAPTURE.cmd. It finds the installation from your latest
   OpenMW log, or asks you to select openmw.exe if it cannot find it. The engine
   path is printed in the capture window. Do not select openmw-launcher.exe.
3. Leave your current graphics, mods, frame generator and limiter unchanged.
   Load your usual save and wait until movement responds. Spend about five minutes:
   - stand and turn in one place for about 20 seconds;
   - walk the usual route where it feels sluggish for about 60 seconds;
   - use an interior/exterior door in both directions;
   - open dialogue, inventory and journal, then close each;
   - open Options, then close it (this can itself produce a pause);
   - repeat the same walking route once, then exit.
4. Quit OpenMW normally. Add an optional short note when the collector asks.
   Send the ZIP it selects in Documents\OpenMW Responsiveness Captures.

Do not reset quests, remove mods, clear caches or change security software for
this baseline. The collector does none of those things. It does not copy saves,
Lua storage contents, executables, DLLs or memory dumps.

OPTIONAL ACTION MARKERS
You can label the trace without leaving the game. These are optional:
  Ctrl+Alt+1  stationary segment starts
  Ctrl+Alt+2  walking/turning segment starts
  Ctrl+Alt+3  door transition starts
  Ctrl+Alt+4  dialogue starts
  Ctrl+Alt+5  inventory starts
  Ctrl+Alt+6  you notice sluggishness or a pause
  Ctrl+Alt+7  declare that you just turned FG interpolation OFF
  Ctrl+Alt+8  declare that you just turned FG interpolation ON

The last two only LABEL your chosen FG state. They do not toggle FG. Do not
change FG for the first baseline capture. Skip any marker shortcut already
used by one of your mods. A note after exit is enough if you prefer.

WHAT IS RECORDED
The engine's per-frame OSG timings/resource counters and per-script/phase timing
CSV; fresh game/FG logs; stdout/stderr and process exit code; relevant config
files before/after; loaded config/data/plugin/script references from the actual
launch; installed engine/DLL/default/resource hashes; a small sample of current
Lua script provider hashes and bounded copies of selected Lua source files;
Lua storage sizes (not contents); hardware/driver
metadata; one-second process CPU/memory and window/focus state. If NVIDIA's
nvidia-smi is already installed, it also records one-second GPU load/memory/
clock/power. No download or admin permission is needed for that optional part.

The archive contains local paths and mod names, as your game log already does.
Only the listed Ctrl+Alt marker chords are polled. General typing is not logged.

BOUNDARIES
- The game keeps its current configuration. Only the collector's three diagnostic
  environment variables are temporarily set; all other overrides are preserved.
- Current source enables recording at launch and keeps it on until game exit.
  Keep the run short. A reminder is printed after eight minutes, but the collector
  never kills the game to enforce a deadline. Very long runs make large files.
- Timing has overhead. A later same-route FrameOnly capture can quantify how much
  the detailed recorder changes the result. It is not required for this first ZIP.
- These are base engine frames, not generated/displayed frames or a measurement
  of mouse-to-screen latency. Menus, loading redraws, minimized time and overlapping
  Lua/rendering work must be separated when interpreting the capture.
- FG DLL presence does not prove interpolation is enabled. FG's runtime log and
  optional declared-state markers are separate evidence. Turning interpolation
  off with its hotkey leaves the injector/presenter installed.
- The resource hash list covers the engine's resources folder. Explicit resource
  overrides and all mod providers still need reconciliation from config/logs.
  Targeted Lua files are a sample, read after exit, not a full mod/VFS audit.
  Selection includes known performance-related entry points and up to 16 Lua
  callback sources ranked by the captured self time. Source copies are capped
  at 2 MiB each / 32 MiB total. Timing parsing and provider lookup have time
  budgets; any skipped or incomplete coverage is explicitly recorded.
- Setup reads core binaries/configs before launch. This is a gameplay capture;
  do not treat it as an untouched cold-disk benchmark.
- If collection is interrupted, a file is missing or a log cannot be bound to this
  run, warnings mark the result incomplete. Raw files remain in the run folder.

ADVANCED USE (OPTIONAL)
Open Windows PowerShell in this folder. Parameters can select an executable,
custom user config/log directory, label, starting declared FG state, or disable
the optional GPU sampler. The script accepts existing game arguments unchanged:

  .\Start-Capture.ps1 -OpenMWExe 'F:\Your engine\openmw.exe' -RunLabel baseline
  .\Start-Capture.ps1 -OpenMWExe 'F:\Your engine\openmw.exe' -Mode FrameOnly
  .\Start-Capture.ps1 -UserConfigDir 'F:\Your profile' -NoGpu
  .\Start-Capture.ps1 -GameArguments @('--config','F:\Your profile')

Use the same custom command-line arguments as your normal shortcut if it has
any. The collector cannot infer arguments from an unused desktop shortcut.

The CMD uses PowerShell's process-only ExecutionPolicy Bypass to run this local
collector; it makes no persistent execution-policy change and cannot override
an enforced organisation policy. If an enforced policy blocks it, send the error
instead of changing the system policy.

This collector has been prepared for Windows PowerShell 5.1. The accompanying
verification report states which offline and Windows checks actually ran. Live
gameplay and your current installed state remain to be measured by this capture.
