# Windows diagnostic build profile

User action: none beyond installing the diagnostic runtime as usual. No compiler, cache software or additional mod is needed on the gaming PC.

## What remains in the build

The game, launcher, installation wizard, INI importer and navmesh tool are built from the same source commit. The launcher retains its helper programs. Existing engine fixes and diagnostic instrumentation remain included.

Release optimization remains `/O2 /Ob2 /DNDEBUG`, with link-time optimization (`/GL` and the CMake IPO link settings), `/OPT:REF` and `/OPT:ICF`. The matching PDB files remain available in the separate symbols artifact. `/Z7` embeds compile-time debugging information in object files; `/DEBUG` produces the final PDB at link time.

The timing-recorder and actor-diagnostic smoke tests, full component and engine test suites, early launcher regression checks and actual Lua callback timing checks remain enabled. OpenMW-CS, its tests, standalone development tools, the ESS importer and benchmarks are omitted. A symbol-server directory is no longer generated for this profile; this does not remove the downloadable PDBs.

## Reusing compilation results

The diagnostic workflow uses Mozilla sccache 0.18.0 with the GitHub Actions cache backend. Cache identity includes compiler and compilation inputs; it does not include the changing source commit as a blanket invalidation key. Only matching compilation results can be reused. Linking still runs.

MSVC precompiled headers are disabled for this profile because sccache cannot cache `/Fp` and `/Yc` compilations. CMake policy CMP0141 and the Embedded debug-information format provide `/Z7`, avoiding the shared compile-time PDB problem with `/Zi`. The workflow checks generated engine/component/launcher compile commands before starting the long compilation, including Release optimization and `/GL`.

The first build populates the cache and pays the cost of compiling without precompiled headers. Its time is not a reliable estimate of subsequent builds. Compiler updates, changed dependencies, widespread header edits, or cache eviction can cause a substantial rebuild. Cache availability affects build speed, not the source features included in the executable.

## Measuring the saving

The job summary includes compiler-cache hits, misses and non-cacheable requests. The logs artifact includes `sccache-stats.txt`, `sccache-stats.json`, `compile_commands.json`, `CMakeCache.txt` and `.ninja_log`. GitHub step durations provide the elapsed build time; Ninja's log separates individual compilation and link work.

Compare a first build with the next small diagnostic edit on the same branch and toolchain. Do not report the first run as a cached build or sum overlapping compilation durations as elapsed time. The pre-change Windows run 39440100036 took about 57 minutes, including 51m20s across its two compilation stages and 2m52s preparing the symbol-server directory. Any new turnaround estimate remains unverified until the CI runs finish.

## Full-build fallback

The Diagnostic timing build workflow has a manual `full_build` option. Setting it to true restores all normal targets, benchmarks, MSVC precompiled headers, `/Zi` and symbol-server preparation without sccache. Other callers of the reusable Windows workflow retain that full profile by default. Diagnostic-branch pushes use the cached, reduced profile.

The runtime archive records its exact source commit in `CI-ID.txt`. Custom Rubic0n and Frame Generation DLLs remain separately installed files, as before.
