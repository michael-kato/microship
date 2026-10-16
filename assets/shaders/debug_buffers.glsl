// Current-frame surface guides and diagnostic AOVs. No lighting history.
struct DebugPixel
{
    vec4 vertex_hit;   // First film sample's vertex reflectance; W: 0 background, 1 opaque surface, 3 transmitting membrane.
    vec4 normal_depth; // Shading normal and first-hit ray distance.
    vec4 direct;       // Camera-visible/direct radiance; W: primary coverage, then packed display RGB8 after display conversion.
    vec4 surface;      // Surface-pass HDR; W: actual closest-hit BVH queries.
    vec4 volume;       // Current-frame in-scattering; W: actual shadow BVH queries.
};
#ifndef MICROSHIP_DEBUG_BINDING
#define MICROSHIP_DEBUG_BINDING 8
#endif
layout(set = 0, binding = MICROSHIP_DEBUG_BINDING, std430) buffer DebugBuffer { DebugPixel debug_pixels[]; };
