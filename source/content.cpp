#include "content.h"
#include "asset_json.h"
#include "vector_math.h"
#include <string.h>
#include <stdio.h>
#include <cmath>

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
    return json_string(json_get(j,"name"),name,32);
}
static Vec3 normal(Json j)
{
    Vec3 n=json_vector(j,-100,100);float length=dot(n,n);
    if (length<.01f || length>10000) json_error(j,"normal must have squared length in [.01,10000]");
    return length>0 ? normalize(n) : n;
}
static bool load_bodies(Content* c,Json root,const char* path)
{
    json_object(root,"$schema|bodies");JsonArray a=json_array(json_get(root,"bodies"),MAX_BODY_TYPES);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"name|label|geometry|model|hosts_site");BodyDefinition b={};char geometry[32]={};
        definition_name(j,b.name);json_string(json_get(j,"label"),b.label,32);
        json_string(json_get(j,"geometry"),geometry,32);b.hosts_site=json_bool(json_get(j,"hosts_site"));
        if (!strcmp(geometry,"mesh")) json_path(json_get(j,"model"),path,b.model_path);
        else if (strcmp(geometry,"sphere") || json_present(json_find(j,"model"))) json_error(j,"geometry must be mesh with a model, or sphere without a model");
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
        json_object(j,"id|name|label|color|production");FactionDefinition f={};
        uint32_t id=json_uint(json_get(j,"id"),1,MAX_FACTIONS-1);
        definition_name(j,f.name);json_string(json_get(j,"label"),f.label,32);f.color=json_color(json_get(j,"color"));
        if (c->factions[id].declared || content_faction(c,f.name)>=0) return json_error(j,"duplicate or reserved faction ID or name");
        Json p=json_find(j,"production");
        if (json_present(p))
        {
            json_object(p,"building|default_unit|assault_unit");
            json_string(json_get(p,"building"),f.producer_name,32);
            json_string(json_get(p,"default_unit"),f.default_name,32);
            json_string(json_get(p,"assault_unit"),f.assault_name,32);
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
        json_string(json_get(j,"key"),g.key,32);json_string(json_get(j,"selection"),g.selection,32);
        json_string(json_get(j,"singular"),g.singular,32);json_string(json_get(j,"plural"),g.plural,32);
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
        json_object(j,"name|faction|group|model|scale|yaw_degrees|tint|speed|health|range|cost|shot_interval|separation|targets|colonizes|detaches|trails|pick_radius|cue_radius|hits");
        UnitDefinition u={};definition_name(j,u.name);
        json_string(json_get(j,"faction"),u.faction_name,32);json_string(json_get(j,"group"),u.group_name,32);
        json_path(json_get(j,"model"),path,u.model_path);
        u.scale=json_positive(json_get(j,"scale"),1000);u.yaw_degrees=json_float(json_get(j,"yaw_degrees"),-360000,360000);
        u.color=json_color(json_get(j,"tint"));
        u.speed=json_positive(json_get(j,"speed"),10000);
        u.health=json_positive(json_get(j,"health"),10000);
        u.range=json_positive(json_get(j,"range"),10000);
        u.cost=json_positive(json_get(j,"cost"),10000);
        u.shot_interval=json_positive(json_get(j,"shot_interval"),10000);
        u.separation=json_positive(json_get(j,"separation"),10000);
        u.pick_radius=json_positive(json_get(j,"pick_radius"),1000);
        u.cue_radius=json_positive(json_get(j,"cue_radius"),1000);
        u.hits=json_uint(json_get(j,"hits"),1,32);
        u.colonizes=json_bool(json_get(j,"colonizes"));u.detaches=json_bool(json_get(j,"detaches"));u.trails=json_bool(json_get(j,"trails"));
        JsonArray targets=json_array(json_get(j,"targets"),2);Json target={};
        while (json_next(&targets,&target))
        {
            char name[32]={};json_string(target,name,32);
            uint32_t bit=!strcmp(name,"units") ? TARGET_UNITS : !strcmp(name,"structures") ? TARGET_STRUCTURES : 0;
            if (!bit || (u.targets&bit)) return json_error(target,"unknown or duplicate target category");
            u.targets|=bit;
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
        json_object(j,"name|faction|model|growth_seconds|health|collection|height|width|hits|branches");ProducerDefinition p={};
        definition_name(j,p.name);json_string(json_get(j,"faction"),p.faction_name,32);json_path(json_get(j,"model"),path,p.model_path);
        p.growth_seconds=json_positive(json_get(j,"growth_seconds"),10000);
        p.health=json_positive(json_get(j,"health"),10000);
        p.collection=json_positive(json_get(j,"collection"),10000);
        p.height=json_positive(json_get(j,"height"),10000);
        p.width=json_positive(json_get(j,"width"),10000);
        p.hits=json_uint(json_get(j,"hits"),1,32);Json b=json_get(j,"branches");
        json_object(b,"seconds|count|spacing|production|seed|normal");
        p.branches.seconds=json_positive(json_get(b,"seconds"),300);
        p.branches.count=json_uint(json_get(b,"count"),1,MAX_FACTORY_STRUCTURES);
        p.branches.spacing=json_positive(json_get(b,"spacing"),10);
        p.branches.production=json_positive(json_get(b,"production"),10);
        p.branches.seed=json_uint(json_get(b,"seed"),0,16777215);
        p.branches.normal=normal(json_get(b,"normal"));p.branches.fields=3;
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
    for (uint32_t i=0;i<5 && manifest.valid;++i)
    {
        char file[256]={};if (!json_path(json_get(root,keys[i]),path,file)) return false;
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
        if (faction<=0 || p->branches.fields!=3) { valid=false;break; }
        p->faction=(Faction)faction;
    }
    for (uint32_t i=1;valid && i<MAX_FACTIONS;++i)
    {
        FactionDefinition* f=&c->factions[i];if (!f->declared || !f->producer_name[0]) continue;
        int p=content_producer(c,f->producer_name),u=content_unit(c,f->default_name),a=content_unit(c,f->assault_name);
        if (p<0 || u<0 || a<0 || c->producers[p].faction!=i || c->units[u].faction!=i || c->units[a].faction!=i) { valid=false;break; }
        f->producer=(uint32_t)p;f->default_unit=(uint32_t)u;f->assault_unit=(uint32_t)a;
    }
    if (!valid) fprintf(stderr,"%s: content has incomplete or invalid references\n",path);
    return valid;
}
