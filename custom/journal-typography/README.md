# Journal typography — custom Windows OpenMW build

This build adds separate controls for journal text size and paragraph alignment.
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

- **Font size:** Use interface size (default), or an integer from **10 to 24**.
  Try **14** first, or **12** for more compact text.
- **Alignment:** **Left**, **Centred**, **Right**, or **Justified**.
  Choose **Justified** for straight left and right edges on wrapped paragraphs.

In-game changes apply when you close and reopen the journal; no engine restart
is needed. Options saves the values to your user settings when it closes.

For configuration-file users, the equivalent values in the existing `[GUI]`
section of `settings.cfg` are:

```ini
journal font size = 14
journal text alignment = 3
```

Font size `0` follows the regular interface font size. Alignment values are
`0` left, `1` centred, `2` right, and `3` justified. Other values are rejected.
The defaults are `0` and `0`, preserving the old appearance until changed.

## Exact behavior

Font size changes journal entries, quest text and topic text, including their
date and title lines. It does not change dialogue, books, scrolls, inventory,
HUD text, journal buttons or the alphabetical topic index. Existing GUI scaling
still scales the whole journal window as before.

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
defaults, every offered value and rejection of unsupported values.

The archive includes `CI-ID.txt`, both test-result XML files,
`journal-typography-source-manifest.json`, `PACKAGE-SHA256.json`, and the retained
lookup-repair evidence. CI does **not** constitute a visual OpenMW playthrough.

## In-game checks

Use an existing save with several journal entries and known clickable topic
links. No quest console command is needed.

1. With **Use interface size / Left**, inspect the journal, inventory, dialogue
   and a book. The old journal appearance should be retained.
2. Select **14 / Justified**, close Options and close/reopen the journal. Long
   paragraphs should have even side margins; final lines should remain natural.
   Dates and titles should stay at their original alignment.
3. Turn forward/back through entries, open a quest and a topic, and click a link
   near the right edge of a justified line. The correct topic should open.
4. Try **12**, then **24**. Entries should reflow without missing text or an empty
   first page. Check long entries that cross multiple pages.
5. Check **Centred** and **Right** on short and long paragraphs. Return to your
   preferred size and **Justified**.
6. Reopen inventory, dialogue and an ordinary book. Their text size must match
   the baseline. Alphabetical topic navigation and journal buttons keep their
   old size.
7. Exit normally, restart the same engine, and confirm the two selections persist.

Live visual appearance with your exact font/UI mods, mouse/controller behavior
and the Windows launcher must still be observed in game. Report any clipping
with the font size, alignment, UI scale, active font mod and a screenshot.

## Rollback

Close the game and launcher, restore the old executable shortcut/folder, and
launch the previous complete engine build. These controls do not alter quest
records or save-game content. To restore this build's original journal layout,
select **Use interface size / Left**.
