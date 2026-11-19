Location rendering profiles
###########################

Location rendering profiles are ordinary data files that temporarily override a
small set of object-lighting options in exact interior cells. The engine contains
no mod-specific cell list. Put YAML or JSON files under ``shader_profiles/`` in
an active OpenMW data directory.

The master switch is ``location rendering profiles`` in the ``[Shaders]``
settings category and in the in-game Lights options. Disabling it restores the
saved global shader settings everywhere.

Format 2
********

All four shader values are required so a profile is self-contained::

    format: 2
    id: example-classic-caves
    name: Example classic cave rendering
    enabled: true
    priority: 100
    required-content:
      - "Example.esp"
    match:
      type: interior
      cells:
        - "Example, Old Cave"
        - "Example, Lower Depths"
    shaders:
      clamp-lighting: true
      force-per-pixel-lighting: false
      auto-use-object-normal-maps: false
      auto-use-object-specular-maps: false

``id``
    A unique identifier containing letters, digits, ``.``, ``-`` or ``_``.

``enabled``
    Optional, defaults to ``true``. This is the per-profile configuration switch.

``priority``
    Optional integer, defaults to ``0``. A higher-priority profile wins when two
    profiles name the same cell. Equal-priority overlaps are rejected instead of
    depending on data-directory order.

``required-content``
    Optional list of content-file basenames. The profile remains inactive unless
    every listed file is enabled. Paths are not accepted.

``match``
    ``type`` must currently be ``interior``. ``cells`` is a nonempty literal
    allowlist. Matching is ASCII case-insensitive. Prefixes, wildcards, regular
    expressions and exterior matching are intentionally not supported.

``shaders``
    The four temporary values to use while the profile is active. Explicit maps
    authored into meshes are preserved when automatic map discovery is disabled.

Profiles are read at startup. Editing a profile requires restarting OpenMW. A
malformed or inactive profile is logged and does not prevent other profiles from
loading. Entering an unmatched cell, disabling the master switch, clearing the
world or loading a save elsewhere restores the ordinary saved settings.
