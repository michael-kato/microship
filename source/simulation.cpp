#include "simulation.h"
#include "vector_math.h"
#include "geometry.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

static float length(Vec3 v) { return std::sqrt(dot(v,v)); }

Vec3 simulation_spike_direction(uint32_t index,uint32_t count)
{
    float y=1-2*(index+.5f)/count,angle=index*2.39996323f,r=std::sqrt(std::fmax(0.0f,1-y*y));
    return Vec3{r*std::cos(angle),y,r*std::sin(angle)};
}
void simulation_unit_axes(const Unit* u,Vec3* right,Vec3* up,Vec3* forward)
{
    *forward=u->forward;Vec3 axis=std::fabs(forward->y)<.95f ? Vec3{0,1,0} : Vec3{1,0,0};
    Vec3 x=normalize(cross(axis,*forward)),y=cross(*forward,x);float c=std::cos(u->bank),v=std::sin(u->bank);
    *right=add(scale(x,c),scale(y,v));*up=subtract(scale(y,c),scale(x,v));
}
static Vec3 world_direction(const Unit* u,Vec3 local)
{
    Vec3 x,y,z;simulation_unit_axes(u,&x,&y,&z);return add(add(scale(x,local.x),scale(y,local.y)),scale(z,local.z));
}
static Vec3 local_direction(const Unit* u,Vec3 world)
{
    Vec3 x,y,z;simulation_unit_axes(u,&x,&y,&z);return Vec3{dot(world,x),dot(world,y),dot(world,z)};
}

static int home_body(const Simulation* s, Faction faction)
{
    for (uint32_t i=0;i<s->world.body_count;++i)
        if (s->home_owner[i]==faction && s->world.content.bodies[s->world.bodies[i].type].hosts_site) return (int)i;
    return NO_BODY;
}

bool simulation_in_sight(const Simulation* s,Vec3 position,float radius)
{
    if (s->world.sight_radius<=0) return true;
    float reach=s->world.sight_radius+radius;
    for (uint32_t p=0;p<s->world.body_count;++p)
        if (s->colonies[p].health>0 && s->colonies[p].faction==s->world.player_faction
            && length(subtract(position,s->world.bodies[p].position))<reach+s->world.bodies[p].radius) return true;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].faction==s->world.player_faction
            && length(subtract(position,s->units[i].position))<reach) return true;
    return false;
}
bool simulation_body_known(const Simulation* s,uint32_t p)
{ return p<s->world.body_count && (s->world.sight_radius<=0 || s->discovered[p]); }

void simulation_update_sight(Simulation* s)
{
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        s->visible_bodies[p]=simulation_in_sight(s,s->world.bodies[p].position,s->world.bodies[p].radius);
        s->discovered[p]=s->discovered[p] || s->visible_bodies[p];
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* u=&s->units[i];
        s->visible_units[i]=u->health>0 && (u->faction==s->world.player_faction || simulation_in_sight(s,u->position,0));
    }
}

void simulation_producer_axes(const ProducerDefinition* definition,Vec3* right,Vec3* up,Vec3* normal)
{
    *normal=definition->branches.normal;
    Vec3 axis=std::fabs(normal->y)<.95f ? Vec3{0,1,0} : Vec3{0,0,1};
    *right=normalize(cross(axis,*normal));*up=cross(*normal,*right);
}
Vec3 simulation_structure_position(const Simulation* s,uint32_t p,uint32_t structure)
{
    const Colony* c=&s->colonies[p];Vec3 up=s->world.bodies[p].colony_normal;
    Vec3 base=c->surface;float growth=c->growth;
    if (s->world.gameplay.enabled)
    {
        Vec3 right,normal;simulation_producer_axes(simulation_producer_definition(s,p),&right,&up,&normal);
        if (structure) { base=add(base,c->branches[structure-1].offset);growth=c->branches[structure-1].growth; }
    }
    return add(base,scale(up,simulation_producer_definition(s,p)->height*std::fmax(.02f,growth)));
}

static uint32_t branch_random(uint32_t* seed)
{ *seed=*seed*1664525u+1013904223u;return *seed; }
static void prepare_factory(Simulation* s,uint32_t p)
{
    Colony* c=&s->colonies[p];const BranchRules* f=&simulation_producer_definition(s,p)->branches;
    Vec3 right,up,normal;simulation_producer_axes(simulation_producer_definition(s,p),&right,&up,&normal);
    uint32_t seed=f->seed+p*747796405u;
    for (uint32_t j=1;j<f->count;++j)
    {
        uint32_t parent=(j-1)/2,depth=0;for (uint32_t v=j+1;v>1;v>>=1) ++depth;
        float side=j%2 ? -1.0f : 1.0f;
        float width=f->spacing/std::sqrt((float)depth);
        float jitter=((branch_random(&seed)>>8)*(1.0f/16777216)-.5f)*width*.35f;
        Vec3 offset=add(scale(right,side*width+jitter),scale(up,width*(.65f+(branch_random(&seed)%100)*.003f)));
        offset=add(offset,scale(normal,jitter*.4f));
        c->branches[j-1].offset=add(parent ? c->branches[parent-1].offset : Vec3{},offset);
    }
}

static void establish_colony(Simulation* s,uint32_t p,Faction faction)
{
    Colony* c=&s->colonies[p];Vec3 surface=c->surface;uint32_t generation=c->generation+1;
    *c=Colony();c->surface=surface;c->generation=generation;c->faction=faction;
    c->type=s->world.content.factions[faction].producer;
    c->health=simulation_producer_definition(s,p)->health;c->growth=.02f;c->priority=s->world.content.factions[faction].default_unit;
    s->world.bodies[p].owner=faction;
    if (s->world.gameplay.enabled) prepare_factory(s,p);
}

