#ifndef NANOVA_RENDER_CONFIG_H
#define NANOVA_RENDER_CONFIG_H

#include <stdint.h>
#include "types.h"

// These settings are loaded from assets; there are no compiled scene defaults.
struct RenderConfig
{
    bool vsync; // Runtime preference; false requests immediate presentation.
    uint32_t width;
    uint32_t height;
    uint32_t samples_per_pixel;
    uint32_t max_transmission_layers;
    uint32_t seed;
    uint32_t sphere_segments;
    uint32_t sphere_rings;
    float exposure;
    float vertical_fov_degrees;
    Vec3 camera_position;
    Vec3 camera_target;
    Vec3 camera_up;
    float camera_keyboard_speed;
    float camera_min_distance;
    float camera_max_distance;
    uint32_t volume_steps;
    uint32_t volume_rays_per_pixel;
    uint32_t microfacet_energy_compensation,shadow_grid_resolution;
    bool static_lighting_cache,force_lighting_rebuild;
    uint32_t bloom_radius;
    float bloom_strength;
    float bloom_threshold;
    float focus_center,focus_half_width,focus_falloff,focus_radius;
    uint32_t subsurface_samples;
    float subsurface_strength,subsurface_wrap,subsurface_transmission,subsurface_thickness;
};

bool render_config_load(RenderConfig* config, const char* path);

#endif
