# Combined save, load and repeated-work update

Based on the cumulative RC1 engine at f60b558148e4edc54cc8778a1a610295cc4b5fef.
All existing features and grouped settings remain part of the build.

- Save serialization reuses its small record stack. The explored map keeps its encoded PNG until changed; the completed encoder releases its temporary uncompressed image.
- Metadata-only unloaded cells reuse encoded CSTA records after comparing every serialized field and the format. Cells with references, fog or loaded runtime state use normal serialization. Live inventory, Lua onSave and actor state are never reused through an unsafe dirty flag.
- Immutable final save bytes are written on a worker while the loading screen remains serviced. The save completes only after flushing and replacing a sibling file. Failure preserves the previous save; unpublished siblings are excluded from save discovery. The old slot profile remains unchanged until publication succeeds.
- Saved plugin names use a case-insensitive index, preserving the first duplicate match and missing-plugin behavior.
- After dynamic records and legacy actor IDs are validated, destination resources and terrain preparation start before player rendering. The destination worldspace controls the grid size. Existing scene visibility, distance, collision, navigation and readiness checks remain in force.
- Static chunk construction shares template classification and bounds across repeated model instances. Existing batching is preserved; this is not a new draw-call reduction feature.
- Complete cell object queries join existing revision-validated typed snapshots. Duplicate group notifications no longer rebuild identical object lists.
- Inventory widgets and the two main tooltip text widgets are reused while contents, ownership, counts and layout are refreshed. Destructive tooltip cleanup clears pooled pointers.
- Inactive Lua script cleanup yields between containers after a 1 ms budget, with at least one container processed. Live script callbacks and events keep their ordering.

## Validation and measurement

Native regressions cover byte-equivalent record reuse, metadata mutation and format changes, mixed object membership, repeated map encoding, interrupted-save discovery and inventory widget/tooltip ownership. Standalone tests exercise the production atomic writer, OS failures, cleanup/retry, the production mapping method and destination planning. Existing engine, component, GUI, streaming and graphics-readiness checks remain required by CI.

These are candidates for improved performance, not a measured FPS or whole-save/load speedup on the installed modlist. Worker file writing improves responsiveness but still waits before gameplay resumes; durable flushing can cost more than the previous buffered flush. The supplied trace had no save-write phase timings. PNG decode was already overlapped; the observed remaining costs were terrain readiness and first-frame Lua/input work. Actual before/after measurements must include the first playable frames, not only the loading bar.

Use the complete successful Windows artifact in a separate engine folder. Keep the prior folder for rollback. Compare cold and repeat loads of the same save, then two saves without moving, a save after moving/looting, reload, inventory/barter/container tooltips, and an interior-to-exterior transition. Existing OpenMW logs report save/load phase timings. No mod-specific cells, IDs or plugin names are encoded in these optimizations.
