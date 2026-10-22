#ifndef NANOVORE_WORLD_H
#define NANOVORE_WORLD_H

#include <stdint.h>
#include "types.h"
#include "content.h"
#include "asset_limits.h"
#include "communications.h"

struct Body
{
    uint32_t type;
    uint32_t seed;
    Faction owner;
    Vec3 position;
    float radius;
    Color color;
    char model_path[ASSET_PATH_CAPACITY];
    Vec3 colony_normal;
    Vec3 factory_root;
    Vec3 factory_roots[MAX_FACTORY_STRUCTURES];
    Vec3 factory_centers[MAX_FACTORY_STRUCTURES],factory_origin; // Transformed organelle centers and nucleus center.
    float factory_root_radii[MAX_FACTORY_STRUCTURES]; // Local major radii after startup part scaling.
    uint32_t factory_root_count;
    float factory_surface_radius; // Startup bound includes every faction wall shape.
    float availability;
    float interior_radius, interior_fade;
    Color interior_scattering;
    float interior_absorption, interior_anisotropy;
    float interior_blur_rate,interior_blur_sharp,interior_blur_max;
    bool interior_blur_fraction;
};

struct Spawn
{
    char type_name[ASSET_NAME_CAPACITY];
    uint32_t type;
    Vec3 position;
};
struct Background
{
    Vec3 position;
    float scale;
    Color color;
    char model_path[ASSET_PATH_CAPACITY];
};
enum { MAX_BODIES=CELL_STORAGE_CAPACITY,MAX_SPAWNS=64 };
struct GameplayRules
{
    bool enabled;
    float storage,manufacture_seconds,landing_seconds,enemy_attack_seconds;
    float cell_morph_seconds,cell_tint_seconds;
    uint32_t default_fighter_percent;bool production_ratio_default;
};
enum { COMBAT_FIELD_PROJECTILE=1, COMBAT_FIELD_ORBIT=2, COMBAT_FIELD_DAMAGE=4, COMBAT_FIELDS_COMPLETE=7, STUDIO_LIGHTS_COMPLETE=7 };
struct StudioLight { Vec3 direction; Color color; float irradiance; };
struct CombatVisuals
{
    bool enabled;
    float projectile_speed, shot_interval, trail_width, trail_lifetime,trail_length_scale;
    float flash_seconds, pop_seconds, pop_radius;
    float target_line_width,target_line_alpha,target_line_hold,target_line_fade;
    float orbit_clearance, orbit_speed;
    bool flight_enabled;
    float acceleration, turn_rate, separation, bank;
    float lookahead,obstacle_clearance,weave_radius,weave_height;
    Color flash, injured;
    float break_seconds,break_speed,break_drag,break_spin,break_shrink;
    Color cloud_color;float cloud_alpha; // Spore cloud glow; zero alpha draws none.
    Color haze_color;float haze_alpha,haze_fade_seconds;
    Color aura_color;float aura_alpha; // Healing aura glow around a unit after devouring.
};
struct SpikeRules
{
    bool enabled,recovery_set,injury_set;
    char model[ASSET_PATH_CAPACITY];
    uint32_t count;
    float root,length,regrow,scale,heal_interval,heal_delay,heal_clearance;
    Vec3 attached_scale; // Render-only stretch of the attached spike envelope.
    Color injury[2];
};

struct World
{
    Body bodies[MAX_BODIES];
    uint32_t body_count;
    Faction start_owner[MAX_BODIES];
    bool neutral_start,auto_colonize,colonized_starts;
    uint32_t active_factions;
    bool fully_established,rally_enabled;Vec3 rally_point;
    uint32_t production_ticks;float formation_spacing; // Production-only stress preparation.
    bool player_ai; // Enable AI control of the player's faction.
    Spawn spawns[MAX_SPAWNS];
    uint32_t spawn_count;
    Content content;
    char content_path[ASSET_PATH_CAPACITY],appearance_path[ASSET_PATH_CAPACITY];
    GameplayRules gameplay;
    CommunicationSettings communications;
    float sight_radius;
    Faction player_faction,opponent_faction; // Opponent is zero when starts list the sides.
    uint32_t layout_homes; // Leading home cells of a generated layout.
    bool ambient_enabled;
    Color ambient;
    Color ambient_top, ambient_bottom;
    bool ambient_gradient;
    Background backdrop; // Vertex-colored background art; not a gameplay cell.
    uint32_t backdrop_samples; // Explicit background budget; zero inherits the surface budget.
    bool backdrop_soft;
    float backdrop_blur, backdrop_glow, backdrop_ambient;
    float backdrop_pulse, backdrop_period;
    Vec3 backdrop_wave; // Ray-warp amplitude, spatial frequency, angular speed.
    bool backdrop_wave_set;
    bool atmosphere_enabled,atmosphere_shadows;
    Vec3 atmosphere_center;
    float atmosphere_radius,atmosphere_fade,atmosphere_absorption,atmosphere_phase;
    Color atmosphere_scattering;
    float fog_blur,fog_feather,fog_plane_z,fog_half_life;
    Color fog_tint;
    float glow_interior,glow_organelles,glow_stems,glow_leaves,glow_units;
    float factory_emission_color[4]; // RGB override; W enables it.
    float factory_emission_fresnel[4]; // Strength and power; remaining values reserved.
    float organelle_style[4]; // Edge saturation/width, center emission multiplier/power.
    float organelle_squash,organelle_period,membrane_ripple,membrane_wavelength,membrane_period,factory_sway,factory_period;
    StudioLight studio[3];
    uint32_t studio_mask;
    float thin_transmission, thin_absorption;
    bool thin_transmission_set;
    float thin_clarity,thin_edge_strength,thin_edge_power,thin_blur_radius,ai_production,ai_expansion;
    bool thin_clarity_set,ai_production_set;
    SpikeRules spikes;
    CombatVisuals combat;
    uint32_t combat_fields;
};

// A non-null player names the controlled faction instead of match.player.
bool world_load(World* world, const char* path,uint32_t seed,const char* player=0);
bool world_reload_appearance(World* output,const World* current,uint32_t seed);
void world_copy_appearance(World* output,const World* source);

#endif
