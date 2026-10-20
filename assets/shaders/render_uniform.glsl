#include "render_bindings.glsl"
// Append-only layout shared with C++ RenderUniform. No previous-frame image samples.
layout(set = 0, binding = RENDER_BINDING_UNIFORM, std140) uniform RenderUniform
{
    vec4 camera_position;
    vec4 camera_forward;
    vec4 camera_right;
    vec4 camera_up;
    vec4 background;
    uvec4 settings;   // Width, height, film samples per pixel, max transparent crossings.
    uvec4 scene;      // Fixed film seed, media count, sight count, static lighting cache enabled.
    vec4 display;     // Exposure, tan(vertical FOV / 2), aspect, volume integration steps.
    uvec4 quality;    // Volume film rays, visibility grid size, conductor compensation, medium blur enabled.
    vec4 scene_min;   // Conservative bounds of actual stationary mesh vertices.
    vec4 scene_max;
    vec4 ambient_top, ambient_bottom;
    vec4 foreground_min, foreground_max;
    uvec4 background_quality; // Background budgets XY; W: current-frame emissive guide allocated.
    vec4 backdrop_style, backdrop_animation;
    vec4 studio_direction[3],studio_color[3],thin_material,backdrop_wave;
    vec4 fog; // Blur radius in pixels, sight feather in world units, map plane Z.
    vec4 fog_tint;
    vec4 membrane_fade; // View-angle opacity strength/power, transmitted blur radius, surface emission enabled.
    vec4 focus_band; // Screen center, half width, falloff, maximum pixel radius.
    vec4 subsurface; // Global weight, diffuse wrap, transmission budget, thin-wall thickness.
    uvec4 subsurface_quality; // Number of existing film samples used for thickness transport.
    vec4 fog_grid; // World origin XY, grid spacing, side length.
    vec4 organelle_style; // Edge saturation/width, central glow multiplier/power.
    vec4 membrane_ripple; // Normal amplitude, spatial frequency, current phase.
    vec4 emission_fresnel; // Strength and power for instances with angle-masked emission.
} render;
