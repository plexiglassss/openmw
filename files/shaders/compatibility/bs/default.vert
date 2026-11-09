#version 120

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#define PER_PIXEL_LIGHTING 1

#include "lib/light/classicinterior.glsl"
#include "lib/core/vertex.h.glsl"

#if @diffuseMap
varying vec2 diffuseMapUV;
uniform mat4 texMat@diffuseMapUV;
#endif

#if @classicNormalMap
varying vec2 classicNormalMapUV;
#if (!@diffuseMap || @diffuseMapUV != @classicNormalMapUV) && (!@emissiveMap || @emissiveMapUV != @classicNormalMapUV) && (!@normalMap || @normalMapUV != @classicNormalMapUV)
uniform mat4 texMat@classicNormalMapUV;
#endif
#endif
#if @classicTangent
attribute vec4 classicTangent;
#endif

#if @emissiveMap
varying vec2 emissiveMapUV;
uniform mat4 texMat@emissiveMapUV;
#endif

#if @normalMap
varying vec2 normalMapUV;
uniform mat4 texMat@normalMapUV;
varying vec4 passTangent;
#endif

varying float euclideanDepth;
varying float linearDepth;

varying vec3 passViewPos;
varying vec3 passNormal;

#include "lib/view/depth.glsl"
#include "lib/material/vertexcolors.glsl"

#include "compatibility/shadows_vertex.glsl"
#include "compatibility/normals.glsl"

centroid varying vec4 passColor;

void main(void)
{
    gl_Position = modelToClip(gl_Vertex);

    vec4 viewPos = modelToView(gl_Vertex);
    gl_ClipVertex = viewPos;
    euclideanDepth = length(viewPos.xyz);
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);
    passColor = gl_Color;
    passViewPos = viewPos.xyz;
    passNormal = gl_Normal.xyz;
    normalToViewMatrix = gl_NormalMatrix;

#if @normalMap || @classicNormalMap
    vec4 tangent = gl_MultiTexCoord7.xyzw;
#if @classicTangent
    if (CLASSIC_INTERIOR_LIGHTING)
        tangent = classicTangent;
#endif
    normalToViewMatrix *= generateTangentSpace(tangent, passNormal);
#endif

#if @diffuseMap
    diffuseMapUV = (texMat@diffuseMapUV * gl_MultiTexCoord@diffuseMapUV).xy;
#endif

#if @emissiveMap
    emissiveMapUV = (texMat@emissiveMapUV * gl_MultiTexCoord@emissiveMapUV).xy;
#endif

#if @normalMap
    normalMapUV = (texMat@normalMapUV * gl_MultiTexCoord@normalMapUV).xy;
#endif
#if @classicNormalMap
    classicNormalMapUV = (texMat@classicNormalMapUV * gl_MultiTexCoord@classicNormalMapUV).xy;
#endif


#if @shadows_enabled
    vec3 viewNormal = normalize(gl_NormalMatrix * passNormal);
    setupShadowCoords(viewPos, viewNormal);
#endif
}
