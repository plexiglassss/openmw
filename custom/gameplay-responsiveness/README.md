# Gameplay responsiveness: inactive Lua retention

This cumulative Windows engine preserves the delivered dialogue colours and settings layout,
journal controls, frame-generation controls and loading fixes, indexed lookups, and shrine
tooltip bridge. Its baseline is `1b78b9c1a566b91bbe1576387842625f3ae509d1`.

## What changes

OpenMW normally unloads inactive local Lua sandboxes after a short frame-based grace period.
Returning to those objects then reconstructs the sandboxes and reloads their saved state.
This build adds an optional longer retention period for recently used inactive scripts.
It targets repeated initialization when walking back across nearby exterior cells.
It does not avoid initializing scripts for an object encountered for the first time.

The original 50-frame minimum, activity-based extension, active-script protection, and
incremental eviction queue remain the fallback. Activation/deactivation events and updates
are not delayed. Expiry uses accumulated engine frame time, not a separate wall clock.
An inactive sandbox retains its unsaved locals and pending unsavable timers while cached;
the engine already does this during the existing short grace period. The optional preset
extends that lifetime. Inactive scripts are not newly added to the active update/timer list.

Extra retention stops when total Lua memory reaches the configurable pressure threshold.
The existing unloading policy then applies. The threshold works with the Lua profiler off;
it is an eviction trigger, not a hard limit on Lua or process memory. No full garbage
collection is forced by this change.

## Settings

The engine defaults preserve previous behavior. To enable the supplied preset, close
OpenMW and its launcher, then run `Apply-Settings.cmd` from the accompanying gameplay
package. It backs up and edits the existing user settings file. It sets only:

```ini
[Lua]
inactive script cache duration = 30
inactive script cache memory threshold = 2147483648

[Shadows]
compute scene bounds = bounds
```

The first value is seconds of accumulated engine frame time. A zero duration or zero memory
threshold disables extra retention. The memory threshold is bytes; the preset uses 2 GiB.
Restart OpenMW after changing these values. Keep the existing profiler, thread count,
viewing distance, resolution, shadow-map size and other settings.

`bounds` is OpenMW's default shadow coverage method. It reduces CPU work compared with the
captured `primitives` setting, with less precise shadow coverage. Shadows stay enabled.

To undo the preset, use `Apply-Settings.ps1 -RestoreBackup <backup-folder>` while the game
is closed. It restores exact original bytes only if settings have not changed since the
preset. If you have made later settings edits, keep those edits and set cache duration to
`0` and shadow bounds back to `primitives` manually.

## Verification and limits

The Windows workflow verifies source preservation, tests settings editing and rollback,
builds the engine in Release mode with LTO, and runs the existing component and engine
regressions before packaging. Cache-specific tests exercise real Lua serialization,
re-entry, expiry, memory pressure, active/inactive events and pending timers.

The capture establishes Lua-dominated exterior stalls, including repeated cell visits.
It does not measure this candidate's frame-time benefit. No FPS gain, complete elimination
of those stalls, or live-game acceptance is claimed by the offline tests.
