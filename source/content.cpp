#include "content.h"
#include "asset_json.h"
#include "vector_math.h"
#include <string.h>
#include <stdio.h>
#include <cmath>
#include "asset_limits.h"

static const int MAX_BUILDING_SEED = 16777215;
static const int MAX_BUILDING_PRODUCTION = 10;
static const int MAX_BUILDING_SPACING = 10;
static const int MAX_BRANCH_EMERGENCE_SECONDS = 300;
static const int MAX_BUILDING_WIDTH = 10000;
static const int MAX_BUILDING_HEIGHT = 10000;
static const int MAX_BUILDING_COLLECTION = 10000;
static const int MAX_BUILDING_HEALTH = 10000;
static const int MAX_BUILDING_GROWTH_SECONDS = 10000;
static const int MAX_BUILDING_HEAD_RADIUS = 10;
static const int MAX_BUILDING_STEM_RADIUS = 10;
static const int MAX_UNIT_TARGETS = 2;
static const int MAX_UNIT_INTERVAL_FACTOR = 100;
static const int MAX_UNIT_CUE_RADIUS = 1000;
static const int MAX_UNIT_PICK_RADIUS = 1000;
static const int MAX_UNIT_SEPARATION = 10000;
static const int MAX_UNIT_SHOT_INTERVAL = 10000;
static const int MAX_UNIT_COST = 10000;
static const int MAX_UNIT_RESPONSE_SECONDS = 10;
static const int MAX_UNIT_LEASH_RADIUS = 100;
static const int MAX_UNIT_COLONIZE_RADIUS = 100;
static const int MAX_UNIT_STRUCTURE_RADIUS = 100;
static const int MAX_UNIT_TARGET_RADIUS = 100;
static const int MAX_UNIT_RANGE = 10000;
static const int MAX_UNIT_HEALTH = 10000;
static const int MAX_UNIT_SPEED = 10000;
static const int MAX_UNIT_YAW_DEGREES = 360000;
static const int MAX_UNIT_SCALE = 1000;
static const int MAX_UNIT_LIFETIME = 300;
static const int MAX_UNIT_BLAST_RADIUS = 100;
static const int MAX_UNIT_SEEK_RADIUS = 10000;
static const int MAX_CELL_TINT_SECONDS = 3600;
static const double MAX_CELL_INTERIOR_VARIATION = .15;
static const double MAX_CELL_SHAPE_VARIATION = .12;
static const int MAX_NORMAL_COMPONENT = 100;
static const float MIN_NORMAL_SQUARED_LENGTH = .01f;
static const int MAX_NORMAL_SQUARED_LENGTH = 10000;
static const int MAX_ESTABLISHMENT_SECONDS = 10000;
static const int MAX_BRANCH_HEALTH = 10000;
static const int MAX_BRANCH_REGROW_SECONDS = 300;

