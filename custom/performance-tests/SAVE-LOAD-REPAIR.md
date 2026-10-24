## Engine changes

The supplied log records a 14.32-second load: 37,907 CSTA cell-state records take 10.16 seconds, player setup 2.19 seconds, global map restoration 0.97 seconds, and initial scene loading 0.66 seconds. These phases do not imply that 37,907 cells were personally visited; mods and lookups can also cause cell state to be retained.

Saved object restoration previously scanned a cell's entire same-type list for every matching saved reference. The new temporary index is keyed by reference number AND base record ID, separately per object type. It preserves first-match behaviour, appended references, content-file mapping, conversion and object-state application. Small lists and infrequently searched lists keep the original scan; an index is built only after eight searches in a list of at least 64 objects. It is discarded after that cell's reference records are read. No state is removed, no save format changes, and no load/script order changes are introduced.

This is a targeted optimisation, not a measured end-to-end speedup yet. Additional CSTA timings distinguish cell resolution/base reference loading from saved-object restoration, including a count of metadata-only cell records. The latter are still loaded normally; they are not discarded or lazily restored.

The upload contains no save operation. Added save-stage timings for screenshot, Lua, world/cells, other managers, map/UI, serialization and file write/flush. Explicit stream flush is included before checking file errors and reporting completion. Saving is not claimed faster yet. Persistent world changes, inventories, scripts and map data can grow during play; the actual expensive component must be measured on the mature save.

## Focused checks

1. Acquire an item. Hover 0.5 seconds, leave, return: require a fresh full second. Keep hovering: the marker should disappear immediately after the second. Repeat in table/grid views, on a second stack, and after save/reload. Check a merchant item with the same record does not acknowledge yours.
2. Click the book icon: all markers clear. Hover the icon: Clear new appears. Acquire another item and confirm marking still works.
3. Compare the same shirt and shoes: the shirt retains its temperature section and extra divider. All dividers within each tooltip should reach the same content edges. Check books, ingredients, lore and comparison tooltips too.
4. Load the same save used for the supplied timing capture. Check moved/disabled objects, equipment, containers, quest state and transitions. Save and reload to verify persistence.
5. Load a long-running save that saves slowly, then save twice normally. Send openmw.log. Both loads and saves now report their phases; no special profiler setup or unoptimised build is required.

## Validation status

Local Lua syntax, tooltip/protection/layout/paper-doll regressions pass. New tests execute the shipped timer integration and divider normalizer, covering focus interruption, ownership, hidden/drag/modal states, save/load timer reset, and provider sections. C++ tests compare the transient index with linear lookup across duplicates, mismatched base IDs, misses, appended references and changed object state. A 10,000-reference fixture with 5,000 lookups reduces visited list nodes from 37,502,500 to 89,972; this is an algorithm test, not a game FPS or load-time claim. Windows CI runs both the C++ and UI checks before the engine build. No live UI or save-game pass is claimed by the author.
