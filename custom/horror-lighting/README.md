# Horror interior lighting

This engine feature applies the accepted classic lighting setup only inside the
63 dedicated interiors in the included Horror profile. Ordinary rendering resumes
when the player leaves. Shared Suran interiors and exterior cells are excluded.

The profile is included in the complete Windows build at
`resources/vfs/shader_profiles/horror_mod.yaml`. OpenMW reads this folder
automatically. The profile requires `Horror Mod.esp` to be active and uses exact
interior names, including the five renamed interiors in the accepted Horror v1.8
main plugin. No additional plugin or data-directory entry is needed.

## Install

1. Close OpenMW and its launcher. Back up the active engine folder.
2. Install the **complete Windows engine package**, including its `resources`
   folder and `defaults.bin`. The executable and changed shaders must come from
   the same build.
3. Retain the installation's custom `lua51.dll` and its matching
   `resources/lua_libs/content.lua` together, if using the custom Lua runtime.
   Keep the existing OpenMW configuration and optional Frame Generation files.
   This follows the cumulative build's established upgrade instructions.
4. In the existing `[Shaders]` section of
   `C:\Users\pexCo\Documents\My Games\OpenMW\settings.cfg`, restore the four
   ordinary values captured before the Horror comparison and enable profiles:

   ```ini
   [Shaders]
   clamp lighting = false
   force per pixel lighting = true
   auto use object normal maps = true
   auto use object specular maps = true
   interior lighting profiles = true
   ```

   Edit the existing entries; do not duplicate the section. These four ordinary
   values come from the user's 9 November 07:38 configuration. If ordinary
   preferences have since changed, keep those preferred ordinary values instead.
   The profile is temporary scene state and never rewrites them.
5. Restart the game. Keep the accepted Horror main and S3 corrections installed.
   The renderer upgrade itself does not require S3 or Delta regeneration.

## What happens inside and outside

| Setting | Included Horror interior | Elsewhere |
| --- | --- | --- |
| Clamp lighting | true | Saved global preference |
| Force per-pixel lighting | false | Saved global preference |
| Automatically discovered object normal maps | disabled | Saved global preference |
| Automatically discovered object specular maps | disabled | Saved global preference |

Maps explicitly assigned by a mesh retain the behavior of the original four
settings. Post-processing remains enabled as in the accepted comparison; this
feature does not change the HDR/bloom/SSR chain or terrain-map preferences.

If an unusually complex material exhausts the available texture slots while
retaining an explicit fallback map, that material keeps ordinary rendering and
logs a warning. This exception is local to that material.

The world profile covers scenery, actors, visiting companions and the player's
first- and third-person geometry. Reflection rendering shares the world profile.
Inventory/race previews and local-map rendering retain ordinary settings.

The profile is resolved during native cell transitions, including doors, console
teleports and direct save loading. Returning to an ordinary cell or clearing the
world resets the temporary mode. It does not poll the modlist each frame, reload
all shaders on entry, replace shared meshes, or reset actor animation.

## Verify the result

Start with the same view in `Horror, Old Cave` that was accepted in the 11:34
comparison. From the console, the direct test command is:

```text
coc "Horror, Old Cave"
```

The dark passages and localized illumination should match the accepted setup.
Check the door and rock surfaces for the earlier white speckles, then switch
first/third person and bring a companion through the boundary.

Test an ordinary location, for example:

```text
coc "Suran, Suran Tradehouse"
```

That shared venue must use the saved ordinary settings. Return to the Horror cave,
save there, exit the game and load that save directly. Also test loading a save
outside Horror after visiting it. The saved global settings should stay unchanged.

The OpenMW log reports the loaded profile and transitions. Expected messages
include:

```text
Interior lighting profile: loaded 'Horror Mod'
Interior lighting profile: 'Horror Mod' active in horror, old cave
Interior lighting profile: ordinary rendering restored
```

Cell-name capitalization in the final log can differ. A missing required content
file or malformed profile is logged and that profile is not applied.

## Disable or roll back

Set `interior lighting profiles = false` under `[Shaders]` and restart to use the
saved ordinary settings everywhere. To return to the earlier globally accepted
Horror comparison, set the four values to `true / false / false / false` in the
order shown above. To remove this engine revision entirely, restore the complete
previous engine folder, including its matching shader resources.

## Scope and verification

The profile's exact cell list was compared with accepted Horror v1.8, all three
complete official masters, and the retained provider captures. It excludes Suran
Tradehouse, all exterior cells and the unprefixed legacy cells. The retained
captures include partial files; this work does not close the wider Horror audit.

The engine tests cover the parser, active-content requirements, exact names,
exterior exclusion, overlapping-rule rejection and scene-uniform reset. Shader
tests compare the new ordinary mode with the preserved combined build, the active
mode with the original four-setting configuration, and restoration after exit.
Consult the build's executed test reports for results. Synthetic rendering and
build tests do not establish the final appearance or performance in the user's
installed game; the short gameplay checks above remain the final visual check.

S3's separate `lightconfig.toml` retains the native light and interior record
corrections when S3 is regenerated. It does not control this renderer profile.
