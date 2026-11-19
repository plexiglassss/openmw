# Shrine tooltip support for the custom OpenMW engine

This build adds the small native UI bridge used by the separate **Shrine
Tooltips for OpenMW** mod. Hovering over an annotated shrine blessing uses the
engine's existing spell tooltip, including the effects of the winning loaded
spell record, icons, magnitudes, durations and target attributes/skills.

The preserved source baseline is
`528bffb348872a964d25c092fa5dd574c1b7f673`. Its dialogue colours and settings
repair, journal controls, Inventory Extender support, frame-generation/loading
work and separately verified lookup-only build patch are retained. The bridge
does not change shrine scripts, button answers, donation prices or spell casts.

## Install

Exit OpenMW. Keep your previous working engine folder as your rollback copy.
Extract this complete engine package into a separate engine folder and launch
its `openmw.exe`; retain your existing user configuration and mod list.
If your old engine directory also contains a local `openmw.cfg`, retain that
local configuration in the new folder so its data/config paths remain in use.

Install the separate Shrine Tooltips for OpenMW mod folder and enable its
`ShrineTooltips.omwscripts` entry. This engine addition alone does not register
any shrine mappings. The mod does not need the original MWSE mod installed.

## API

Player scripts can call:

```lua
local box = ui.getInteractiveMessageBox(lastSeenId) -- argument optional
-- nil if there is no unanswered native box, or its ID is unchanged.
-- Otherwise: {id, message, buttons, sourceScriptId}.
ui.setMessageBoxButtonSpellTooltips(box.id, {[2] = "veloth's indwelling"})
```

Buttons use Lua's one-based indices. Captions include native translation/tag
replacement. `sourceScriptId` identifies the actual executing MWScript, when
known; ordinary engine and unattributed dialogue-result boxes have no source.
The setter queues an action and rechecks the ID, unanswered state, button
indices and spell availability before touching the widgets. Old actions are
discarded after selection, replacement or load/clear. An empty table clears
the annotations owned by the bridge for the same box. No raw MyGUI pointers
are exposed to Lua.

## Verification and limits

The build workflow checks source preservation, compiles the complete Windows
engine, runs the engine and component tests, and packages only after they pass.
`CI-ID.txt`, test XMLs and `PACKAGE-SHA256.json` identify the resulting files.
These automated checks do not establish live rendering with the user's fonts,
scale and UI mods. In game, check the tooltip before buying a blessing, then
confirm the same donation and blessing still occur after selection. Also check
an ordinary non-shrine choice box and a second shrine menu.

To roll back, launch the preserved previous engine and disable the separate
`ShrineTooltips.omwscripts` mod. No save conversion is required.
