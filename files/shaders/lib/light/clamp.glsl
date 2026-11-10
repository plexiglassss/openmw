#include "lib/light/locationrenderingprofile.glsl"

void clampLighting(inout vec3 lighting)
{
    if (useLocationProfileClampLighting())
        lighting = clamp(lighting, vec3(0.0), vec3(1.0));
    else
        lighting = max(lighting, 0.0);
}
