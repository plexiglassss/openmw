# Automatic negative-light compatibility

This RC1 feature consumes a single `location-rendering.yaml` beside the active
user `openmw.cfg`. Lumenaire generates it when `negative_lights = "auto"` is
enabled. The engine does not discover per-mod `shader_profiles` files, and the
distribution no longer contains the former Horror Mod profile.

## Behavior

Lumenaire preserves effective incoming negative-light definitions unchanged and
continues its configured processing of positive lights. It does not resurrect
deleted lights or a negative flag intentionally replaced by a later input.
There is still one ordinary `Lumenaire.omwaddon`; no per-mod protection addons
are generated or required by this feature.

The default generated rule selects interiors containing an actual attached
negative light with nonzero authored RGB. Radius zero remains eligible because
the renderer applies a minimum radius. A placed `OffDefault` light that does not
emit does not qualify; an equipped or hidden actor light that actually emits
does. Merely having a light record in the load order or a non-emitting inventory
does not qualify. Camera visibility, flickering, pulsing and actor fade do not
toggle the policy.

The rule enables lighting clamping and disables forced per-pixel lighting,
automatic object normal maps and automatic object specular maps. These are
rendering compatibility defaults. Preserving negative definitions alone does
not promise every positive light or the complete original scene balance.

Membership follows scene insertion/removal, cell movement, attached equipment,
hidden actor lights and world clearing/recreation. Only a zero/nonzero transition
for the current cell triggers a profile refresh. This adds no per-frame scan of
all cells or plugins. Exteriors and unmatched interiors retain global rendering
settings. The existing Lights master switch can disable profile application.

## Configuration and migration

1. Install the matching custom RC1 engine and Lumenaire build together.
2. In the existing `lumenaire.toml`, set `negative_lights = "auto"`. Existing TOML
   values are deliberately retained by upgrades; a former `disable` value does
   not silently change.
3. Remove obsolete generated protection addons from the active load order before
   generation. Their overrides would otherwise remain effective input. Retain
   the actual source mods and intentional compatibility patches.
4. Run Lumenaire normally. It emits the usual native plugin in its output
   directory and the shared YAML in the active OpenMW configuration directory.
   With nested `config=` layers this can differ from the initial selected file.
5. Keep the normal generated plugin last in the load order and restart OpenMW.
   Keep optional explicit profile rules in `lumenaire.toml`; regenerate after
   changes. Do not edit Lumenaire's generated YAML directly.

The former `shader_profiles/horror_mod.yaml` may be removed from any old engine
or mod installation. The new engine never reads it, even if an old copy remains.
No per-mod YAML should be retained as a configuration source.

Existing manually written shared YAML is not silently overwritten. Move its
desired explicit rules into `lumenaire.toml`, retain a backup, and then allow
Lumenaire to generate the shared file. The generator verifies its ownership
marker and body hash before replacing a previous generated file.

Disabling auto regenerates an owned shared file with only the remaining explicit
rules, or an empty profile list. Generation failures retain the previous output
set under ordinary I/O failure handling. Cross-filesystem publication is staged
and rolls back on reported errors; it is not a power-loss-atomic transaction.

## Verification

`fixtures/location-rendering.yaml` is unmodified output from the actual modified
Lumenaire executable, generated from synthetic TES3 plugins on 2026-11-26. It is
a test fixture and is not installed as a live rule. Its SHA-256 is
`f9b50eeae918f81528b3bca31fd17fccb42ccba9c3cf675ef30375b0139f71f5`.

`verify-generated-profile.cpp` loads that file through the production reader and
checks the generic predicate, complete shader policy, unmatched fallback and
exterior boundary. The Windows pipeline runs parser, precedence, uniform and
actual scene-object lifecycle tests; the packaging gate requires those results
and verifies the absence of legacy bundled profiles. These checks are separate
from observations in a user's live save and mod installation.

See `docs/source/reference/modding/location-rendering-profiles.rst` for the
shared format-3 schema and priority/conflict semantics.
