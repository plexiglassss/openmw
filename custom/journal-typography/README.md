# Journal typography — custom Windows OpenMW build

This build extends journal text sizing to the quest/topic lists and alphabetical
index, and adds independent page-number size and button-label scale controls.
It preserves the user-confirmed journal build `26f1abfa`, including its tiny
two-pixel right clearance, selected text size and paragraph alignment.
It is based on the accepted `00af1d5c63313c61a46c6c8782ce45b3e05f023a` engine,
retaining its lookup optimization and removal of the rejected initializing stage.

## Install

Close OpenMW and its launcher. Extract the **complete Windows build** into a new
folder alongside your current custom engine. Run the new `openmw-launcher.exe`
and check that it uses your existing OpenMW configuration/content list, then start
the game. Keep the old engine folder for rollback. If using a mod manager's
executable shortcut, point it to the new engine executable.

The executable, `defaults.bin`, DLLs and `resources` directory are one build:
do not copy only `openmw.exe`. No ESP, ESM or load-order change is required, and
no save-game conversion or external builder is involved. Do not replace your
personal `openmw.cfg`, `settings.cfg`, mods or saves with a generic configuration.
Portable installations with local configuration should copy that existing local
configuration into the new engine folder before launching.

## Controls

Open **Launcher → Settings → Interface → Journal Text**, or in game open
**Options → Interface → Journal text**.

If a UI mod supplies an older settings layout, the engine adds a separate
**Journal text** tab to Options for any missing controls. Controls that the
layout already provides stay in their existing location. The launcher controls
are also available.

- **Font size:** Use interface size (default), or an integer from **10 to 24**.
  Keep your existing choice; it now also applies to the quest/topic lists and alphabet.
- **Alignment:** **Left**, **Centred**, **Right**, or **Justified**.
  Choose **Justified** for straight left and right edges on wrapped paragraphs.
- **Page number size:** Use current layout (default), or **10 to 24** logical pixels.
  Select the same number as your journal text size if you want matching heights.
- **Button label scale:** **50% to 100%**, in one-percent steps. **100%** keeps the
  current artwork size; **75%** is a useful starting point for smaller labels.
  These labels are images, so this scales their artwork rather than changing fonts.

In-game changes apply when you close and reopen the journal; no engine restart
is needed. Options saves the values to your user settings when it closes.

For configuration-file users, the equivalent values in the existing `[GUI]`
section of `settings.cfg` are shown below. This is an optional example, not a
requirement to replace your existing font-size/alignment values:

```ini
journal font size = 14
journal text alignment = 3
journal page number font size = 14
journal button scale = 75
```

Font size `0` follows the regular interface font size. Alignment values are
`0` left, `1` centred, `2` right, and `3` justified. Other values are rejected.
Page-number size `0` retains the font height from the journal layout; other allowed
values are 10 through 24. Button scale is an integer percentage; out-of-range
values are clamped to 50 through 100. New defaults are page size `0` and button
scale `100`, retaining their existing appearance until you choose a size.

## Exact behavior

Font size changes journal entries, quest text and topic text, including their
date and title lines, quest/topic lists and the alphabetical topic index. Lists
remain left aligned; the alphabet remains centred. Large alphabet text uses an
additional column with compact `(A)` labels so all letters remain accessible.
The column widgets have separate click areas. Existing GUI scaling still
scales the whole journal window as before.

Page-number size applies to the two numbers below the pages. Button scale applies
to Options, Close, Previous/Next, Journal, Topics, Quests, Cancel and Show All/Show
Active. It retains their artwork and hover/pressed states, resizing around their
original centres. Reopening the journal applies the selected scale from the
original geometry, so repeated openings do not progressively shrink the labels.
The mod-owned Bestiary tab is excluded. Dialogue, inventory, books, scrolls and
HUD text retain their own sizes.

Journal reading pages reserve **two logical pixels** at the right edge of their
240-pixel widget. The text is laid out across 238 pixels so small glyph overhangs
have room before the clipping boundary. The left edge, page height and font size
are unchanged. A few lines or page breaks may reflow; ordinary books retain their
existing widths. The alphabetical index has its own column layout.

Alignment changes prose paragraphs. Dates and standalone topic/quest titles
retain their original alignment. Inline speaker attributions stay with their
quoted paragraph. Justification enlarges spaces between words only on lines
that wrapped automatically. A paragraph's last line, an explicit newline,
single-word lines and non-breaking spaces are not stretched. Glyphs retain
their original proportions; clickable topic areas follow the adjusted words.

The font metrics are captured per document, so layout, page breaks, rendering
and link hit testing use the same size. The existing font resources remain in
use. Bitmap fonts can look softer when scaled; this change does not replace
your font or provide new font artwork.

The pagination fix also avoids an empty opening page when a first entry is
too tall to fit on one page.

Carriage returns in Windows-style CRLF text no longer contribute the font's
invisible fallback-glyph width. Measurement now matches rendering, including
consecutive paragraph breaks, without changing content or hyperlink offsets.

## Validation included in the build

Packaging occurs only after the Windows compiler, all component tests, all
engine tests and the four existing cell-lookup configurations succeed.
Typography tests use the actual typesetter and deterministic font metrics to
check size isolation, atlas scaling, pagination, alignment, paragraph endings,
Unicode, styled links and click coordinates. Settings tests check persistence,
defaults, every offered value, rejected font-size values and bounded button scale.
Index tests cover Latin/Cyrillic letter coverage and click targets at all offered
sizes; production geometry helpers cover page bounds, centred button resizing
and controller navigation. List/widget integration is source-reviewed; these
automated checks do not establish actual GUI rendering or input interaction.

The archive includes `CI-ID.txt`, both test-result XML files,
`journal-typography-source-manifest.json`, `PACKAGE-SHA256.json`, and the retained
lookup-repair evidence. CI does **not** constitute a visual OpenMW playthrough.

## In-game checks

Use an existing save with several journal entries and known clickable topic
links. No quest console command is needed.

1. Keep your current text size/alignment. Open the journal, then its Options,
   Quests and Topics pages. List entries and alphabet letters should now follow
   that text size. Select a quest, an alphabet letter and a topic; each should open.
2. Set **Page number size** to the same numeric size as your journal text and
   **Button label scale** to **75%**. Close/reopen the journal. Page numbers and
   native button labels should use the selected height and 75% artwork size,
   respectively, while the main text retains your choice.
3. Hover and click Options, Close, Previous/Next, Journal, Topics, Quests, Cancel
   and Show All/Show Active where available. Check their full artwork in each state.
   The Bestiary tab should retain its mod-defined appearance.
4. Close/reopen the journal several times. Labels must keep the selected size.
   Scroll long quest/topic lists and click entries near the bottom and after wrapping.
5. Record your preferred settings, then optionally test text sizes **10**, **20**
   and **24**. Check every alphabet column, including its last letter, and keyboard/
   controller selection if used. Restore your preferred text size afterwards.
6. Check journal right-edge strokes, links and page navigation. Reopen inventory,
   dialogue and an ordinary book; their text size should retain its own setting.
7. Exit normally and restart this build. Confirm all four selections persist.

Live visual appearance with your exact font/UI mods, mouse/controller behavior
and the Windows launcher must still be observed in game. Report any clipping
with all four journal settings, UI scale, active font mod and a screenshot.

## Rollback

Close the game and launcher, restore the old executable shortcut/folder, and
launch the previous complete engine build. These controls do not alter quest
records or save-game content. To restore the original font size and alignment,
select **Use interface size / Left**; the small right-side clearance remains.
