#version 120

varying vec2 diffuseMapUV;
varying float alphaPassthrough;

uniform mat4 texMat0;

#include "lib/core/vertex.h.glsl"
#include "lib/core/skinning.glsl"
#include "lib/material/vertexcolors.glsl"

void main()
{
    Material material = getMaterial();
    vec4 modelPosition = openmwRigMatrix() * gl_Vertex;

    gl_Position = modelToClip(modelPosition);

    vec4 viewPos = modelToView(modelPosition);
    gl_ClipVertex = viewPos;

    if (material.vertexColorMode == ColorMode_AmbientAndDiffuse)
        alphaPassthrough = gl_Color.a;
    else
        alphaPassthrough = material.diffuse.a;

    diffuseMapUV = (texMat0 * gl_MultiTexCoord0).xy;
}
