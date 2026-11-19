# Public fallback artwork and location catalogue

The 19 transparent 64 × 64 PNG glyphs in `files/data/textures/mapmarkers/` are
original artwork created for this user's OpenMW map feature. They reproduce the
category concepts (house, cave, mine, tomb and similar), without copying an
MWSE mod's icon artwork. Their editable SVG sources are in `icon-sources/`.
They may be distributed and modified with this OpenMW fork under its existing
GPL-3.0-or-later licence.

`known-locations.tsv` contains location-name/type facts adapted from the
user-supplied **Improved Global Map Markers 1.7** by **Gerotaritor**. No MWSE
source code, coordinates, door records, saved discovery state, or icon artwork
from that mod is distributed by this feature. The factual catalogue was
previously checked against the supplied `allExtCells.lua`, whose SHA-256 is
`0de7fe3dcf226fb9bcf7e2a30772fb0eef1c9b9f79e8ba9c49aeae7496b1ac96`.

There were 1,522 primary assignments and 1,517 distinct primary names. The source
table's final assignment wins for duplicate names. An additional 1,173
unambiguous aliases remove explicit exterior/entry suffixes and common terminal
type words, or expand a terminal `tomb` to `ancestral tomb`. Compound names do
not receive the type-word-removal alias. Existing primary names always take
precedence over derived aliases, and aliases with conflicting types are omitted.
Only the source's primary category is used for locations with more than one icon.

The resulting 2,690 location names are not a current or complete list of this
user's installed mods. Unknown English names and distinctive door-model prefixes
receive conservative classification; other names remain generic. Canonical cell
IDs are checked before translated display names. Location coordinates are always
the engine's actual coordinates, including any relocated landmass data.

Run `python custom/map-icons/generate-known.py --check` to verify the checked-in
C++ catalogue. The generator uses the included TSV facts and needs no MWSE mod,
Lua runtime, internet connection, or game data. Classification uses a static string
pool and binary-search index at discovery time; it does not scan the world or
perform work in the frame loop.

User-supplied private map skins are loaded via `mapicons/markers.tsv`. The public repository does not redistribute the artwork from either supplied archive. Credits and resource hashes for the private distribution accompany that distribution.
