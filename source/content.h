#ifndef MICROSHIP_CONTENT_H
#define MICROSHIP_CONTENT_H
#include "types.h"
#include <stdint.h>

typedef uint32_t Faction;
enum { FACTION_NEUTRAL=0, MAX_FACTIONS=9, MAX_GROUPS=8, MAX_UNIT_TYPES=32, MAX_PRODUCER_TYPES=8, MAX_BODY_TYPES=32, MAX_FACTORY_STRUCTURES=9 };
enum { TARGET_UNITS=1, TARGET_STRUCTURES=2 };
struct GroupDefinition { bool declared;char key[32],selection[32],singular[32],plural[32]; };
struct FactionDefinition
{
    bool declared;
    char name[32],label[32],producer_name[32],default_name[32],assault_name[32];
    Color color;
    uint32_t producer,default_unit,assault_unit;
};
struct UnitDefinition
{
    char name[32],faction_name[32],group_name[32],model_path[256];
    Faction faction;
    uint32_t group,targets,hits;
    bool colonizes,detaches,trails;
    float scale,yaw_degrees;
    Color color;
    float speed,health,range,cost,shot_interval,separation,pick_radius,cue_radius;
};
struct BranchRules
{
    uint32_t fields,count,seed;
    float seconds,spacing,production;
    Vec3 normal;
};
struct ProducerDefinition
{
    char name[32],faction_name[32],model_path[256];
    Faction faction;
    float growth_seconds,health,collection,height,width;
    uint32_t hits;
    BranchRules branches;
};
struct BodyDefinition
{
    char name[32],label[32],model_path[256];
    bool hosts_site;
};
struct Content
{
    BodyDefinition bodies[MAX_BODY_TYPES];
    uint32_t body_count;
    char resource_label[32];
    FactionDefinition factions[MAX_FACTIONS];
    GroupDefinition groups[MAX_GROUPS];
    UnitDefinition units[MAX_UNIT_TYPES];
    ProducerDefinition producers[MAX_PRODUCER_TYPES];
    uint32_t unit_count,producer_count;
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
