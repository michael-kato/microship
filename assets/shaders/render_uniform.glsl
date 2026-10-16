#include "render_bindings.glsl"
// Append-only layout shared with C++ RenderUniform. No sampled lighting history.
layout(set = 0, binding = RENDER_BINDING_UNIFORM, std140) uniform RenderUniform
{
    vec4 camera_position;
    vec4 camera_forward;
    vec4 camera_right;
    vec4 camera_up;
    vec4 background;
    uvec4 settings;   // Width, height, film samples per pixel, max transparent crossings.
    uvec4 scene;      // Fixed film seed, media count, two padding words.
    vec4 display;     // Exposure, tan(vertical FOV / 2), aspect, volume integration steps.
    uvec4 quality;    // Volume film rays, reserved, conductor compensation, padding.
    vec4 scene_min;   // Conservative bounds of actual stationary mesh vertices.
    vec4 scene_max;
    vec4 ambient_top, ambient_bottom;
    vec4 foreground_min, foreground_max;
    uvec4 background_quality;
    vec4 backdrop_style, backdrop_animation;
    vec4 studio_direction[3],studio_color[3],thin_material,backdrop_wave;
    vec4 fog; // Blur radius in pixels, sight feather in world units, map plane Z.
} render;
