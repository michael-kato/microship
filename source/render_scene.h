#ifndef MICROSHIP_RENDER_SCENE_H
#define MICROSHIP_RENDER_SCENE_H

#include "geometry.h"
#include "types.h"
#include <vector>

// CPU render input. No game rules or graphics API resources are stored here.
struct RenderInstance
{
    float transform[3][4];
    uint32_t mesh, mask;
    bool background;
    float style[8];
};
struct RenderMedium
{
    float center_radius[4], scattering[4], absorption_phase[4];
};
struct RenderLight { Vec3 direction; Color color; float irradiance; };
struct EffectSegment { float start[4],end[4],color[4]; };
enum { EFFECT_TILE=16, MAX_EFFECT_SEGMENTS=8192, MAX_EFFECT_REFERENCES=MAX_EFFECT_SEGMENTS*64 };
struct RenderScene
{
    Geometry geometry;
    std::vector<RenderInstance> instances;
    std::vector<RenderMedium> media;
    std::vector<EffectSegment> effects;
    std::vector<uint32_t> effect_tiles;
    uint32_t effect_count, tile_count;
    Color ambient, ambient_top, ambient_bottom;
    RenderLight lights[3];
    float thin_transmission, thin_absorption, thin_clarity;
    bool lights_enabled, guides_required;
    uint32_t background_samples, background_triangles;
    float background_blur, background_glow, background_ambient;
    Vec3 background_wave;
    float background_animation[4];
    float bounds_min[3],bounds_max[3],foreground_min[3],foreground_max[3];
    double time_seconds;
};

void render_scene_bounds(RenderScene*);
bool render_scene_validate(const RenderScene*);
bool render_scene_write(const RenderScene*,const char* path);

#endif
