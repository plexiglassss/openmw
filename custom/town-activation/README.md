# Town activation and resource reuse — 21 November 2026

This extends cumulative source e12caf7424, retaining the terrain, loading,
settings, UI and earlier native performance changes. It targets repeated CPU
construction work. No cell names or particular mods are hardcoded in the engine.

Actor attachment recipes reuse the selection of source rig subgraphs for a
normalized body-part filter. Every actor still receives fresh cloned nodes,
controllers, skeleton and mutable rig/morph state. The existing cleaned skeleton
cache is replaced by an owner-scoped bounded cache whose identity includes model,
default skeleton, custom-bone injection, model settings and VFS revision.

Recently consumed canonical model templates receive a short extra retention
window. This avoids reloading eligible resources on quick revisits after their
ordinary references disappear. Only the path-based demand instance call feeds it;
speculative template reads and arbitrary/live scene nodes do not. It retains no
active cells, actors or preload jobs. LRU entry/estimated-byte bounds and idle
expiry limit retention. Estimate metadata uses weak references, so rejected models
do not stay resident or trigger repeated full estimation on each body-part filter.

| Cache | Entries | Admission budget | Idle expiry |
|---|---:|---:|---:|
| Attachment recipes | 512 | 64 MiB estimated | 60 seconds |
| Cleaned actor templates | 128 | 64 MiB estimated | 60 seconds |
| Consumed model templates | 256 | 128 MiB estimated | 30 seconds |
| Weak estimate metadata | 512 | 256 KiB estimated | 60 seconds |

These are admission estimates, not measured RAM/VRAM caps. Hidden rig/morph source
arrays and retained source roots are included; shared data can be counted more
than once. Unsupported drawable layouts and oversized resources use the normal
construction path. Settings changes and explicit cache clearing invalidate entries;
cache generations prevent an earlier producer from republishing after a clear.
The normal renderer still owns graphics work and actor activation order.

`OPENMW_ACTOR_PREPARATION=0` disables recipe/template reuse; cleaned actor templates
are then constructed afresh. The prior executable already cached cleaned skeletons;
this flag is not an unchanged prior-build performance baseline. `OPENMW_RECENT_MODEL_REUSE=0` disables the extra demand
retention. `OPENMW_COMBINED_PERFORMANCE=0` disables both. Set switches before launch.
The collectors observe them without changing them. Cache counts, hits, estimated
bytes and budgets are available in resource statistics.

The companion Quest Guider Lite update is a separate mod folder. Its nearby-quest
menu scan uses the existing indexed queue and shared 2 ms soft budget. A complete
reply can take several updates; single native queries remain indivisible. Existing
parallel position sorting is preserved. No arbitrary gameplay events are deferred.
See its README for ordering and rollback.

Required checks include actual native cache/recipe/clone tests, SceneManager
identity/clear/revisit integration, enabled and disabled process modes, Lua legacy
result parity and cancellation, full Windows engine/editor builds, historical
preservation, startup and distribution validation. Fixture results do not establish
an installed-game FPS or stall improvement. Balmora capture remains outstanding.

Install the complete engine package in a separate folder and retain the previous
folder for rollback. Compare the same save and outward/return route through town,
a door transition and repeated load; check NPC appearance/animation/equipment,
complete scenery, worst frames, loading bars and memory.
