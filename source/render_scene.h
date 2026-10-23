#ifndef NANOVORE_RENDER_SCENE_H
#define NANOVORE_RENDER_SCENE_H

#include "geometry.h"
#include "sight_field.h"
#include "types.h"
#include <vector>

// CPU render input. No game rules or graphics API resources are stored here.
enum
{
	INSTANCE_STYLE_FLOATS = 8,
	INSTANCE_UNSHADOWED = 1,
	INSTANCE_TINT_THIN_ONLY = 2,
	INSTANCE_ORGANELLE = 4,
	INSTANCE_EMISSION_FRESNEL = 8
};
// Craft: ambient light fades with screen-space detail. No subsurface: the
// surface never scatters.
enum
{
	INSTANCE_DETAIL_AMBIENT = 16,
	INSTANCE_NO_SUBSURFACE = 32
};
struct RenderInstance
{
	float transform[3][4];
	uint32_t mesh, mask;
	bool background, static_lighting;   // Static light blockers can be baked
	                                    // until their inputs change.
	float style[INSTANCE_STYLE_FLOATS]; // RGB tint, weight; emission, target
	                                    // roughness, roughness weight, flags.
};
// Ascending, disjoint instance slot ranges. Every instance outside them is
// empty this frame.
struct RenderRange
{
	uint32_t first, count;
};
struct RenderMedium
{
	float center_radius[4], scattering[4], absorption_phase[4], lighting[4],
	    blur[4]; // Lighting: shadow enabled, grid eligible, grid active, full
		         // cell radius.
	float
	    detail[4]; // Written by the renderer each frame: screen size in pixels,
		           // fog reveal, moving-shadow weight, cascade weight.
};
enum
{
	MAX_RENDER_SIGHT = 272
};
struct RenderSight
{
	float center_radius[4];
};
struct RenderGridBounds
{
	float low[4], high[4];
};
struct RenderLight
{
	Vec3 direction;
	Color color;
	float irradiance;
};
struct EffectSegment
{
	float start[4], end[4], color[4];
};
enum
{
	EFFECT_TILE = 16,
	MAX_EFFECT_SEGMENTS = 8192,
	MAX_EFFECT_TILES_PER_SEGMENT = 64,
	MAX_EFFECT_REFERENCES = MAX_EFFECT_SEGMENTS * MAX_EFFECT_TILES_PER_SEGMENT
};
struct RenderScene
{
	Geometry geometry;
	std::vector<RenderInstance> instances;
	std::vector<RenderRange> ranges;
	std::vector<RenderMedium> media;
	std::vector<RenderGridBounds> lighting_bounds;
	std::vector<EffectSegment> effects;
	std::vector<uint32_t> effect_tiles;
	uint32_t effect_count, tile_count;
	RenderSight sight[MAX_RENDER_SIGHT];
	uint32_t sight_count;
	SightField fog_field;
	float fog_blur, fog_feather, fog_plane_z;
	Color fog_tint;
	Color ambient, ambient_top, ambient_bottom;
	RenderLight lights[3];
	float thin_transmission, thin_absorption, thin_clarity, thin_edge_strength,
	    thin_edge_power, thin_blur_radius;
	bool lights_enabled, guides_required, self_glow;
	uint32_t background_samples, background_triangles;
	float background_blur, background_glow, background_ambient;
	Vec3 background_wave;
	float background_animation[4];
	float organelle_style[4], membrane_ripple[4], emission_fresnel[4],
	    emission_color[4];
	float relief[8], electron[8]; // Layouts of World::relief and ::electron.
	float saturation;
	float bounds_min[3], bounds_max[3], foreground_min[3], foreground_max[3];
	double time_seconds;
};

void render_scene_bounds(RenderScene*);
bool render_scene_validate(const RenderScene*);
bool render_scene_write(const RenderScene*, const char* path);

#endif
