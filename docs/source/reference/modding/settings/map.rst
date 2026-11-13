Map Settings
############

.. omw-setting::
   :title: global
   :type: boolean
   :range: true, false
   :default: false

   If true, the map window displays the world map; otherwise, it displays the local map.
   Updates automatically when switching map views in-game.

.. omw-setting::
   :title: global map cell size
   :type: int
   :range: 1 to 50
   :default: 18

   Sets the width in pixels of each cell on the world map.
   Larger values show more detail, smaller values less.
   Recommended values: 12 to 36.
   Changing this affects saved games due to scaling of explored area overlays.

.. omw-setting::
   :title: local map hud fog of war
   :type: boolean
   :range: true, false
   :default: false

   Enables fog of war rendering on the HUD map.
   Default off since map size limits fog impact.

.. omw-setting::
   :title: local map resolution
   :type: int
   :range: ≥ 1
   :default: 256

   Controls resolution of the GUI local map window.
   Larger values increase detail but may cause load time and VRAM issues.

.. omw-setting::
   :title: local map widget size
   :type: int
   :range: ≥ 1
   :default: 512

   Sets the canvas size of the GUI local map window.
   Larger sizes increase on-screen map size and panning.

.. omw-setting::
   :title: allow zooming
   :type: boolean
   :range: true, false
   :default: false
   :location: :bdg-success:`Launcher > Settings > Interface`

   Enables zooming on local and global maps via mouse wheel.

.. omw-setting::
   :title: max local viewing distance
   :type: int
   :range: > 0
   :default: 10

   Controls viewing distance on the local map when 'distant terrain' is enabled.
   Increasing this may increase cell load times.


Location icons
--------------

The native world map can display categorized location icons. These settings do not
change the local map or require Advanced World Map. They can also be changed under
Options > Map icons.

``location icons`` (boolean, default true)
    Enable categorized native markers. Disabling restores ordinary named markers
    and hides additional discovered entrance markers.

``location entrance icons`` (boolean, default true)
    Discover and show meaningful interior entrances in exterior cells entered by
    the player. Ordinary settlement doors are excluded.

``location icon tooltips`` (boolean, default true)
    Include the location type in marker tooltips.

``location icon size`` (integer, 16 to 40, default 24)
    Icon size in logical UI pixels. Interface scaling applies.

``location icon colours`` (integer, default 0)
    0 selects the original artwork colours on both maps. 2 selects colours by marker type.
    Earlier value 1 selects the original artwork colours.

``location local door icons`` (boolean, default true)
    Use the selected marker skin on teleport doors in the local map and HUD minimap.

``location local icon size`` (integer, 8 to 40, default 15)
    The local door size at normal zoom, in logical interface pixels.

``location icon maximum size`` (integer, 8 to 64, default 24)
    Maximum side length after zoom and world marker grouping, in logical interface pixels.
    World and local map positions continue to follow the zoom. The interface scale still applies.

``location icon categories`` (integer bit mask, default 524287)
    Categories with their bit clear use square markers. Bits 0 through 18 are:
    city, town, settlement, house, cave, mine, tomb, barrow, Dwemer, Daedric,
    stronghold, fort, tower, ruin, camp, ship, shipwreck, landmark and miscellaneous.
    Use the in-game category controls for ordinary configuration.
