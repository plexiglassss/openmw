# Cooperative preload work and grouped settings

Baseline: cumulative 82dcf5a82f442c78affe08f78dac300c8d160f4a. All earlier engine features remain in its ancestry, including Smart Map. This change does not alter view distance, object visibility, detail thresholds, required activation or navmesh readiness.

Cell resource jobs now yield between completed preparation operations, after a soft two-millisecond allowance or sixteen operations. A single native resource operation is indivisible. Required arrival work retains priority across continuations; ordinary border predictions are promoted only for their next dispatch. A newly required destination can run before an older required continuation. Cleanup retains its existing fairness interval.

Preparation timing distinguishes execution from waits between slices. Intermediate work is not published as complete; cancellation cannot publish a nonterminal slice. Temporary mesh/animation indexing state is released on the worker, while prepared resources and instance tickets remain retained as before. OPENMW_SLICED_CELL_PRELOAD=0 disables slicing for comparison; the shared queue-priority correction remains active. OPENMW_COMBINED_PERFORMANCE=0 also disables slicing. Benchmark captures record the new flag without overriding it.

Settings now use six main pages: Gameplay, Interface, Controls, Video, Audio and Mods. Gameplay and Interface use category selectors. Existing controls, setting keys, callbacks and saved values are moved intact. Unknown layout pages remain accessible under Mods. Partial old journal layouts and the native missing-control fallback share one section. Newly wrapped sections support mouse-wheel scrolling and keyboard/controller focus reveal. Native/Lua scroll regions retain their own input handling.

Verification: counted production PreloadItem body; real WorkQueue cancellation, continuation and priority tests; actual MyGUI 3.4.3 widget hierarchy tests; cumulative Windows component/engine and existing Lua UI checks. These establish correctness on their tested inputs, not an in-game performance gain. The user's next comparable traversal capture remains the performance acceptance test.

Advanced World Map v1.18.0's separate rebuild overlay is in custom/advanced-world-map. It preserves the captured NPC/door query integration and replaces recursive graph traversal with depth-first continuation frames. It has its own paired Lua5.4/LuaJIT2.1 behavioral suite and installs separately from the engine.
