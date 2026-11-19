# Generic named MWScript variable lookup — 23 October 2026

User action: install the cumulative engine build after CI succeeds. No scripts,
plugins, mod settings or saves need changes.

## Problem and change

Compiler::Locals::searchIndex linearly searches a variable declaration list.
InterpreterContext calls it for named member-variable reads/writes; compiler and
other engine callers also use it. Repeated accesses to larger lists keep comparing
unrelated names. This change adds a sorted index of declaration positions once a
single type's list reaches 32 names. Smaller lists retain linear search.

The index is maintained on declaration, not lazily inside const reads. It stores
integer positions rather than string views, so vector growth and copied Locals
objects cannot leave dangling names. It does not cache variable values or object
references. Declaration order, numeric slots, lowercase-on-declare behavior,
case-sensitive searches, short/long/float precedence, first-duplicate selection,
missing-name results and unknown-type exceptions are preserved. Clear removes the
index; subsequent declarations rebuild it. Written declaration output is unchanged.

This benefits named lookup, not every bytecode instruction: normal direct-index
local reads already avoid name search. No per-mod code or quest logic is changed.

## Validation and limits

The test compiles the actual locals.cpp and compares results against the original
linear-search semantics after every declaration. Coverage includes threshold
crossing, unsorted/duplicate names, all variable types, misses, unknown type,
copy/move construction and assignment, clearing/redeclaration, and output order.
Linux optimized warning-clean compilation and AddressSanitizer/UndefinedBehaviorSanitizer
checks passed. LeakSanitizer cannot inspect /proc in this runtime, so leak detection
was disabled for the successful sanitizer run; no leak-check claim is made.
The same actual-source regression runs on Windows before full engine compilation.

An informational late-hit/miss microbenchmark showed a regression at a trial
threshold of 16, so the final threshold is 32. At 32/64/256 declarations the local
optimized run was faster than linear search; these are synthetic lookup timings,
not FPS or full-script measurements. Timing is not a pass/fail test. Early hits
may still favor linear search. The index adds declaration-time work and memory:
three vector descriptors per Locals object and integer entries for indexed types.

Later, use the usual walking/dialogue capture and exercise quests with object
member variables. Check dialogue outcomes, script errors and save/reload behavior.
Compare MWScript execution timings with existing captures. No navmesh regeneration
is involved.

## Other areas reviewed this pass

The cumulative branch already has model-preparation sharing, paging-reference
reuse, directory-scan parallelism and world reference lookup indices. They remain
intact. VFS index construction still merges archives in priority order; no new
parallel merge was introduced without an ordering-preserving design and evidence.
The world-tooltip bridge already throttles rebuilds, and API.getContext returns
an existing context rather than reconstructing it. No new HUD Markers optimization
is claimed in this batch. Broad concurrent execution of gameplay scripts remains
out of scope because execution order and shared state matter.
