#version 120

#pragma import_defines(FORCE_OPAQUE, DISTORTION)

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#if @diffuseMap
uniform sampler2D diffuseMap;
varying vec2 diffuseMapUV;
#endif

#if @darkMap
uniform sampler2D darkMap;
varying vec2 darkMapUV;
#endif

#if @detailMap
uniform sampler2D detailMap;
varying vec2 detailMapUV;
#endif

#if @decalMap
uniform sampler2D decalMap;
varying vec2 decalMapUV;
#endif

#if @emissiveMap
uniform sampler2D emissiveMap;
varying vec2 emissiveMapUV;
#endif

#if @normalMap
uniform sampler2D normalMap;
varying vec2 normalMapUV;
#endif

#if @envMap
uniform sampler2D envMap;
varying vec2 envMapUV;
uniform vec4 envMapColor;
#endif

#if @specularMap
uniform sampler2D specularMap;
varying vec2 specularMapUV;
#endif

#if @classicNormalMap
uniform sampler2D classicNormalMap;
varying vec2 classicNormalMapUV;
#endif
#if @classicSpecularMap
uniform sampler2D classicSpecularMap;
varying vec2 classicSpecularMapUV;
#endif

#if @bumpMap
uniform sampler2D bumpMap;
varying vec2 bumpMapUV;
uniform vec2 envMapLumaBias;
uniform mat2 bumpMapMatrix;
#endif

#if @glossMap
uniform sampler2D glossMap;
varying vec2 glossMapUV;
#endif

uniform vec2 screenRes;
uniform float near;
uniform float far;
uniform float alphaRef;
uniform float distortionStrength;
uniform float alpha;
uniform float actorFade;

#include "lib/light/objectlighting.glsl"

centroid varying vec4 passColor;

#if HAS_VERTEX_LIGHTING
centroid varying vec3 shadedLighting;
centroid varying vec3 shadedSpecular;
centroid varying vec3 passLighting;
centroid varying vec3 passSpecular;
#endif
#if HAS_PIXEL_LIGHTING
#include "lib/light/clamp.glsl"
#endif
varying vec3 passViewPos;
varying vec3 passNormal;
#if @normalMap || @classicNormalMap || @diffuseParallax
varying vec4 passTangent;
#endif

#if @additiveBlending
#define ADDITIVE_BLENDING
#endif

#include "lib/core/fragment.h.glsl"
#include "lib/material/parallax.glsl"
#include "lib/material/alpha.glsl"
#include "lib/util/distortion.glsl"

#include "fog.glsl"
#include "lib/material/vertexcolors.glsl"
#include "shadows_fragment.glsl"
#include "compatibility/normals.glsl"

#if @softParticles
#include "lib/particle/soft.glsl"

uniform float particleSize;
uniform bool particleFade;
uniform float softFalloffDepth;
#endif

#if @particleOcclusion
#include "lib/particle/occlusion.glsl"
uniform sampler2D orthoDepthMap;
varying vec3 orthoDepthMapCoord;
#endif

void main()
{
#if @particleOcclusion
    applyOcclusionDiscard(orthoDepthMapCoord, texture2D(orthoDepthMap, orthoDepthMapCoord.xy * 0.5 + 0.5).r);
#endif

    Material material = getMaterial();

    // only offset diffuse and normal maps for now, other textures are more likely to be using a completely different UV set
    vec2 offset = vec2(0.0);

#if @parallax || @classicParallax || @diffuseParallax
    bool useParallax = false;
    float height = 0.0;
    if (CLASSIC_INTERIOR_LIGHTING)
    {
#if @classicParallax && @classicNormalMap
        height = texture2D(classicNormalMap, classicNormalMapUV).a;
        useParallax = true;
#elif @diffuseParallax
        height = texture2D(diffuseMap, diffuseMapUV).a;
        useParallax = true;
#endif
    }
    else
    {
#if @parallax
        height = texture2D(normalMap, normalMapUV).a;
        useParallax = true;
#elif @diffuseParallax
        height = texture2D(diffuseMap, diffuseMapUV).a;
        useParallax = true;
#endif
    }
    if (useParallax)
        offset = getParallaxOffset(transpose(normalToViewMatrix) * normalize(-passViewPos), height);
#endif

vec2 screenCoords = gl_FragCoord.xy / screenRes;

#if @diffuseMap
    gl_FragData[0] = texture2D(diffuseMap, diffuseMapUV + offset);

#if defined(DISTORTION) && DISTORTION
    gl_FragData[0].a *= getDiffuseColor(material, passColor).a;
    gl_FragData[0] = applyDistortion(gl_FragData[0], distortionStrength, gl_FragCoord.z, sampleOpaqueDepthTex(screenCoords / @distorionRTRatio).x);
    return;
#endif

#if @diffuseParallax
    gl_FragData[0].a = 1.0;
#else
    gl_FragData[0].a *= coveragePreservingAlphaScale(diffuseMap, diffuseMapUV + offset);
#endif
#else
    gl_FragData[0] = vec4(1.0);
#endif

    vec4 diffuseColor = getDiffuseColor(material, passColor);
    gl_FragData[0].a *= diffuseColor.a * alpha * actorFade;

#if @darkMap
    gl_FragData[0] *= texture2D(darkMap, darkMapUV);
    gl_FragData[0].a *= coveragePreservingAlphaScale(darkMap, darkMapUV);
#endif

    gl_FragData[0].a = alphaTest(gl_FragData[0].a, alphaRef);

    vec3 viewNormal = normalize(gl_NormalMatrix * passNormal);
    if (CLASSIC_INTERIOR_LIGHTING)
    {
#if @classicNormalMap
        vec4 normalTex = texture2D(classicNormalMap, classicNormalMapUV + offset);
        vec3 normal = normalTex.xyz * 2.0 - 1.0;
#if @classicReconstructNormalZ
        normal.z = sqrt(1.0 - dot(normal.xy, normal.xy));
#endif
        viewNormal = normalToView(normal);
#endif
    }
    else
    {
#if @normalMap
        vec4 normalTex = texture2D(normalMap, normalMapUV + offset);
        vec3 normal = normalTex.xyz * 2.0 - 1.0;
#if @reconstructNormalZ
        normal.z = sqrt(1.0 - dot(normal.xy, normal.xy));
#endif
        viewNormal = normalToView(normal);
#endif
    }

    vec3 viewVec = normalize(passViewPos);

#if @detailMap
    gl_FragData[0].xyz *= texture2D(detailMap, detailMapUV).xyz * 2.0;
#endif

#if @decalMap
    vec4 decalTex = texture2D(decalMap, decalMapUV);
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, decalTex.xyz, decalTex.a * diffuseColor.a);
#endif

