# Startup, scene loading and terrain rays — combined test build

Based on 37e1b7098f. All preceding engine changes, diagnostic recording, and the 60-second continuous-hang warning delay remain included.

## Changes

* Loose-file archive construction uses up to four workers. Each directory keeps the existing recursive scanner, symlink behavior, case normalization, and within-directory duplicate handling. Completed archives enter the VFS in original configuration order, after BSAs, so mod priority is unchanged. No persistent directory cache is introduced. All workers finish before exceptions propagate; the first failing directory in input order is reported.
* Exterior changes start terrain preparation asynchronously, prepare the incoming cell/reference list while it runs, then wait at the original dependency barrier before collecting paged references and inserting postponed physics. Cell activation order and gameplay callbacks are retained. Cell preloading also avoids repeating shared mesh/template/keyframe/shape retrieval for the same corrected mesh. When physics instances are preloaded, each occurrence still gets its own instance.
* Immutable terrain chunks receive an OSG KdTree after their vertices and stitching indices are final. Each stitching variant owns its tree; animated meshes are untouched. Rendering queries explicitly enable available trees and avoid ignore-list metadata searches when the list is empty. Ray masks, distance, nearest-hit behavior and paged-object filtering are unchanged.

## Measurement

New timing rows: startup/vfs/registerArchives, startup/vfs/scanDataDirectories, startup/vfs/buildIndex, scene/exterior/waitTerrain and render/ray/traverse. Directory announcements now precede concurrent scanning: do NOT compare the first-to-last announcement span with the previous build as a scan-duration metric. Use the new scan timing row.

Local checks passed before submission:

* Bounded worker execution: zero items, zero/one/four-worker requests, exactly-once execution, join-before-error and deterministic input-order exceptions.
* Real VFS serial versus parallel scans: winning archive and contents match for overlapping case-normalized filenames, unique files, Unicode names, directory symlinks, and missing-directory exceptions.
* Actual VFS registration translation unit compiles with C++20.
* 1,200 terrain ray comparisons using OpenMW's real index-buffer generator and OSG 3.6.5: hit/miss, nearest ratio, world position, world normal and vertex indices agree across stitching variants, transforms, upward/downward/oblique rays and out-of-bounds misses.
* Synthetic ray traversal totals: about 110 ms without the tree versus 2.2 ms with it in this local fixture. This excludes tree construction and is NOT a whole-game speedup prediction.

The terrain regression is also wired into the Windows component test gate. The standalone parallel-worker test runs before the Windows engine build. Windows compilation and these tests must pass before treating its artifact as installable.

## Limits and live checklist

Trees cost construction time and memory; actual net scene-load and gameplay benefits require your capture. These changes do not establish the cause of the entire ten-second Census Office exit or 1.8-second HUD ray outlier. The added scopes help separate remaining work. No shutdown-crash fix is claimed.

Use the same configuration and profiling script, with timing enabled. Run twice, recording both. Start in the Census Office, exit to Seyda Neen, walk the same route, use dialogue, and cross an interior/exterior door in both directions. Check ground targeting/markers over slopes and shorelines, terrain visibility, collision and NPC movement. Keep the first and second runs distinct. Send both run folders, including stderr.

Keep the installed Rubic0n lua51.dll and Frame Generation version.dll/INI when installing the engine artifact. Do not overwrite Rubic0n with the stock Lua DLL bundled by CI.

## Local reproduction

Compile `parallel.cpp` with a C++20 compiler and thread support. `vfs.cpp` links filesystemarchive.cpp, manager.cpp, conversion.cpp, constrainedfilestreambuf.cpp, platform/fileposix.cpp and debuglog.cpp on Linux; pass a nonexistent temporary directory. `terrain-rays.cpp` links terrain/buffercache.cpp with osg, osgUtil and OpenThreads. Assertions must be enabled in these standalone tests. The Windows GTest terrain equivalent lives in apps/components_tests/terrain/testrayacceleration.cpp and uses assertions active in Release builds.
