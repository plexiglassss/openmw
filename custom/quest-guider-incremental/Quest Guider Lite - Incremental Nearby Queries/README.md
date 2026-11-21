# Quest Guider Lite — Incremental Nearby Queries

The nearby-quest menu previously scanned neighbouring cells and linked door
interiors synchronously. It now uses the existing indexed work queue, publishing
one complete result after the scan finishes. The existing shared 2 ms soft budget
is unchanged; an individual native object query cannot be interrupted. Superseded
requests, leaving the source cell and loading a save discard pending results;
quest-data invalidation restarts the private scan. Normal quest/gameplay handlers
and the existing parallel position sorter are preserved.

Keep Quest Guider Lite, Quest Guider Lite - Indexed Data and your generated data.
Add this folder as a data directory AFTER the existing Indexed Data, Parallel
Quest Sorting and engine-native Quest Guider adapter folders. It must win
scripts/quest_guider_lite/main.lua. The two unchanged parallel sorting helpers are
included, so the new override is self-contained on top of the indexed installation.
There is no new plugin or regeneration step. Avoid reinstalling an older main.lua
over this folder. Remove this override to return to the previous winning scripts.

Baseline: the user's 17 November QuestGuiderLite.zip, Parallel Quest Sorting layer,
with indexed queue 1.5. A different/newer winning main.lua must be reconciled before
using this replacement. Lua 5.1/5.4/LuaJIT fixture parity does not prove installed-game
behavior or a measured Balmora frame-time improvement. Nearby menu results may
arrive a few updates later; the UI receives no partial list.
