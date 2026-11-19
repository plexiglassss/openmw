# Generic MWScript lookup investigation — 21 October 2026

## Scope and current evidence

This change optimizes engine reference lookup. It does not edit any mod, reduce
script frequency, defer quest initialization or special-case a script name.

The recovered Profiling 7 report (diagnostic build `6c4d952`) records these largest
walking invocations: BMStartUpScript 3435.4 ms, TK_NPC_Disable 2492.8 ms,
CriaturasMiticasStart 849.7 ms, TTD_Space_Global_Script_new 832.0 ms,
fkoa_disable_npcs_script 541.0 ms and _ALM_BCoM_patch 382.0 ms. Those are inclusive
`ScriptManager::run` measurements. They cannot tell us how much time was spent
compiling, interpreting, finding references, loading cells or updating the scene.
The exact current winning script providers have not been certified in this pass.
No claim about their individual source behavior is needed for this engine change.

Source baseline: `817b511648ae709563095b52b8f276010e396dfb` on
`plexiglassss/openmw`, branch `diagnostic-timing-2829ae6d`. Its Windows CI run
39470180412 completed successfully. Existing dialogue, reference-to-cell candidate
index, Lua, sound and Windows compiler-cache changes remain in place.

## What changed

`CellStore::search` and `searchConst` previously traversed every accessible
reference until they found an ID. A miss traversed the entire loaded cell.
`World::searchPtr` checks active cells first; `WorldModel::getPtrByRefId` can then
check multiple known cells before consulting its existing content-file candidate
index. The candidate index did not eliminate these repeated loaded-cell scans.

Loaded cells now lazily build a separate stable, sorted pointer view for repeated
ID searches. The first search after a merged-view change still uses the existing
linear traversal, avoiding sorting for a one-off search. Further searches use a
binary search followed only by the matching duplicate group. A normal indexed
lookup is O(log N + matching duplicates), instead of O(N).

The original merged list is never reordered. Stable sorting preserves exactly
which accessible duplicate wins within a cell, including record-type ordering and
moved-in references. World-level active-cell, exterior, interior and cache order
is unchanged. Every lookup checks current accessibility; count-zero content-file
references remain accessible, runtime-deleted references do not, and disabled
references remain discoverable. Insertions, movements and save restoration use
the existing merged-view invalidation path. No hits or misses are cached as final
answers. Unloaded/preloaded cell behavior and container search are unchanged.

Trade-off: repeatedly searched loaded cells retain one extra pointer per merged
reference (normally 8 bytes on x64, plus vector capacity/metadata). Stable sorting
temporarily needs workspace. Rebuilding costs CPU after relevant mutations; the
new diagnostic row measures that cost. This is not a change to the save format.

## New measurements

The existing `mwscript / <script> / run` inclusive row remains comparable. New
`mwscript_phase` rows separate `compile` and `execute`. `execute` still includes
native work; its residual self time is not proof of pure bytecode-dispatch cost.

`mwscript_native` records the current owning script and these shared operations:

| Callback | Meaning |
| --- | --- |
| `world.searchPtr` | Full world reference lookup, including active cells and fallbacks |
| `world.findInactive` | WorldModel cache/known-cell/candidate-cell search |
| `cell.preload` | Reading a cell's reference-ID list when actually needed |
| `cell.load` | Loading full references when actually needed |
| `cell.refIdIndex.build` | Sorting the new loaded-cell view |
| `world.enable`, `world.disable` | Native object state update after resolution |
| `world.pagingEnableObject` | Paging update, including any nested terrain reload |
| `world.reloadTerrain` | Terrain reload within the enable/disable path |
| `world.disable.sceneAndPhysics` | Scene removal and postponed physics work |

These are nested elapsed-time scopes: do not add their inclusive durations. Use
self time and nesting to avoid double counting. Native rows are collected only
while a timed MWScript runs. Explicit Disable is covered even though it bypasses
InterpreterContext's usual resolver. There are no per-opcode or per-reference
comparison clocks. Other native operations remain part of unclassified execute
time and can be instrumented if the next capture points there.

## Validation gates

`CellLookupTest` uses actual engine CellStore/WorldModel objects and compares
results against the original forEach traversal. Cases cover unloaded/preloaded
cells, empty cells, missing IDs, case-insensitive IDs, duplicates across record
types, live count/deletion changes, disabled objects, content references with zero
count, insert-after-miss, movement between three cells and back, moved duplicates,
include-deleted traversal, repeated randomized mutations, unchanged serialized
reference bytes and restoring into a previously searched cell.

The informational benchmark uses actual CellStore lookup with 4096 references and
20000 mixed hit/miss queries. Candidate time includes its first linear query and
initial index construction. It is a synthetic lookup benchmark, not an FPS
estimate or a measured reduction in the recorded multi-second stalls.

Windows CI runs these tests in separate processes with indexing enabled and
disabled, before packaging. Full existing engine/component tests still run.
The standalone timing regression checks phase accounting, native nesting,
exception unwinding, thread isolation and silence outside an active script.

## Installation and controlled in-game comparison

Use the complete Windows build in its own folder, following the existing custom
build installation procedure. Keep the same mod list, save, settings, Frame
Generation DLL/config and Lua DLL in both runs. Do not replace those optional
custom DLLs with different versions during comparison. No ESP/ESM replacement,
load-order change or new game is required.

Timing stays enabled for both runs. From PowerShell in the new build folder:

```powershell
.\Start-Profiling.ps1 -CellLookup Linear
.\Start-Profiling.ps1 -CellLookup Indexed
```

Run one command, perform the test and quit before running the next. Each launches
a fresh game process. Load the same untouched save, wait the same interval, then
follow the same Seyda Neen walking/dialogue route and cell transitions. Do not
save over the comparison save. Repeat in reverse order to expose OS/file-cache
warm-up effects. Each capture records the lookup mode and executable/DLL hashes.
Do not use `-WithoutTiming` for this comparison.

Compare load/first-run stalls separately from settled walking frames. Compare
per-script run/compile/execute, reference lookup, cell load, index build and scene
work, plus frame median/p95/p99 and counts of large stalls. Verify identical quest
state, disabled/enabled objects, successful transitions, dialogue and save/load.
The baseline mode is the same executable with `OPENMW_CELL_ID_INDEX=0`; it retains
all instrumentation. Direct normal launching defaults to indexing enabled.

Offline tests establish lookup semantics and controlled algorithm cost. Only
matched live OpenMW captures can establish the gain for this installation or
identify how much of the earlier stalls this change removes.
