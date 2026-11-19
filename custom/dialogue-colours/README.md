# Configurable dialogue and journal colours

This cumulative Windows engine build adds a **Dialogue colours** tab in the in-game Settings menu.
This revision builds on the delivered dialogue-colour engine, source `b34db577`, and fixes the
settings-tab layout reported in game. It retains the journal typography engine from `cbdf7a0a`,
its separately applied lookup optimization and removal of the rejected extra loading stage.

## What changes

- Ordinary dialogue follows the current system/theme font colour by default.
- Your selected topic/question headings, such as "Haldenshore" and "latest rumors", use soft cyan.
  This also applies to other response headings, such as persuasion/service headings.
- Inventory additions and removals use soft green.
- Recognised attribute, skill and other stat-change notices use light blue.
- Recognised stage directions, actions and sensory descriptions in dialogue use lavender.
- Journal-update notices use neutral grey.
- Recognised narration in the journal uses a separate dark purple for readability on paper.

Existing topic links, their hover/pressed colours, numbered choices, normal journal text, journal
font sizes/alignment, authored wording and dialogue result scripts retain their existing behaviour.
This feature changes presentation; it does not add notifications for stat changes that were silent.

## Install

1. Close OpenMW and its launcher.
2. Keep a copy of your current engine folder so the whole engine update can be rolled back together.
3. Copy the supplied engine files into that engine folder and allow matching files to be replaced.
   Include **defaults.bin and resources**, not just openmw.exe: they contain the new defaults and labels.
4. Start the game using the updated openmw.exe. Open **Options → Dialogue colours**.

Keep your current openmw.cfg, settings.cfg, mods and saves. This package contains no game plugin.
It does not replace your separately installed frame-generation version.dll, openmw_fg.ini or its settings.

## Configure

The feature is enabled by default. **Use theme colour for normal dialogue** is also enabled by default.
Leave it enabled to retain the colour you already like, including theme changes.

Choose one of seven text types and adjust Red, Green and Blue from 0 to 255 in one-unit steps.
Select **Your topics/questions** to change the headings independently of NPC speech.
The preview shows the result on a dark background, or on paper for journal narration. The reset button
restores only the selected colour. To set a custom ordinary-dialogue colour, first turn off the system-colour option.

Dialogue narration recognition and journal narration colouring have separate toggles. Turning off
the main colour feature restores the original colours. Settings persist through the normal settings.cfg mechanism.
Existing dialogue history is recoloured when the dialogue window resumes. Reopen the journal to rebuild its pages.
The tab is created by the engine and is available even if a mod provides an older settings-window layout;
use the tab bar's arrows when all tabs do not fit, and scroll within the colour tab for lower controls.

## Settings-tab repair

The previous tab gave several row controls horizontal stretching without top anchoring. MyGUI
then vertically centred those controls when their parent canvas resized, causing the overlap and
missing labels in the reported screenshot. The controls now use explicit row anchors and layout
based on the available panel width and measured text. The actual production widget layout is
covered by a headless MyGUI regression, in addition to the existing text/typesetter tests.

## Recognition limits

Native inventory and journal notifications carry a category directly from the engine. Their colours therefore
do not depend on English wording. Current localised inventory, skill-notice and journal GMST templates are also
recognised for messages sent through the ordinary notification path.

Mods can put spoken dialogue, narrative descriptions and status notices into the same unlabelled text field.
For that content, recognition uses conservative English patterns and stage-direction punctuation. It is not
a language model and cannot identify every possible author-written sentence. Ordinary references to a skill,
item or sound should remain dialogue. The narration toggles allow these inferred descriptions to be left unchanged.

## Checks to perform in game

- Open **Options → Dialogue colours**. Check that each toggle has a readable description, the
  text-type selector and RGB tracks are separate, and the preview/help/reset controls do not overlap.
  Repeat after switching tabs or resizing the game window; scroll to any controls below the viewport.
- Select **Your topics/questions** and change its colour. Ask about "Haldenshore" or "latest rumors":
  the heading should change while the NPC response keeps your normal dialogue colour.
- Repeat a conversation that awards an item and a journal entry. The NPC speech should keep its familiar colour;
  the inventory notice should be green and the journal notice grey.
- Try a conversation that explicitly prints a stat gain or loss. The printed notice should use the stat colour.
- Check a recognised direction such as `[Ariela coughs.]` and a sound description. Its prose should be coloured;
  any topic link inside it must still be clickable and keep its existing link colour.
- Open an existing journal entry with sensory or action narration. Check readability, topic links, page edges,
  and your chosen journal font size/alignment.
- Change a colour, close the settings and reopen the relevant window. Restart once to check persistence.
- Turn the feature off and confirm the original dialogue/journal appearance returns.

Automated tests exercise the real typesetter, colour selection, UTF-8/text preservation, link hit tests,
wrapping/justification, classification examples and negatives, and settings defaults/save/reload. The build
also runs the existing component/engine regression suites before packaging. CI is not an in-game visual test.

For rollback, restore the previous complete engine files together, including defaults.bin/resources. The added
settings can remain in settings.cfg; they do not alter plugins or saves.