#if @envMap

    vec2 envTexCoordGen = envMapUV;
    float envLuma = 1.0;

#if @normalMap || @classicNormalMap
    // if using normal map + env map, take advantage of per-pixel normals for envTexCoordGen
    if (useObjectNormalMap())
    {
        vec3 r = reflect( viewVec, viewNormal );
        float m = 2.0 * sqrt( r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0) );
        envTexCoordGen = vec2(r.x/m + 0.5, r.y/m + 0.5);
    }
#endif

#if @bumpMap
    vec4 bumpTex = texture2D(bumpMap, bumpMapUV);
    envTexCoordGen += bumpTex.rg * bumpMapMatrix;
    envLuma = clamp(bumpTex.b * envMapLumaBias.x + envMapLumaBias.y, 0.0, 1.0);
#endif

    vec3 envEffect = texture2D(envMap, envTexCoordGen).xyz * envMapColor.xyz * envLuma;

#if @glossMap
    envEffect *= texture2D(glossMap, glossMapUV).xyz;
#endif

#if @preLightEnv
    gl_FragData[0].xyz += envEffect;
#endif

#endif

    float shadowing = unshadowedLightRatio(-passViewPos.z);
    vec3 lighting, specular;
#if HAS_VERTEX_LIGHTING
    if (!useObjectPerPixelLighting())
    {
        lighting = mix(shadedLighting, passLighting, shadowing);
        specular = mix(shadedSpecular, passSpecular, shadowing);
    }
#if HAS_PIXEL_LIGHTING
    else
#endif
#endif
#if HAS_PIXEL_LIGHTING
    {
        float shininess = material.shininess;
        vec3 specularColor = getSpecularColor(material, passColor).xyz;
        if (CLASSIC_INTERIOR_LIGHTING)
        {
#if @classicSpecularMap
            vec4 specTex = texture2D(classicSpecularMap, classicSpecularMapUV);
            shininess = specTex.a * 255.0;
            specularColor = specTex.xyz;
#endif
        }
        else
        {
#if @specularMap
            vec4 specTex = texture2D(specularMap, specularMapUV);
            shininess = specTex.a * 255.0;
            specularColor = specTex.xyz;
#endif
        }
        vec3 diffuseLight, ambientLight, specularLight;

        doLighting(gl_FragCoord.xy, passViewPos, viewNormal, shininess, shadowing, diffuseLight, ambientLight, specularLight);

        lighting = diffuseColor.xyz * diffuseLight + getAmbientColor(material, passColor).xyz * ambientLight + getEmissionColor(material, passColor).xyz * material.emissiveMult;
        specular = specularColor * specularLight * material.specStrength;
        clampLighting(lighting);
    }
#endif

    gl_FragData[0].xyz = gl_FragData[0].xyz * lighting + specular;

#if @envMap && !@preLightEnv
    gl_FragData[0].xyz += envEffect;
#endif

#if @emissiveMap
    gl_FragData[0].xyz += texture2D(emissiveMap, emissiveMapUV).xyz;
#endif

    gl_FragData[0] = applyFogAtPos(gl_FragData[0], passViewPos, near, far);

#if !defined(FORCE_OPAQUE) && @softParticles
    gl_FragData[0].a *= calcSoftParticleFade(
        viewVec,
        passViewPos,
        viewNormal,
        near,
        far,
        sampleOpaqueDepthTex(screenCoords).x,
        particleSize,
        particleFade,
        softFalloffDepth,
        waterEnabled,
        isReflection,
        waterHeight,
        osg_ViewMatrixInverse
    );
#endif

#if defined(FORCE_OPAQUE) && FORCE_OPAQUE
    // having testing & blending isn't enough - we need to write an opaque pixel to be opaque
    gl_FragData[0].a = 1.0;
#endif

#if !defined(FORCE_OPAQUE) && !@disableNormals
    gl_FragData[1].xyz = viewNormal * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
