# Cumulative Windows engine

This source combines the existing message-position controls, inactive Lua cache,
gameplay installer corrections, frame-generation source fixes and collector tools
with the accepted shrine, dialogue, journal and lookup engine baseline. The
unrelated upstream `master` update is outside this consolidation.

## Source verification

`source-plan.json` names five immutable source heads and their actual merge bases.
The cumulative verifier checks every tracked path against the accepted baseline
plus the complete contribution deltas. Existing feature files must match their
selected source commit exactly. The shared settings file must equal a clean
three-way merge. The latest gameplay README and installer files have explicit
resolutions. Only the workflow and this integration directory contain newly
authored integration code.

The earlier dialogue, shrine, gameplay and message-position source verifiers run
unchanged in detached worktrees at their original commits. Their historical scope
limits are preserved. The final union is checked before and after applying the
existing lookup-only build patch. Its unchanged integration check also rejects
the abandoned engine initializing/waiting stage.

## Build checks

The native Windows job keeps the existing optimized Release/LTO/sccache build,
the complete component and engine test suites, all four lookup configurations,
message-placement geometry tests, installer fixtures, and settings fixtures in
Windows PowerShell 5.1 and PowerShell 7. Separate jobs retain the headless GL,
Windows frame-generation DLL/export checks and all four historical tooling test
workflows. Every job must pass before this cumulative build is accepted.

The pull request targets the existing gameplay branch so GitHub can reuse its
compiler cache. All seven jobs explicitly check out the cumulative pull-request
head, and artifact/build identities use that same head commit. Manual dispatch
also builds its exact selected commit.

## Complete engine package

The engine archive includes all required bootstrap and runtime files:
`openmw.cfg`, `lua51.dll`, `resources/lua_libs/content.lua`, `defaults.bin`, the
executables, their dependencies and the engine resources. Packaging must not
remove any of these files. The `openmw.cfg` beside the executables is the engine
bootstrap, which locates resources and the separate user configuration. It is
not the user's modlist configuration in `Documents\My Games\OpenMW`.

The engine package can be extracted into a fresh directory. It still needs the
user's separately installed game data and mods to play. The optional
frame-generation `version.dll` and `openmw_fg.ini` remain separate. The compiled
`defaults.bin` is decoded and compared byte-for-byte with the merged defaults;
the final inventory includes the complete packaged engine.

Close OpenMW and make a separate backup COPY of the current engine directory
before replacing files. Keep the backup intact for rollback. When upgrading an
installation that uses a custom Lua runtime, preserve its existing `lua51.dll`
and matching `resources/lua_libs/content.lua` together; do not mix one custom file
with one bundled file. Preserve the optional FG proxy/configuration too. The
complete standard package supplies its own tested CI runtime pair for a fresh
engine directory. Do not replace the separate user modlist configuration.

Specific optional gameplay settings and their rollback remain in
`GAMEPLAY-RESPONSIVENESS.md`; notification controls are described in
`MESSAGE-POSITION.md`.

## Packaging repair and startup regression

The earlier cumulative artifact `13537633072` incorrectly omitted the three
required files above. `restore-package.py` repairs that artifact using exact
original bytes from artifact `13520058314`, checked against the cumulative
build's pre-omission inventory. The cumulative engine revision remains
`29022d6176d82b9281075cc2189d19b11ebce463`: this repair does not recompile or change
the engine. The repair record distinguishes that engine revision from the
packaging source revision, and retains the earlier package evidence.

The one-off `fix/windows-package-20261106` pull request runs only the package
repair job. The seven normal build/test jobs remain unchanged apart from their
branch exclusion and the new startup check in the native job. Previously passed
native suites apply to the unchanged binary hashes; they are not presented as
new executions in the repair job.

`check-startup.ps1` launches the actual packaged `openmw.exe` from a temporary
copy, with fresh empty configuration/data directories and no game assets. It
must load the packaged bootstrap, parse defaults, reach the exact deliberate
no-content message, and exit normally with code 0. A negative control renames
only the copied bootstrap, reproduces the exact `No config dirs!` failure, then
restores the file and repeats the successful startup. These checks exercise
configuration startup; `--help` and `--version` would bypass it.

CI tests use the bundled dependency Lua runtime. The startup check does not
exercise Lua UI initialization or gameplay and does not establish compatibility
with a custom replacement runtime, installation success on the user's machine,
in-game appearance or a measured performance gain.