int content_faction(const Content* c,const char* name)
{
    if (!name) return -1;
    if (!strcmp(name,"neutral")) return 0;
    for (uint32_t i=1;i<MAX_FACTIONS;++i) if (c->factions[i].declared && !strcmp(c->factions[i].name,name)) return (int)i;
    return -1;
}
int content_group(const Content* c,const char* name)
{
    if (!name) return -1;
    for (uint32_t i=0;i<MAX_GROUPS;++i) if (c->groups[i].declared
        && (!strcmp(c->groups[i].key,name) || !strcmp(c->groups[i].selection,name))) return (int)i;
    return -1;
}
int content_unit(const Content* c,const char* name)
{
    for (uint32_t i=0;name && i<c->unit_count;++i) if (!strcmp(c->units[i].name,name)) return (int)i;
    return -1;
}
int content_body(const Content* c,const char* name)
{
    for (uint32_t i=0;name && i<c->body_count;++i) if (!strcmp(c->bodies[i].name,name)) return (int)i;
    return -1;
}
int content_product(const Content* c,Faction faction,int group)
{
    const FactionDefinition* f=&c->factions[faction];
    if (f->producer_name[0] && (int)c->units[f->default_unit].group==group) return (int)f->default_unit;
    if (f->producer_name[0] && (int)c->units[f->assault_unit].group==group) return (int)f->assault_unit;
    return -1;
}
int content_producer(const Content* c,const char* name)
{
    for (uint32_t i=0;name && i<c->producer_count;++i) if (!strcmp(c->producers[i].name,name)) return (int)i;
    return -1;
}
Color content_color(const Content* c,Faction faction) { return c->factions[faction].color; }
static bool definition_name(Json j,char* name)
{
    return json_string(json_get(j,"name"),name,ASSET_NAME_CAPACITY);
}
static Vec3 normal(Json j)
{
    Vec3 n=json_vector(j,-MAX_NORMAL_COMPONENT,MAX_NORMAL_COMPONENT);float length=dot(n,n);
    if (length<MIN_NORMAL_SQUARED_LENGTH || length>MAX_NORMAL_SQUARED_LENGTH) json_error(j,"normal must have squared length in [.01,10000]");
    return length>0 ? normalize(n) : n;
}
static bool load_bodies(Content* c,Json root,const char* path)
{
    json_object(root,"$schema|bodies");JsonArray a=json_array(json_get(root,"bodies"),MAX_BODY_TYPES);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"name|label|geometry|model|hosts_site|variation");BodyDefinition b={};char geometry[ASSET_NAME_CAPACITY]={};
        definition_name(j,b.name);json_string(json_get(j,"label"),b.label,ASSET_NAME_CAPACITY);
        json_string(json_get(j,"geometry"),geometry,ASSET_NAME_CAPACITY);b.hosts_site=json_bool(json_get(j,"hosts_site"));
        if (!strcmp(geometry,"mesh")) json_path(json_get(j,"model"),path,b.model_path);
        else if (strcmp(geometry,"sphere") || json_present(json_find(j,"model"))) json_error(j,"geometry must be mesh with a model, or sphere without a model");
        Json variation=json_find(j,"variation");
        if (json_present(variation))
        {
            json_object(variation,"parts|shape|interior");
            json_path(json_get(variation,"parts"),path,b.parts_path);
            b.shape_variation=json_float(json_get(variation,"shape"),0,MAX_CELL_SHAPE_VARIATION);
            b.part_variation=json_float(json_get(variation,"interior"),0,MAX_CELL_INTERIOR_VARIATION);
            if (!b.model_path[0]) json_error(variation,"variation requires a mesh");
        }
        if (content_body(c,b.name)>=0) return json_error(j,"duplicate body definition");
        c->bodies[c->body_count++]=b;
    }
    return root.document->valid;
}
static bool load_factions(Content* c,Json root)
{
    json_object(root,"$schema|factions");JsonArray a=json_array(json_get(root,"factions"),MAX_FACTIONS-1);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"id|name|label|color|production|cell_appearance");FactionDefinition f={};
        Json appearance=json_find(j,"cell_appearance");
        if (json_present(appearance))
        {
            json_object(appearance,"tint|strength|seconds");f.cell_tint=json_color(json_get(appearance,"tint"));
            f.cell_strength=json_float(json_get(appearance,"strength"),0,1);f.transform_seconds=json_positive(json_get(appearance,"seconds"),MAX_CELL_TINT_SECONDS);
        }
        uint32_t id=json_uint(json_get(j,"id"),1,MAX_FACTIONS-1);
        definition_name(j,f.name);json_string(json_get(j,"label"),f.label,ASSET_NAME_CAPACITY);f.color=json_color(json_get(j,"color"));
        if (c->factions[id].declared || content_faction(c,f.name)>=0) return json_error(j,"duplicate or reserved faction ID or name");
        Json p=json_find(j,"production");
        if (json_present(p))
        {
            json_object(p,"building|default_unit|assault_unit|mode");
            Json mode=json_find(p,"mode");if (json_present(mode))
            {
                char value[ASSET_NAME_CAPACITY]={};json_string(mode,value,ASSET_NAME_CAPACITY);
                if (!strcmp(value,"spores")) f.automatic=true;
                else if (strcmp(value,"manufacture")) json_error(mode,"production mode must be manufacture or spores");
            }
            json_string(json_get(p,"building"),f.producer_name,ASSET_NAME_CAPACITY);
            json_string(json_get(p,"default_unit"),f.default_name,ASSET_NAME_CAPACITY);
            json_string(json_get(p,"assault_unit"),f.assault_name,ASSET_NAME_CAPACITY);
        }
        f.declared=true;c->factions[id]=f;
    }
    return root.document->valid;
}
static bool load_groups(Content* c,Json root)
{
    json_object(root,"$schema|groups");JsonArray a=json_array(json_get(root,"groups"),MAX_GROUPS);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"id|key|selection|singular|plural");GroupDefinition g={};
        uint32_t id=json_uint(json_get(j,"id"),0,MAX_GROUPS-1);
        json_string(json_get(j,"key"),g.key,ASSET_NAME_CAPACITY);json_string(json_get(j,"selection"),g.selection,ASSET_NAME_CAPACITY);
        json_string(json_get(j,"singular"),g.singular,ASSET_NAME_CAPACITY);json_string(json_get(j,"plural"),g.plural,ASSET_NAME_CAPACITY);
        if (c->groups[id].declared || !strcmp(g.key,"all") || !strcmp(g.selection,"all")
            || content_group(c,g.key)>=0 || content_group(c,g.selection)>=0) return json_error(j,"duplicate or reserved group ID or name");
        g.declared=true;c->groups[id]=g;
    }
    return root.document->valid;
}
static bool load_units(Content* c,Json root,const char* path)
{
    json_object(root,"$schema|units");JsonArray a=json_array(json_get(root,"units"),MAX_UNIT_TYPES);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"name|faction|group|model|scale|yaw_degrees|tint|speed|health|range|cost|shot_interval|separation|targets|colonizes|detaches|trails|pick_radius|cue_radius|hits|attack|secondary_attack|autonomy");
        UnitDefinition u={};definition_name(j,u.name);
        Json attack=json_find(j,"attack");
        if (json_present(attack))
        {
            json_object(attack,"mode|seek_radius|blast_radius|lifetime|friendly_fire");char mode[ASSET_NAME_CAPACITY]={};json_string(json_get(attack,"mode"),mode,ASSET_NAME_CAPACITY);
            if (!strcmp(mode,"digest")) u.attack=ATTACK_DIGEST;
            else if (!strcmp(mode,"spore"))
            {
                u.attack=ATTACK_SPORE;u.seek_radius=json_positive(json_get(attack,"seek_radius"),MAX_UNIT_SEEK_RADIUS);
                u.blast_radius=json_positive(json_get(attack,"blast_radius"),MAX_UNIT_BLAST_RADIUS);u.lifetime=json_positive(json_get(attack,"lifetime"),MAX_UNIT_LIFETIME);
                u.friendly_fire=json_bool(json_get(attack,"friendly_fire"));
            }
            else if (strcmp(mode,"projectile")) json_error(attack,"unknown attack mode");
            if (u.attack!=ATTACK_SPORE && (json_present(json_find(attack,"seek_radius")) || json_present(json_find(attack,"blast_radius")) || json_present(json_find(attack,"lifetime")) || json_present(json_find(attack,"friendly_fire"))))
                json_error(attack,"spore parameters require spore mode");
        }
        json_string(json_get(j,"faction"),u.faction_name,ASSET_NAME_CAPACITY);json_string(json_get(j,"group"),u.group_name,ASSET_NAME_CAPACITY);
        json_path(json_get(j,"model"),path,u.model_path);
        u.scale=json_positive(json_get(j,"scale"),MAX_UNIT_SCALE);u.yaw_degrees=json_float(json_get(j,"yaw_degrees"),-MAX_UNIT_YAW_DEGREES,MAX_UNIT_YAW_DEGREES);
        u.color=json_color(json_get(j,"tint"));
        u.speed=json_positive(json_get(j,"speed"),MAX_UNIT_SPEED);
        u.health=json_positive(json_get(j,"health"),MAX_UNIT_HEALTH);
        u.range=json_positive(json_get(j,"range"),MAX_UNIT_RANGE);
        u.unit_radius=u.structure_radius=u.range;u.cruise_fraction=1;
        Json autonomy=json_find(j,"autonomy");
        if (json_present(autonomy))
        {
            json_object(autonomy,"unit_radius|structure_radius|colonize_radius|leash_radius|response_seconds|cruise_fraction");
            u.unit_radius=json_float(json_get(autonomy,"unit_radius"),0,MAX_UNIT_TARGET_RADIUS);
            u.structure_radius=json_float(json_get(autonomy,"structure_radius"),0,MAX_UNIT_STRUCTURE_RADIUS);
            u.colonize_radius=json_float(json_get(autonomy,"colonize_radius"),0,MAX_UNIT_COLONIZE_RADIUS);
            u.leash_radius=json_positive(json_get(autonomy,"leash_radius"),MAX_UNIT_LEASH_RADIUS);
            u.response_seconds=json_float(json_get(autonomy,"response_seconds"),0,MAX_UNIT_RESPONSE_SECONDS);
            u.cruise_fraction=json_positive(json_get(autonomy,"cruise_fraction"),1);
        }
        u.cost=json_positive(json_get(j,"cost"),MAX_UNIT_COST);
        u.shot_interval=json_positive(json_get(j,"shot_interval"),MAX_UNIT_SHOT_INTERVAL);
        u.separation=json_positive(json_get(j,"separation"),MAX_UNIT_SEPARATION);
        u.pick_radius=json_positive(json_get(j,"pick_radius"),MAX_UNIT_PICK_RADIUS);
        u.cue_radius=json_positive(json_get(j,"cue_radius"),MAX_UNIT_CUE_RADIUS);
        u.hits=json_uint(json_get(j,"hits"),1,MAX_HIT_COUNT);
        u.colonizes=json_bool(json_get(j,"colonizes"));u.detaches=json_bool(json_get(j,"detaches"));u.trails=json_bool(json_get(j,"trails"));
        JsonArray targets=json_array(json_get(j,"targets"),MAX_UNIT_TARGETS);Json target={};
        while (json_next(&targets,&target))
        {
            char name[ASSET_NAME_CAPACITY]={};json_string(target,name,ASSET_NAME_CAPACITY);
            uint32_t bit=!strcmp(name,"units") ? TARGET_UNITS : !strcmp(name,"structures") ? TARGET_STRUCTURES : 0;
            if (!bit || (u.targets&bit)) return json_error(target,"unknown or duplicate target category");
            u.targets|=bit;
        }
        Json secondary=json_find(j,"secondary_attack");u.secondary_interval=1;
        if (json_present(secondary))
        {
            json_object(secondary,"targets|interval_factor");u.secondary_interval=json_float(json_get(secondary,"interval_factor"),1,MAX_UNIT_INTERVAL_FACTOR);
            JsonArray a=json_array(json_get(secondary,"targets"),MAX_UNIT_TARGETS);
            while (json_next(&a,&target))
            {
                char name[ASSET_NAME_CAPACITY]={};json_string(target,name,ASSET_NAME_CAPACITY);
                uint32_t bit=!strcmp(name,"units") ? TARGET_UNITS : !strcmp(name,"structures") ? TARGET_STRUCTURES : 0;
                if (!bit || (bit&(u.targets|u.secondary_targets))) return json_error(target,"secondary targets must be distinct from primary targets");
                u.secondary_targets|=bit;
            }
        }
        if (content_unit(c,u.name)>=0) return json_error(j,"duplicate unit definition");
        c->units[c->unit_count++]=u;
    }
    return root.document->valid;
}
static bool load_buildings(Content* c,Json root,const char* path)
{
    json_object(root,"$schema|buildings");JsonArray a=json_array(json_get(root,"buildings"),MAX_PRODUCER_TYPES);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"name|faction|model|growth_seconds|establishment_seconds|health|collection|height|width|hits|branches|growth_meshes|navigation|growth_plane");ProducerDefinition p={};
        Json plane=json_find(j,"growth_plane");
        if (json_present(plane))
        {
            json_object(plane,"normal|depth_ratio");p.growth_plane_normal=normal(json_get(plane,"normal"));
            p.growth_depth_ratio=json_float(json_get(plane,"depth_ratio"),0,1);p.growth_plane_enabled=true;
        }
        Json navigation=json_find(j,"navigation");
        if (json_present(navigation))
        {
            json_object(navigation,"stem_radius|head_radius");
            p.stem_radius=json_positive(json_get(navigation,"stem_radius"),MAX_BUILDING_STEM_RADIUS);
            p.head_radius=json_positive(json_get(navigation,"head_radius"),MAX_BUILDING_HEAD_RADIUS);
        }
        Json growth=json_find(j,"growth_meshes");
        if (json_present(growth))
        {
            json_object(growth,"segment|tip");json_path(json_get(growth,"segment"),path,p.segment_path);
            json_path(json_get(growth,"tip"),path,p.tip_path);
        }
        definition_name(j,p.name);json_string(json_get(j,"faction"),p.faction_name,ASSET_NAME_CAPACITY);json_path(json_get(j,"model"),path,p.model_path);
        p.growth_seconds=json_positive(json_get(j,"growth_seconds"),MAX_BUILDING_GROWTH_SECONDS);
        Json establishment=json_find(j,"establishment_seconds");
        if (json_present(establishment)) p.establishment_seconds=json_positive(establishment,MAX_ESTABLISHMENT_SECONDS);
        p.health=json_positive(json_get(j,"health"),MAX_BUILDING_HEALTH);
        p.collection=json_positive(json_get(j,"collection"),MAX_BUILDING_COLLECTION);
        p.height=json_positive(json_get(j,"height"),MAX_BUILDING_HEIGHT);
        p.width=json_positive(json_get(j,"width"),MAX_BUILDING_WIDTH);
        p.hits=json_uint(json_get(j,"hits"),1,MAX_HIT_COUNT);Json b=json_get(j,"branches");
        json_object(b,"seconds|count|spacing|production|seed|normal|health|hits|regrow_seconds|roots|grammar");
        char roots[ASSET_NAME_CAPACITY]={},grammar[ASSET_NAME_CAPACITY]={};
        Json roots_json=json_find(b,"roots"),grammar_json=json_find(b,"grammar");
        if (json_present(roots_json))
        {
            json_string(roots_json,roots,ASSET_NAME_CAPACITY);
            p.branches.organelle_roots=!strcmp(roots,"organelles");
            if (!p.branches.organelle_roots && strcmp(roots,"single")) json_error(roots_json,"roots must be organelles or single");
        }
        if (json_present(grammar_json))
        {
            json_string(grammar_json,grammar,ASSET_NAME_CAPACITY);
            if (!strcmp(grammar,"fan")) p.branches.grammar=GROWTH_FAN;
            else if (!strcmp(grammar,"hypha")) p.branches.grammar=GROWTH_HYPHA;
            else if (strcmp(grammar,"axial")) json_error(grammar_json,"grammar must be axial, fan or hypha");
        }
        Json branch_health=json_find(b,"health"),branch_hits=json_find(b,"hits");
        p.branches.health=json_present(branch_health) ? json_positive(branch_health,MAX_BRANCH_HEALTH) : p.health;
        p.branches.hits=json_present(branch_hits) ? json_uint(branch_hits,1,MAX_HIT_COUNT) : p.hits;
        p.branches.seconds=json_positive(json_get(b,"seconds"),MAX_BRANCH_EMERGENCE_SECONDS);
        Json regrow=json_find(b,"regrow_seconds");p.branches.regrow_seconds=json_present(regrow) ? json_positive(regrow,MAX_BRANCH_REGROW_SECONDS) : p.branches.seconds;
        p.branches.count=json_uint(json_get(b,"count"),1,MAX_FACTORY_STRUCTURES);
        p.branches.spacing=json_positive(json_get(b,"spacing"),MAX_BUILDING_SPACING);
        p.branches.production=json_positive(json_get(b,"production"),MAX_BUILDING_PRODUCTION);
        p.branches.seed=json_uint(json_get(b,"seed"),0,MAX_BUILDING_SEED);
        p.branches.normal=normal(json_get(b,"normal"));p.branches.fields=BRANCH_FIELDS_COMPLETE;
        if (content_producer(c,p.name)>=0) return json_error(j,"duplicate building definition");
        c->producers[c->producer_count++]=p;
    }
    return root.document->valid;
}
bool content_load(Content* content,const char* path)
{
    JsonDocument manifest;if (!json_load(&manifest,path)) return false;
    Json root=json_root(&manifest);json_object(root,"$schema|bodies|factions|groups|units|buildings");
    const char* keys[]={"bodies","factions","groups","units","buildings"};
    for (uint32_t i=0;i<sizeof(keys)/sizeof(keys[0]) && manifest.valid;++i)
    {
        char file[ASSET_PATH_CAPACITY]={};if (!json_path(json_get(root,keys[i]),path,file)) return false;
        JsonDocument document;if (!json_load(&document,file)) return false;Json data=json_root(&document);
        bool valid=i==0 ? load_bodies(content,data,file) : i==1 ? load_factions(content,data) : i==2 ? load_groups(content,data)
            : i==3 ? load_units(content,data,file) : load_buildings(content,data,file);
        if (!valid) return false;
    }
    return manifest.valid && content_finish(content,path);
}

