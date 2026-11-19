# GPU character skinning

This cumulative Windows build can deform eligible animated NIF meshes on the GPU. The engine sends bone transforms instead of rewriting and uploading every vertex, normal and tangent each frame. Meshes stay at their original animation rate and detail level.

The object, shadow and transparent-depth shaders use the same deformation rules. CPU geometry is created only when picking, primitive-based near/far fitting or a fallback renderer requests it. It uses the last submitted pose. Opaque shadow batching retains the palette, and reactivated skeletons never overwrite the last submitted buffer.

The optimization is generic and needs no mod-specific adapter. It applies to `SceneUtil::RigGeometry` meshes with at most 64 bones and four influences per vertex. Weights are neither trimmed nor normalized. Empty influences, missing bones, normal maps and fallback tangents retain the existing behavior.

Unsupported layouts, occupied attributes, larger rigs and non-affine transforms use CPU skinning. The actual draw program is checked again because custom shaders and depth passes can replace it after culling. The fallback has separate vertex arrays and restores shader state. Morph meshes and the separate osgAnimation rig backend retain their existing implementations.

GPU skinning requires at least GLSL 1.20, 16 vertex attributes and 4,096 vertex-uniform components. It is enabled automatically when supported. For a controlled comparison, launch the same executable from a Command Prompt with `set OPENMW_GPU_SKINNING=0`, then run `openmw.exe`. Remove that variable or close the prompt to return to automatic selection. This changes no save data or mod files.

Validation includes native lifecycle/picking/shadow tests, real OSG draws with custom-program and vertex-array fallbacks, and Mesa transform-feedback comparisons using the production object/shadow/depth shaders. The synthetic benchmark measures CPU skinning preparation; it excludes GPU execution and is not a measurement of game FPS. The user's full modlist and NVIDIA driver still require gameplay validation.