static bool spawn(Simulation* s, uint32_t type, Vec3 position, int target)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health<=0)
        {
            const UnitDefinition* definition=&s->world.content.units[type];
            Unit unit={};
            unit.position=position;unit.forward=Vec3{0,0,1};unit.type=type;unit.faction=definition->faction;
            unit.health=definition->health;
            unit.target=target;unit.generation=++s->next_generation;
            for (uint32_t j=0;j<s->world.spikes.count;++j) unit.spikes[j]=1;
            unit.orbit_phase=unit.generation*2.39996323f;unit.cooldown=(unit.generation%5)*.04f;
            s->trails[i]=Trail();s->trails[i].points[0]=position;s->trails[i].count=1;
            s->units[i]=unit;++s->births;
            return true;
        }
    return false;
}

bool simulation_initialize(Simulation* s, const char* world_path)
{
    *s=Simulation();s->selected_body=s->order_body=NO_BODY;
    if (!world_load(&s->world,world_path)) return false;
    for (uint32_t i=0;i<s->world.body_count;++i) s->home_owner[i]=s->world.bodies[i].owner;
    if (!s->world.gameplay.enabled) return true;
    if (home_body(s,s->world.player_faction)<0 || home_body(s,s->world.opponent_faction)<0)
    { fprintf(stderr,"Both match sides require a home body.\n");return false; }
    for (uint32_t i=0;i<s->world.spawn_count;++i)
    {
        const Spawn* placement=&s->world.spawns[i];Faction faction=s->world.content.units[placement->type].faction;
        int home=home_body(s,faction);
        if (home<0) { fprintf(stderr,"Mobile entities require a home body.\n");return false; }
        spawn(s,placement->type,placement->position,home);
    }
    for (uint32_t i=0;i<s->world.body_count;++i)
        if (s->world.content.bodies[s->world.bodies[i].type].hosts_site && !geometry_surface_point(s->world.bodies[i].model_path,s->world.bodies[i].position,s->world.bodies[i].radius,s->world.bodies[i].colony_normal,&s->colonies[i].surface)) return false;
    if (s->world.gameplay.enabled)
        for (uint32_t p=0;p<s->world.body_count;++p)
            if (s->home_owner[p]!=FACTION_NEUTRAL && s->world.content.bodies[s->world.bodies[p].type].hosts_site)
            { establish_colony(s,p,s->home_owner[p]);s->colonies[p].growth=1; }
    simulation_update_sight(s);
    snprintf(s->message,sizeof(s->message),"RIGHT-CLICK OPEN SPACE TO SCOUT. ORDER A KNOWN BODY TO INTERACT.");
    return true;
}

void simulation_set_time(Simulation* s, double seconds)
{ s->time_seconds=seconds; }

void simulation_select(Simulation* s, int group)
{
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        u->selected=u->health>0 && u->faction==s->world.player_faction && (group<0 || (int)simulation_unit_definition(s,u)->group==group);
        if (u->selected) ++count;
    }
    snprintf(s->message,sizeof(s->message),"SELECTED %u %s",count,group>=0 && group<MAX_GROUPS ? s->world.content.groups[group].plural : "UNITS");
}

void simulation_order(Simulation* s, uint32_t cell)
{
    if (!simulation_body_known(s,cell) || s->outcome) return;
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].selected && s->units[i].health>0)
        { s->units[i].target=(int)cell;s->units[i].moving=false;s->units[i].landing=0;++count; }
    s->selected_body=(int)cell;
    if (count) { s->order_body=(int)cell;s->order_tick=s->tick_count;s->order_count=count; }
    if (count) snprintf(s->message,sizeof(s->message),"%u UNITS ORDERED TO %s %u",count,s->world.content.bodies[s->world.bodies[cell].type].label,cell+1);
    else snprintf(s->message,sizeof(s->message),"SELECT A GROUP FIRST");
}

void simulation_move_to(Simulation* s,Vec3 destination)
{
    if (s->outcome || !std::isfinite(destination.x) || !std::isfinite(destination.y) || !std::isfinite(destination.z)
        || std::fabs(destination.x)>1000000 || std::fabs(destination.y)>1000000 || std::fabs(destination.z)>1000000) return;
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].selected)
        {
            Unit* u=&s->units[i];u->target=NO_BODY;u->destination=destination;u->moving=true;u->landing=0;++count;
        }
    s->order_body=NO_BODY;s->order_count=count;s->order_tick=s->tick_count;
    snprintf(s->message,sizeof(s->message),"%u UNITS MOVING. SCOUT TO FIND CELLS.",count);
}

void simulation_priority_type(Simulation* s,uint32_t type)
{
    int p=s->selected_body;
    if (p<0 || !simulation_body_known(s,(uint32_t)p) || s->colonies[p].health<=0 || s->colonies[p].faction!=s->world.player_faction)
    { snprintf(s->message,sizeof(s->message),"SELECT YOUR PRODUCTION SITE FIRST");return; }
    if (type>=s->world.content.unit_count || s->world.content.units[type].faction!=s->colonies[p].faction) return;
    s->colonies[p].priority=type;
    snprintf(s->message,sizeof(s->message),"NEXT UNIT: %s",s->world.content.groups[s->world.content.units[type].group].singular);
}
void simulation_priority(Simulation* s,int group)
{
    int type=content_product(&s->world.content,s->world.player_faction,group);
    if (type>=0) simulation_priority_type(s,(uint32_t)type);
}

static Vec3 colony_tip(const Simulation* s, uint32_t p)
{
    return simulation_structure_position(s,p,0);
}



