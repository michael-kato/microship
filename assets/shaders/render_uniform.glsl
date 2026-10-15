// Append-only layout shared with C++ RenderUniform. No sampled lighting history.
layout(set = 0, binding = 4, std140) uniform RenderUniform
{
    vec4 camera_position;
    vec4 camera_forward;
    vec4 camera_right;
    vec4 camera_up;
    vec4 background;
    vec4 light;        // W: star radiance multiplier.
    uvec4 settings;   // Width, height, paths per pixel, max scattering events.
    uvec4 scene;      // First light triangle, light triangle count, RNG seed, media count.
    vec4 display;     // Exposure, tan(vertical FOV / 2), aspect, volume integration steps.
    vec4 sun_sphere;  // Current center and radius; finite distances, not a directional light.
    vec4 sun_color;
    uvec4 quality;    // Volume spp, local density proposal, conductor compensation, spatial sun reuse.
    vec4 scene_min;   // Conservative bounds of actual stationary planet/ship vertices.
    vec4 scene_max;
    vec4 ambient_top, ambient_bottom;
    vec4 foreground_min, foreground_max;
    uvec4 background_quality;
    vec4 backdrop_style, backdrop_animation;
    vec4 studio_direction[3],studio_color[3],cell_material,backdrop_wave;
} render;
