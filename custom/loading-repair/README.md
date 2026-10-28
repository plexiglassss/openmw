# Initializing-game waiting stage reverted

The user rejected the added "Initializing game..." stage because it greatly increased loading time.
This revision removes that mechanism, not merely its label or timeout.

## Removed
- The post-load visual cover, settling/short-frame checks, timeout, and release controller.
- Withholding player input and setting the simulation time step to zero during that stage.
- The stage-specific suppression of background Lua garbage collection.
- The extra lookup-index preparation inserted at the end of save restoration.
- The handoff-only header and lifecycle tests.

The engine loop, loading-screen implementation/header, and save-state manager are checked against
exact pre-handoff blob hashes. Their ordinary loading, input, simulation and GC behavior is restored.

## Preserved
The independent lookup optimization maintains exceptional unloaded-cell membership when cells
change, instead of repeatedly scanning every metadata-only cell. The existing metadata-only filter,
reference ordering, runtime/moved-reference fallback and diagnostic timing remain.
No changes to mods, saved games or configuration files are required.

## Already-installed 041bdfc4 build
That older executable has a process-local escape switch. Close OpenMW, then launch from the folder
containing its executable:

```powershell
$env:OPENMW_POSTLOAD_HANDOFF="0"; .\openmw.exe
```
This disables that old executable's cover/wait stage, but does not undo its extra pre-game index
preparation. This source rollback removes both. The rebuilt executable requires no such switch.

## Build and validation
CI still validates the exact baseline, runs the source rollback checks, compiles Windows, runs all
component and engine tests and four lookup configurations, and packages only after success.
The source baseline is e9476be66251d181d0016a5fd112e1d90e3875f6. The build-time apply script is
an author-side step, never something an end user must run.

Do not claim the previous post-load freezes are solved by this rollback. In-game performance is
unverified until measured. Further performance work must reduce actual work, not add a waiting gate.
