# Native Travel Tooltips, If The Key Fits and Been There, Done That

These features are part of the cumulative OpenMW engine. They build on
`ca144536f3dd813f9fb13c832b7396d1554d71f5`, the verified native world-map-icons
build, and retain its earlier engine fixes and settings. They do not require
Advanced World Map or run the original MWSE Lua scripts.

## Install and configure

1. Extract the complete Windows engine package into a new engine directory.
   Keep the old engine directory for rollback. Use the package's executable,
   runtime libraries, resources, defaults and bootstrap configuration together.
   Keep your existing user OpenMW configuration and content order.
2. Extract the separately supplied `Travel_Tooltips_Native_Data` archive into
   its own mod folder. Add its `Data_Files` directory to OpenMW's data
   directories. There is no ESP, ESM or omwscripts entry to enable.
3. Start this engine and open **Options → Tooltips**. This is an engine tab,
   not a Lua mod settings page. Restart OpenMW after adding/changing the data
   folder. Key and cleared-location hints work without the travel artwork.

Use your existing save. No generated-data rebuild or merged plugin is needed
for these three features. The original MWSE mods are not required by this
engine; OpenMW does not execute their MWSE entry points.

## Options and defaults

All options live in the native Tooltips tab and persist through OpenMW's
normal settings mechanism. Reset affects only this page's fourteen options.

| Option | Default | Effect |
| --- | --- | --- |
| Illustrated destination previews | On | Hover a supported destination in the travel menu |
| Travel overview map | On | Hover the Travel service in dialogue |
| Gondolier previews | On | Include Vivec gondolier destinations |
| Missing-image fallback | Off | Use Stuporstar's image when the selected pack lacks one |
| Wide preview layout | On | Wider, centred destination preview |
| Destination art pack | Vehk's Ink | Also offers Stuporstar and ComradeRaven |
| Overview map | Travel routes | Also offers Vvardenfell and book maps |
| Artwork tint | Indoril | Eleven original palette choices |
| Destination title font | Daedric | Also offers interface font and monospace |
| Description font | Interface font | Also offers monospace and Daedric |
| Preview size | 100% | Range 50–200%; constrained by the available screen space |
| Matching-key hint | On | Add `(Use Key)` beside the lock level when the matching key is carried |
| Cleared entrance tooltip | On | Add `(Cleared)` to a teleport door's destination |
| Cleared map title | On | Add `(Cleared)` to the current interior's native map title |

The native font choices use the fonts OpenMW actually exposes. The original
MWSE mod's second font slot is replaced by OpenMW's monospace option.
Destination art coverage is exactly the supplied packs' coverage; unknown
locations keep their normal travel button. Fallback does not invent new art.
The Vvardenfell overview is suppressed for the original mod's recognised
Tamriel Rebuilt, Project Cyrodiil and Skyrim provider prefixes.

## Gameplay behavior

**If The Key Fits:** the hint checks the actual player inventory and the
target's assigned key. It only affects tooltips for a locked door/container.
Activation, key consumption, lockpicking, traps, crimes and ownership remain
under the existing engine code.

**Been There, Done That:** this follows the original mod's per-interior
hostile criterion: a living NPC/creature with modified Fight at least 82.
The player is excluded. A room first visited without such a hostile remains
ineligible, avoiding cleared labels on ordinary shops/homes. A previously
eligible room becomes cleared when no qualifying living actor remains;
new/respawned hostiles can revoke that status. Re-entry refreshes the current
room. Connected dungeon rooms are tracked separately. The label does not
promise quest completion, all loot collected, or every actor killed.

History is per save, stored in an optional `DCLR` record. Installing on an
existing save cannot infer what was cleared before tracking began. Disabling
the two labels hides them and retains tracking/history. No distant interior
is loaded for a tooltip, and actor checks are driven by the current cell's
entry/lifecycle events rather than a whole-world or per-frame actor scan.

**Travel Tooltips:** the engine reads a bounded, non-executable VFS manifest
and creates a native illustrated panel. It preserves the current travel
destinations, prices and travel actions. A missing/invalid optional art pack
falls back to normal travel UI and records a diagnostic in the OpenMW log.
Images remain stable during a hover and can vary when reopening travel.

## Data format and attribution

The engine contains only a generic reader and synthetic tests. The private
data conversion uses the user's supplied Travel Tooltips 2.0.0 archive.
Original artwork and destination descriptions are not committed here.
Do not redistribute that private data package without the relevant authors'
permission. Credits and exact original file hashes accompany the data ZIP.

Original mods:

- Travel Tooltips 2.0.0: <https://www.nexusmods.com/morrowind/mods/48306>
- If The Key Fits 1.0, trainwiz: <https://www.nexusmods.com/morrowind/mods/52036>
- Been There, Done That 1.0.1, Merlord: <https://www.nexusmods.com/morrowind/mods/52807>

The manifest is `travel_tooltips/data.tsv` in the active VFS. It starts with
`version<TAB>1`; records describe image paths/dimensions, destination
prefixes, descriptions and overview maps. Backslash, tab, CR and LF are
escaped. Paths must be relative, normalized texture paths. Pack selection
uses longest matching prefixes. See `traveltooltipdata.hpp` for the exact
record contract. User data is never executed as code.

## Verification and rollback

CI requires actual native option-form tests, parser/selection tests, save
round-trip/state tests, the complete earlier engine/component regressions,
the preserved source gates, full Windows linking, complete-package inventory
and startup probes before publishing the engine artifact. Test receipts are
included in the package. These checks do not claim a live in-game play test
or full installed-modlist compatibility test.

To roll back, close the game and launch the retained previous complete engine
directory. The extra save record is optional and can be skipped by older
OpenMW builds. An older build will not preserve this new tracking history
when resaving. Keep a pre-upgrade save to retain an exact rollback point.
The travel data directory can be removed independently; it contains no game
plugin or placement edits.
