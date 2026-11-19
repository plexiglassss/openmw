# OpenMW 0.52 RC1 — cumulative custom port

This branch combines official `openmw-52-rc1` (`4ff261bf7c0d42a6cc06dd1ebc5d451802cff055`)
with the cumulative custom engine through `c4b71b6754e905ec820a05407038c37ef6c51c02`.
Its tree is identical to the reviewed local integration `61bf55d21b5537f0665fd86540ab9dcf635209f6`.
That union includes both the complete follower/keybindings branch and the latest
cell activation and streaming batch. The separate player-only animation feature
from `e39f2dd83a8308589cbd9940978fe142c713590b` is also ported.

## Included work

- The redesigned **Options → Controls → Mod Keybindings** page: searchable mod
  list, mod names and action descriptions, wrapped responsive rows, expandable
  conflict details and readable controller buttons.
- Existing inventory and tooltip integrations, window snapping, dialogue colours
  and selection controls, journal typography, message positioning, Continue and
  rest/wait improvements, native map icons and travel/key/cleared tooltips.
- Native deleveler, location rendering profiles, follower extension compatibility
  and the generic player-only additional-animation loader.
- Cumulative dialogue/script/resource lookup work, native query jobs, UI updates,
  save/load and cell activation/streaming work, including the most recent batch.
  Existing optional adapters and rollback switches remain available. Performance
  gains in a particular game/modlist require measurement.

The branch inventory and source plan record exact provenance. Historical source
checks run against their original commits. A separate RC1 gate binds the entire
new source tree; historical PASS files do not certify RC1 behavior.

## RC1 adaptation

Resource and worker ownership follows RC1's shared/unique ownership APIs while
OSG objects keep their OSG ownership. Real-time Lua timers remain active during
paused simulation and retain relative deadlines through saves. The new cursor
resource API takes precedence, with compatibility for older `pointer` strings.
RC1's profiling interfaces and matching October dependency archive are used.

The accepted inactive-cell lookup optimization is committed in this branch.
CI does not rewrite the engine before compilation. The rejected extra post-load
waiting stage and extra pre-game lookup preparation remain absent.

## Installation and checks

Use the complete Windows distribution from a successful workflow for this exact
commit. Extract it into a separate engine folder and keep its executables, DLLs,
resources and bootstrap configuration together. Keep the prior engine folder for
rollback. Continue using your existing mod directories and OpenMW user settings;
the package does not replace your modlist or saves. Optional Frame Generation is
separate. Existing Inventory Extender Redone and UI toolkit mods remain separate.

Check Mod Keybindings at your normal resolution, including a long description and
a conflicting binding. Load a separate save slot and check inventory/tooltips,
dialogue, a cell transition, save/reload and follower spells before replacing a
regular save. Native/fixture tests do not establish game FPS or visual correctness
for the installed modlist. Consult the workflow evidence for the actual test and
package result; this source README does not claim an unrun build passed.

## Follower Balance distribution

Follower Balance 1.1.1 is a separate Lua-only mod. Its complete feature set uses
standard OpenMW 0.52 RC1 (API166); it does **not** require this custom branch.
The follower manager fix and effective maximum-health display are in that mod's
ZIP. A third-party follower HUD that displays base health is not rewritten by
either the manager repair or this RC1 port.
