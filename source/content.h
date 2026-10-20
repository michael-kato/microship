#ifndef NANOVA_CONTENT_H
#define NANOVA_CONTENT_H
#include "types.h"
#include <stdint.h>
#include "asset_limits.h"

typedef uint32_t Faction;
enum { FACTION_NEUTRAL=0, MAX_FACTIONS=9, MAX_GROUPS=8, MAX_UNIT_TYPES=32, MAX_PRODUCER_TYPES=8, MAX_BODY_TYPES=32, MAX_FACTORY_STRUCTURES=128 };
enum { MAX_UNIT_PAYLOADS=6, MAX_HIT_COUNT=32, BRANCH_FIELDS_COMPLETE=3 };
enum { MAX_CELL_DISPLACEMENT_FEATURES=256 };
enum GrowthGrammar { GROWTH_AXIAL,GROWTH_FAN,GROWTH_HYPHA,GROWTH_TREE };
enum { TARGET_UNITS=1, TARGET_STRUCTURES=2 };
enum AttackMode { ATTACK_PROJECTILE,ATTACK_DIGEST,ATTACK_SPORE };
struct GroupDefinition { bool declared;char key[ASSET_NAME_CAPACITY],selection[ASSET_NAME_CAPACITY],singular[ASSET_NAME_CAPACITY],plural[ASSET_NAME_CAPACITY]; };
struct FactionDefinition
{
    bool declared;
    char name[ASSET_NAME_CAPACITY],label[ASSET_NAME_CAPACITY],producer_name[ASSET_NAME_CAPACITY],default_name[ASSET_NAME_CAPACITY],assault_name[ASSET_NAME_CAPACITY];
    Color color;
    uint32_t producer,default_unit,assault_unit;
    bool automatic;
    Color cell_tint;
    float cell_strength,transform_seconds,morph_seconds,cell_roughness;
    bool cell_material;
    uint32_t displacement_features,displacement_seed;
    float displacement_height,displacement_width;
    bool displacement_pointed;
};
struct UnitDefinition
{
    char name[ASSET_NAME_CAPACITY],faction_name[ASSET_NAME_CAPACITY],group_name[ASSET_NAME_CAPACITY],model_path[ASSET_PATH_CAPACITY];
    Faction faction;
    uint32_t group,targets,secondary_targets,hits;
    float secondary_interval;
    bool colonizes,detaches,trails;
    Vec3 trail_origin;
    char payload_model[ASSET_PATH_CAPACITY];
    Vec3 payload_sockets[MAX_UNIT_PAYLOADS];
    uint32_t payload_count;
    float payload_reload;
    float scale,yaw_degrees;
    Color color;
    float speed,health,range,cost,shot_interval,separation,pick_radius,cue_radius;
    float unit_radius,structure_radius,colonize_radius,leash_radius,response_seconds,cruise_fraction;
    AttackMode attack;
    float lifetime; // Spores expire after this many seconds; zero keeps them.
    float burst_damage; // A bursting spore's damage to the target it touched.
    float cloud_radius,cloud_seconds,cloud_damage; // A bursting spore leaves this toxic cloud; damage per second.
    uint32_t brood; // Units hatched by one manufacture job.
    float charge_radius,charge_speed,charge_acceleration,release_seconds;
    uint32_t release_hits;
    Color rear_tint;float rear_tint_strength;
    bool friendly_fire;
};
struct BranchRules
{
    GrowthGrammar grammar;
    bool organelle_roots;
    uint32_t fields,count,seed;
    uint32_t hits;
    float health;
    float seconds,spacing,production,regrow_seconds;
    Vec3 normal;
    uint32_t children,levels,root_limit;
    float root_minimum_radius,branch_probability,angle_jitter,length_jitter,width_jitter;
    float length_scale,width_scale,angle,bend,root_offset_seconds;
};
struct ProducerDefinition
{
    char name[ASSET_NAME_CAPACITY],faction_name[ASSET_NAME_CAPACITY],model_path[ASSET_PATH_CAPACITY];
    char segment_path[ASSET_PATH_CAPACITY],tip_path[ASSET_PATH_CAPACITY];
    Faction faction;
    float growth_seconds,sprout_seconds,establishment_seconds,health,collection,height,width;
    float stem_radius,head_radius;
    Vec3 growth_plane_normal;
    float growth_depth_ratio;
    bool growth_plane_enabled;
    uint32_t hits;
    BranchRules branches;
};
struct BodyDefinition
{
    char name[ASSET_NAME_CAPACITY],label[ASSET_NAME_CAPACITY],model_path[ASSET_PATH_CAPACITY];
    char parts_path[ASSET_PATH_CAPACITY];
    float shape_variation,part_variation;
    float wall_noise_strength,wall_noise_frequency;
    Color wall_noise_warm,wall_noise_cool;
    bool hosts_site;
};
struct Content
{
    BodyDefinition bodies[MAX_BODY_TYPES];
    uint32_t body_count;
    char resource_label[ASSET_NAME_CAPACITY];
    FactionDefinition factions[MAX_FACTIONS];
    GroupDefinition groups[MAX_GROUPS];
    UnitDefinition units[MAX_UNIT_TYPES];
    ProducerDefinition producers[MAX_PRODUCER_TYPES];
    uint32_t unit_count,producer_count;
    float max_unit_range,max_unit_separation; // Largest values over all unit types, for neighbor queries.
};
int content_faction(const Content*,const char*);
int content_group(const Content*,const char*);
int content_unit(const Content*,const char*);
int content_body(const Content*,const char*);
int content_product(const Content*,Faction,int group);
int content_producer(const Content*,const char*);
Color content_color(const Content*,Faction);
bool content_load(Content*,const char* path);
bool content_finish(Content*,const char* path);
#endif
