GUI Settings
############

.. omw-setting::
   :title: scaling factor
   :type: float32
   :range: 0.5 to 8.0
   :default: 1.0
   :location: :bdg-success:`Launcher > Settings > Interface`

   This setting scales GUI windows.
   A value of 1.0 results in normal scale.
   Larger values increase GUI scale for high resolution displays.

.. omw-setting::
   :title: font size
   :type: int
   :range: 12 to 18
   :default: 16
   :location: :bdg-success:`Launcher > Settings > Interface`

   Specifies glyph size for in-game fonts.
   Default bitmap fonts work best at 16px; others may be blurry.
   trueType fonts do not have this issue.

.. omw-setting::
   :title: journal font size
   :type: int
   :range: 0, 10 to 24
   :default: 0
   :location: :bdg-success:`Launcher > Settings > Interface` :bdg-success:`In Game > Options > Interface`

   Sets the logical glyph size for journal entries and quest/topic text, including
   their date and title lines, quest/topic lists and the alphabetical topic index.
   Zero follows the regular ``font size`` setting. Page numbers and button artwork
   have separate controls. GUI scaling still scales the journal window normally.
   Close and reopen the journal to apply an in-game change; a restart is unnecessary.
   If a UI mod supplies an older settings layout, a separate Journal text tab
   provides any missing controls. Controls already present in that layout stay there.
   Values outside the listed range are rejected. Custom bitmap fonts may become less sharp when resized.

.. omw-setting::
   :title: journal text alignment
   :type: int
   :range: 0, 1, 2, 3
   :default: 0
   :location: :bdg-success:`Launcher > Settings > Interface` :bdg-success:`In Game > Options > Interface`

   Controls the horizontal alignment of journal, quest and topic paragraphs:
   zero is left, one is centred, two is right, and three is justified.
   Justification distributes additional space between words on automatically
   wrapped lines. The last line of a paragraph and explicitly broken lines stay
   left aligned, and non-breaking spaces stay inside their words.
   Dates, standalone titles and the alphabetical index retain their original alignment.
   Inline speaker attributions stay with their quoted paragraph.
   Close and reopen the journal to apply a change. Values outside zero through three are rejected.

.. omw-setting::
   :title: journal page number font size
   :type: int
   :range: 0, 10 to 24
   :default: 0
   :location: :bdg-success:`Launcher > Settings > Interface` :bdg-success:`In Game > Options > Interface`

   Sets the logical font height of the two journal page numbers independently of
   the journal text. Zero preserves the font height supplied by the current layout.
   Close and reopen the journal to apply a change. Values outside the listed range
   are rejected.

.. omw-setting::
   :title: journal button scale
   :type: int
   :range: 50 to 100
   :default: 100
   :location: :bdg-success:`Launcher > Settings > Interface` :bdg-success:`In Game > Options > Interface`

   Scales the journal's existing button artwork by this percentage. 100 keeps its
   original size; 75 makes it three quarters of that size. Applies to Options,
   Close, Previous/Next, Journal, Topics, Quests, Cancel and Show All/Show Active.
   The label artwork and its hover/pressed appearance are retained. Mod-owned tabs
   are outside this setting. Values outside 50 through 100 are clamped to that range.
   Close and reopen the journal to apply a change.

.. omw-setting::
   :title: dialogue colouring
   :type: boolean
   :range: true, false
   :default: true
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Uses separate colours for selected topic headings, inventory notices, stat changes, narration and
   journal-update notices in dialogue. It also enables the optional journal
   narration colour. Disable this setting to restore the previous text colours.
   Topic links and response choices keep their existing colours and behaviour.
   The settings tab is provided by the engine even when a UI mod replaces the
   settings-window layout. Select a text type to adjust its red, green and blue
   channels from 0 to 255, or reset only that colour. Journal narration is
   previewed against a light background; the other roles use a dark preview.
   Settings are saved when Options closes. Dialogue colours update immediately;
   reopen the journal to refresh text that has already been drawn.

.. omw-setting::
   :title: dialogue use system colour
   :type: boolean
   :range: true, false
   :default: true
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Keeps ordinary dialogue in the normal text colour supplied by the current
   interface theme. This default preserves the existing appearance of spoken
   dialogue. Disable it to use ``color dialogue normal`` instead. The custom
   colour is retained when theme mode is enabled; switching modes does not
   overwrite it.

