# Model preparation: shared in-flight work and chunk-local template reuse

This builds on 0c7b6377: all prior performance work, the frame-generation
continuity source change and the three UI features remain included.

The previous investigation identified model-template preparation as the dominant
remaining paged-object preparation cost. Source review confirms that concurrent
SceneManager cache misses previously each loaded, filtered, prepared shaders and
optimized the same path independently. A scenery chunk also requested the same
cached template for every placement of that model.

Changes:
- Cache misses for one normalized model path share a pending result. Different
  models load outside the registry mutex and remain parallel. The producer
  rechecks the normal cache after registering, closing the lookup/registration
  race. Only fully prepared nodes are published. Errors reach existing waiters;
  later calls can retry. Normal missing-model error-marker handling is retained.
- Each object-paging chunk reuses templates by the final normalized model path,
  after the existing actor and LOD rules. The local map expires at the end of
  chunk construction. Per-reference eligibility, transforms, scale, disabling,
  distance checks and clone/merge behavior are unchanged.
- Timing category model/template now separates request, cacheLookup, prepare,
  load, filtering, shaders, optimizeAndShare, compileOrBounds and
  waitForPreparation. These are nested elapsed timings: do not sum parents and
  children, or treat sums across workers as wall-clock time. Existing
  terrain/objects/getModelTemplate still includes every chunk placement lookup.

This targets loading and cell-transition preparation. No steady-state FPS or
percentage gain is claimed before profiling. The normal cache still owns expiry
and clearing. The pending registry retains no completed results; it does not add
an on-disk cache. The first producer retains existing optional precompile behavior
(as ordinary cache hits already do). No shader quality, geometry, ray-hit rules,
mod scripts or quest behavior have been changed.

Validation completed locally: C++20 concurrent same-key result identity,
independent-key progress, concurrent exception propagation, retry after failure,
and no completed-result retention. The same tests run under MSVC before the
Windows engine build. Full engine compilation and live testing are pending at
publication; check the associated Actions run for the subsequent outcome.

Test using the same profiling procedure and route as before, including Census
Office exit and the previously slow outdoor transitions. Keep HUD Markers and
other mods/settings unchanged. Run twice, labeling first and warm runs separately.
Exit normally to flush worker measurements. Send both capture ZIPs and logs.
Compare prepare calls and model stage times, overall paged-object preparation,
cell-transition duration and long frames against the existing baseline. Confirm
normal scenery, missing-model markers, item targeting and repeated cell visits.
No unoptimized build or WithoutTiming capture is requested. Preserve the custom
lua51.dll and FG version.dll/INI when installing the engine.
