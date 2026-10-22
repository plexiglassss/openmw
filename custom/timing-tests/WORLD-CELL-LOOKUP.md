# Known-cell reference lookup

This change follows the indexed-build capture from 21 October 2026. In that
capture, 37 reference lookups in BMStartUpScript caused 29,146 per-cell index
build calls. The earlier per-cell optimization remains in place.

WorldModel now maintains candidate cells for each reference ID. It preserves
the existing pointer-cache precedence, reverse exterior order, case-insensitive
interior order, and the existing content-store fallback for unseen cells.
Unknown cells remain in their original search position until they are preloaded
or loaded. Index maintenance does not itself load them. The draft cell and cells
excluded by the existing name/location maps do not join normal lookup.

The candidate index includes inaccessible references because counts and deletion
flags can change without an insertion. The final CellStore lookup still checks
accessibility. Insertion, movement, content loading and save restoration mark
the affected cells dirty. Refresh replaces membership for those cells; misses
and final object pointers are not cached by the new index. World clear discards
all candidate and dirty-cell state. No new data is written into saves.

Index construction has a CPU and memory cost, measured separately as
`world.cellCandidates.update`. Candidate visits are measured as
`world.cellCandidate`. A subsequent game capture is needed to establish the
net benefit in the full modlist; synthetic timings are not an FPS prediction.

The Windows workflow runs CellLookupTest and WorldCellLookupTest with both
per-cell algorithms, and WorldCellLookupTest with the old world traversal.
`OPENMW_WORLD_CELL_INDEX=0` is a developer regression switch; users do not need
to run the old traversal. The new index is enabled by default.

WorldCellLookupTest covers insertion after a miss, a newly known cell, movement
and return, exterior/interior ordering, cache precedence, changing accessibility,
zero-count content references, preload/load transitions, draft exclusion, clear,
save restoration and unchanged serialized reference bytes. Its benchmark includes
initial index construction while searching 1,000 known cells containing 32
references each for 100 different absent IDs.

No individual mod scripts, quest logic, script scheduling, Frame Generation,
Lua behavior or timing-writer behavior are changed by this patch.

## Construction refinement after the 22 October capture

The original world index spent 2678.26 ms constructing its initial membership
map. Subsequent seven updates totalled 16.26 ms. This revision replaces a
separately allocated tree node per ID/cell association with sorted contiguous
candidate vectors. Dirty cells are processed in lookup order so initial shared
candidate lists grow at the end. Sorted old/new ID lists are compared in one
linear merge. Ordinary fully indexed lookups visit the stored vector directly;
the snapshot remains for unknown cells. Preloaded ID duplicates are still
removed before membership updates. Cache precedence and final accessibility
checks remain unchanged.

Two additional tests cover moved duplicate candidates and a benchmark with
128,000 distinct IDs plus an ID shared by 2,000 cells. The earlier benchmark
reused only 32 IDs and did not represent construction with many distinct IDs.
These changes require Windows CI and a subsequent real capture; no measured
in-game improvement is claimed before those results exist.