static bool defended(const Simulation* s, uint32_t p, Faction faction)
{
    const Body* cell=&s->world.bodies[p];
    if (s->colonies[p].health>0 && s->colonies[p].faction!=faction) return true;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].faction!=faction
            && length(subtract(s->units[i].position,cell->position))<cell->radius+simulation_unit_definition(s,&s->units[i])->range) return true;
    return false;
}

static bool clear_shot(const Simulation* s, Vec3 from, Vec3 to)
{
    Vec3 delta=subtract(to,from);float distance=length(delta);
    if (distance<.0001f) return true;
    Vec3 direction=scale(delta,1.0f/distance);
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        Vec3 offset=subtract(from,s->world.bodies[p].position);float along=dot(offset,direction);
        float radius=s->world.bodies[p].radius,disc=along*along-dot(offset,offset)+radius*radius;
        if (disc>0 && -along+std::sqrt(disc)>.001f && -along-std::sqrt(disc)<distance-.001f) return false;
    }
    return true;
}

static void move_units(Simulation* s, float dt)
{
    const CombatVisuals* flight=&s->world.combat;
    Vec3 positions[MAX_UNITS],velocities[MAX_UNITS];
    for (uint32_t i=0;i<MAX_UNITS;++i) { positions[i]=s->units[i].position;velocities[i]=s->units[i].velocity; }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (u->health<=0 || (u->target<0 && !u->moving)) continue;
        Vec3 combat_target={};bool strafe=false;
        uint32_t p=(uint32_t)u->target;
        bool colonizing=simulation_unit_definition(s,u)->colonizes && u->target>=0 && s->world.content.bodies[s->world.bodies[p].type].hosts_site
            && s->colonies[p].health<=0 && !defended(s,p,u->faction);
        Vec3 goal=u->moving ? u->destination : add(s->colonies[p].surface,scale(s->world.bodies[p].colony_normal,colonizing ? .25f : 1.3f));
        if (!u->moving && !colonizing)
        {
            Vec3 normal=s->world.bodies[p].colony_normal;
            Vec3 axis=std::fabs(normal.z)<.9f ? Vec3{0,0,1} : Vec3{1,0,0};
            Vec3 tangent=normalize(cross(normal,axis)),bitangent=cross(tangent,normal);
            float angle=u->generation*2.39996323f;
            if (s->world.combat.enabled)
            {
                u->orbit_phase=std::fmod(u->orbit_phase+s->world.combat.orbit_speed*dt,6.28318530718f);angle=u->orbit_phase;
            }
            float spread=s->world.bodies[p].radius*(.25f+.06f*std::sqrt((float)i+1));
            if (s->world.combat.enabled)
            {
                spread=s->world.bodies[p].radius+s->world.combat.orbit_clearance;
                goal=add(s->world.bodies[p].position,scale(normal,.4f*s->world.bodies[p].radius));
            }
            goal=add(goal,add(scale(tangent,std::cos(angle)*spread),scale(bitangent,std::sin(angle)*spread)));
            float nearest=1e10f;
            for (uint32_t j=0;(simulation_unit_definition(s,u)->targets&TARGET_UNITS) && j<MAX_UNITS;++j)
                if (s->units[j].health>0 && s->units[j].faction!=u->faction)
                {
                    Vec3 enemy=flight->flight_enabled ? positions[j] : s->units[j].position;
                    float target_distance=length(subtract(enemy,s->world.bodies[p].position));
                    float distance=length(subtract(enemy,u->position));
                    if (target_distance< s->world.bodies[p].radius+simulation_unit_definition(s,u)->range+2 && distance<nearest)
                    { nearest=distance;goal=enemy; }
                }
            if (flight->flight_enabled && nearest<simulation_unit_definition(s,u)->range*1.1f)
            { combat_target=goal;strafe=true; }
            else if (nearest<simulation_unit_definition(s,u)->range*.85f) goal=u->position;
            else if (nearest==1e10f && (simulation_unit_definition(s,u)->targets&TARGET_STRUCTURES) && s->colonies[p].health>0 && s->colonies[p].faction!=u->faction)
            {
                goal=colony_tip(s,p);
                if (flight->flight_enabled && length(subtract(goal,u->position))<simulation_unit_definition(s,u)->range*1.1f)
                { combat_target=goal;strafe=true; }
                else if (length(subtract(goal,u->position))<simulation_unit_definition(s,u)->range*.85f) goal=u->position;
            }
        }
        Vec3 delta=subtract(goal,u->position);float distance=length(delta);
        float speed=simulation_unit_definition(s,u)->speed;
        if (flight->flight_enabled)
        {
            Vec3 desired=distance>.0001f ? scale(delta,std::fmin(speed,distance*3)/distance) : Vec3{};
            if (strafe)
            {
                Vec3 away=subtract(u->position,combat_target);float d=length(away);
                if (d>.001f)
                {
                    away=scale(away,1/d);Vec3 side=cross(s->world.bodies[p].colony_normal,away);
                    if (dot(side,side)>.001f) side=normalize(side);
                    desired=add(scale(side,speed*.7f),scale(away,(simulation_unit_definition(s,u)->range*.72f-d)*3));
                }
            }
            // Read a stable pass input: separation must not depend on unit update order.
            for (uint32_t j=0;j<MAX_UNITS;++j)
                if (j!=i && s->units[j].health>0)
                {
                    Vec3 apart=subtract(positions[i],positions[j]);float d=length(apart);
                    float spacing=flight->separation*std::fmax(simulation_unit_definition(s,u)->separation,simulation_unit_definition(s,&s->units[j])->separation);
                    if (d<spacing && d>.0001f) desired=add(desired,scale(apart,speed*(1-d/spacing)/d));
                }
            // Steer around a cell before the hard collision shell becomes necessary.
            for (uint32_t j=0;j<s->world.body_count;++j)
            {
                if (colonizing && j==p && distance<1.5f) continue;
                Vec3 probe=add(u->position,scale(velocities[i],.35f));
                Vec3 away=subtract(probe,s->world.bodies[j].position);float d=length(away);
                float safe=s->world.bodies[j].radius*1.08f+.6f;
                if (d<safe && d>.001f) desired=add(desired,scale(away,speed*3*(1-d/safe)/d));
            }
            float desired_speed=length(desired);if (desired_speed>speed) desired=scale(desired,speed/desired_speed);
            Vec3 change=subtract(desired,velocities[i]);float change_length=length(change);
            if (change_length>flight->acceleration*dt) change=scale(change,flight->acceleration*dt/change_length);
            u->velocity=add(velocities[i],change);u->position=add(u->position,scale(u->velocity,dt));
            if (length(u->velocity)>.02f)
            {
                Vec3 heading=normalize(u->velocity),side=cross(Vec3{0,1,0},u->forward);
                float blend=1-std::exp(-flight->turn_rate*dt);
                float bank=-dot(subtract(heading,u->forward),side)*5;
                u->bank+=(std::fmax(-flight->bank,std::fmin(flight->bank,bank))-u->bank)*blend;
                Vec3 forward=add(scale(u->forward,1-blend),scale(heading,blend));
                if (length(forward)>.001f) u->forward=normalize(forward);
            }
        }
        else if (distance>.0001f) u->position=add(u->position,scale(delta,std::fmin(1.0f,speed*dt/distance)));
        // Conservative spherical collision keeps transit outside cell terrain.
        for (uint32_t p2=0;p2<s->world.body_count;++p2)
        {
            Vec3 offset=subtract(u->position,s->world.bodies[p2].position);float radial=length(offset);
            float clearance=s->world.bodies[p2].radius*1.08f+.12f;
            if (colonizing && p2==p && distance<1.5f) continue;
            if (radial<clearance && radial>.0001f)
                u->position=add(s->world.bodies[p2].position,scale(offset,clearance/radial));
        }
        if (u->moving && length(subtract(u->destination,u->position))<.15f) { u->moving=false;u->velocity=Vec3{}; }
    }
}



