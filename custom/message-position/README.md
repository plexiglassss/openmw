# Message position in OpenMW

Temporary notifications such as “Lockpick successful!” can overlap a custom
hotkey bar. This engine adds **Options → Messages** with nine screen positions,
horizontal and vertical offsets, **Reset position**, and **Show test message**.

## Using it

- Choose **Top centre** to move notifications away from a bottom hotkey bar.
- To keep them near the bottom, choose **Bottom centre** and set **Vertical
  offset** to approximately **-80**. Negative values move upward.
- Positive horizontal values move right; positive vertical values move down.
- Offset units follow the interface scaling setting. Slider arrow buttons or
  the mouse wheel adjust one unit at a time.
- Click **Show test message** to check the location. Changes apply immediately
  and use OpenMW's normal settings persistence.
- **Reset position** restores bottom centre and zero offsets, which preserves
  the original 48-pixel bottom padding.

Messages keep their existing duration and maximum queue length. At the top they
stack downward; at the bottom they stack upward. Centre positions centre the
whole group. The group is kept within the current logical viewport whenever
it fits; oversized notifications can still be taller/wider than a tiny window.

This affects native temporary/static notification boxes, including messages
sent through the usual engine notification APIs. Buttoned dialogs, shrine
blessing tooltips, subtitles, and independently drawn Lua HUD widgets keep
their existing placement.

## Settings file

The in-game controls write these values in `[GUI]`:

```ini
message box position = 7
message box horizontal offset = 0
message box vertical offset = 0
```

Positions are row-major: 0 top left, 1 top centre, 2 top right, 3 middle left,
4 centre, 5 middle right, 6 bottom left, 7 bottom centre, 8 bottom right.
Offsets range from -1000 to 1000. Invalid numeric position values fall back to
bottom centre. No manual configuration editing is needed.

## Installation and preservation

Close OpenMW and retain a copy of the working engine folder. Install the full
updated Windows package, including `defaults.bin` and `resources`, into the
engine folder you actually launch. Updating only `openmw.exe` is insufficient.

The user's Rubic0n runtime and Frame Generation are separate:

- Preserve/reapply `lua51.dll` and `resources/lua_libs/content.lua` from the same
  Rubic0n build.
- Preserve/reapply the existing custom Frame Generation `version.dll` and its
  configuration.

The engine source is based on cumulative revision
`1b78b9c1a566b91bbe1576387842625f3ae509d1`, including the earlier journal,
dialogue-colour, settings-layout, and shrine-tooltip work. The existing
build-time lookup repair is retained and verified separately.

## In-game checks

1. Open Options → Messages. Choose Top centre and click Show test message.
2. Try Bottom centre with vertical offset -80 and check that the real lockpick
   message clears the hotkey bar.
3. Trigger several messages and confirm that their boxes remain separated.
4. Resize the window or change UI scaling and check the selected position.
5. Restart OpenMW and confirm the chosen position/offsets remain selected.
6. Confirm ordinary buttoned dialogs and shrine spell tooltips still work.

CI compiles the Windows engine, tests the placement geometry and real MyGUI
settings controls, and runs the retained engine/component regressions before
packaging. Native gameplay, screenshots and the user's installed modlist are
not certified by those automated tests.
