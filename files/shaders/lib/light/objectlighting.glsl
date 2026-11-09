#ifndef OPENMW_OBJECT_LIGHTING_GLSL
#define OPENMW_OBJECT_LIGHTING_GLSL

#include "lib/light/classicinterior.glsl"

#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)
#define CLASSIC_PER_PIXEL_LIGHTING (@classicNormalMap || @classicSpecularMap)
#define HAS_PIXEL_LIGHTING (PER_PIXEL_LIGHTING || CLASSIC_PER_PIXEL_LIGHTING)
#define HAS_VERTEX_LIGHTING (!PER_PIXEL_LIGHTING || !CLASSIC_PER_PIXEL_LIGHTING)

bool useObjectPerPixelLighting()
{
#if PER_PIXEL_LIGHTING && CLASSIC_PER_PIXEL_LIGHTING
    return true;
#elif PER_PIXEL_LIGHTING
    return !CLASSIC_INTERIOR_LIGHTING;
#elif CLASSIC_PER_PIXEL_LIGHTING
    return CLASSIC_INTERIOR_LIGHTING;
#else
    return false;
#endif
}

bool useObjectNormalMap()
{
    return CLASSIC_INTERIOR_LIGHTING ? @classicNormalMap != 0 : @normalMap != 0;
}

bool useObjectSpecularMap()
{
    return CLASSIC_INTERIOR_LIGHTING ? @classicSpecularMap != 0 : @specularMap != 0;
}

#endif
