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

## Upgrade artifact

The engine artifact is an overlay for the existing installation. It deliberately
omits these installed files:

- `lua51.dll`
- `resources/lua_libs/content.lua`
- `version.dll`
- `openmw.cfg`
- `openmw_fg.ini`

Keep the existing custom Lua DLL and its matching `content.lua` together, and keep
the existing frame-generation proxy and configuration. The separate FG artifact
is validation output; it is not bundled into the engine upgrade. The compiled
`defaults.bin` is decoded and compared byte-for-byte with the merged defaults
before packaging. The final inventory is generated after all exclusions.

Close OpenMW and make a separate backup COPY of the current engine directory.
Then MERGE the reviewed engine files into the original existing directory, keeping
the five omitted files in place. Keep the separate backup intact for rollback.
Do not move the old directory away and extract this overlay into an empty replacement.
Do not use the overlay as a fresh standalone installation. Specific optional
gameplay settings and their rollback remain in `GAMEPLAY-RESPONSIVENESS.md`;
notification controls are described in `MESSAGE-POSITION.md`.

CI tests use the build dependency Lua runtime. They do not prove compatibility
with the separately preserved custom runtime, installation success, in-game
appearance or a measured gameplay improvement. Those observations remain separate.
