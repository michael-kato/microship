#include "render_bindings.glsl"
// Current-frame surface guides and diagnostic AOVs. No lighting history.
// Normal guide mode writes first-hit fields and surface.w = log2(1 + terminal depth).
// Spatial volume mode uses grid-anchor volume/scatter_blur entries for fresh total
// and sharp scattering, with log depth in volume.w. F2 overwrites the full AOVs.
struct DebugPixel
{
    vec4 vertex_hit;   // First film sample's vertex reflectance; W: 0 background, 1 opaque surface, 3 transmitting membrane.
    vec4 normal_depth; // Shading normal and first-hit ray distance.
    vec4 direct;       // Camera-visible/direct radiance; W: primary coverage, then packed display RGB8 after display conversion.
    vec4 surface;      // Surface-pass HDR; W: actual closest-hit BVH queries.
    vec4 volume;       // Current-frame in-scattering; W: actual shadow BVH queries.
    vec4 scatter_blur; // SSS light contribution RGB; maximum cell blur radius in pixels.
};
#ifndef NANOVA_DEBUG_BINDING
#define NANOVA_DEBUG_BINDING RENDER_BINDING_DEBUG
#endif
layout(set = 0, binding = NANOVA_DEBUG_BINDING, std430) buffer DebugBuffer { DebugPixel debug_pixels[]; };

// Compact current-frame guides used outside F2: x first-hit distance, y hit code
// (0 background, 1 opaque surface, 3 thin membrane), z log2(1 + terminal depth).
// Spatial volume anchors follow, two vec4 (volume + log depth, sharp scattering)
// per stride cell. Every entry is overwritten each frame.
#ifndef NANOVA_GUIDE_BINDING
#define NANOVA_GUIDE_BINDING RENDER_BINDING_GUIDES
#endif
layout(set = 0, binding = NANOVA_GUIDE_BINDING, std430) buffer GuideBuffer { vec4 guides[]; };
