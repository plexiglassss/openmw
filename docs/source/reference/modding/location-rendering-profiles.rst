Location rendering profiles
###########################

Location rendering profiles temporarily override four object-lighting options in
matching interiors. All profiles come from one shared file,
``location-rendering.yaml``, next to the active user ``openmw.cfg``. With chained
``config=`` directories, this is the user configuration directory selected by
OpenMW, not necessarily the directory of the first configuration file.

This shared file is the only source. OpenMW does not read profiles from mod data
directories, ``shader_profiles/``, or older profile formats. No mod names or cell
lists are built into the engine. An absent shared file leaves the ordinary global
settings in effect.

Lumenaire can generate this file together with its lighting plugin. Its automatic
rule matches qualifying negative lights in any interior, including a newly added
mod, without an inventory of mod names or a generated cell allowlist. Regenerate
the output after changing the load order and restart OpenMW. If a generator owns
the file, make persistent custom rules through that generator rather than editing
an output that will be replaced on the next generation.

The master switch is ``location rendering profiles`` in the ``[Shaders]``
settings category and in the in-game Lights options. Disabling it restores the
saved global shader settings everywhere.

Format 3
********

The root contains a format version and a list of complete profiles. All four
shader values are required in each profile::

    format: 3
    profiles:
      - id: automatic-negative-light-interiors
        name: Negative-light compatibility
        enabled: true
        priority: 10
        match:
          type: interior
          condition: contains-negative-light
        shaders:
          clamp-lighting: true
          force-per-pixel-lighting: false
          auto-use-object-normal-maps: false
          auto-use-object-specular-maps: false

      - id: example-custom-caves
        name: Example custom cave rendering
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

Use ``profiles: []`` for an explicitly empty configuration. The root accepts only
``format`` and ``profiles``; there is no per-profile ``format`` field. YAML
comments may record generator ownership or explanations without affecting the
rules.

``id``
    A unique identifier containing letters, digits, ``.``, ``-`` or ``_``. IDs are
    case-insensitive for uniqueness. Every occurrence of a duplicated valid ID is
    skipped so file order cannot decide which settings it supplies.

``name``
    A required human-readable name, used in log messages.

``enabled``
    Optional, defaults to ``true``. This is the per-profile configuration switch.

``priority``
    Optional integer, defaults to ``0``. A higher-priority profile wins when two
    profiles match the same interior. One whole profile supplies all four values;
    values are not merged. Exact-cell rules do not automatically outrank condition
    rules, so assign a higher priority to an intentional exception.

    If the highest matching priority contains contradictory shader settings,
    that match uses ordinary global settings and OpenMW logs a diagnostic.
    Non-overlapping matches from those profiles still work. Identical settings at
    the same priority are harmless; the lexically first ID is used for reporting.
    Both cases are independent of document order.

``required-content``
    Optional list of content-file basenames. The profile remains inactive unless
    every listed file is enabled. Matching is case-insensitive. Paths are not
    accepted. The generic automatic rule needs no required-content list.

``match``
    ``type`` must be ``interior``. Supply exactly one of ``cells`` or ``condition``.

    ``cells`` is a nonempty literal list of interior names. Matching is ASCII
    case-insensitive. Prefixes, wildcards and regular expressions are not
    interpreted.

    ``condition`` currently accepts only ``contains-negative-light``. Its runtime
    behavior is described below. No exterior condition or multi-cell exterior
    rendering policy is implemented.

``shaders``
    The four temporary values to use while the profile is active. Explicit maps
    authored into meshes are preserved when automatic map discovery is disabled.

Negative-light matching
***********************

``contains-negative-light`` matches the current interior when a world animation
in that cell has at least one attached native light source with the Negative flag
and a nonzero authored RGB component. Merely having a negative-light base record
in the loaded content files is insufficient. An all-black RGB value does not
qualify, regardless of alpha. Radius zero does qualify: native rendering uses a
minimum light radius of 16.

This follows actual scene attachments:

* Placed enabled negative lights qualify. A placed ``OffDefault`` light has no
  native source and does not qualify.
* An unequipped carryable inventory light does not qualify. An equipped light
  qualifies when its source is attached, including an ``OffDefault`` record if
  the equipment renderer actually lights it.
* Non-carryable actor inventory lights that OpenMW renders as hidden attached
  lights qualify. They really contribute to the world scene.
* Inventory, character and other preview animations do not register with the
  world tracker.

Membership is updated when sources are attached or removed and when their owner
moves between cells. Creating, deleting, enabling, disabling or equipping a light
therefore updates the condition through the normal scene lifecycle. Loading a
save rebuilds membership from the restored scene; no match is stored in a save.
There is no per-frame scan of cell references, inventories or the scene graph.

The condition depends on authored RGB and attachment, not camera visibility,
distance culling, actor fade or instantaneous pulse/flicker intensity. A pulsing
light cannot repeatedly switch the whole interior between rendering profiles.
Removing one source leaves the condition active while another qualifying source
remains in the cell.

This is a rendering compatibility rule, not a claim that every interior with a
negative light requires identical art direction. A higher-priority exact-cell
profile can express an intentional exception. The engine does not reconstruct
authored positive-light placements, ambient colours or cell settings; the
generated content plugin determines which native light records are available.

Loading and restoration
************************

Profiles are read at startup; editing the shared file requires restarting
OpenMW. A malformed individual profile is logged and skipped while valid profiles
continue to load. An unreadable file, malformed document, unsupported root format
or ambiguous document structure uses global settings for the whole file. OpenMW
does not fall back to old per-mod files.

Entering an unmatched interior or any exterior, disabling the master switch,
removing the last qualifying source, clearing the world, or loading a save
elsewhere restores ordinary rendering. The profile values are temporary scene
uniforms and never rewrite saved global settings.

Resource preparation at startup uses the union of global and enabled profile map
requirements. Switching locations selects the already prepared map behavior;
it does not rebuild object resources on every transition.