.. omw-setting::
   :title: dialogue classify narration
   :type: boolean
   :range: true, false
   :default: true
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Recognises narrative descriptions and stage directions within dialogue text
   and applies ``color dialogue narration``. Recognition cannot cover every way
   a mod may describe an action or sound. Disabling this option leaves that text
   in its ordinary colour while retaining the notice colours.

.. omw-setting::
   :title: journal colour narration
   :type: boolean
   :range: true, false
   :default: true
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Colours recognised narration in journal entries. The separate
   ``color journal narration`` setting defaults to a darker colour suited to
   the journal's paper background. Other journal text keeps its current colour.

.. omw-setting::
   :title: color dialogue normal
   :type: color
   :range: [0, 1]
   :default: 0.80 0.70 0.50 1
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Custom colour for ordinary dialogue, used only when ``dialogue colouring``
   is enabled and ``dialogue use system colour`` is disabled. Like the other
   colour settings below, its four components are red, green, blue and alpha.
   The in-game controls edit the three colour channels and preserve alpha.

.. omw-setting::
   :title: color dialogue topic
   :type: color
   :range: [0, 1]
   :default: 0.55 0.85 0.85 1
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Colour for the topic/question headings displayed above NPC replies in the
   conversation history, such as "Haldenshore" and "latest rumors". Other
   response headings, including persuasion/service headings, use this colour
   too. The default is soft cyan. It is independent of normal speech and
   clickable topic links. Turning off ``dialogue colouring`` restores the
   theme's original heading colour.

.. omw-setting::
   :title: color dialogue inventory
   :type: color
   :range: [0, 1]
   :default: 0.60 0.80 0.56 1
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Colour for notices about items being added to or removed from inventory.

.. omw-setting::
   :title: color dialogue stat
   :type: color
   :range: [0, 1]
   :default: 0.52 0.76 0.90 1
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Colour for recognised notices about changes to attributes, skills or other
   player statistics. This changes only the displayed text, not the statistics.

.. omw-setting::
   :title: color dialogue narration
   :type: color
   :range: [0, 1]
   :default: 0.76 0.66 0.86 1
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Colour for recognised narration and stage directions in the dialogue window.

.. omw-setting::
   :title: color dialogue journal
   :type: color
   :range: [0, 1]
   :default: 0.65 0.65 0.65 1
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Neutral grey colour for the journal-update notice in the dialogue window.

.. omw-setting::
   :title: color journal narration
   :type: color
   :range: [0, 1]
   :default: 0.32 0.22 0.42 1
   :location: :bdg-success:`In Game > Options > Dialogue colours`

   Darker colour for recognised narration on the journal's paper background.
   It can be adjusted independently of narration in the dialogue window.

.. omw-setting::
   :title: menu transparency
   :type: float32
   :range: 0.0 (transparent) to 1.0 (opaque)
   :default: 0.84

   Controls transparency of GUI windows.
   Adjustable in game via Menu Transparency slider in the Prefs panel of Options.

.. omw-setting::
   :title: tooltip delay
   :type: float32
   :range: ≥ 0.0
   :default: 0.0

   Seconds delay before tooltip appears when hovering over an item in GUI mode.
   Tooltips show context-sensitive info (weight, value, damage, etc.).
   Adjustable in game with Menu Help Delay slider in Prefs panel.

.. omw-setting::
   :title: keyboard navigation
   :type: boolean
   :range: true, false
   :default: true

   Enable or disable keyboard navigation (arrow keys, tab focus, spacebar, Use key).

.. omw-setting::
   :title: stretch menu background
   :type: boolean
   :range: true, false
   :default: false
   :location: :bdg-success:`Launcher > Settings > Interface`

   Stretch or shrink main menu, splash screens, intro movies, and cut scenes
   to fill the video resolution, possibly distorting aspect ratio.
   Bethesda assets are 4:3 ratio; others may differ.
   If false, assets are centered with black bars filling remainder.

.. omw-setting::
   :title: controller menus
   :type: boolean
   :range: true, false
   :default: false
   :location: :bdg-success:`Launcher > Settings > Interface`

   Menus are now fully navigable with a controller using the DPad, triggers, and
   face buttons. Most button mappings follow the Xbox version, with modern
   additions like R3 to toggle item info. All menus can be opened and closed
   with the controller, though some elements—such as editing settings or viewing
   certain tooltips—still use the controller’s mouse cursor.
   A button hint bar appears at the bottom of each menu, with icons that match
   the connected controller type (Xbox, PlayStation, Switch, or Steam). In the
   inventory menu, L2/R2 switch between sub-menus (Map, Inventory, Magic, Stats),
   while L1/R1 adjust filters in inventory and barter screens. R3 toggles
   tooltips, similar to Oblivion Remastered. Mouse input remains fully supported.
   If false, the controller works as a GUI mouse.

