#ifndef OPENMW_OBJECT_LIGHTING_GLSL
#define OPENMW_OBJECT_LIGHTING_GLSL

#include "lib/light/locationrenderingprofile.glsl"

// A profile may independently turn forced per-pixel lighting on or off at
// runtime, so both paths are present in a profile-capable object program.
#define HAS_PIXEL_LIGHTING 1
#define HAS_VERTEX_LIGHTING 1

bool useOrdinaryNormalMap()
{
#if @normalMap
    if (@autoNormalMap == 0)
        return true;
    return useLocationProfileAutomaticNormalMaps();
#else
    return false;
#endif
}

bool useFallbackNormalMap()
{
#if @fallbackNormalMap && @normalMap
    return @autoNormalMap != 0 && !useLocationProfileAutomaticNormalMaps();
#else
    return false;
#endif
}

bool useOrdinarySpecularMap()
{
#if @specularMap
    if (@autoSpecularMap == 0)
        return true;
    return useLocationProfileAutomaticSpecularMaps();
#else
    return false;
#endif
}

bool useFallbackSpecularMap()
{
#if @fallbackSpecularMap && @specularMap
    return @autoSpecularMap != 0 && !useLocationProfileAutomaticSpecularMaps();
#else
    return false;
#endif
}

bool useObjectPerPixelLighting()
{
    return useLocationProfileForcePerPixelLighting() || useOrdinaryNormalMap() || useFallbackNormalMap()
        || useOrdinarySpecularMap() || useFallbackSpecularMap();
}

bool useObjectNormalMap()
{
    return useOrdinaryNormalMap() || useFallbackNormalMap();
}

bool useObjectSpecularMap()
{
    return useOrdinarySpecularMap() || useFallbackSpecularMap();
}

#endif
