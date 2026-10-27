# Loading repair — first implementation batch

Status: experimental source changes, not an accepted in-game fix.
Baseline: e9476be66251d181d0016a5fd112e1d90e3875f6, preserving the existing cumulative engine changes and the revised metadata-only cell filter.

## Root cause established
Commit 275731d678e5fed37887896507ef0f795a605cf8 deliberately stopped eagerly loading references for saved cells containing only metadata/fog. Subsequent MWScript object queries could still force those deferred cells to load, after the loading UI had closed. The earlier reduced save-restoration time was therefore not proof of an equal reduction in time to usable gameplay. It does not establish that every recorded Lua or exterior stall was introduced by that commit.

## Included in this batch
1. Partition exceptional unloaded cells when the lookup index is dirty. The indexed search no longer walks every metadata-only unloaded cell for every query. Static candidates, moved/runtime additions, ESM4 fallback and the existing search order are preserved.
2. Prepare the current cell-reference lookup index at the end of save restoration, while the ordinary loading screen still owns the transition. This moves remaining index preparation under loading; it is not counted as eliminated work.
3. Retain the loading image over initial normal engine update cycles, without retaining the GM_Loading GUI mode that would suppress some startup scripts. Player input is withheld and simulation delta is zero during this phase. World/script/Lua/render update phases still run. The cover is released at the next input boundary, after the preceding render and Lua worker have finished. Background GC is not launched into that boundary while the cover is held.
4. Log the covered interval and the reason for release. At least four complete samples and three consecutive samples <=50 ms are required for a settled release. A 30-second/120-completed-frame limit and frame errors are explicitly logged as NOT settled. The limit cannot interrupt a currently executing blocking callback. This escape path prevents an indefinitely covered game, but is not a successful performance result.

## Important limitations
This is a bounded settling check, NOT a universal proof that arbitrary mods have completed initialization. Mods which delay work until simulation time advances or a later event can still cause stalls. The cover itself does not shorten blocking work or keep the UI animating during a monolithic main-thread callback. Runtime behavior, UI layering, zero-delta script compatibility, reload/cancel paths and actual latency still require in-game verification.

Do not declare success because the loading image stays longer, or because isolated unit tests compile. Compare total load-to-controls time and the next seconds of gameplay, not just the save-read routine. Preserve the original save and do not reset any quest/map database to manufacture an improvement.

## Other workstreams — not implemented here
- Advanced World Map: split rebuild into resumable, budgeted work, keep incomplete results private, disable affected features with an initializing label, and publish only a completed result. The user's current customized winning scripts have not been recovered in this batch. Do not overwrite them with upstream defaults.
- Quest Guider: preserve Indexed Data and queue fixes; add actual initialization/queue completion signals when its current source is available. Existing evidence does not establish a database rebuild as the cause of the rare 90-second incident.
- Settings/Crafting startup UI: inspect and reduce synchronous creation separately; do not defer mandatory game-state changes by dropping actions.
- Exterior terrain/object templates and rendering-ray queries: separate next source change, not included or claimed fixed here.
- Intermittent minute-long incident: remains unconfirmed until reproduced with appropriate capture.

## Author-side build
CI applies `custom/loading-repair/apply.py` to exact Git blob hashes, with all anchors checked before any source file is changed. This is an author-side integration step, NOT an end-user builder. The complete native source diff and before/after blob manifest are included in the build evidence and binary artifact for reproducibility. No user's mod, save or configuration files are modified.

Windows only. Use the existing Release/sccache profile; compile changed engine files early, run all component and engine tests, and exercise four cell-lookup configurations before producing an install artifact. No executable is published by the packaging step when those tests fail.

## Diagnostic controls
- `OPENMW_POSTLOAD_HANDOFF=0` disables only the new visual handoff.
- `OPENMW_WORLD_CELL_CANDIDATES=0` selects the legacy candidate traversal.
- `OPENMW_TIMING_FILE` remains supported.
These are optional diagnostic controls, not required installation commands.

## Local validation completed
- The new handoff controller compiled under GCC with C++20, warnings-as-errors and pedantic checks.
- Executed lifecycle cases: long initial frames; consecutive short frames; release only on the following boundary; later slow-frame reset; cancellation; reload generation isolation; timeout; error; nonfinite duration; destruction without a dangling UI callback.
- Python syntax and workflow YAML parsed locally.
- Source application, full engine compilation and Windows test-suite status are separate CI results, not asserted by these isolated tests.