static void pop_body(Simulation* s,Vec3 position,Faction faction,bool unit_death)
{
    uint32_t slot=0;
    while (slot<MAX_POPS && s->pops[slot].duration>0) ++slot;
    if (slot==MAX_POPS) slot=s->deaths%MAX_POPS;
    Pop* p=&s->pops[slot];p->position=position;p->age=0;
    p->duration=s->world.combat.pop_seconds;p->faction=faction;
    if (unit_death) { ++s->deaths;s->last_death_tick=s->tick_count; }
}

static void advance_visuals(Simulation* s,float dt)
{
    const CombatVisuals* v=&s->world.combat;
    for (uint32_t i=0;i<MAX_POPS;++i)
        if (s->impacts[i].duration>0 && (s->impacts[i].age+=dt)>=s->impacts[i].duration) s->impacts[i].duration=0;
    for (uint32_t i=0;i<MAX_POPS;++i)
        if (s->pops[i].duration>0 && (s->pops[i].age+=dt)>=s->pops[i].duration) s->pops[i].duration=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health>0) u->age+=dt;u->flash=std::fmax(0.0f,u->flash-dt);u->cooldown=std::fmax(0.0f,u->cooldown-dt);
        Trail* t=&s->trails[i];if (u->health<=0 || !simulation_unit_definition(s,u)->trails) { t->count=0;continue; }
        t->timer+=dt;if (t->timer<v->trail_lifetime/(TRAIL_POINTS-1)) continue;
        t->timer=0;uint32_t count=t->count<TRAIL_POINTS ? t->count : TRAIL_POINTS-1;
        for (uint32_t j=count;j>0;--j) t->points[j]=t->points[j-1];
        t->points[0]=u->position;t->count=count+1;
    }
}

static void recover_units(Simulation* s,float dt)
{
    const SpikeRules* r=&s->world.spikes;if (!r->enabled) return;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health<=0) continue;
        u->damage_age+=dt;
        for (uint32_t j=0;j<r->count;++j)
            if (u->spikes[j]<1)
            { float before=u->spikes[j];u->spikes[j]=std::fmin(1.0f,before+dt/r->regrow);if (u->spikes[j]==1) ++s->regrown; }
        bool friendly=false;
        if (u->target>=0)
        {
            const Body* c=&s->world.bodies[u->target];
            friendly=c->owner==u->faction && length(subtract(u->position,c->position))<c->radius+r->heal_clearance;
        }
        if (!friendly || !u->wounds || u->damage_age<r->heal_delay) { u->heal_timer=0;continue; }
        u->heal_timer+=dt;
        if (u->heal_timer>=r->heal_interval)
        {
            u->heal_timer=0;--u->wounds;if (u->wounds<MAX_EMBEDDED) u->embedded[u->wounds]=EmbeddedSpike();
            u->health=simulation_unit_definition(s,u)->health*(simulation_unit_definition(s,u)->hits-u->wounds)/simulation_unit_definition(s,u)->hits;++s->healed;
        }
    }
}

static float structure_health(const Colony* c,uint32_t k)
{ return k ? c->branches[k-1].health : c->health; }
static uint32_t structure_generation(const Colony* c,uint32_t k)
{ return k ? c->branches[k-1].generation : c->generation; }
static uint32_t structure_wounds(const Colony* c,uint32_t k)
{ return k ? c->branches[k-1].wounds : c->wounds; }
static uint32_t structure_slot(uint32_t cell,uint32_t structure)
{ return MAX_UNITS+cell*MAX_FACTORY_STRUCTURES+structure; }

