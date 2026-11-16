#ifndef OPENMW_SKINNING_GLSL
#define OPENMW_SKINNING_GLSL

#if @gpuSkinning
// OPENMW_RIG_SKINNING_V1: object, shadow and transparent-depth passes share this contract.
attribute vec4 openmwRigWeights;
attribute vec4 openmwRigIndices;
uniform bool openmwRigEnabled = false;
uniform mat4 openmwRigPalette[64];
uniform mat4 openmwRigTransform;
#endif

mat4 openmwRigMatrix()
{
#if @gpuSkinning
    // Empty influences bypass both skinning and the skin/root transform, as in RigGeometry::skinCpu.
    if (openmwRigEnabled && openmwRigIndices.x >= 0.0)
    {
        mat4 skin = openmwRigWeights.x * openmwRigPalette[int(openmwRigIndices.x)];
        skin += openmwRigWeights.y * openmwRigPalette[int(openmwRigIndices.y)];
        skin += openmwRigWeights.z * openmwRigPalette[int(openmwRigIndices.z)];
        skin += openmwRigWeights.w * openmwRigPalette[int(openmwRigIndices.w)];
        // Do not normalize weights. The CPU blends affine entries and forces homogeneous w to one.
        skin[0].w = 0.0;
        skin[1].w = 0.0;
        skin[2].w = 0.0;
        skin[3].w = 1.0;
        return openmwRigTransform * skin;
    }
#endif
    return mat4(1.0);
}

#endif
