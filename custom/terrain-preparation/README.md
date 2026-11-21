# Terrain preparation — 21 November 2026

Extends cumulative source e37906e66c (the same tested engine tree as d367b21f).

Terrain preload groups prepare independent vertex, normal and colour buffers on
the existing native pool, with at most four selected chunks per group. Preparation
joins before the existing model pipeline. Scene nodes, textures, materials, ray
trees, graphics requests, progress and arrival publication keep their existing
ordered path. Active-grid preparation still precedes distant arrival work.

Concurrent requests share completed vertex buffers by world owner, centre, chunk
size and effective vertex LOD. Stitching variants use the same immutable arrays
and VBO, but keep separate index topology and ray trees. Exact chunk construction
also coalesces concurrent requests. A CPU cache hit is not a graphics-completion
receipt. Required scenery, collision, navigation and existing graphics waits are
retained. No new cell-activation or script scheduling policy is introduced.

Cache clear waits for active producers before discarding results, so completed old
work cannot repopulate a cleared cache. Terrain nodes keep their shared buffers
alive; unused buffers expire through the existing resource-cache sweep. The cache
uses the configured expiry delay, not a new hard memory cap. Cancelled preloads
skip unstarted fills and join any already-running fill; a single fill is indivisible.
Failed speculative fills are retried by the actual request. No editor worker pool
is installed; ordinary editor requests retain synchronous operation and cache clearing.

Set `OPENMW_TERRAIN_PREPARATION=0` before launching for the previous preparation,
copying and duplicate-work policy. The cumulative performance master switch also
disables this update. Zero native workers disable parallel filling while retaining
sharing. The collectors record this switch without overriding it. Normal play
needs no new setting or mod patch.

Validation exercises production cache/fill and pool code, production chunk-cache
and grouping bodies, shared-array ray parity, and actual OpenGL draws after sibling
release and buffer recompilation. Full Windows engine/editor, native regressions,
preservation and package checks remain required. Synthetic work counts and fixture
progress are not measurements of the installed game. No Balmora capture of this
candidate is available; no FPS, whole-load or town-stall reduction is claimed.

Compare the same save and exterior route at normal movement speed, including an
outward and return trip through Balmora, a door transition and a repeated load.
Check complete scenery, terrain seams, ground textures, water edges, activation
rays, collision, loading bars, worst frames and memory. Retain the prior complete
engine folder for rollback and install the new complete distribution separately.
