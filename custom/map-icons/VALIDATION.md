# Native classifier validation

The standalone executable compiles the production classification component. It
checks 76 regression inputs, all 2,690 catalogue names in both place and entrance
contexts, all 19 category definitions, invalid category values, ordinary-door
filtering, and names around and beyond the 256-byte stack-buffer boundary. The
same regression fixtures also run in the normal `components-tests` target.

The same executable tests the production `SpatialIndex<Key>` used by the native
map. With 100,000 indexed markers, a small 5 × 3-cell viewport visits only its
15 occupied XY buckets and 15 keys. The tests cover negative coordinates,
inclusive edges, integer limits, duplicate insertion, missing removal, removal
of the final key in a bucket, empty indexes, and inverted bounds. Removing a key
does not remove neighbours, and empty buckets and columns are pruned.

From the repository root, a Windows developer shell can build and run it with:

```bat
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I . custom/map-icons/test-classification.cpp components/mapmarkers/classification.cpp /Fe:native-map-classifier.exe
python custom/map-icons/verify-classification.py --binary native-map-classifier.exe --receipt native-map-classifier.json --cmake-copy
```

The equivalent Linux build is:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I . custom/map-icons/test-classification.cpp components/mapmarkers/classification.cpp -o native-map-classifier
python3 custom/map-icons/verify-classification.py --binary ./native-map-classifier --receipt native-map-classifier.json --cmake-copy
```

The Python verifier uses only the standard library. It checks deterministic index
generation with both LF and CRLF checkouts, validates every PNG's metadata and
chunk CRCs, compares original glyph hashes, and verifies every CMake resource
entry and category texture path. `--cmake-copy` configures an isolated project
that executes the real OpenMW asset-list and copy macro, then compares the output
bytes. This does not configure or compile the full engine. An actual build's
`resources/vfs` directory can also be checked with `--resources <directory>`.

An optional `--reference <allExtCells.lua>` checks every primary catalogue entry
against the original user-supplied reference. That reference is not redistributed
by this feature. The included TSV and provenance are sufficient to reproduce
the native index without it.

The executable measures 100,000 mixed classification calls by default. The
microbenchmark includes known names, unknown names, false-positive guards,
translated display names, model clues, and exterior/interior contexts. Its heap
allocation counter surrounds only the normal-length calls; setup, file I/O,
result formatting and timer calls are outside that region. Longer mod names
remain supported with a temporary allocation instead of being truncated.

A second microbenchmark performs 100,000 changing small-viewport queries against
the 100,000-marker index, with no query allocations. It measures the actual bucket
lookup and visitor callbacks. It excludes the engine's projection, widget creation,
tooltips, texture work and save data; the synthetic marker count does not represent
the user's installed world. This verifies selection cost without claiming that
rendering 100,000 markers would be cheap.

Timing is for this pure component on the machine running the test. It does not
measure the GUI, texture-driver overhead, map opening, save loading, the complete
discovery path, or game FPS. No frame-rate claim follows from this benchmark.
Full engine compilation and in-game map testing are separate checks.

The catalogue contains 42,468 bytes of string storage (including the compiler's
final terminator) and 21,520 bytes of index entries. The 19 compressed PNG files
total 47,251 bytes. At 64 × 64 RGBA pixels, their base image data totals 311,296
bytes before any graphics-driver or mipmap overhead.
