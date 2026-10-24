# Lazy restoration of metadata-only saved cells

No configuration or mod update required. Install the resulting Windows engine build; retain Inventory Extender Bridge 1.2.9 and existing Frame Generation files.

## Evidence

User's openmw(20261024-195258).zip, engine ec015282bb: total load 15959.2 ms, CSTA 10104.5 ms. Instrumented cells 37894; metadata-only 37613. Interior resolution/base load 8281.74 ms, explicit exterior base load 1708.75 ms, saved references 58.5814 ms. These measurements identify an optimization opportunity, not a promised time saving. Lua or gameplay lookups may still demand some deferred cells.

## Change

WorldModel::readRecord resolves CSTA without forcing reference loading. Water, respawn and fog restore normally. If subrecords remain after metadata/fog, the original full reference restoration runs (including moved-reference handling). Otherwise no plugin reference loading occurs. The moved-cell callback itself is unchanged, and moveFrom still loads destination references when required.

WorldModel::writeCell no longer forces unloaded/preloaded cells to load. Metadata/fog and existing live reference changes are still written. This avoids shifting the eliminated work to the next save. Unloaded/preloaded cells have not instantiated their plugin references; existing reference changes and moved-reference maps require loaded cells. CellStore has no transition that unloads live changes. Missing-cell handling, save format, master mappings, reference IDs and script handlers are unchanged. No cell records are deleted.

## Source checks

Inspected findCell/getExterior forceLoad paths, CellStore load/preload/loadRefs, loadState/saveState/readFog/writeFog, readReferences/writeReferences, moveTo/moveFrom, writeReferenceCollection and World::saveLoaded. Lazy base loading does not reset restored water/respawn/fog fields. Metadata-only records lack saved reference identities that need early registration. Records containing object or movement subrecords still eagerly instantiate and restore references.

## Verification

Run python3 custom/performance-tests/test-save-metadata.py. It extracts and compiles the actual readRecord/writeCell bodies with instrumented engine/reader doubles; covers unloaded/preloaded/loaded cells, fog/no fog, references/no references, metadata preservation through save, no forced load at save, later demand, missing cells and unrelated records. The test passes for this change and fails for the previous eager implementation. It is a control-flow regression test, not a full-engine save round-trip test. GitHub CI runs it before the Windows engine build.

## In-game verification

1. Load the same early save used for the 15.96-second baseline. Preserve openmw.log.
2. Check the current cell, inventory, journal and map exploration. Enter a previously visited interior and return; check door travel and object state.
3. Save to a new slot, then reload it. Check map exploration and any known moved objects, containers and changed water levels relevant to that save.
4. Send the log containing load, save and reload timings. Compare total load as well as CSTA phases; slower player/scene phases could indicate shifted demand.

No live Windows result is available at commit time. Map/UI save optimization and old ProximityTool marker-state growth remain separate investigations. Existing cumulative performance and UI changes remain included.
