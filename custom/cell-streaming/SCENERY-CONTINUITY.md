# Scenery regression correction

This candidate extends cumulative source 157fc965a1fd909753bd6695d8a6e562e0c7f6b0.
It preserves the tooltip, QuickLoot, HBFS god-mode and other engine fixes, the
existing mod-facing APIs, and the full Windows editor/companion tools.

The 157fc965 test capture showed prolonged unmerged distant scenery and a graphics
compilation backlog, alongside the reported lower city FPS. Unmerged coverage is
now disabled by default. Required scenery uses ordinary merged, size-pruned chunks;
preparation can run ahead, and a miss finishes complete scenery immediately. This
avoids intentionally blank scenery but can still stall on cold assets or activation.

Gameplay graphics preparation now shares an elapsed-time allowance between deletion
and compilation: at most 1 ms, or 0.5 ms at 120 Hz, and 256 operations. Already-compiled
shared dependencies can progress without a two-operation-per-frame bottleneck.
Deadline checks happen between operations. A single driver call can overrun the
allowance; it is not a strict maximum frame stall. Loading screens keep higher
throughput and restore gameplay limits afterward. Readiness still uses context-bound
completion callbacks, not an object handle or queue marker.

Source-cell revisions prevent unrelated enable/disable changes from cancelling all
pending distant work. Active-only blacklisting leaves far selection valid. Runtime
moves are traced to the original cell/worldspace, including exterior objects now
inside an interior. Affected source references cannot publish an older result. Full
selection resets, resource/settings/VFS changes and graphics-context changes retain
the broader invalidation safeguards.

Detached static/actor resource preparation remains enabled. It does not activate AI,
run scripts, select spawns or insert physics early. Existing activation order and
exact query behaviour remain unchanged. There is no new persistent scenery cache.

The former unmerged coverage can be enabled explicitly with
OPENMW_SCENERY_COVERAGE=1 for diagnostics only. It still uses conservative whole-view
selection invalidation and can incur the observed rendering cost. Leave it unset for
normal play. OPENMW_ASYNC_OBJECT_PAGING=0 separately disables asynchronous object
preparation; the temporary recovery launcher for 157fc965 uses that older switch.
Use the normal Run-Combined.cmd when testing this corrected candidate.

Tests cover real graphics progress behind 1,024 shared-resource sets without forcing
compileAll, cooperative stopping after an overrun, loading-to-gameplay restoration,
source-cell liveness, moved-reference routing, stale publication rejection and complete
visible fallback. They do not measure city FPS recovery or establish seamless cell
transitions. Compare the same save, settings and route, including first exterior entry,
walking boundaries, reversing direction, returning from an interior and teleporting.
Keep the installed current QuickLoot and Object Insight companions.