.. omw-setting::
   :title: controller trigger press
   :type: int
   :range: 1, 32767
   :default: 30720

   Sets how far the triggers (L2/R2) must be pulled before controller menus
   recognize them as a button press. 
   The default value is 15/16ths depressed.

.. omw-setting::
   :title: controller trigger release
   :type: int
   :range: 0, 32766
   :default: 26624

   Sets how far the triggers (L2/R2) must be released before their latch 
   is reset and it can be pressed again in controller menus. 
   This value must be less than controller trigger press. 
   If it is not, it will be clamped to controller trigger press - 1.
   The default value is 13/16ths depressed.

.. omw-setting::
   :title: controller tooltips
   :type: boolean
   :range: true, false
   :default: false
   :location: :bdg-success:`Launcher > Settings > Interface`

   When true, you do not need to press R3 to show tooltips when using
   controller menus.
   If false, controller menu tooltips are hidden until R3 is pressed.

.. omw-setting::
   :title: subtitles
   :type: boolean
   :range: true, false
   :default: false

   Enable or disable subtitles for NPC dialog and some sound effects.
   Subtitles appear in a tooltip box at screen lower center.
   Toggleable in game with Subtitles button in Prefs panel.

.. omw-setting::
   :title: hit fader
   :type: boolean
   :range: true, false
   :default: true


   Enables or disables the red flash overlay when the character takes damage.

.. omw-setting::
   :title: werewolf overlay
   :type: boolean
   :range: true, false
   :default: true


   Enable or disable the werewolf visual effect in first-person mode.

.. omw-setting::
   :title: color background owned
   :type: color
   :range: [0, 1]
   :default: 0.15 0.0 0.0 1.0


   Background color of tooltip and crosshair when hovering over an NPC-owned item.
   Four floating point values: red, green, blue, alpha (alpha ignored).
   No effect if "show owned" in Game Settings is false.

.. omw-setting::
   :title: color crosshair owned
   :type: color
   :range: [0, 1]
   :default: 1.0 0.15 0.15 1.0


   Crosshair color when hovering over an NPC-owned item.
   Four floating point values: red, green, blue, alpha (alpha ignored).
   No effect if crosshair setting in HUD is false or "show owned" in Game Settings is false.

.. omw-setting::
   :title: color topic enable
   :type: boolean
   :range: true, false
   :default: false
   :location: :bdg-success:`Launcher > Settings > Interface`

   Controls whether dialogue topics in the list are colored by their state.
   See related "color topic specific" and "color topic exhausted".

.. omw-setting::
   :title: color topic specific
   :type: color
   :range: [0, 1]
   :default: 0.45 0.5 0.8 1

   Overrides color of dialogue topics with unique actor responses.
   Four floating point values: red, green, blue, alpha (alpha ignored).
   Unique if Actor filter matches speaking actor and not read yet.

.. omw-setting::
   :title: color topic specific over
   :type: color
   :range: [0, 1]
   :default: 0.6 0.6 0.85 1

   "Over" color for dialogue topics meeting "color topic specific" criteria.
   Four floating point values; alpha ignored.
   Active GUI element via keyboard or mouse events.

.. omw-setting::
   :title: color topic specific pressed
   :type: color
   :range: [0, 1]
   :default: 0.3 0.35 0.75 1

   "Pressed" color for dialogue topics meeting "color topic specific".
   Four floating point values; alpha ignored.
   Active GUI element receiving sustained input.

.. omw-setting::
   :title: color topic exhausted
   :type: color
   :range: [0, 1]
   :default: 0.3 0.3 0.3 1

   Overrides color of dialogue topics exhausted by the player.
   Four floating point values; alpha ignored.
   Exhausted if response has been seen.

.. omw-setting::
   :title: color topic exhausted over
   :type: color
   :range: [0, 1]
   :default: 0.55 0.55 0.55 1

   "Over" color for dialogue topics meeting "color topic exhausted".
   Four floating point values; alpha ignored.
   Active GUI element via keyboard or mouse.

.. omw-setting::
   :title: color topic exhausted pressed
   :type: color
   :range: [0, 1]
   :default: 0.45 0.45 0.45 1

   "Pressed" color for dialogue topics meeting "color topic exhausted".
   Four floating point values; alpha ignored.
   Active GUI element receiving sustained input.
