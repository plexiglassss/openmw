# Terrain and rendering-ray investigation

Diagnostic follow-up to b2aa3caffa. No optimization is removed or newly claimed by this change.

Run-20261022-184211-238 shows an 8.883 s terrain wait inside the Census Office exterior transition, followed in the same frame by a 3.281 s rendering-ray traversal requested by Quick Loot. It does not isolate ray-tree construction cost or prove the Lua caller is responsible.

Source review: QuadTreeWorld::accept handles intersection visitors by obtaining/reusing a terrain view, selecting LOD when needed, then calling loadRenderingNode for every view entry before passing it to the intersection visitor. loadRenderingNode can create terrain and object-paging chunks. Consequently the measured ray traversal can include cache misses, model loading, geometry merging, material/composite-map preparation and ray-tree construction, rather than just intersection tests. Existing view reuse must be measured; it would be incorrect to assume every ray rebuilds all terrain.

New diagnostic scopes distinguish:

* terrain/worker/terrainJob and preloadView: background preparation wall time (including any nested waits).
* terrain/ray/accept, selectLod, prepareRenderingNode, traverseRenderingNode: ray-side preparation versus intersection traversal.
* terrain/ground/getChunk, createChunk, createCompositeMapGeometry and buildRayIndex.
* terrain/objects/getChunk, createChunk and mergeGeometry.

Nested totals are inclusive; do not add parent and child times. Worker scopes aggregate independently on their worker threads and may lack gameplay frame IDs. Do not add concurrent worker durations to main-thread wait time. Fine-grained diagnostic overhead exists and is not a production speed claim. Normal camera traversal is not given the extra per-entry ray scopes.

## HUD Markers source review

Inputs: attached (OpenMW 0.49) HUDMarkers.zip and HUDMarkers - UI Language Fix.zip. The language fix contains only English catalogs and a README; it does not replace executable Lua.

Confirmed code findings in HUDM_p.lua:

1. One rendering ray per onFrame invocation when eligible entries exist; no global elapsed-time interval or per-target next-check time. This scales request rate with engine-rendered frame rate, not generated display frames.
2. No pending flag for an individual ray entry. Callback checks only the string key exists; it does not verify the entry is the same generation or reject results issued before a cell change/removal/recreation. It reads mutable distance at callback time.
3. Invalid marker cleanup writes tbl[i] = nil, while the collection being iterated is modMarkers[i]. This does not remove the invalid descriptor from its owner collection.
4. Out-of-FOV entries retain old ray data. End-of-frame cleanup removes barCache/raytracing only when b.bar exists. A never-visible/occluded entry can have no UI bar and therefore remain in the ray table. Range cleanup reads the cached distance, which need not have been refreshed for an off-screen target.
5. Callback confirmed-visibility time is captured before request submission, rather than when the result arrives. A long engine stall can make that timestamp old by the next frame.
6. Ray offsets selected by failedHits modulo four are two duplicated positions: branches 0 and 2 are equivalent, as are 1 and 3. Simplifying them saves arithmetic but is a minor optimization and does not solve the engine stall.
7. Non-NPC bounding boxes are sampled with 5% probability per eligible marker per rendered frame. Repeated markers can multiply work. Replace with an explicit per-object schedule only with dynamic bounds and actor animation accounted for; permanent caching would be unsafe.
8. The ray scheduler uses `if true or playerSection:get("RAYTRACING")`; this is always true. No active RAYTRACING setting is declared in the supplied settings.lua, so presenting a working existing disable toggle would be misleading. Ray use is requested through descriptor.raytracing, including markers registered by other mods through the exported interface. Internal detection descriptors in this supplied script do not themselves set raytracing=true.

Recommended first mod change: fix invalid/stale entry cleanup and add identity/pending checks without altering detection ranges, heartbeat animation, or wall occlusion. Then add a bounded round-robin real-time schedule (e.g. a configurable global rate and per-target refresh interval), ensuring periodic rechecks even when stationary so moving doors/occluders are detected. Visible UI motion should remain per-frame. Invalidation must cover cell changes, marker-provider updates, removal and object invalidity. Do not deduplicate solely by object ID: two descriptors can have different target heights/offsets.

These are inspected-source findings, not an installed optimized HUD Markers release. Keep the base mod and UI Language Fix as currently installed for the next engine capture so mod changes do not confound the engine investigation.
