#ifndef MICROSHIP_WORLD_H
#define MICROSHIP_WORLD_H

#include <stdint.h>
#include "types.h"

enum BodyKind
{
    BODY_CELL,
    BODY_ASTEROID
};

enum Faction
{
    FACTION_NEUTRAL,
    FACTION_PLAYER,
    FACTION_ENEMY,
    FACTION_FLOWERY = FACTION_PLAYER,
    FACTION_CRYSTALLINE = FACTION_ENEMY,
    FACTION_LIQUID_METAL,
    FACTION_INSECTOID,
    FACTION_ROCKY,
    FACTION_ROBOTIC
};
enum BiologySide { BIOLOGY_NONE, BIOLOGY_VIRUS, BIOLOGY_IMMUNE };

struct Cell
{
    BodyKind kind;
    Faction owner;
    Vec3 position;
    float radius;
    Color color;
    char model_path[256];
    float atmosphere_height;
    float atmosphere_scale_height;
    Color rayleigh;
    float mie;
    float absorption;
    float anisotropy;
    Vec3 colony_normal;
    float nutrients;
};

struct Star
{
    Vec3 position;
    float radius;
    Color color;
};

struct Ship
{
    Faction faction;
    bool large;
    Vec3 position;
    float scale;
    float yaw_degrees;
    Color color;
    char model_path[256];
};

enum { MAX_BODIES = 16, MAX_SHIPS = 64 };

struct GameplayRules
{
    bool enabled;
    float fighter_speed, fighter_health, fighter_damage, fighter_range, fighter_cost;
    float seed_speed, seed_health, seed_cost;
    float growth_seconds, stalk_health, collection_rate, stalk_height, stalk_width;
    float storage, manufacture_seconds, landing_seconds, enemy_attack_seconds;
    char stalk_model[256];
};
struct StudioLight { Vec3 direction; Color color; float irradiance; };
struct CombatVisuals
{
    bool enabled;
    float projectile_speed, shot_interval, trail_width, trail_lifetime;
    float flash_seconds, low_health, pop_seconds, pop_radius;
    float orbit_clearance, orbit_speed;
    bool flight_enabled;
    float acceleration, turn_rate, separation, bank;
    Color faction[3], flash, injured;
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
    Star sun;
    Cell bodies[MAX_BODIES];
    uint32_t body_count;
    Ship ships[MAX_SHIPS];
    uint32_t ship_count;
    Vec3 orbit_center;
    Vec3 orbit_axis;
    float orbit_period;
    Vec3 haze_center;
    float haze_radius;
    Color haze_scattering;
    float haze_anisotropy;
    float haze_edge_fade; // Fraction of the radius occupied by the outer fade; zero is uniform.
    Vec3 global_atmosphere_center;
    float global_atmosphere_radius;
    Color global_atmosphere_scattering;
    float global_atmosphere_anisotropy;
    float global_atmosphere_edge_fade;
    GameplayRules gameplay;
    BiologySide biology;
    Faction player_faction;
    bool ambient_enabled;
    Color ambient;
    Color ambient_top, ambient_bottom;
    bool ambient_gradient;
    Ship backdrop; // Vertex-colored background art; not a gameplay cell.
    uint32_t backdrop_samples; // Explicit background budget; zero inherits the surface budget.
    bool backdrop_soft;
    float backdrop_blur, backdrop_glow, backdrop_ambient;
    float backdrop_pulse, backdrop_period;
    Vec3 backdrop_wave; // Ray-warp amplitude, spatial frequency, angular speed.
    bool backdrop_wave_set;
    StudioLight studio[3];
    uint32_t studio_mask;
    float cell_transmission, cell_absorption;
    bool cell_transmission_set;
    float cell_clarity,ai_production,ai_expansion;
    bool cell_clarity_set,ai_production_set;
    SpikeRules spikes;
    CombatVisuals combat;
    uint32_t combat_fields;
};

bool world_load(World* world, const char* path);

#endif
