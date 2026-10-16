#ifndef MICROSHIP_SIMULATION_H
#define MICROSHIP_SIMULATION_H

#include "world.h"
#include "sound_events.h"

enum { MAX_UNITS = 256, NO_BODY = -1, MAX_PROJECTILES=512, MAX_POPS=256, TRAIL_POINTS=8 };
enum { MAX_SPIKES=32, MAX_EMBEDDED=2, GROWTH_SEGMENTS=4,MAX_LIMB_DEBRIS=256 };
struct EmbeddedSpike { Vec3 direction,anchor; Faction faction; };
struct Unit
{
    Vec3 position;
    float health, landing;
    uint32_t type, generation;
    Faction faction;
    int32_t target;
    bool selected;
    bool hatched;
    bool moving;
    Vec3 destination;
    float flash, cooldown, orbit_phase;
    Vec3 velocity,forward;
    float bank,age;
    float spikes[MAX_SPIKES],damage_age,heal_timer;
    uint32_t wounds;
    EmbeddedSpike embedded[MAX_EMBEDDED];
    int32_t latch;
    uint32_t latch_generation,latch_structure,latch_root_generation;
    bool latch_colony;
    Vec3 latch_offset;
};
struct Trail { Vec3 points[TRAIL_POINTS]; uint32_t count; float timer; };
struct Projectile
{
    Vec3 position, previous;
    float age;
    bool active;
    int32_t target;
    uint32_t generation;
    Faction faction;
    bool colony;
    uint32_t structure, structure_generation;
    Vec3 direction;
    Vec3 anchor;
    float launch_scale;
};
struct Pop { Vec3 position; float age, duration,blast_radius; Faction faction; };
struct Impact { Vec3 position; float age,duration; Faction faction; };
struct FactoryStructure
{
    Vec3 offset;
    float health, growth,regrow_timer;
    uint32_t generation,wounds;
    EmbeddedSpike embedded[MAX_EMBEDDED];
};
struct Colony
{
    Vec3 surface;
    uint32_t generation;
    Faction faction;
    float health, growth, uptake, energy, manufacturing;
    uint32_t type,priority,building_kind;
    bool building, capacity_blocked;
    uint32_t wounds;
    EmbeddedSpike embedded[MAX_EMBEDDED];
    float branch_timer;
    FactoryStructure branches[MAX_FACTORY_STRUCTURES-1];
    Vec3 stems[MAX_FACTORY_STRUCTURES][GROWTH_SEGMENTS+1];
    uint32_t production_tip;
};
struct CellAppearance { Color tint;float strength,seconds; };
struct LimbDebris
{
    Vec3 start,end,velocity,axis;
    float radius,age,seconds,spin;
    uint32_t producer;
    Faction faction;
};
struct Simulation
{
    SoundEvents sounds;
    World world;
    uint32_t tick_count;
    double time_seconds;
    Unit units[MAX_UNITS];
    Trail trails[MAX_UNITS];
    Projectile projectiles[MAX_PROJECTILES];
    Pop pops[MAX_POPS];
    Impact impacts[MAX_POPS];
    Colony colonies[MAX_BODIES];
    CellAppearance cell_appearance[MAX_BODIES];
    LimbDebris debris[MAX_LIMB_DEBRIS];
    uint32_t limbs_broken,debris_replaced;
    Faction home_owner[MAX_BODIES];
    bool discovered[MAX_BODIES], visible_bodies[MAX_BODIES], visible_units[MAX_UNITS];
    uint32_t healed,regrown,detached;
    uint32_t births, losses, landings, blocked_landings, next_generation;
    int32_t selected_body;
    int32_t order_body;
    uint32_t order_tick,order_count;
    int32_t outcome; // 0 playing, 1 won, -1 lost.
    bool paused;
    char message[96];
    uint32_t shots, hits, deaths;
    uint32_t digestions,spore_bursts,digest_heals;
    uint32_t last_flash_tick,last_low_tick,last_death_tick;
};

bool simulation_initialize(Simulation* simulation, const char* world_path,uint32_t seed=0);
void simulation_step(Simulation* simulation);
void simulation_set_time(Simulation* simulation, double seconds);
void simulation_select(Simulation* simulation, int group); // Asset group ID; a negative value selects all.
void simulation_order(Simulation* simulation, uint32_t cell);
void simulation_move_to(Simulation* simulation, Vec3 destination);
void simulation_priority(Simulation* simulation,int group);
void simulation_priority_type(Simulation*,uint32_t type);
bool simulation_write_state(const Simulation* simulation, const char* path);
Vec3 simulation_spike_direction(uint32_t index,uint32_t count);
void simulation_unit_axes(const Unit*,Vec3* right,Vec3* up,Vec3* forward);
void simulation_producer_axes(const ProducerDefinition*,Vec3* right,Vec3* up,Vec3* normal);
Vec3 simulation_structure_position(const Simulation*,uint32_t cell,uint32_t structure);
Vec3 simulation_growth_point(const Simulation*,uint32_t cell,uint32_t structure,float growth);
bool simulation_in_sight(const Simulation*,Vec3 position,float radius);
bool simulation_body_known(const Simulation*,uint32_t cell);
void simulation_update_sight(Simulation*);

inline const UnitDefinition* simulation_unit_definition(const Simulation* s,const Unit* u)
{ return &s->world.content.units[u->type]; }
inline const ProducerDefinition* simulation_producer_definition(const Simulation* s,uint32_t body)
{ return &s->world.content.producers[s->colonies[body].type]; }

#endif
