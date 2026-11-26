# Saved AI target remapping

This change applies the save reader's existing content-file mapping to modern
cached AI actor references when loading Follow, Escort, Combat, and Pursue
packages. A content plugin moving within the load order must not change which
placed actor a saved package targets.

## Scope and invariants

- The change is confined to the shared `loadActorId` helper in
  `components/esm3/aisequence.cpp`.
- Formats newer than `MaxActorIdSaveGameFormatVersion` use the existing
  `ESMReader::applyContentFileMapping` operation exactly once after decoding.
- A content provider absent from the mapping clears the cached reference;
  its old numeric slot must not select a replacement provider's object.
- Runtime-generated references with content index -1, unset references, and
  readers with no mapping retain the existing mapping API's semantics.
- A zero local reference index is not itself grounds to discard a content
  reference. `FormId::isZeroOrUnset` has ESM4-specific semantics and is not used.
- Legacy 32-bit actor IDs keep their existing loading/conversion path.
- No save-format change, filename special case, gameplay setting, global reader
  change, or runtime guess about the intended target is introduced.

## Regression coverage

`apps/components_tests/esm3/testaitargetmapping.cpp` exercises the actual
`ESMReader`, `ESMWriter`, and each of the four saved package implementations.
The same 13 typed cases run for Follow, Escort, Combat, and Pursue (52 tests).
One additional mixed-sequence test preserves the package queue, last-package
state, and unrelated Travel/Activate packages through reordering and reload.
Modern cases cover the first supported modern-target format and the current
save format. Historical fixtures explicitly encode legacy 32-bit actor IDs.

The cases cover insertion, a non-monotonic permutation applied exactly once,
unchanged order, no installed mapping, removed/reoccupied providers, an empty
mapping, generated references, unset references, zero local reference indices,
omitted target fields, repeated serialization/reload and another reorder,
legacy IDs, and omitted legacy IDs. Surrounding package fields are compared
using native serialization and a following sentinel subrecord is checked.

Run the focused suite with:

```
components-tests --gtest_filter='Esm3AiTargetMappingTest/*.*:Esm3AiSequenceTargetMappingTest.*'
```

The existing cumulative Windows workflow runs these registered tests together
with all component and engine tests and the retained source/package gates.
Source provenance, native test results, and live gameplay acceptance are
separate evidence; passing this suite does not establish a full modlist audit.

## Existing saves

This corrects the load-time translation of references that still correspond to
the content list stored with the save. If an earlier faulty load was resaved,
the current header may already describe a different content list while a nested
AI target still contains an old index. The original owner cannot generally be
reconstructed from that header alone. Repairing such a target requires separate
evidence; the engine deliberately does not apply guessed offsets or replace
arbitrary targets based on a base-record name.
