# Native world and local map icons

This cumulative OpenMW build displays location-type icons on the built-in world map and door symbols on the built-in local map and minimap.
It does not require Advanced World Map, a Lua icon mod, an ESP, or a regenerated Delta plugin.
The existing Continue, native deleveler, Lua UI repair and earlier cumulative engine changes remain included.

## Installation

1. Close OpenMW and keep your previous engine folder for rollback.
2. Extract the complete Windows build into a new engine folder. It contains the executable, defaults and resources together. Do not copy only openmw.exe.
3. If you use Rubic0n / OpenMW Lua Go Vroom, copy your existing lua51.dll AND matching resources/lua_libs/content.lua into the new folder together. Preserve your existing optional frame-generation version.dll and openmw_fg.ini together if you still use them.
4. Select the new executable in your normal launcher setup. Keep your current openmw.cfg, mod data directories and other settings.
5. Disable the Advanced World Map main .omwscripts entry, its Tracking and Gridmap extension entries, and PXM World Map Icons.omwscripts if enabled. Those four map components are replaced or no longer used for this feature. Other mods keep their own configuration; this change does not replace Quest Guider.
6. Start OpenMW, open Options and select Map icons (use the tab arrows if necessary).
7. Open the world map. Use Options > Map icons to choose the appearance.

## Options

- Enable location icons: master switch. Off restores native named-location markers and hides the added entrance markers.
- Show discovered entrance icons: independently show caves, tombs, mines, ruins and other meaningful destinations. Ordinary settlement house doors are excluded to avoid a crowded map.
- Show location type in tooltips.
- World base size: 16–40, default 24. Local door base size: 8–40, default 15. Maximum after zoom: 8–64, default 24. All sizes use logical UI pixels.
- Icon colours apply to both maps: original artwork colours or different colours by type.
- Location type / Use an icon for this type: configure each of the 19 categories. Off uses a square marker for that type.
- Reset icon options: resets only these icon settings.

Options apply immediately and are saved when you close Options. The native tab remains available with replacement settings layouts.

## Discovery and saved games

Existing native named-location markers receive their category icons. Entrance discovery examines only the already-loaded exterior cell the player enters, using actual door positions and destination metadata. It never loads the destination interior just to classify an icon. Multiple entrances to the same interior are represented by one discovered marker.

The feature adds a separate optional LMRK save record for discovered entrance metadata. Native GMAP exploration and marker records keep their existing format. Old saves are supported: revisit exterior cells to discover their entrances. Advanced World Map's private discovery database is not imported. No new game or Delta regeneration is required for the engine feature itself.

## Resource use

There is no map-icon per-frame scan, whole-world discovery pass or Lua service. New entrance widgets exist only while the world map is visible and are restricted to its current view. Closing the map releases them. The small classification table is immutable and is consulted during discovery. The 19 public fallback 64x64 icon PNGs total about 47 KB on disk (304 KiB of base RGBA pixels before graphics-driver overhead).

Benchmarks measure the classifier separately; they do not establish a target-PC FPS improvement. The full native test report and build identity are included with the Windows package.

## Quick tests

- Load an existing save with Advanced World Map disabled. Verify that the normal OpenMW world map appears.
- Visit Balmora or Seyda Neen and check the native settlement icon and tooltip.
- Re-enter an exterior cell containing a cave or ancestral tomb, then inspect its entrance icon on the world map. The exact doors in your installed plugins determine positions.
- Change icon size, colours and a category. Close and reopen Options and restart once to confirm settings persist.
- Save and reload after discovering an entrance. Confirm its marker remains.
- Switch between local/world maps, pan and zoom if native map zoom is enabled, and close the map. Existing player arrow, local map, notes and normal dragging should still work.
- Turn the master switch off to return to the previous native marker presentation.

## Rollback

Exit OpenMW and start your previous engine folder. The previous engine skips the optional unknown LMRK record, so entrance discoveries from this feature are not retained if that engine saves again. Keep a save made before the upgrade if you want an exact rollback point. Re-enable the previous AWM entries only if you want to return to AWM's interface.

## Limits

This is an icon feature, not an Advanced World Map replacement suite. It does not add AWM tracking, routes, search, grids, map notes or custom-map machinery. Unknown named places can retain a generic marker. Unrecognized entrances are omitted to avoid mislabelling ordinary house doors. Classification is not guaranteed to match every mod author's intended category. The user's installed fonts, full modlist and real gameplay performance require an in-game check.

## World and local marker skins

The native Options > Map icons tab has a shared original-colour/type-colour selector, separate world and local base sizes, and a maximum size after map zoom and world marker grouping. The maximum defaults to 24 logical UI pixels and can be set to 8–64. Local door icons also appear on the HUD minimap. Existing discovery, fog, notes and door tooltips use the same native data as before. The local door switch restores the 8-pixel door marker. Older colour setting values 0 and 1 select original colours, and 2 selects the type palette.

A skin is a VFS data directory. `mapicons/markers.tsv` declares `world<TAB>id` and `local<TAB>id` entries. Use the existing world category IDs, or a local ID containing lowercase ASCII letters, digits, underscores or hyphens. Each entry uses a 32×32 image at `textures/mapmarkers/vanilla/{world|local}/id.dds`. Its optional 32×32 type-colour variant goes at `textures/mapmarkers/type/{world|local}/id.png`. The renderer uses white at rest so the original pixel colours survive a custom menu theme. A missing type variant uses the original image. Missing world artwork uses the previous native fallback; missing local artwork uses the generic door.

After the declarations, `name<TAB>id<TAB>substring` identifies destination names and `door<TAB>id<TAB>substring` identifies fallback door record IDs. Name rules apply from exterior and quasi-exterior cells. Later matching rules take priority and spaces inside the literal pattern are significant. `active_door` is the generic local icon. The renderer classifies a door when binding its widget and reapplies the selected texture and size when reusing widgets or changing options. It does not scan destination cell contents.

The portable build prepared for the user contains the Morrowind-style artwork from their supplied packs. The public source tree provides the skin interface and the existing independently drawn fallback assets. The matching private resource pack can be kept as a separate data directory when upgrading to another compatible build. File credits and the scope of the imported visual assets accompany that pack.
