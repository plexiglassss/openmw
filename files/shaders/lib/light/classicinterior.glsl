#ifndef OPENMW_CLASSIC_INTERIOR_GLSL
#define OPENMW_CLASSIC_INTERIOR_GLSL

// Scene state selects this profile; model templates and the user's global settings stay unchanged.
// UI previews use false even when their source actor is currently in a classic interior.
uniform bool classicInteriorLighting;
#define CLASSIC_INTERIOR_LIGHTING (classicInteriorLighting && @classicInteriorAvailable != 0)

#endif
