#ifndef MICROSHIP_WORLD_H
#define MICROSHIP_WORLD_H

#include <stdint.h>
#include "types.h"
#include "content.h"

struct Body
{
    uint32_t type;
    uint32_t seed;
    Faction owner;
    Vec3 position;
    float radius;
    Color color;
    char model_path[256];
    Vec3 colony_normal;
    Vec3 factory_root;
    Vec3 factory_roots[MAX_FACTORY_STRUCTURES];
    uint32_t factory_root_count;
    float availability;
    float interior_radius, interior_fade;
    Color interior_scattering;
    float interior_absorption, interior_anisotropy;
};

struct Spawn
{
    char type_name[32];
    uint32_t type;
    Vec3 position;
};
struct Background
{
    Vec3 position;
    float scale;
    Color color;
    char model_path[256];
};
enum { MAX_BODIES=16,MAX_SPAWNS=64 };
struct GameplayRules
{
    bool enabled;
    float storage,manufacture_seconds,landing_seconds,enemy_attack_seconds;
};
struct StudioLight { Vec3 direction; Color color; float irradiance; };
struct CombatVisuals
{
    bool enabled;
    float projectile_speed, shot_interval, trail_width, trail_lifetime;
    float flash_seconds, pop_seconds, pop_radius;
    float orbit_clearance, orbit_speed;
    bool flight_enabled;
    float acceleration, turn_rate, separation, bank;
    Color flash, injured;
    float break_seconds,break_speed,break_drag,break_spin,break_shrink;
};
struct SpikeRules
{
    bool enabled,recovery_set,injury_set;
    char model[256];
    uint32_t count;
    float root,length,regrow,scale,heal_interval,heal_delay,heal_clearance;
    Color injury[2];
};

struct World
{
    Body bodies[MAX_BODIES];
    uint32_t body_count;
    Spawn spawns[MAX_SPAWNS];
    uint32_t spawn_count;
    Content content;
    char content_path[256];
    GameplayRules gameplay;
    float sight_radius;
    Faction player_faction,opponent_faction;
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
    StudioLight studio[3];
    uint32_t studio_mask;
    float thin_transmission, thin_absorption;
    bool thin_transmission_set;
    float thin_clarity,ai_production,ai_expansion;
    bool thin_clarity_set,ai_production_set;
    SpikeRules spikes;
    CombatVisuals combat;
    uint32_t combat_fields;
};

bool world_load(World* world, const char* path);

#endif
