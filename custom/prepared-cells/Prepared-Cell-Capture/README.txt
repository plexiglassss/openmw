Prepared-cell capture launchers - 22 November 2026

The earlier Run-Prepared-Cells-Experiment.cmd enabled the experiment but did
not enable detailed recording or create a capture ZIP. Its normal game log
is in the configured Logs dir; your latest log identifies:
C:\Users\pexCo\Documents\My Games\OpenMW\openmw.log
OpenMW replaces the normal log on a later launch. Your uploaded copy is retained.

Installation
Extract this archive into the experimental engine folder. The resulting
Prepared-Cell-Capture folder must be directly beside openmw.exe.
These launchers use that exact sibling executable, not another installed copy.

Use
Run Prepared-Cell-Capture\Capture-Experiment.cmd to enable prepared cells, or
Prepared-Cell-Capture\Capture-Baseline.cmd to disable them and record a comparison.
The game launches directly. Load the same save and walk the same route.
Keep graphics, mods, frame generation and other settings identical between modes.
For an engine comparison, use the same Greater Intervention version in both runs.
For a mod comparison, use the same engine mode with the original/fixed mod.

If useful, Ctrl+Alt+6 marks a hitch and Ctrl+Alt+5 marks inventory opening.
Skip these optional shortcuts if another mod uses them. Quit OpenMW normally
to finish the ZIP; do not close the capture window first.

Results
Windows Documents\OpenMW Responsiveness Captures\Run-<timestamp>-prepared-on.zip
Windows Documents\OpenMW Responsiveness Captures\Run-<timestamp>-prepared-off.zip
The window prints the exact output path. A redirected Documents folder is
resolved through Windows, rather than assuming C:\Users\...\Documents.

The ZIP includes the current game log, frame/resource statistics, detailed
script timings, the prepared-cells flag, executable identity and targeted Lua
source copies. Greater Intervention's five Lua paths are explicit collection
targets; missing/archive-provided files are reported, not assumed verified.
No save or memory dump is copied and nothing is uploaded automatically.
Recording adds overhead: compare matching recorded modes, not recorded versus
unrecorded FPS. Menu/loading frames need separate treatment in analysis.

Implementation and validation
The collector is the existing engine capture script at f6b6bc529d5fc0f53ddb4fbe2c8f1ac68efec160,
with the prepared-cells flag recorded and the Greater Intervention Lua paths
prioritized for source collection. The two wrappers set only the child-process
experiment flag. The collector sets its three diagnostic variables; it does
not edit the game's configuration. These additions have been source-reviewed;
Windows launch/capture has not been executed in this Linux repair session.

Recorder v1.1: the end-of-file validation now accommodates resource-statistics blocks longer than 200 lines. The 22 November prepared-on recording was readable despite its old warning; it does not need to be repeated for that warning. A tail sample that cannot establish the final frame is recorded as uncertain, not as missing data. This change does not alter engine settings or recording overhead. Windows execution of this revised validator has not been performed in the Linux analysis workspace.

Iteration 2: Capture-Experiment enables prepared cells AND staged arrival.
Capture-Baseline disables both. Results remain in Documents\OpenMW Responsiveness Captures.
