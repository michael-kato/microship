#ifndef MICROSHIP_SIMULATION_H
#define MICROSHIP_SIMULATION_H

#include "world.h"

enum UnitKind { UNIT_FIGHTER, UNIT_SEEDSHIP };
enum { MAX_UNITS = 256, NO_CELL = -1, MAX_PROJECTILES=512, MAX_POPS=256, TRAIL_POINTS=8 };
enum { MAX_SPIKES=32, MAX_EMBEDDED=2 };
struct EmbeddedSpike { Vec3 direction,anchor; Faction faction; };
struct Unit
{
    Vec3 position;
    float health, landing;
    uint32_t model, generation;
    Faction faction;
    UnitKind kind;
    int32_t target;
    bool selected;
    float flash, cooldown, orbit_phase;
    Vec3 velocity,forward;
    float bank,age;
    float spikes[MAX_SPIKES],damage_age,heal_timer;
    uint32_t wounds;
    EmbeddedSpike embedded[MAX_EMBEDDED];
};
struct Trail { Vec3 points[TRAIL_POINTS]; uint32_t count; float timer; };
struct Projectile
{
    Vec3 position, previous;
    float damage, age;
    int32_t target;
    uint32_t generation;
    Faction faction;
    bool colony;
    Vec3 direction;
    Vec3 anchor;
    float launch_scale;
};
struct Pop { Vec3 position; float age, duration; Faction faction; };
struct Impact { Vec3 position; float age,duration; Faction faction; };
struct Colony
{
    Vec3 surface;
    uint32_t generation;
    Faction faction;
    float health, growth, uptake, energy, manufacturing;
    UnitKind priority, building_kind;
    bool building, capacity_blocked;
    uint32_t wounds;
    EmbeddedSpike embedded[MAX_EMBEDDED];
};
struct Simulation
{
    World world;
    uint32_t tick_count;
    Vec3 sun_origin;
    double time_seconds;
    Unit units[MAX_UNITS];
    Trail trails[MAX_UNITS];
    Projectile projectiles[MAX_PROJECTILES];
    Pop pops[MAX_POPS];
    Impact impacts[MAX_POPS];
    Colony colonies[MAX_BODIES];
    Faction home_owner[MAX_BODIES];
    uint32_t healed,regrown,detached;
    uint32_t births, losses, landings, blocked_landings, next_generation;
    int32_t selected_cell;
    int32_t order_cell;
    uint32_t order_tick,order_count;
    int32_t outcome; // 0 playing, 1 won, -1 lost.
    bool paused;
    char message[96];
    uint32_t shots, hits, deaths;
    uint32_t last_flash_tick,last_low_tick,last_death_tick;
};

bool simulation_initialize(Simulation* simulation, const char* world_path);
void simulation_step(Simulation* simulation);
void simulation_set_time(Simulation* simulation, double seconds);
void simulation_select(Simulation* simulation, int group); // 0 fighters, 1 seedships, 2 all.
void simulation_order(Simulation* simulation, uint32_t planet);
void simulation_priority(Simulation* simulation, UnitKind kind);
bool simulation_write_state(const Simulation* simulation, const char* path);
Vec3 simulation_spike_direction(uint32_t index,uint32_t count);
void simulation_unit_axes(const Unit*,Vec3* right,Vec3* up,Vec3* forward);

#endif
