#ifndef OPENMW_LOCATION_RENDERING_PROFILE_GLSL
#define OPENMW_LOCATION_RENDERING_PROFILE_GLSL

// External shader_profiles/*.yaml files select these temporary scene values.
// The user's saved global shader settings remain unchanged.
uniform bool locationRenderingProfileActive;
uniform bool locationProfileClampLighting;
uniform bool locationProfileForcePerPixelLighting;
uniform bool locationProfileAutoUseObjectNormalMaps;
uniform bool locationProfileAutoUseObjectSpecularMaps;
uniform bool ordinaryAutoUseObjectNormalMaps;
uniform bool ordinaryAutoUseObjectSpecularMaps;

// A material may opt out locally when retaining its explicit fallback map would
// exceed the available texture-unit limit. Other materials remain profiled.
#define LOCATION_RENDERING_PROFILE (locationRenderingProfileActive && @locationProfileAvailable != 0)

bool useLocationProfileClampLighting()
{
    return LOCATION_RENDERING_PROFILE ? locationProfileClampLighting : @clamp != 0;
}

bool useLocationProfileForcePerPixelLighting()
{
    return LOCATION_RENDERING_PROFILE ? locationProfileForcePerPixelLighting : @forcePPL != 0;
}

bool useLocationProfileAutomaticNormalMaps()
{
    return LOCATION_RENDERING_PROFILE ? locationProfileAutoUseObjectNormalMaps
                                      : ordinaryAutoUseObjectNormalMaps;
}

bool useLocationProfileAutomaticSpecularMaps()
{
    return LOCATION_RENDERING_PROFILE ? locationProfileAutoUseObjectSpecularMaps
                                      : ordinaryAutoUseObjectSpecularMaps;
}

#endif
