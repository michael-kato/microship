#ifndef NANOVA_SIMULATION_H
#define NANOVA_SIMULATION_H

#include "world.h"
#include "sight_field.h"
#include <stdio.h>
#include "sound_events.h"

enum { MAX_UNITS = UNIT_STORAGE_CAPACITY, NO_BODY = -1, MAX_PROJECTILES=512, MAX_POPS=256, TRAIL_POINTS=8 };
enum { SIMULATION_MESSAGE_CAPACITY=96 };
enum { UNIT_LAUNCH_SECONDS=2 };
enum { MAX_SPIKES=32, MAX_EMBEDDED=2, GROWTH_SEGMENTS=4,MAX_LIMB_DEBRIS=256 };
static const float SIMULATION_TICKS_PER_SECOND_FLOAT=(float)SIMULATION_TICKS_PER_SECOND;
static const double SIMULATION_TICKS_PER_SECOND_DOUBLE=(double)SIMULATION_TICKS_PER_SECOND;
struct EmbeddedSpike { Vec3 direction,anchor; Faction faction; };
// Live units bucketed by a clamped uniform grid over the world and by faction,
// rebuilt at phase boundaries. Keys are cell*faction_slots+slot; units keep index
// order within a key and queries visit keys in order, so results are deterministic.
enum { MAX_UNIT_GRID_KEYS=49152 };
struct UnitGrid
{
    Vec3 low;
    float inverse_cell;
    int32_t size[3];
    uint32_t slots,slot[MAX_FACTIONS],slot_faction[MAX_FACTIONS];
    uint32_t start[MAX_UNIT_GRID_KEYS+1],cursor[MAX_UNIT_GRID_KEYS];
    uint16_t units[MAX_UNITS];
};
// Short-range neighbors for separation: hashed cells as wide as the largest spacing.
enum { NEIGHBOR_BUCKETS=4096 };
struct NeighborGrid
{
    float inverse_cell;
    uint32_t start[NEIGHBOR_BUCKETS+1],cursor[NEIGHBOR_BUCKETS];
    uint16_t units[MAX_UNITS];
};
// Per-tick combat inputs, kept off the stack: one entry per unit, then per factory structure.
enum { COMBAT_TARGETS=MAX_UNITS+MAX_BODIES*MAX_FACTORY_STRUCTURES };
struct CombatScratch
{
    uint32_t hit_count[COMBAT_TARGETS],incoming[COMBAT_TARGETS];
    uint8_t landed_mask[COMBAT_TARGETS]; // Bit k: landed[target][k] was written this tick.
    bool digest_heal[MAX_UNITS],burst[MAX_UNITS],expired[MAX_UNITS],held[MAX_UNITS];
    uint16_t holder[MAX_UNITS]; // One more than the index of the unit holding this one; zero when free.
    EmbeddedSpike landed[COMBAT_TARGETS][2]; // Not cleared between ticks; see landed_mask.
};
enum UnitTask { TASK_PATROL,TASK_MOVE,TASK_ATTACK_UNIT,TASK_ATTACK_STRUCTURE,TASK_COLONIZE,TASK_LAUNCH };
struct Unit
{
    UnitTask task;
    int32_t attention;
    uint32_t attention_generation,attention_structure,attention_colony_generation;
    float response_timer;
    bool commanded;
    Vec3 flight_goal;
    Vec3 position;
    float health, landing;
    uint32_t type, generation;
    uint32_t path_marker_tick; // Selection/order feedback only.
    Faction faction;
    int32_t target;
    bool selected;
    bool hatched;
    bool moving;
    Vec3 destination;
    float flash, cooldown, orbit_phase;
    Vec3 velocity,forward;
    float bank,age;
    float spikes[MAX_SPIKES],payloads[MAX_UNIT_PAYLOADS],damage_age,heal_timer;
    uint32_t wounds;
    float haze_recovery; // Healing credit toward one whole wound.
    float exposure; // Toxic cloud damage that has not yet added up to a hit.
    float blob_cooldown,stun,aura; // Seconds: next blob, incapacitated by a blob, healing aura after devouring.
    EmbeddedSpike embedded[MAX_EMBEDDED];
    int32_t latch;
    uint32_t latch_generation,latch_structure,latch_root_generation,latch_hits;
    float latch_recovery;bool charging;
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
    uint32_t unit_type;
    bool payload;
    bool blob; float stun; // A blob incapacitates its unit target for stun seconds instead of wounding it.
};
struct Pop { Vec3 position; float age, duration,blast_radius; Faction faction; };
struct Impact { Vec3 position; float age,duration; Faction faction; };
// A burst spore's toxic cloud: damage per second to enemies inside, until its seconds pass.
struct SporeCloud { Vec3 position; float radius,age,seconds,damage; Faction faction; bool friendly_fire; };
enum { MAX_SPORE_CLOUDS=1024 };
struct FactoryStructure
{
    Vec3 offset;
    float health, growth,regrow_timer;
    uint32_t generation,wounds;
    float exposure;
    EmbeddedSpike embedded[MAX_EMBEDDED];
};
struct LeafProduction
{
    float progress;
    uint32_t type;
    bool active;
};
struct Colony
{
    Vec3 surface;
    uint32_t generation;
    Faction faction;
    float health, growth, uptake, energy, manufacturing;
    uint32_t type,priority,building_kind;
    uint32_t fighter_percent,production_error,produced[2],established_tick;
    bool ratio_enabled;
    bool building, capacity_blocked;
    bool active;
    float root_regrow_timer;
    uint32_t root_generation,root_count,large_root_count,structure_count;
    float factory_scale[MAX_FACTORY_STRUCTURES],limb_scale[MAX_FACTORY_STRUCTURES],deployment_start[MAX_FACTORY_STRUCTURES],deployment_seconds[MAX_FACTORY_STRUCTURES];
    int32_t parents[MAX_FACTORY_STRUCTURES];
    uint32_t root_attachments[MAX_FACTORY_STRUCTURES];
    uint32_t wounds;
    float exposure;
    EmbeddedSpike embedded[MAX_EMBEDDED];
    float branch_timer;
    float establishment_age;
    FactoryStructure branches[MAX_FACTORY_STRUCTURES-1];
    Vec3 stems[MAX_FACTORY_STRUCTURES][GROWTH_SEGMENTS+1];
    uint32_t production_tip,production_cursor;
    LeafProduction leaves[MAX_FACTORY_STRUCTURES];
    uint32_t leaf_births[MAX_FACTORY_STRUCTURES];
    float repair_credit[MAX_FACTORY_STRUCTURES],repair_delay[MAX_FACTORY_STRUCTURES];
    float repair_nutrients;bool repairing;
};
struct CellAppearance { Color tint;float strength,seconds;float factions[MAX_FACTIONS]; };
struct LimbDebris
{
    Vec3 start,end,velocity,axis;
    float radius,age,seconds,spin;
    uint32_t producer;
    Faction faction;
    bool tip;
};
struct Simulation
{
    SoundEvents sounds;
    World world;
    bool faction_established[MAX_FACTIONS];
    uint32_t tick_count;
    double time_seconds;
    Unit units[MAX_UNITS];
    Trail trails[MAX_UNITS];
    CombatScratch combat_scratch;
    UnitGrid grid; // Scratch spatial index; positions may move up to UNIT_GRID_MOTION_MARGIN after a build.
    NeighborGrid neighbors; // Scratch; built from the positions at the start of movement.
    SightField fog_memory;
    Projectile projectiles[MAX_PROJECTILES];
    Pop pops[MAX_POPS];
    Impact impacts[MAX_POPS];
    SporeCloud clouds[MAX_SPORE_CLOUDS]; // When full, a new cloud replaces the oldest slot in turn.
    uint32_t cloud_cursor;
    float haze_damage,haze_healing;
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
    struct FactionStats { uint32_t built,lost,limbs_lost,colonies_gained,colonies_lost,peak_units; } faction_stats[MAX_FACTIONS]; // Match diagnostics only.
    int32_t outcome; // 0 playing, 1 won, -1 lost.
    uint32_t outcome_tick; // Tick at which the outcome was decided.
    bool paused;
    char message[SIMULATION_MESSAGE_CAPACITY];
    uint32_t shots, hits, deaths;
    uint32_t avoidance_events,factory_contacts;
    uint32_t digestions,spore_bursts,digest_heals,captures,releases,blobs,devoured,aura_heals;
    uint32_t last_flash_tick,last_low_tick,last_death_tick;
};