static void projectile_combat(Simulation* s,float dt)
{
    const CombatVisuals* v=&s->world.combat;
    uint32_t incoming[MAX_UNITS+MAX_BODIES*MAX_FACTORY_STRUCTURES]={};
    if (s->world.spikes.enabled) for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        const Projectile* p=&s->projectiles[i];if (!p->active) continue;
        bool valid=p->colony ? s->colonies[p->target].generation==p->generation
            && structure_generation(&s->colonies[p->target],p->structure)==p->structure_generation : s->units[p->target].generation==p->generation;
        if (valid) ++incoming[p->colony ? structure_slot(p->target,p->structure) : p->target];
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health<=0 || u->cooldown>0) continue;
        int target=-1;bool colony=false;uint32_t structure=0;float nearest=simulation_unit_definition(s,u)->range;
        for (uint32_t j=0;(simulation_unit_definition(s,u)->targets&TARGET_UNITS) && j<MAX_UNITS;++j)
            if (s->units[j].health>0 && s->units[j].faction!=u->faction)
            {
                float d=length(subtract(u->position,s->units[j].position));
                if (d<nearest && clear_shot(s,u->position,s->units[j].position)) { nearest=d;target=(int)j; }
            }
        if (target<0 && (simulation_unit_definition(s,u)->targets&TARGET_STRUCTURES)) for (uint32_t j=0;j<s->world.body_count;++j)
            if (s->colonies[j].health>0 && s->colonies[j].faction!=u->faction)
            {
                uint32_t count=simulation_producer_definition(s,j)->branches.count;
                for (uint32_t k=0;k<count;++k) if (structure_health(&s->colonies[j],k)>0)
                {
                    Vec3 point=simulation_structure_position(s,j,k);float d=length(subtract(u->position,point));
                    if (d<nearest && clear_shot(s,u->position,point)) { nearest=d;target=(int)j;colony=true;structure=k; }
                }
            }
        if (target<0) continue;
        uint32_t slot=0;while (slot<MAX_PROJECTILES && s->projectiles[slot].active) ++slot;
        if (slot==MAX_PROJECTILES) continue; // Wait for capacity; never apply invisible damage.
        Vec3 destination=colony ? simulation_structure_position(s,(uint32_t)target,structure) : s->units[target].position;
        Vec3 start=u->position;float launch_scale=s->world.spikes.scale;
        if (s->world.spikes.enabled && simulation_unit_definition(s,u)->detaches)
        {
            int spike=-1;float best=-2;Vec3 x,y,z;simulation_unit_axes(u,&x,&y,&z);
            Vec3 aim=subtract(destination,start);float aim_length=length(aim);aim=aim_length>.0001f ? scale(aim,1/aim_length) : u->forward;
            for (uint32_t j=0;j<s->world.spikes.count;++j) if (u->spikes[j]>=1)
            {
                Vec3 local=simulation_spike_direction(j,s->world.spikes.count);
                Vec3 direction=add(add(scale(x,local.x),scale(y,local.y)),scale(z,local.z));float score=dot(direction,aim);
                if (score>best) { best=score;spike=(int)j; }
            }
            if (spike<0) continue;
            Vec3 local=simulation_spike_direction((uint32_t)spike,s->world.spikes.count);
            Vec3 outward=add(add(scale(x,local.x),scale(y,local.y)),scale(z,local.z));
            float size=simulation_unit_definition(s,u)->scale;
            start=add(start,scale(outward,s->world.spikes.root*size));launch_scale=size*s->world.spikes.length;
            u->spikes[spike]=0;++s->detached;
        }
        Projectile p={};p.position=p.previous=start;p.target=target;p.colony=colony;p.faction=u->faction;p.structure=structure;
        if (s->world.spikes.enabled)
        {
            uint32_t index=colony ? structure_slot(target,structure) : (uint32_t)target;
            uint32_t wounds=colony ? structure_wounds(&s->colonies[target],structure) : s->units[target].wounds;
            uint32_t socket=(wounds+incoming[index]++)%3;
            Vec3 outward=subtract(start,destination);float distance=length(outward);
            outward=distance>.0001f ? scale(outward,1/distance) : Vec3{0,0,1};
            Vec3 axis=std::fabs(outward.y)<.95f ? Vec3{0,1,0} : Vec3{1,0,0};
            Vec3 x=normalize(cross(axis,outward)),y=cross(outward,x);float angle=socket*2.094395102f;
            Vec3 normal=normalize(add(scale(outward,.75f),add(scale(x,.65f*std::cos(angle)),scale(y,.65f*std::sin(angle)))));
            p.anchor=colony ? scale(normal,simulation_producer_definition(s,(uint32_t)target)->width*.18f) : local_direction(&s->units[target],scale(normal,.55f));
            destination=add(destination,colony ? p.anchor : scale(world_direction(&s->units[target],p.anchor),simulation_unit_definition(s,&s->units[target])->scale));
        }
        Vec3 aim=subtract(destination,start);float aim_length=length(aim);p.direction=aim_length>.0001f ? scale(aim,1/aim_length) : u->forward;p.launch_scale=launch_scale;
        p.generation=colony ? s->colonies[target].generation : s->units[target].generation;
        p.structure_generation=colony ? structure_generation(&s->colonies[target],structure) : 0;
        p.active=true;s->projectiles[slot]=p;
        u->cooldown=v->shot_interval*simulation_unit_definition(s,u)->shot_interval;++s->shots;
    }
    uint32_t hit_count[MAX_UNITS+MAX_BODIES*MAX_FACTORY_STRUCTURES]={};
    EmbeddedSpike landed[MAX_UNITS+MAX_BODIES*MAX_FACTORY_STRUCTURES][2]={};
    for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        Projectile* p=&s->projectiles[i];if (!p->active) continue;
        bool valid=p->colony ? s->colonies[p->target].health>0 && s->colonies[p->target].faction!=p->faction && s->colonies[p->target].generation==p->generation
            && structure_health(&s->colonies[p->target],p->structure)>0 && structure_generation(&s->colonies[p->target],p->structure)==p->structure_generation
            : s->units[p->target].health>0 && s->units[p->target].generation==p->generation;
        p->age+=dt;if (!valid || p->age>4) { p->active=false;continue; }
        Vec3 target=p->colony ? simulation_structure_position(s,p->target,p->structure) : s->units[p->target].position;
        if (s->world.spikes.enabled) target=add(target,p->colony ? p->anchor :
            scale(world_direction(&s->units[p->target],p->anchor),simulation_unit_definition(s,&s->units[p->target])->scale));
        Vec3 delta=subtract(target,p->position);float d=length(delta),step=v->projectile_speed*dt;
        Vec3 next=d<=step ? target : add(p->position,scale(delta,step/d));
        if (!clear_shot(s,p->position,next)) { p->active=false;continue; }
        p->previous=p->position;p->position=next;
        if (d>.0001f) p->direction=scale(delta,1/d);
        if (d<=step)
        {
            if (s->world.spikes.enabled)
            {
                uint32_t index=p->colony ? structure_slot(p->target,p->structure) : (uint32_t)p->target;
                uint32_t slot=hit_count[index]++;
                if (slot<2)
                {
                    Vec3 outward=scale(p->direction,-1);
                    landed[index][slot].direction=p->colony ? outward : local_direction(&s->units[p->target],outward);
                    landed[index][slot].faction=p->faction;landed[index][slot].anchor=p->anchor;
                }
            }
            uint32_t slot=s->hits%MAX_POPS;
            Impact impact={};impact.position=target;impact.duration=v->flash_seconds*1.8f;impact.faction=p->faction;
            s->impacts[slot]=impact;
            p->active=false;++s->hits;
        }
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && hit_count[i]>0)
        {
            Unit* u=&s->units[i];
            if (s->world.spikes.enabled)
            {
                uint32_t old=u->wounds;u->wounds=std::min(simulation_unit_definition(s,u)->hits,old+hit_count[i]);
                for (uint32_t j=old;j<u->wounds && j<2;++j) u->embedded[j]=landed[i][j-old];
                float maximum=simulation_unit_definition(s,u)->health;
                u->health=maximum*(simulation_unit_definition(s,u)->hits-u->wounds)/simulation_unit_definition(s,u)->hits;u->damage_age=u->heal_timer=0;
            }
            u->flash=v->flash_seconds;
            if (u->health>0)
            {
                s->last_flash_tick=s->tick_count;
                if (u->wounds+1>=simulation_unit_definition(s,u)->hits) s->last_low_tick=s->tick_count;
            }
            if (u->health==0) { pop_body(s,u->position,u->faction,true);++s->losses;u->selected=false;s->trails[i].count=0; }
        }
    for (uint32_t i=0;i<s->world.body_count;++i)
        for (uint32_t k=0;k<simulation_producer_definition(s,i)->branches.count;++k)
        if (s->colonies[i].health>0 && structure_health(&s->colonies[i],k)>0 && hit_count[structure_slot(i,k)]>0)
        {
            Colony* c=&s->colonies[i];FactoryStructure* branch=k ? &c->branches[k-1] : 0;
            float* health=k ? &branch->health : &c->health;uint32_t* wounds=k ? &branch->wounds : &c->wounds;
            EmbeddedSpike* embedded=k ? branch->embedded : c->embedded;
            uint32_t index=structure_slot(i,k);
            if (s->world.spikes.enabled)
            {
                uint32_t old=*wounds;*wounds=std::min(simulation_producer_definition(s,i)->hits,old+hit_count[index]);
                for (uint32_t j=old;j<*wounds && j<2;++j) embedded[j]=landed[index][j-old];
                *health=simulation_producer_definition(s,i)->health*(simulation_producer_definition(s,i)->hits-*wounds)/simulation_producer_definition(s,i)->hits;
            }
            if (*health==0)
            {
                if (s->world.spikes.enabled) pop_body(s,simulation_structure_position(s,i,k),c->faction,false);
                if (k) { branch->growth=0;c->branch_timer=0; }
                else
                {
                    c->energy=0;c->building=false;c->growth=0;s->world.bodies[i].owner=FACTION_NEUTRAL;
                    for (uint32_t j=0;j<MAX_FACTORY_STRUCTURES-1;++j) { c->branches[j].health=0;c->branches[j].growth=0; }
                }
            }
        }
}

