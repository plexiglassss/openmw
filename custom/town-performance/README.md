# Town CPU candidate with Lua UI template repair

This engine continues the town-performance work from the combined Continue-menu
and native-deleveling revision `ce3d5a2e981999df432cf8df416352cc85842e97`.
The sustained gameplay improvement remains to be measured in the user's game.
Native correctness tests and helper benchmarks do not establish a town FPS gain.

An initial template-attachment candidate was discarded after its native benchmark
showed no useful gain. The replacement targets temporary registry references while
reading ordinary stored UI properties. In the earlier, pre-repair experiment, ten
alternating local native process pairs measured median full-HUD updates of about
0.896 ms candidate and 1.138 ms legacy, 21.2% lower in that authored workload.
The then-existing 11 native cases passed in both modes and their six layout
snapshot sets matched, but they did not exercise an actual Image widget inheriting
a texture. Those historical results do not establish correctness of image
inheritance or a new measured gain for this corrected candidate. That harness used
Linux GCC 13.3.0, MyGUI 3.4.3 and Lua 5.4.6 with a minimal settings service. Windows
Release/LTO validation and matched gameplay measurement remain separate steps.

## Missing Lua UI images: engine repair

The optimized stored-property reader treated a missing or nil Lua-object property
as an explicit value. For an Image widget, that empty local `resource` could
override the valid texture inherited from its template. ZHI's editor uses
inherited MWUI templates for its window background, group borders and empty assignment boxes;
its directly assigned icons could remain visible while those decorations vanished.

When the optimized raw lookup finds nil, it now uses the same owning Lua-object
conversion as the unchanged legacy reader. An image with no local resource
inherits its template resource, an explicit resource still overrides it, and
removing the override returns to inheritance. Types that deliberately accept a
nil sentinel retain their legacy meaning. Non-nil stored values keep the fast
read path. Template validation, metatable behavior and stack/lifetime rules retain
their existing checks. The other town-performance candidates remain in place.

This is an engine change. Keep the installed ZHI F1 and miscellaneous-item fixes;
no ZHI script, hotkey assignment, mod setting or load-order change is needed.
The native regression exercises actual LuaImage code and inherited image state.
It uses headless platform services and does not establish rendered appearance in
the user's installation. Open F1 after installing the verified complete package
and check the outer background, hotbar-group borders and empty-slot borders, then
open and close an item/spell picker and reopen the editor.

## Why this change exists

The supplied `Run-20261108-160715-006-baseline.zip` confirms that both previous Lua
fixes were installed. In the selected steady Seyda Neen section, AttendMe's
synchronized UI work fell from about 1.79 to 0.19 ms per frame, but overall engine
FPS remained about 24. The Lua worker averaged about 20.85 ms per frame across
roughly 4,921 callbacks. Proximity continued to account for about 2.63 ms per frame
of native UI updates. These costs overlap, so they must not be added together as
independent parts of a frame.

Both captures used Detailed recording, and their routes/cameras were not recorded
well enough for a controlled FPS comparison. The source analysis supports further
bounded native work; it does not justify predicting a particular speedup.

## What changes

The Lua UI property reader uses scoped Lua-stack values for ordinary stored
property tables, avoiding temporary registry references. Both owners stay rooted
on the Lua stack throughout the read, preserving their lifetime and identity. Tables with any metatable
and other source types retain the original lookup path. Every read still sees the
current value; no property is cached between updates. Template values are still
validated even when a widget property overrides them.

All Flex and Container measurement/coordinate passes and all child attachments
remain intact. Normal children are still detached before template replacement to
preserve retained elements. Mutable layout tables, callbacks, text and properties
retain their existing behavior. The original type conversion/error logic is shared
by both reader paths.

Loaded script containers avoid entering Lua when neither timer queue has a due
head. They still mark activity for the tracker, and the existing initialization,
load, save, due-callback and ordering paths remain intact. Deadline equality and
nonfinite clock comparisons use the same predicate as before. Local native
Release/LTO LuaJIT tests passed all 31 cases on baseline and candidate with the
Lua profiler both on and off. Ten alternating pairs saved only about 0.022 ms per
160 containers with no due work. This is a small cleanup, not an explanation for
the roughly 21 ms recorded worker budget; actual town queue coverage is unknown.

Magic queries resolve their actor once within each call. They continue to use
the current world registry and current effect values; no actor pointer, effect
value or stats collection is cached across calls. Effect-ID validation remains
before potentially lazy stats initialization, preserving the error order. Local,
global and self-object contexts keep their existing checks.

The magic-query change is a small candidate. SafePtr already caches registry
resolution, and Release/LTO can remove some repeated work. The included benchmark
records actual optimized native-helper timings without assuming a benefit.

## Preserved engine and mod behavior