bool simulation_attachment_valid(const Simulation*,const Unit*);
int32_t simulation_held_by(const Simulation*,uint32_t unit);
bool simulation_initialize(Simulation* simulation, const char* world_path,uint32_t seed=0);
enum SimulationPhase { SIM_AI,SIM_VISUALS,SIM_RECOVERY,SIM_MOVEMENT,SIM_COMBAT,
    SIM_GROWTH,SIM_SIGHT,SIM_APPEARANCE,SIM_FOG,SIM_PHASE_COUNT };
struct SimulationTimings { double ms[SIM_PHASE_COUNT]; };
// Optional phase times are collected only by real-application CPU benchmarks.
void simulation_step(Simulation* simulation,SimulationTimings* timings=0);
void simulation_set_time(Simulation* simulation, double seconds);
void simulation_select(Simulation* simulation, int group); // Asset group ID; a negative value selects all.
void simulation_order(Simulation* simulation, uint32_t cell);
void simulation_move_to(Simulation* simulation, Vec3 destination);
void simulation_priority(Simulation* simulation,int group);
bool simulation_ratio_available(const Simulation*,uint32_t cell);
void simulation_production_ratio(Simulation*,uint32_t fighter_percent);
void simulation_priority_type(Simulation*,uint32_t type);
bool simulation_write_state(const Simulation* simulation, const char* path);
// One JSON line: seed, outcome, blocked sites and per-faction totals, for balance runs.
void simulation_write_summary(const Simulation* simulation,uint32_t seed,FILE* file);
Vec3 simulation_spike_direction(uint32_t index,uint32_t count);
float simulation_unit_scale(const Simulation*,const Unit*);
Vec3 simulation_trail_origin(const Simulation*,const Unit*);
void simulation_unit_axes(const Unit*,Vec3* right,Vec3* up,Vec3* forward);
void simulation_producer_axes(const ProducerDefinition*,Vec3* right,Vec3* up,Vec3* normal);
Vec3 simulation_structure_position(const Simulation*,uint32_t cell,uint32_t structure);
Vec3 simulation_growth_point(const Simulation*,uint32_t cell,uint32_t structure,float growth);
float simulation_structure_scale(const Simulation*,uint32_t cell,uint32_t structure);
float simulation_structure_head_scale(const Simulation*,uint32_t cell,uint32_t structure);
bool simulation_structure_terminal(const Simulation*,uint32_t cell,uint32_t structure);
float simulation_structure_growth(const Simulation*,uint32_t cell,uint32_t structure);
float simulation_production(const Simulation*,uint32_t cell);
bool simulation_in_sight(const Simulation*,Vec3 position,float radius);
bool simulation_body_known(const Simulation*,uint32_t cell);
void simulation_update_sight(Simulation*);
void simulation_update_fog(Simulation*,float dt);

inline const UnitDefinition* simulation_unit_definition(const Simulation* s,const Unit* u)
{ return &s->world.content.units[u->type]; }
inline const ProducerDefinition* simulation_producer_definition(const Simulation* s,uint32_t body)
{ return &s->world.content.producers[s->colonies[body].type]; }

#endif