static void colonize_cells(Simulation* s, float dt)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (u->health<=0 || u->target<0 || !simulation_unit_definition(s,u)->colonizes
            || !s->world.content.bodies[s->world.bodies[u->target].type].hosts_site) continue;
        uint32_t p=(uint32_t)u->target;Colony* c=&s->colonies[p];
        if (defended(s,p,u->faction))
        {
            if (u->landing>=0) ++s->blocked_landings;
            u->landing=-1;
            if (u->selected) snprintf(s->message,sizeof(s->message),"CLEAR DEFENDERS BEFORE COLONIZING.");
            continue;
        }
        if (u->landing<0) u->landing=0;
        if (c->health>0) continue;
        Vec3 goal=add(c->surface,scale(s->world.bodies[p].colony_normal,.25f));
        if (length(subtract(u->position,goal))>.35f) continue;
        u->landing+=dt;
        if (u->landing<s->world.gameplay.landing_seconds) continue;
        establish_colony(s,p,u->faction);u->health=0;u->selected=false;s->trails[i]=Trail();++s->landings;
        if (u->faction==s->world.player_faction) snprintf(s->message,sizeof(s->message),"PRODUCTION SITE ESTABLISHED: %u",p+1);
    }
}

static void grow_colonies(Simulation* s, float dt)
{
    const GameplayRules* rules=&s->world.gameplay;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        Colony* c=&s->colonies[p];
        if (c->health<=0) continue;
        // The sacrificed unit supplies initial growth. Nutrients fund production.
        c->growth=std::fmin(1.0f,c->growth+dt/simulation_producer_definition(s,p)->growth_seconds);
        float production=1;
        if (s->world.gameplay.enabled)
        {
            const BranchRules* f=&simulation_producer_definition(s,p)->branches;
            if (c->growth>=1) c->branch_timer+=dt;
            for (uint32_t j=1;j<f->count;++j)
            {
                FactoryStructure* b=&c->branches[j-1];uint32_t parent=(j-1)/2;
                bool parent_ready=parent ? c->branches[parent-1].health>0 && c->branches[parent-1].growth>=1 : c->growth>=1;
                if (b->health<=0 && parent_ready && c->branch_timer>=f->seconds)
                {
                    c->branch_timer=0;b->health=simulation_producer_definition(s,p)->health;b->growth=.02f;b->wounds=0;++b->generation;
                    memset(b->embedded,0,sizeof(b->embedded));
                }
                if (b->health<=0) continue;
                b->growth=std::fmin(1.0f,b->growth+dt/simulation_producer_definition(s,p)->growth_seconds);
                production+=f->production*b->growth*b->growth;
            }
        }
        c->uptake=s->world.bodies[p].availability;
        c->energy=std::fmin(rules->storage,c->energy+dt*simulation_producer_definition(s,p)->collection*c->uptake*c->growth*c->growth*production);
        if (c->growth<1) continue;
        if (!c->building)
        {
            float cost=s->world.content.units[c->priority].cost;
            if (c->energy<cost) continue;
            c->energy-=cost;c->building=true;c->building_kind=c->priority;c->manufacturing=0;
        }
        float rate=s->world.ai_production_set && c->faction!=s->world.player_faction ? s->world.ai_production : 1;
        c->manufacturing=std::fmin(1.0f,c->manufacturing+dt*rate*production/rules->manufacture_seconds);
        if (c->manufacturing<1) continue;
        uint32_t type=c->building_kind;const UnitDefinition* definition=&s->world.content.units[type];
        Vec3 normal=s->world.bodies[p].colony_normal;
        Vec3 axis=std::fabs(normal.z)<.9f ? Vec3{0,0,1} : Vec3{1,0,0};
        Vec3 tangent=normalize(cross(normal,axis));
        float angle=(s->next_generation+1)*2.39996323f,spread=definition->scale*2;
        Vec3 position=add(add(colony_tip(s,p),scale(normal,1.0f)),
            add(scale(tangent,std::cos(angle)*spread),scale(cross(tangent,normal),std::sin(angle)*spread)));
        if (spawn(s,type,position,(int)p))
        { c->building=false;c->manufacturing=0;c->capacity_blocked=false; }
        else c->capacity_blocked=true;
    }
}