The baseline contains the cumulative loading/lookup, dialogue and journal,
shrine-tooltip, notification-placement, inactive-script-cache, rest/wait time,
Continue colour/hover, native deleveling and installer corrections. The source
gate binds every changed file and checks the rest of the complete tree against
that baseline. Historical gates run at their own immutable commits. This repair
also retains the installed town revision `41b969ecf6` and canonical combined
engine `1e2191f221079d89e46c15331ba6911b44f28b53` in its accepted ancestry.

The native deleveler controls and their existing defaults are retained. No mod
plugin, load order, saved data, callback frequency or gameplay rule is changed by
this performance patch. Keep the installed AttendMe and SkillPerks fixes.

## What the build must verify

The Windows build uses Release optimization and LTO. It runs the complete native
component and engine suites, four lookup configurations, the existing installer
and settings fixtures, and the retained GL/frame-generation/tool jobs. The package requires the seven
new timer lifecycle/benchmark cases and zero unused Lua entries in both native
no-due workloads.

`LuaUiLayoutTest` executes actual MyGUI and OpenMW Element, WidgetExtension, Flex,
Container, Text and Image code using headless platform file/render/font services.
Its existing cases cover native parent/order/style repair, mutable template slots,
retained elements moving between roots, a single nested resize, text/wrapping/
visibility/reordering, and repeated HUD updates. The seventh case adds an Image
regression: inherited texture resources must survive creation and updates, with
explicit override and return-to-inheritance behavior. The suite runs in two fresh
processes. Every recorded widget-tree geometry/order/text/visibility/image snapshot
must match between the candidate and legacy property-read paths. Five existing
`LuaUiPropertyTest` cases exercise defaults, mutable types, template
validation, metatables, error propagation, stack restoration, owning value
lifetimes and template-owner replacement during a getter. Two new cases require
texture inheritance/default/error behavior and legacy nil-sentinel semantics.
All seven layout and seven property cases must execute in each mode.

`MWLuaMagicBindingsTest` uses actual ObjectVariant, SafePtr, WorldModel registry
and MagicEffects code. Actor stats initialization and effect-key service boundaries
are controlled fixtures. Tests cover contexts, missing/current effects, mutation,
actor removal/replacement and exception order. Its paired native-helper benchmark
has no Lua dispatch, game simulation or recording overhead and has no timing
pass/fail threshold.

The package gate revalidates both source-manifest chains and native XML evidence,
requires the Image/inherited-resource regression in both delivered mode reports,
recomputes layout parity, verifies exact compiled defaults/resources, and requires
the complete bootstrap and runtime. Startup is checked with the actual packaged
executable and deliberately empty game data. The release report records which
checks passed for the delivered commit.

## Install and compare

Close OpenMW and keep a backup copy of the complete current engine folder. Extract
the complete verified archive into a separate engine folder or replace the supplied
files together. It includes `openmw.cfg`, `lua51.dll`,
`resources/lua_libs/content.lua`, defaults, resources and executable dependencies.
The bootstrap beside the executable is separate from the user's modlist config.

If retaining a replacement Rubic0n runtime, preserve its matching `lua51.dll` and
`resources/lua_libs/content.lua` together. Native CI tests use the bundled standard
runtime; a custom replacement runtime is not certified by those results. Keep
optional frame-generation files together and use the same FG state for both runs.

Inside the extracted engine folder, open `tools/town-performance`:

1. Run `Compare-Legacy-Layout.cmd`. Load the same save, let loading settle, stand
   in the troublesome town with a fixed camera for one minute, then walk a short
   repeatable route. Quit normally.
2. Run `Compare-Optimized.cmd` and repeat the same save, view and route.
3. For a second pair, reverse the order. Keep settings, runtime, mods and FG state
   identical. Return the capture ZIPs produced by the collector.

Both wrappers use FrameOnly recording and the same executable. Their process
environment selects the property-reader path and is restored afterward. They do not
edit settings or installed files. The recorded inherited environment includes
the selected layout flag. LegacyLayout isolates only the UI property-read change;
both runs still contain the magic-query and no-due timer candidates and all
combined features.

For a custom executable location, use PowerShell:

```powershell
./Start-Comparison.ps1 -Variant LegacyLayout -OpenMWExe 'D:\OpenMW\openmw.exe'
./Start-Comparison.ps1 -Variant Optimized -OpenMWExe 'D:\OpenMW\openmw.exe'
```

`OPENMW_LUA_UI_LEGACY_LAYOUT=1` selects legacy UI property reads in a new game
process. The flag is read once at its first layout use. Other values select the
candidate. Optimized reads now match the unchanged legacy nil/template semantics;
the flag selects the property-read implementation for diagnosis and comparison.
It is not a transparency setting. To roll back the entire engine change, restore
the complete backed-up engine folder; the two CMD variants only switch the layout
diagnostic.

Do not compare one Detailed capture with one FrameOnly capture as evidence of an
engine speedup. FrameOnly still has observer/OSG recording overhead and does not
measure input-to-photon latency. Native benchmark results are kept separate from
the matched gameplay comparison.
