# Loading progress and worker concurrency — 20 November 2026

The loading-progress listener could hold the resource worker's progress mutex
while updating the loading screen, rendering and compiling graphics. Resource
workers trying to report progress or completion then had to wait for that work.
The listener now takes a coherent snapshot and releases the mutex before calling
the UI. A revision predicate retains progress/completion notifications that arrive
during either callback. It still waits for the same actual completion condition.

Object insertion also updated progress-bar geometry for every increment, including
thousands of increments between displayed frames. The counter and existing draw
admission remain the same; widget geometry is updated only when a frame is drawn.
Forced progress before blocking save/load work continues to draw current progress.

## Visual behavior and cumulative features

This change leaves draw distance, LOD, scenery content, texture quality, groundcover,
cell activation order, arrival readiness, graphics-compile budgets and gameplay
frame pacing unchanged. It does not defer required objects or remove readiness
checks. The cumulative RC1 branch is retained, including prior UI/tooltip,
quickloot/HBFS integration, mod controls, companion tools and streaming changes.
Existing separately installed Lua mods remain separate.

Tests execute the actual Reporter implementation and the production LoadingScreen
method bodies with counted UI/viewer doubles. They cover worker completion while
a callback is blocked, notifications arriving during callbacks (including zero
progress and total-only changes), coherent snapshots, exceptions, forced refresh,
range reset, decreasing progress, subpixel increments and preserved graphics
service. The cumulative Windows build runs these before compiling the engine.
Its existing full native, scenery/graphics and package checks remain required.

The synthetic UI check reduces 12,000 per-object widget updates to the two admitted
draws in that fixture. This is an operation-count result, not a measured game FPS
or transition-time improvement. The prior capture's progress-path elapsed time
also includes necessary rendering/compilation; it cannot all be removed.

## Install and compare

Use the complete Windows distribution from a successful workflow for this commit.
Extract it into a separate engine folder and keep executables, DLLs and resources
together. Keep the previously accepted engine folder for rollback. No modlist,
save or graphics-setting changes are needed for this optimization.

Run `tools/combined-performance/Run-Combined.cmd` as before, using the same save,
route, movement speed, landscape textures and graphics/frame-generation settings.
Compare cold and repeat runs separately. Check the horizon and town approaches for
late scenery, ordinary travel and the speed-2000 stress route, and save/load progress.
Real-game improvement and visual parity for the installed modlist need that test;
native/fixture tests do not establish them.