static void enemy_orders(Simulation* s)
{
    Faction enemy=s->world.opponent_faction;
    int home=home_body(s,enemy),target=-1;float nearest=1e30f;bool assault_ready=false;
    bool attack=s->time_seconds>=s->world.gameplay.enemy_attack_seconds;
    for (uint32_t p=0;p<s->world.body_count;++p) if (s->world.content.bodies[s->world.bodies[p].type].hosts_site && s->world.bodies[p].owner!=enemy
        && (s->home_owner[p]==FACTION_NEUTRAL || attack))
    {
        float d=length(subtract(s->world.bodies[p].position,s->world.bodies[home].position));
        if (d<nearest) { nearest=d;target=(int)p; }
    }
    for (uint32_t i=0;i<MAX_UNITS;++i) assault_ready=assault_ready || (s->units[i].health>0 && s->units[i].faction==enemy && (simulation_unit_definition(s,&s->units[i])->targets&TARGET_STRUCTURES));
    for (uint32_t p=0;p<s->world.body_count;++p) if (s->colonies[p].health>0 && s->colonies[p].faction==enemy)
        s->colonies[p].priority=!assault_ready && target>=0 ? s->world.content.factions[enemy].assault_unit : s->world.content.factions[enemy].default_unit;
    bool expand=s->time_seconds>=(s->world.ai_production_set ? s->world.ai_expansion : s->world.gameplay.enemy_attack_seconds);
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health<=0 || u->faction!=enemy) continue;
        u->target=expand && target>=0 ? target : home;
    }
}

static void outcome(Simulation* s)
{
    bool friendly=false,hostile=false,units=false;
    for (uint32_t p=0;p<s->world.body_count;++p) if (s->colonies[p].health>0)
    {
        friendly=friendly || s->colonies[p].faction==s->world.player_faction;
        hostile=hostile || s->colonies[p].faction!=s->world.player_faction;
    }
    for (uint32_t i=0;i<MAX_UNITS;++i) units=units || (s->units[i].health>0 && s->units[i].faction==s->world.player_faction);
    if (friendly && !hostile) { s->outcome=1;snprintf(s->message,sizeof(s->message),"VICTORY: ENEMY PRODUCTION SITES DESTROYED. R TO RESTART."); }
    else if (!friendly && !units) { s->outcome=-1;snprintf(s->message,sizeof(s->message),"COLONY LOST. R TO RESTART."); }
}