bool content_finish(Content* c,const char* path)
{
    bool valid=true;
    for (uint32_t i=0;i<c->unit_count;++i)
    {
        UnitDefinition* u=&c->units[i];int faction=content_faction(c,u->faction_name),group=content_group(c,u->group_name);
        if (faction<=0 || group<0) { valid=false;break; }
        u->faction=(Faction)faction;u->group=(uint32_t)group;
    }
    for (uint32_t i=0;valid && i<c->producer_count;++i)
    {
        ProducerDefinition* p=&c->producers[i];int faction=content_faction(c,p->faction_name);
        if (faction<=0 || p->branches.fields!=BRANCH_FIELDS_COMPLETE) { valid=false;break; }
        if (p->branches.organelle_roots && !p->segment_path[0]) { valid=false;break; }
        p->faction=(Faction)faction;
    }
    for (uint32_t i=1;valid && i<MAX_FACTIONS;++i)
    {
        FactionDefinition* f=&c->factions[i];if (!f->declared || !f->producer_name[0]) continue;
        int p=content_producer(c,f->producer_name),u=content_unit(c,f->default_name),a=content_unit(c,f->assault_name);
        if (p<0 || u<0 || a<0 || c->producers[p].faction!=i || c->units[u].faction!=i || c->units[a].faction!=i) { valid=false;break; }
        f->producer=(uint32_t)p;f->default_unit=(uint32_t)u;f->assault_unit=(uint32_t)a;
        if (f->automatic && (u!=a || c->units[u].attack!=ATTACK_SPORE || c->units[u].colonizes)) valid=false;
    }
    if (!valid) fprintf(stderr,"%s: content has incomplete or invalid references\n",path);
    return valid;
}
