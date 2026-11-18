# Cell activation preparation

Extends the working cb4fe859 cumulative engine. Existing tooltip, inventory UI,
QuickLoot, HBFS, navigation, scenery-readiness and companion-tool changes remain
in the source and complete Windows distribution. No mod update is required.

Postponed collision work is now tracked at actual world-object registration.
Cell-grid shifts and script-driven Disable still restore it at the same points,
but visit pending references instead of scanning every object in active cells.
The registry resolves the current location after moves and removes dead handles;
inactive, disabled, content-deleted and count-zero cases preserve the existing
accessibility rules. A failed insertion does not discard later pending objects.
World reset clears the queue. The rollback mode does not allocate pending entries.

The existing preload workers now scale their detached collision and avoidance
shapes before publication. Static plugin snapshots retain each winning reference's
scale; live-cell snapshots copy each occurrence's current scale on the main thread.
The cache is keyed by exact normalized model and finite uniform scale. Consumers
take exclusive instances. A changed runtime scale uses an ordinary instance or
demand clone, and Object applies the live scale before physics-world registration.
Matching collision and avoidance scales independently skip redundant Bullet work.
The existing clone counts, admission bounds and cancellation paths are retained.

Physics world insertion, rendering attachment, scripts, actor creation, leveled
spawn selection and navigation registration still happen at activation. This is
not fully asynchronous cell activation and does not defer visible scenery or
collision. It adds no startup cache-generation pass and no extra active-cell ring.

Process-local rollback switches (restart the game):

- `OPENMW_SPARSE_POSTPONED_PHYSICS=0`: the previous active-cell scan.
- `OPENMW_PREPARED_COLLISION_SCALE=0`: the previous unscaled preparation path.
- `OPENMW_COMBINED_PERFORMANCE=0`: disables both with the existing cumulative switch.

Validation covers actual registry/cell lifecycles, real Bullet shape-manager
preparation and matching/mismatched scales, source ownership, avoidance geometry,
exclusive concurrent consumption, cache cleanup, and the production static-reader
snapshot body with counted dependencies. Windows CI requires operation counters
and lifecycle parity in optimized, feature rollback and master-off modes.
Independent negative controls must reject ignored scales, reused instances and
repeated scaling. Synthetic operation counts and timings are not game FPS results.

Compare the same town route with the same mods, settings and Frame Generation
state, first at ordinary speed and then at Speed 2000. Check first and return
traversals separately, including turnarounds, door transitions and object-heavy
scripted areas. Confirm collision and NPC behavior, complete scenery, worst-frame
times and loading bars. Gameplay benefit on the user's installation remains to
be measured; rendering/actor setup may still dominate some transitions.