void simulation_step(Simulation* s)
{
    if (s->paused) return;
    const float dt=1.0f/60.0f;
    if (s->outcome)
    {
        if (s->world.combat.enabled) { advance_visuals(s,dt);recover_units(s,dt);for (uint32_t i=0;i<MAX_PROJECTILES;++i) s->projectiles[i].active=false;simulation_set_time(s,s->time_seconds+1.0/60.0); }
        return;
    }
    if (s->world.gameplay.enabled)
    {
        enemy_orders(s);
        if (s->world.combat.enabled) advance_visuals(s,dt);
        recover_units(s,dt);move_units(s,dt);
        projectile_combat(s,dt);
        colonize_cells(s,dt);grow_colonies(s,dt);outcome(s);simulation_update_sight(s);
    }
    ++s->tick_count;simulation_set_time(s,s->time_seconds+1.0/60.0);
}

bool simulation_write_state(const Simulation* s, const char* path)
{
    FILE* file=fopen(path,"wb");if (!file) { fprintf(stderr,"Cannot write state '%s'.\n",path);return false; }
    fprintf(file,"{\"tick\":%u,\"time\":%.9f,\"outcome\":%d,\"births\":%u,\"losses\":%u,\"landings\":%u,\"blocked_landings\":%u,\"colonies\":[",
        s->tick_count,s->time_seconds,s->outcome,s->births,s->losses,s->landings,s->blocked_landings);
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        const Colony* c=&s->colonies[p];
        fprintf(file,"%s{\"%s\":%u,\"faction\":%d,\"health\":%.6f,\"growth\":%.6f,\"%s\":%.6f,\"energy\":%.6f,\"building\":%s,\"priority\":%d,\"manufacturing\":%.6f,\"capacity_blocked\":%s,\"wounds\":%u",
            p ? "," : "","cell",p,(int)c->faction,c->health,c->growth,"nutrients",c->uptake,c->energy,c->building ? "true" : "false",(int)(c->faction ? s->world.content.units[c->priority].group : 0),c->manufacturing,c->capacity_blocked ? "true" : "false",c->wounds);
        fprintf(file,",\"discovered\":%s,\"visible\":%s,\"structures\":[",s->discovered[p] ? "true" : "false",s->visible_bodies[p] ? "true" : "false");
        uint32_t count=s->world.gameplay.enabled && s->world.content.bodies[s->world.bodies[p].type].hosts_site ? simulation_producer_definition(s,p)->branches.count : 0;
        for (uint32_t k=0;k<count;++k)
        {
            const FactoryStructure* b=k ? &c->branches[k-1] : 0;Vec3 position=simulation_structure_position(s,p,k);
            fprintf(file,"%s{\"health\":%.6f,\"growth\":%.6f,\"wounds\":%u,\"generation\":%u,\"position\":[%.6f,%.6f,%.6f]}",k ? "," : "",
                k ? b->health : c->health,k ? b->growth : c->growth,k ? b->wounds : c->wounds,k ? b->generation : c->generation,position.x,position.y,position.z);
        }
        fprintf(file,"]}");
    }
    fprintf(file,"],\"units\":[");bool first=true;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0)
        {
            const Unit* u=&s->units[i];
            uint32_t ready=0;for (uint32_t j=0;j<s->world.spikes.count;++j) ready+=u->spikes[j]>=1;
            fprintf(file,"%s{\"slot\":%u,\"generation\":%u,\"faction\":%d,\"kind\":%d,\"health\":%.6f,\"target\":%d,\"position\":[%.6f,%.6f,%.6f],\"velocity\":[%.6f,%.6f,%.6f],\"bank\":%.6f,\"selected\":%s,\"wounds\":%u,\"ready_spikes\":%u,\"flash\":%.6f,\"visible\":%s,\"moving\":%s}",
                first ? "" : ",",i,u->generation,(int)u->faction,(int)simulation_unit_definition(s,u)->group,u->health,u->target,u->position.x,u->position.y,u->position.z,u->velocity.x,u->velocity.y,u->velocity.z,u->bank,u->selected ? "true" : "false",u->wounds,ready,u->flash,s->visible_units[i] ? "true" : "false",u->moving ? "true" : "false");first=false;
        }
    uint32_t projectiles=0,pops=0,trails=0,flashes=0,low=0,impacts=0;
    for (uint32_t i=0;i<MAX_PROJECTILES;++i) projectiles+=s->projectiles[i].active;
    for (uint32_t i=0;i<MAX_POPS;++i) { pops+=s->pops[i].duration>0;impacts+=s->impacts[i].duration>0; }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        trails+=s->trails[i].count>1;flashes+=s->units[i].health>0 && s->units[i].flash>0;
        low+=s->units[i].health>0 && s->units[i].wounds+1>=simulation_unit_definition(s,&s->units[i])->hits;
    }
    fprintf(file," ],\"recovery\":{\"healed\":%u,\"regrown\":%u,\"detached\":%u},\"order\":{\"cell\":%d,\"tick\":%u,\"count\":%u},\"effects\":{\"impacts\":%u,\"shots\":%u,\"hits\":%u,\"deaths\":%u,\"projectiles\":%u,\"pops\":%u,\"trails\":%u,\"flashes\":%u,\"low_health\":%u,\"last_flash_tick\":%u,\"last_low_tick\":%u,\"last_death_tick\":%u},\"biology\":%d,\"player_faction\":%d,\"ambient\":[%.6f,%.6f,%.6f]}\n",s->healed,s->regrown,s->detached,s->order_body,s->order_tick,s->order_count,impacts,s->shots,s->hits,s->deaths,projectiles,pops,trails,flashes,low,s->last_flash_tick,s->last_low_tick,s->last_death_tick,(int)s->world.player_faction,(int)s->world.player_faction,s->world.ambient.r,s->world.ambient.g,s->world.ambient.b);bool valid=!ferror(file);if (fclose(file)!=0) valid=false;return valid;
}
