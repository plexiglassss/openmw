#ifndef OPENMW_COMPONENTS_SCENEUTIL_VERTEXATTRIBUTES_H
#define OPENMW_COMPONENTS_SCENEUTIL_VERTEXATTRIBUTES_H

namespace SceneUtil
{
    // Used only by object/BS shaders. Groundcover reserves the same generic slot for instance offsets.
    inline constexpr unsigned int FallbackTangentAttribute = 6;
    inline constexpr char FallbackTangentUserValue[] = "fallbackTangent";
    // Compatibility-profile aliases for secondary colour and fog. Rigs using either array stay on the CPU.
    inline constexpr unsigned int SkinWeightsAttribute = 4;
    inline constexpr unsigned int SkinIndicesAttribute = 5;
    inline constexpr unsigned int SkinPaletteSize = 64;
}

#endif
