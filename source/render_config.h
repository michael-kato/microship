#ifndef NANOVORE_RENDER_CONFIG_H
#define NANOVORE_RENDER_CONFIG_H

#include "types.h"
#include <stdint.h>

// These settings are loaded from assets; there are no compiled scene defaults.
struct RenderConfig
{
	bool spatial_antialiasing;
	bool borderless; // Runtime display mode; borderless uses native monitor
	                 // pixels.
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
	bool optical_depth_lut, volume_lighting_grid, volume_gauss,
	    volume_spatial_filter;
	uint32_t volume_steps, volume_spatial_stride;
	uint32_t volume_rays_per_pixel;
	uint32_t microfacet_energy_compensation, shadow_grid_resolution,
	    subgroup_size;
	bool static_lighting_cache, force_lighting_rebuild, texture_lighting_grid,
	    organelle_lighting_cache;
	uint32_t bloom_radius;
	float bloom_strength;
	float bloom_threshold;
	// Depth of field: blur radius in pixels per unit of relative depth error
	// beyond the sharp range, up to the maximum radius.
	float focus_range, focus_blur_scale, focus_radius;
	uint32_t subsurface_samples;
	float subsurface_strength, subsurface_wrap, subsurface_transmission,
	    subsurface_thickness;
	// Radiance cascades; zero probes disables them. Interval in cascade-0 probe
	// spacings.
	uint32_t cascade_probes, cascade_directions, cascade_count,
	    cascade_cells_per_frame; // Zero cells: all active cells each frame.
	float cascade_interval, cascade_growth;
	bool cascade_feedback;
	// Screen-space level of detail: projected radius in pixels where a feature
	// starts to fade in and where it is complete. Zero full radius: always full
	// detail.
	float detail_surface_minimum, detail_surface_full, detail_cell_minimum,
	    detail_cell_full;
	float detail_subsurface_minimum, detail_subsurface_full;
	float animation_margin; // World units around the view within which
	                        // factories animate.
	// Development wireframe view (F3). Zero line width: no wireframe section.
	Color wireframe_line, wireframe_fill;
	float wireframe_pixels;
};

const char* render_config_error(const RenderConfig*);
// Smooth level-of-detail weight of a projected radius in pixels; no full radius
// means full detail.
inline float detail_ramp(float pixels, float minimum, float full)
{
	if (full <= 0)
		return 1;
	float t = (pixels - minimum) / (full - minimum);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return t * t * (3 - 2 * t);
}
bool render_config_load(RenderConfig* config, const char* path);

#endif
