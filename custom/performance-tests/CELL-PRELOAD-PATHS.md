# Cell preload path reuse — 23 October 2026

User action: install the cumulative engine build after CI succeeds. No configuration,
mod, save conversion or navmesh cache regeneration is required.

## Problem and change

The existing cell preloader already loads each final rendering template once per
job, but corrects every source mesh path and checks its VFS existence before that
check. Repeated rocks, walls, trees and clutter therefore repeat identical path
work. A job-local map now resolves each source path once, including missing paths.
The map dies with doWork; it is not a persistent cache and cannot outlive the
snapshot of paths being preloaded. VFS indexing is fixed during a preload job.

Each occurrence still creates its own collision instance when instance preloading
is enabled. Different source paths resolving to the same final mesh still share
rendering preparation. Keyframes, retained resources, cancellation and retry after
resource-loader exceptions preserve the previous behavior. No live world object
mutation was moved to a worker thread. No render, physics or navigation fidelity
was reduced.

## Evidence and validation

`test-preload.py` extracts and compiles the actual production doWork body, using
production VFS path types and counted resource-manager test doubles. It verifies:

- 2,000 identical source paths resolve once, with all 2,000 collision instances.
- Instance preloading disabled still prepares one shape, no instances.
- Source aliases share one final template without losing instances.
- Missing paths are checked once per job and checked again in the next job.
- Animated models retain keyframe preparation.
- Template exceptions still allow retry at the next occurrence.
- Cancellation before and during processing stops further model work.

Linux C++20 checks passed with warnings treated as errors. The production path
class exposed an ambiguous empty assignment during development; the final code
uses an explicit Normalized value and passes compilation. This harness does not
exercise real OSG/Bullet concurrency or disk I/O. Windows harness and complete
engine compilation are CI gates; live transition improvement remains unmeasured.

Additional cost: a temporary map entry per distinct source path, plus lookups.
Cells containing only unique paths may gain nothing and pay a small bookkeeping
cost. This reduces preparation work, not every source of cell-transition stalls.

## Later in-game check

Use the usual timing-enabled capture. Load the same save, walk across the same
cell boundaries and back, then enter/leave an interior. Check missing models,
collision and transitions. Compare cell-load timings and long-frame counts with
the existing capture; keep first-load and warm-return results separate. A full
baseline rerun is not required merely to check correctness. Frame-generation
presentation remains a separate issue with its previously committed fix.
