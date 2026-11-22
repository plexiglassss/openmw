# Isolated prepared-cell experiment

This branch prototypes advance preparation and dormant static-collision staging for exterior cell activation. It is **not a completed seamless-streaming implementation or a measured city-performance release**.

The accepted cumulative baseline is `ddf30674aab696073fdea33e71a635d18dd4c98e` (the same source tree as tested build `e5b6f104a05c9bb7ef425026eede0e25bc6e67dd`). No cumulative branch promotion is part of this experiment.

## Implemented

- Immutable snapshots of eligible loaded exterior ESM3 static references. Workers never receive live gameplay references.
- Workers construct detached render instances and Bullet collision objects without live references or world registration. The owner then stages ready collision objects in Bullet with zero group/mask filters and debug visualization disabled. They remain absent from gameplay queries, rendering, local scripts and gameplay activation.
- Staging runs in slices with a 1 ms target and 64-object maximum. It skips a busy physics lock instead of waiting. An individual Bullet operation can overrun the target; this is measured, not claimed as a hard deadline. Required activation only consumes fully staged packets and otherwise uses the existing path. Publication binds identity while the object is still filtered out, then enables its filters under the physics lock.
- The physics system owns dormant handles until publication or retirement. Cancellation removes orphan handles in bounded slices and releases their resources on the preparation queue. Save clear, queue changes and teardown detach all remaining handles. Speculative memory credit follows their actual lifetime.
- One dedicated preparation worker in the engine, separate from the queue serving required terrain arrivals. Shared resource locks and CPU/memory contention still require measurement.
- Active-grid paging and disabled instance-preloading modes prepare collision only. They do not build redundant individual render graphs or wait for an unused graphics receipt. Paging exclusions retain ordinary rendering.
- Render packets require the actual incremental graphics compiler's completion callback. CPU completion alone is insufficient. This receipt is **not** a GPU fence, shader-link validation or a guarantee that first drawing is free.
- Handover validates reference identity, type, model, position, rotation, scale, enabled/deleted state and count. Model/VFS/graphics/shader/context/settings changes and clearing a save cancel old generations.
- Ordered activation uses prepared objects where valid and retains the existing fallback elsewhere. A recently unloaded cell may prepare fresh instances for a return trip; progressed live controller state is not reused.
- Limits: 2 pending CPU jobs, 4 unresolved jobs, 8 entries, 2,048 statics per packet, approximately 64 MiB per packet and 256 MiB total admission estimate. Retired/cancelled jobs remain charged until resource destruction. Estimates are **not hard RAM/VRAM limits**: collision uses an allowance, resource caches also retain assets, and an indivisible asset can exceed an estimate.

## Enable and compare

The experiment is **off by default**. Use `OPENMW_PREPARED_CELLS=1` before launching this branch's executable. `0` or an unset variable keeps the ordinary activation path. The setting is read once per process. Existing cell-preloading settings remain respected.

Windows packages include `Run-Prepared-Cells-Experiment.cmd` and `Run-Prepared-Cells-Baseline.cmd`. Keep the package in its own folder. Neither launcher edits settings or installed mods.

Diagnostics add `prepared-cell` construction/publication scopes and `Prepared cell consumption` log entries. Rendering/collision-only counters report **consumption attempts**, not successful whole-cell transactions; postponed/no-shape objects can consume a packet too. `unconsumed` counts entries left in a packet that reached activation, not every cancelled prediction.

## Tests

Build `openmw-tests`, `components-tests` and `openmw`. Run the real engine fixtures in separate processes with the switch both off and on. The fixtures independently check the raw environment value and record the actual mode; verification rejects a missing, skipped, failed or wrong-mode test.

```
openmw-tests --gtest_filter=PreparedCellTest.*:RenderModes/PreparedCellBenchmark.* --gtest_output=xml:prepared-off.xml
# Set OPENMW_PREPARED_CELLS=1 in a fresh process:
openmw-tests --gtest_filter=PreparedCellTest.*:RenderModes/PreparedCellBenchmark.* --gtest_output=xml:prepared-on.xml
python custom/prepared-cells/verify-tests.py --off prepared-off.xml --on prepared-on.xml --components components-tests.xml --output prepared-cell-verification.json
```

For actual headless Mesa graphics checks, configure `OPENMW_TEST_PREPARED_CELLS_GL=ON` with EGL development files. This adds a production graphics-compilation and pixel-publication fixture. Add `--require-graphics` to the verifier. A context creation failure fails this test rather than silently skipping it. Other ownership tests explicitly simulate graphics acknowledgement and make no GPU claim.

The three-way synthetic benchmark measures 1,024 statics over 12 rotated-order rounds in both a 32x32 grid and sorted-line stress layout, for collision-only and individual-rendering paths. It reports raw samples for ordinary activation, detached-only preparation and prepared-plus-staged activation, plus background construction, every staging slice, and 256-query batches against empty and dormant collision worlds. The staging cost is paid in advance and remains part of total work; the experiment is intended to move it away from the boundary. Retirement completes outside measured regions. It excludes GPU duration, navigation, scripts, actors, actual Scene::changeCellGrid and real town assets; these timings cannot be translated into FPS or a percentage city improvement.

## Remaining architecture gates

Whole-cell activation is still an ordered synchronous transaction. Eligible static broadphase insertion can now occur before crossing. Navigation registration, actor construction, script callbacks and Lua joins remain on their existing paths, and static misses still use ordinary insertion. This branch neither hides loading bars nor postpones semantic activation events to make measurements look better. Arbitrary cold teleportation is outside the ordinary walking/running prediction envelope.

Before any seamless-performance claim or cumulative promotion:

1. Measure whether dormant staging plus per-object publication actually meets the physical-handover budget on the target workload; continue to scene/collision packets if necessary. Measure reverse crossings, dense cells, unloading and cancellation too.
2. Validate full-cell coherence and failure rollback; this prototype's duplicate-publication checks are not allocation-failure/whole-cell rollback proof.
3. Preserve actor/leveled spawn randomness, respawn, script ordering, saved state and immediate queries while addressing their remaining costs.
4. Measure the exact Windows binary and data identity on Balmora, Vivec and Ebonheart, with cold and warm routes and final menu frames excluded. Account for frame generation separately.
5. Meet the whole-frame deadline, not only a reduced loading duration. The design's 2 ms p99 / 4 ms maximum physical-handover target is provisional; it does not by itself guarantee a 16.67 ms complete frame.

Source, test XML and review evidence must identify the exact commit. The current review/result record is kept with this isolated branch and its CI run.
