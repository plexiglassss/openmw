#ifndef OPENMW_COMPONENTS_SCENEUTIL_VERTEXATTRIBUTES_H
#define OPENMW_COMPONENTS_SCENEUTIL_VERTEXATTRIBUTES_H

namespace SceneUtil
{
    // Used only by object/BS shaders. Groundcover reserves the same generic slot for instance offsets.
    inline constexpr unsigned int ClassicTangentAttribute = 6;
    inline constexpr char ClassicTangentUserValue[] = "classicTangent";
}

#endif
