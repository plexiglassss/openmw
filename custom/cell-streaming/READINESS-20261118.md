# Exterior preparation update

Extends cumulative build 520a8bcd; the previous tooltip, QuickLoot, HBFS, Lua,
rendering and companion-tool changes remain included.

Graphics preparation now exposes each eligible geometry buffer and each shader
as its own incremental operation, followed by the original drawable/program
finalization. Shared dependencies are deduplicated within each context's list.
Unknown custom compilation overrides retain their original behavior. Texture
uploads, individual buffers, shader compilation and program linking can still
contain an indivisible driver call. The existing graphics time allowance and
maximum operations per frame have not been raised. No new graphics thread,
asynchronous link implementation or early-ready shortcut is introduced.

Exterior prediction observes queue delay, resource preparation time and cold
template counts. Expensive recent work increases the prediction lead (at most
four seconds), and cheap warm work reduces it again. Pending job age also
accounts for current congestion. This is an estimate, not a measured GPU deadline.
The projected displacement is capped at one cell: a continuous fast movement
no longer disables next-grid preparation just because its projection exceeds
one cell. Actual discontinuities, invalid input and long stalled-frame samples
do not sweep a distant corridor. Static prediction cadence ranges from 250 ms
down to 25 ms according to actual measured movement speed.

Active simulation radius, view distance, cell-cache capacity, static preparation
limits (two CPU jobs, eight retained cells, 256 MiB estimated admission budget),
and required scenery fallback are unchanged. These limits are admission bounds,
not a hard process RAM or VRAM cap. No mod names, town names or Speed-stat values
are hardcoded. No persistent cache rebuild or startup scan is added.

Process-local comparison switches (restart the game after changing them):

- `OPENMW_GRANULAR_COMPILE=0`: use the earlier compound graphics operations.
- `OPENMW_COST_AWARE_PRELOAD=0`: use the earlier fixed prediction behavior.

The cumulative master switch still disables both. Default operation requires no
configuration change. Detailed captures now distinguish shader, buffer, texture,
finalization, queue snapshot and deletion time; frame-only capture remains the
appropriate initial comparison for general frame pacing.

Validation includes native prediction/cost/publication regressions, production
preload/admission fixtures, actual GL partial-upload state restoration and shader
progress, dirty-resource finalization, custom override behavior, and existing
whole-scenery readiness tests. Independent negative controls that skip actual
pre-stage uploads/compiles must fail the progress tests. These checks establish
correctness on the test implementations, not an in-game performance gain.

Compare the same town route at character Speed 2000, keeping the mod list,
settings, view distance and Frame Generation state fixed. Compare first and
return traversals separately. Keep the customized `lua51.dll` and matching
`resources/lua_libs/content.lua` together when using the existing customized
Lua setup. Gameplay results on the user's Windows installation remain pending.
