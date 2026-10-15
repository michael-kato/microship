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

static int home_cell(const Simulation* s, Faction faction)
{
    for (uint32_t i=0;i<s->world.body_count;++i)
        if (s->home_owner[i]==faction) return (int)i;
    return NO_CELL;
}

static bool spawn(Simulation* s, uint32_t model, Vec3 position, int target)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health<=0)
        {
            const Ship* ship=&s->world.ships[model];
            Unit unit={};
            unit.position=position;unit.forward=Vec3{0,0,1};unit.model=model;unit.faction=ship->faction;
            unit.kind=ship->large ? UNIT_SEEDSHIP : UNIT_FIGHTER;
            unit.health=ship->large ? s->world.gameplay.seed_health : s->world.gameplay.fighter_health;
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
    *s=Simulation();s->selected_cell=s->order_cell=NO_CELL;
    if (!world_load(&s->world,world_path)) return false;
    s->sun_origin=s->world.sun.position;
    for (uint32_t i=0;i<s->world.body_count;++i) s->home_owner[i]=s->world.bodies[i].owner;
    if (!s->world.gameplay.enabled) return true;
    bool templates[3][2]={};
    for (uint32_t i=0;i<s->world.ship_count;++i)
    {
        const Ship* ship=&s->world.ships[i];
        if (ship->faction!=FACTION_PLAYER && ship->faction!=FACTION_ENEMY)
        {
            fprintf(stderr,"Gameplay prototype supports player and enemy units only.\n");return false;
        }
        templates[ship->faction][ship->large ? 1 : 0]=true;
        int home=home_cell(s,ship->faction);
        if (home<0) { fprintf(stderr,"Gameplay units require a home planet.\n");return false; }
        spawn(s,i,ship->position,ship->faction!=s->world.player_faction || (s->world.combat.enabled && !ship->large) ? home : NO_CELL);
    }
    for (uint32_t f=1;f<=2;++f)
        if (!templates[f][0] || !templates[f][1])
        { fprintf(stderr,"Gameplay requires fighter and seedship models for both factions.\n");return false; }
    for (uint32_t i=0;i<s->world.body_count;++i)
        if (!geometry_cell_surface_point(&s->world.bodies[i],&s->colonies[i].surface)) return false;
    snprintf(s->message,sizeof(s->message),s->world.biology ? "2: COLONIZERS  RIGHT-CLICK YOUR CELL TO ESTABLISH A NODE" : "2: SEEDSHIPS  RIGHT-CLICK HOME TO PLANT");
    return true;
}

void simulation_set_time(Simulation* s, double seconds)
{
    s->time_seconds=seconds;
    World* world=&s->world;
    if (world->orbit_period<=0) return;
    double angle=std::fmod(seconds,(double)world->orbit_period)*6.283185307179586/world->orbit_period;
    Vec3 v=subtract(s->sun_origin,world->orbit_center),axis=world->orbit_axis;
    Vec3 rotated=add(add(scale(v,(float)std::cos(angle)),scale(cross(axis,v),(float)std::sin(angle))),
                     scale(axis,dot(axis,v)*(float)(1.0-std::cos(angle))));
    world->sun.position=add(world->orbit_center,rotated);
}

void simulation_select(Simulation* s, int group)
{
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        u->selected=u->health>0 && u->faction==s->world.player_faction && (group==2 || (int)u->kind==group);
        if (u->selected) ++count;
    }
    snprintf(s->message,sizeof(s->message),"SELECTED %u %s",count,group==0 ? "FIGHTERS" : group==1 ? (s->world.biology ? "COLONIZERS" : "SEEDSHIPS") : "UNITS");
}

void simulation_order(Simulation* s, uint32_t planet)
{
    if (planet>=s->world.body_count || s->outcome) return;
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].selected && s->units[i].health>0)
        { s->units[i].target=(int)planet;s->units[i].landing=0;++count; }
    s->selected_cell=(int)planet;
    if (count) { s->order_cell=(int)planet;s->order_tick=s->tick_count;s->order_count=count; }
    if (count) snprintf(s->message,sizeof(s->message),"%u UNITS ORDERED TO %s %u",count,s->world.biology ? "CELL" : "PLANET",planet+1);
    else snprintf(s->message,sizeof(s->message),"SELECT FIGHTERS OR COLONIZERS FIRST");
}

void simulation_priority(Simulation* s, UnitKind kind)
{
    int p=s->selected_cell;
    if (p<0 || (uint32_t)p>=s->world.body_count || s->colonies[p].health<=0 || s->colonies[p].faction!=s->world.player_faction)
    { snprintf(s->message,sizeof(s->message),s->world.biology ? "SELECT YOUR CELL NODE FIRST" : "SELECT YOUR STALK'S PLANET FIRST");return; }
    s->colonies[p].priority=kind;
    snprintf(s->message,sizeof(s->message),"NEXT UNIT: %s",kind==UNIT_FIGHTER ? "FIGHTER" : s->world.biology ? "COLONIZER" : "SEEDSHIP");
}

static Vec3 colony_tip(const Simulation* s, uint32_t p)
{
    return add(s->colonies[p].surface,scale(s->world.bodies[p].colony_normal,
               s->world.gameplay.stalk_height*std::fmax(.02f,s->colonies[p].growth)));
}

static float legacy_solar_uptake(const Simulation* s, uint32_t p)
{
    Vec3 origin=colony_tip(s,p),v=subtract(s->world.sun.position,origin);
    float distance=length(v);Vec3 direction=scale(v,1.0f/distance);
    float cosine=std::fmax(0.0f,dot(direction,s->world.bodies[p].colony_normal));
    if (cosine<=0) return 0;
    for (uint32_t i=0;i<s->world.body_count;++i)
    {
        Vec3 offset=subtract(origin,s->world.bodies[i].position);
        float along=dot(offset,direction),radius=s->world.bodies[i].radius;
        float discriminant=along*along-dot(offset,offset)+radius*radius;
        if (discriminant>0)
        {
            float near=-along-std::sqrt(discriminant),far=-along+std::sqrt(discriminant);
            if (far>.001f && near<distance && (i!=p || near>.001f)) return 0;
        }
    }
    return cosine;
}

static bool defended(const Simulation* s, uint32_t p, Faction faction)
{
    const Cell* planet=&s->world.bodies[p];
    if (s->colonies[p].health>0 && s->colonies[p].faction!=faction) return true;
    float radius=planet->radius+s->world.gameplay.fighter_range;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].kind==UNIT_FIGHTER && s->units[i].faction!=faction
            && length(subtract(s->units[i].position,planet->position))<radius) return true;
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
    const GameplayRules* rules=&s->world.gameplay;
    const CombatVisuals* flight=&s->world.combat;
    Vec3 positions[MAX_UNITS],velocities[MAX_UNITS];
    for (uint32_t i=0;i<MAX_UNITS;++i) { positions[i]=s->units[i].position;velocities[i]=s->units[i].velocity; }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (u->health<=0 || u->target<0) continue;
        Vec3 combat_target={};bool strafe=false;
        uint32_t p=(uint32_t)u->target;
        Vec3 goal=add(s->colonies[p].surface,scale(s->world.bodies[p].colony_normal,u->kind==UNIT_FIGHTER ? 1.3f : .25f));
        if (u->kind==UNIT_FIGHTER)
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
            for (uint32_t j=0;j<MAX_UNITS;++j)
                if (s->units[j].health>0 && s->units[j].faction!=u->faction)
                {
                    Vec3 enemy=flight->flight_enabled ? positions[j] : s->units[j].position;
                    float target_distance=length(subtract(enemy,s->world.bodies[p].position));
                    float distance=length(subtract(enemy,u->position));
                    if (target_distance< s->world.bodies[p].radius+rules->fighter_range+2 && distance<nearest)
                    { nearest=distance;goal=enemy; }
                }
            if (flight->flight_enabled && nearest<rules->fighter_range*1.1f)
            { combat_target=goal;strafe=true; }
            else if (nearest<rules->fighter_range*.85f) goal=u->position;
            else if (nearest==1e10f && s->colonies[p].health>0 && s->colonies[p].faction!=u->faction)
            {
                goal=colony_tip(s,p);
                if (flight->flight_enabled && length(subtract(goal,u->position))<rules->fighter_range*1.1f)
                { combat_target=goal;strafe=true; }
                else if (length(subtract(goal,u->position))<rules->fighter_range*.85f) goal=u->position;
            }
        }
        Vec3 delta=subtract(goal,u->position);float distance=length(delta);
        float speed=u->kind==UNIT_FIGHTER ? rules->fighter_speed : rules->seed_speed;
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
                    desired=add(scale(side,speed*.7f),scale(away,(rules->fighter_range*.72f-d)*3));
                }
            }
            // Read a stable pass input: separation must not depend on unit update order.
            for (uint32_t j=0;j<MAX_UNITS;++j)
                if (j!=i && s->units[j].health>0)
                {
                    Vec3 apart=subtract(positions[i],positions[j]);float d=length(apart);
                    float spacing=flight->separation*(u->kind==UNIT_SEEDSHIP || s->units[j].kind==UNIT_SEEDSHIP ? 2.0f : 1.0f);
                    if (d<spacing && d>.0001f) desired=add(desired,scale(apart,speed*(1-d/spacing)/d));
                }
            // Steer around a cell before the hard collision shell becomes necessary.
            for (uint32_t j=0;j<s->world.body_count;++j)
            {
                if (u->kind==UNIT_SEEDSHIP && j==p && distance<1.5f) continue;
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
        // Conservative spherical collision keeps transit outside planet terrain.
        for (uint32_t p2=0;p2<s->world.body_count;++p2)
        {
            Vec3 offset=subtract(u->position,s->world.bodies[p2].position);float radial=length(offset);
            float clearance=s->world.bodies[p2].radius*1.08f+.12f;
            if (u->kind==UNIT_SEEDSHIP && p2==p && distance<1.5f) continue;
            if (radial<clearance && radial>.0001f)
                u->position=add(s->world.bodies[p2].position,scale(offset,clearance/radial));
        }
    }
}

static void legacy_combat(Simulation* s, float dt)
{
    float damage[MAX_UNITS]={},colony_damage[MAX_BODIES]={};
    float range=s->world.gameplay.fighter_range;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* u=&s->units[i];
        if (u->health<=0 || u->kind!=UNIT_FIGHTER) continue;
        int target=-1;float nearest=range;
        for (uint32_t j=0;j<MAX_UNITS;++j)
            if (s->units[j].health>0 && s->units[j].faction!=u->faction)
            {
                float distance=length(subtract(u->position,s->units[j].position));
                if (distance<nearest && clear_shot(s,u->position,s->units[j].position)) { nearest=distance;target=(int)j; }
            }
        if (target>=0) damage[target]+=s->world.gameplay.fighter_damage*dt;
        else for (uint32_t p=0;p<s->world.body_count;++p)
            if (s->colonies[p].health>0 && s->colonies[p].faction!=u->faction
                && length(subtract(colony_tip(s,p),u->position))<range && clear_shot(s,u->position,colony_tip(s,p)))
            { colony_damage[p]+=s->world.gameplay.fighter_damage*dt;break; }
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && damage[i]>0)
        {
            s->units[i].health=std::fmax(0.0f,s->units[i].health-damage[i]);
            if (s->units[i].health==0) { ++s->losses;s->units[i].selected=false; }
        }
    for (uint32_t p=0;p<s->world.body_count;++p)
        if (s->colonies[p].health>0)
        {
            s->colonies[p].health=std::fmax(0.0f,s->colonies[p].health-colony_damage[p]);
            if (s->colonies[p].health==0)
            {
                s->colonies[p].energy=0;s->colonies[p].building=false;s->colonies[p].growth=0;
                s->world.bodies[p].owner=FACTION_NEUTRAL;
            }
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
        Trail* t=&s->trails[i];if (u->health<=0 || u->kind!=UNIT_FIGHTER) { t->count=0;continue; }
        t->timer+=dt;if (t->timer<v->trail_lifetime/(TRAIL_POINTS-1)) continue;
        t->timer=0;uint32_t count=t->count<TRAIL_POINTS ? t->count : TRAIL_POINTS-1;
        for (uint32_t j=count;j>0;--j) t->points[j]=t->points[j-1];
        t->points[0]=u->position;t->count=count+1;
    }
}

static void recover_fighters(Simulation* s,float dt)
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
        if (u->kind==UNIT_FIGHTER && u->target>=0)
        {
            uint32_t cell=(uint32_t)u->target;const Cell* c=&s->world.bodies[cell];
            friendly=c->owner==u->faction && length(subtract(u->position,c->position))<c->radius+r->heal_clearance;
        }
        if (!friendly || !u->wounds || u->damage_age<r->heal_delay) { u->heal_timer=0;continue; }
        u->heal_timer+=dt;
        if (u->heal_timer>=r->heal_interval)
        {
            u->heal_timer=0;--u->wounds;u->embedded[u->wounds]=EmbeddedSpike();
            u->health=s->world.gameplay.fighter_health*(3-u->wounds)/3.0f;++s->healed;
        }
    }
}

static void projectile_combat(Simulation* s,float dt)
{
    const CombatVisuals* v=&s->world.combat;const GameplayRules* rules=&s->world.gameplay;
    uint32_t incoming[MAX_UNITS+MAX_BODIES]={};
    if (s->world.spikes.enabled) for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        const Projectile* p=&s->projectiles[i];if (p->damage<=0) continue;
        bool valid=p->colony ? s->colonies[p->target].generation==p->generation : s->units[p->target].generation==p->generation;
        if (valid) ++incoming[p->colony ? MAX_UNITS+p->target : p->target];
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health<=0 || u->kind!=UNIT_FIGHTER || u->cooldown>0) continue;
        int target=-1;bool colony=false;float nearest=rules->fighter_range;
        for (uint32_t j=0;j<MAX_UNITS;++j)
            if (s->units[j].health>0 && s->units[j].faction!=u->faction)
            {
                float d=length(subtract(u->position,s->units[j].position));
                if (d<nearest && clear_shot(s,u->position,s->units[j].position)) { nearest=d;target=(int)j; }
            }
        if (target<0) for (uint32_t j=0;j<s->world.body_count;++j)
            if (s->colonies[j].health>0 && s->colonies[j].faction!=u->faction)
            {
                float d=length(subtract(u->position,colony_tip(s,j)));
                if (d<nearest && clear_shot(s,u->position,colony_tip(s,j))) { nearest=d;target=(int)j;colony=true; }
            }
        if (target<0) continue;
        uint32_t slot=0;while (slot<MAX_PROJECTILES && s->projectiles[slot].damage>0) ++slot;
        if (slot==MAX_PROJECTILES) continue; // Wait for capacity; never apply invisible damage.
        Vec3 destination=colony ? colony_tip(s,(uint32_t)target) : s->units[target].position;
        Vec3 start=u->position;float launch_scale=s->world.spikes.scale;
        if (s->world.spikes.enabled && u->faction==FACTION_PLAYER)
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
            float size=s->world.ships[u->model].scale;
            start=add(start,scale(outward,s->world.spikes.root*size));launch_scale=size*s->world.spikes.length;
            u->spikes[spike]=0;++s->detached;
        }
        Projectile p={};p.position=p.previous=start;p.target=target;p.colony=colony;p.faction=u->faction;
        if (s->world.spikes.enabled)
        {
            uint32_t index=colony ? MAX_UNITS+(uint32_t)target : (uint32_t)target;
            uint32_t wounds=colony ? s->colonies[target].wounds : s->units[target].wounds;
            uint32_t socket=(wounds+incoming[index]++)%3;
            Vec3 outward=subtract(start,destination);float distance=length(outward);
            outward=distance>.0001f ? scale(outward,1/distance) : Vec3{0,0,1};
            Vec3 axis=std::fabs(outward.y)<.95f ? Vec3{0,1,0} : Vec3{1,0,0};
            Vec3 x=normalize(cross(axis,outward)),y=cross(outward,x);float angle=socket*2.094395102f;
            Vec3 normal=normalize(add(scale(outward,.75f),add(scale(x,.65f*std::cos(angle)),scale(y,.65f*std::sin(angle)))));
            p.anchor=colony ? scale(normal,rules->stalk_width*.18f) : local_direction(&s->units[target],scale(normal,.55f));
            destination=add(destination,colony ? p.anchor : scale(world_direction(&s->units[target],p.anchor),s->world.ships[s->units[target].model].scale));
        }
        Vec3 aim=subtract(destination,start);float aim_length=length(aim);p.direction=aim_length>.0001f ? scale(aim,1/aim_length) : u->forward;p.launch_scale=launch_scale;
        p.generation=colony ? s->colonies[target].generation : s->units[target].generation;
        p.damage=rules->fighter_damage*v->shot_interval;s->projectiles[slot]=p;
        u->cooldown=v->shot_interval;++s->shots;
    }
    float damage[MAX_UNITS]={},colony_damage[MAX_BODIES]={};
    uint32_t hit_count[MAX_UNITS+MAX_BODIES]={};EmbeddedSpike landed[MAX_UNITS+MAX_BODIES][2]={};
    for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        Projectile* p=&s->projectiles[i];if (p->damage<=0) continue;
        bool valid=p->colony ? s->colonies[p->target].health>0 && s->colonies[p->target].faction!=p->faction && s->colonies[p->target].generation==p->generation
            : s->units[p->target].health>0 && s->units[p->target].generation==p->generation;
        p->age+=dt;if (!valid || p->age>4) { p->damage=0;continue; }
        Vec3 target=p->colony ? colony_tip(s,p->target) : s->units[p->target].position;
        if (s->world.spikes.enabled) target=add(target,p->colony ? p->anchor :
            scale(world_direction(&s->units[p->target],p->anchor),s->world.ships[s->units[p->target].model].scale));
        Vec3 delta=subtract(target,p->position);float d=length(delta),step=v->projectile_speed*dt;
        Vec3 next=d<=step ? target : add(p->position,scale(delta,step/d));
        if (!clear_shot(s,p->position,next)) { p->damage=0;continue; }
        p->previous=p->position;p->position=next;
        if (d>.0001f) p->direction=scale(delta,1/d);
        if (d<=step)
        {
            if (p->colony) colony_damage[p->target]+=p->damage;else damage[p->target]+=p->damage;
            if (s->world.spikes.enabled)
            {
                uint32_t index=p->colony ? MAX_UNITS+(uint32_t)p->target : (uint32_t)p->target;
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
            p->damage=0;++s->hits;
        }
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && damage[i]>0)
        {
            Unit* u=&s->units[i];
            if (s->world.spikes.enabled)
            {
                uint32_t old=u->wounds;u->wounds=std::min(3u,old+hit_count[i]);
                for (uint32_t j=old;j<u->wounds && j<2;++j) u->embedded[j]=landed[i][j-old];
                float maximum=u->kind==UNIT_FIGHTER ? rules->fighter_health : rules->seed_health;
                u->health=maximum*(3-u->wounds)/3.0f;u->damage_age=u->heal_timer=0;
            }
            else u->health=std::fmax(0.0f,u->health-damage[i]);
            u->flash=v->flash_seconds;
            if (u->health>0)
            {
                s->last_flash_tick=s->tick_count;
                float max_health=u->kind==UNIT_FIGHTER ? rules->fighter_health : rules->seed_health;
                if (s->world.spikes.enabled ? u->wounds>=2 : u->health<max_health*v->low_health) s->last_low_tick=s->tick_count;
            }
            if (u->health==0) { pop_body(s,u->position,u->faction,true);++s->losses;u->selected=false;s->trails[i].count=0; }
        }
    for (uint32_t i=0;i<s->world.body_count;++i)
        if (s->colonies[i].health>0 && colony_damage[i]>0)
        {
            Colony* c=&s->colonies[i];
            if (s->world.spikes.enabled)
            {
                uint32_t old=c->wounds;c->wounds=std::min(3u,old+hit_count[MAX_UNITS+i]);
                for (uint32_t j=old;j<c->wounds && j<2;++j) c->embedded[j]=landed[MAX_UNITS+i][j-old];
                c->health=rules->stalk_health*(3-c->wounds)/3.0f;
            }
            else c->health=std::fmax(0.0f,c->health-colony_damage[i]);
            if (c->health==0)
            {
                if (s->world.spikes.enabled) pop_body(s,colony_tip(s,i),c->faction,false);
                c->energy=0;c->building=false;c->growth=0;s->world.bodies[i].owner=FACTION_NEUTRAL;
            }
        }
}

static void land_seeds(Simulation* s, float dt)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (u->health<=0 || u->kind!=UNIT_SEEDSHIP || u->target<0) continue;
        uint32_t p=(uint32_t)u->target;Colony* c=&s->colonies[p];
        if (defended(s,p,u->faction))
        {
            if (u->landing>=0) ++s->blocked_landings;
            u->landing=-1;
            if (u->selected) snprintf(s->message,sizeof(s->message),"LANDING BLOCKED: CLEAR DEFENDERS WITH FIGHTERS");
            continue;
        }
        if (u->landing<0) u->landing=0;
        if (c->health>0) continue;
        Vec3 goal=add(c->surface,scale(s->world.bodies[p].colony_normal,.25f));
        if (length(subtract(u->position,goal))>.35f) continue;
        u->landing+=dt;
        if (u->landing<s->world.gameplay.landing_seconds) continue;
        Vec3 surface=c->surface;uint32_t generation=c->generation+1;*c=Colony();c->surface=surface;c->generation=generation;
        c->health=s->world.gameplay.stalk_health;c->growth=.02f;c->faction=u->faction;
        c->energy=0;c->manufacturing=0;c->priority=UNIT_FIGHTER;c->building=false;
        s->world.bodies[p].owner=u->faction;u->health=0;u->selected=false;++s->landings;
        if (u->faction==s->world.player_faction) snprintf(s->message,sizeof(s->message),s->world.biology ? "NODE ESTABLISHED: CELL %u" : "SEED ROOTED: STALK GROWING ON PLANET %u",p+1);
    }
}

static void grow_colonies(Simulation* s, float dt)
{
    const GameplayRules* rules=&s->world.gameplay;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        Colony* c=&s->colonies[p];
        if (c->health<=0) continue;
        // The colonizer supplies initial growth. Stored nutrients fund biological units;
        // historical space levels retain their explicit solar collection rule.
        c->growth=std::fmin(1.0f,c->growth+dt/rules->growth_seconds);
        c->uptake=s->world.biology ? s->world.bodies[p].nutrients : legacy_solar_uptake(s,p);
        c->energy=std::fmin(rules->storage,c->energy+dt*rules->collection_rate*c->uptake*c->growth*c->growth);
        if (c->growth<1) continue;
        if (!c->building)
        {
            float cost=c->priority==UNIT_FIGHTER ? rules->fighter_cost : rules->seed_cost;
            if (c->energy<cost) continue;
            c->energy-=cost;c->building=true;c->building_kind=c->priority;c->manufacturing=0;
        }
        float rate=s->world.ai_production_set && c->faction!=s->world.player_faction ? s->world.ai_production : 1;
        c->manufacturing=std::fmin(1.0f,c->manufacturing+dt*rate/rules->manufacture_seconds);
        if (c->manufacturing<1) continue;
        for (uint32_t m=0;m<s->world.ship_count;++m)
            if (s->world.ships[m].faction==c->faction && s->world.ships[m].large==(c->building_kind==UNIT_SEEDSHIP))
            {
                Vec3 normal=s->world.bodies[p].colony_normal;
                Vec3 axis=std::fabs(normal.z)<.9f ? Vec3{0,0,1} : Vec3{1,0,0};
                Vec3 tangent=normalize(cross(normal,axis));
                float angle=(s->next_generation+1)*2.39996323f,spread=s->world.ships[m].scale*2;
                Vec3 position=add(add(colony_tip(s,p),scale(normal,1.0f)),
                    add(scale(tangent,std::cos(angle)*spread),scale(cross(tangent,normal),std::sin(angle)*spread)));
                if (spawn(s,m,position,c->building_kind==UNIT_FIGHTER ? (int)p : NO_CELL))
                { c->building=false;c->manufacturing=0;c->capacity_blocked=false; }
                else c->capacity_blocked=true;
                break;
            }
    }
}

static void enemy_orders(Simulation* s)
{
    if (s->world.spikes.enabled)
    {
        Faction enemy=s->world.player_faction==FACTION_PLAYER ? FACTION_ENEMY : FACTION_PLAYER;
        int home=home_cell(s,enemy),target=-1;float nearest=1e30f;bool seed=false;
        bool attack=s->time_seconds>=s->world.gameplay.enemy_attack_seconds;
        for (uint32_t p=0;p<s->world.body_count;++p) if (s->world.bodies[p].owner!=enemy
            && (s->home_owner[p]==FACTION_NEUTRAL || attack))
        {
            float d=length(subtract(s->world.bodies[p].position,s->world.bodies[home].position));
            if (d<nearest) { nearest=d;target=(int)p; }
        }
        for (uint32_t i=0;i<MAX_UNITS;++i) seed=seed || (s->units[i].health>0 && s->units[i].faction==enemy && s->units[i].kind==UNIT_SEEDSHIP);
        for (uint32_t p=0;p<s->world.body_count;++p) if (s->colonies[p].health>0 && s->colonies[p].faction==enemy)
            s->colonies[p].priority=!seed && target>=0 ? UNIT_SEEDSHIP : UNIT_FIGHTER;
        bool expand=s->time_seconds>=(s->world.ai_production_set ? s->world.ai_expansion : s->world.gameplay.enemy_attack_seconds);
        for (uint32_t i=0;i<MAX_UNITS;++i)
        {
            Unit* u=&s->units[i];if (u->health<=0 || u->faction!=enemy) continue;
            if (u->kind==UNIT_FIGHTER && expand && target>=0) u->target=target;
            if (u->kind==UNIT_SEEDSHIP && (u->target<0 || s->colonies[u->target].health>0))
                u->target=s->colonies[home].health<=0 ? home : expand ? target : NO_CELL;
        }
        return;
    }
    bool attack=s->time_seconds>=s->world.gameplay.enemy_attack_seconds;
    int home=home_cell(s,s->world.player_faction==FACTION_PLAYER ? FACTION_ENEMY : FACTION_PLAYER),target=home_cell(s,s->world.player_faction);
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (u->health<=0 || u->faction==s->world.player_faction) continue;
        if (u->kind==UNIT_FIGHTER && attack && target>=0) u->target=target;
        if (u->kind==UNIT_SEEDSHIP && u->target<0) u->target=home;
    }
}

static void outcome(Simulation* s)
{
    bool all=s->world.body_count>0,alive=false;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        bool player=s->colonies[p].health>0 && s->colonies[p].faction==s->world.player_faction;
        if (!s->world.spikes.enabled || s->home_owner[p]!=FACTION_NEUTRAL)
            all=all && player && s->colonies[p].growth>=1;
        alive=alive || player;
    }
    for (uint32_t i=0;i<MAX_UNITS;++i) alive=alive || (s->units[i].health>0 && s->units[i].faction==s->world.player_faction && s->units[i].kind==UNIT_SEEDSHIP);
    if (all) { s->outcome=1;snprintf(s->message,sizeof(s->message),s->world.biology ? "VICTORY: BOTH CELLS CONTROLLED  R TO RESTART" : "VICTORY: BOTH PLANETS ROOTED  R TO RESTART"); }
    else if (!alive) { s->outcome=-1;snprintf(s->message,sizeof(s->message),"COLONY LOST  R TO RESTART"); }
}

void simulation_step(Simulation* s)
{
    if (s->paused) return;
    const float dt=1.0f/60.0f;
    if (s->outcome)
    {
        if (s->world.combat.enabled) { advance_visuals(s,dt);recover_fighters(s,dt);for (uint32_t i=0;i<MAX_PROJECTILES;++i) s->projectiles[i].damage=0;simulation_set_time(s,s->time_seconds+1.0/60.0); }
        return;
    }
    if (s->world.gameplay.enabled)
    {
        enemy_orders(s);
        if (s->world.combat.enabled) advance_visuals(s,dt);
        recover_fighters(s,dt);move_units(s,dt);
        if (s->world.combat.enabled) projectile_combat(s,dt);else legacy_combat(s,dt);
        land_seeds(s,dt);grow_colonies(s,dt);outcome(s);
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
        fprintf(file,"%s{\"%s\":%u,\"faction\":%d,\"health\":%.6f,\"growth\":%.6f,\"%s\":%.6f,\"energy\":%.6f,\"building\":%s,\"priority\":%d,\"manufacturing\":%.6f,\"capacity_blocked\":%s,\"wounds\":%u}",
            p ? "," : "",s->world.biology ? "cell" : "planet",p,(int)c->faction,c->health,c->growth,s->world.biology ? "nutrients" : "sunlight",c->uptake,c->energy,c->building ? "true" : "false",(int)c->priority,c->manufacturing,c->capacity_blocked ? "true" : "false",c->wounds);
    }
    fprintf(file,"],\"units\":[");bool first=true;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0)
        {
            const Unit* u=&s->units[i];
            uint32_t ready=0;for (uint32_t j=0;j<s->world.spikes.count;++j) ready+=u->spikes[j]>=1;
            fprintf(file,"%s{\"slot\":%u,\"generation\":%u,\"faction\":%d,\"kind\":%d,\"health\":%.6f,\"target\":%d,\"position\":[%.6f,%.6f,%.6f],\"velocity\":[%.6f,%.6f,%.6f],\"bank\":%.6f,\"selected\":%s,\"wounds\":%u,\"ready_spikes\":%u,\"flash\":%.6f}",
                first ? "" : ",",i,u->generation,(int)u->faction,(int)u->kind,u->health,u->target,u->position.x,u->position.y,u->position.z,u->velocity.x,u->velocity.y,u->velocity.z,u->bank,u->selected ? "true" : "false",u->wounds,ready,u->flash);first=false;
        }
    uint32_t projectiles=0,pops=0,trails=0,flashes=0,low=0,impacts=0;
    for (uint32_t i=0;i<MAX_PROJECTILES;++i) projectiles+=s->projectiles[i].damage>0;
    for (uint32_t i=0;i<MAX_POPS;++i) { pops+=s->pops[i].duration>0;impacts+=s->impacts[i].duration>0; }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        trails+=s->trails[i].count>1;flashes+=s->units[i].health>0 && s->units[i].flash>0;
        float max_health=s->units[i].kind==UNIT_FIGHTER ? s->world.gameplay.fighter_health : s->world.gameplay.seed_health;
        low+=s->units[i].health>0 && (s->world.spikes.enabled ? s->units[i].wounds>=2 : s->units[i].health<max_health*s->world.combat.low_health);
    }
    fprintf(file," ],\"recovery\":{\"healed\":%u,\"regrown\":%u,\"detached\":%u},\"order\":{\"cell\":%d,\"tick\":%u,\"count\":%u},\"effects\":{\"impacts\":%u,\"shots\":%u,\"hits\":%u,\"deaths\":%u,\"projectiles\":%u,\"pops\":%u,\"trails\":%u,\"flashes\":%u,\"low_health\":%u,\"last_flash_tick\":%u,\"last_low_tick\":%u,\"last_death_tick\":%u},\"biology\":%d,\"player_faction\":%d,\"sun_radius\":%.6f,\"ambient\":[%.6f,%.6f,%.6f]}\n",s->healed,s->regrown,s->detached,s->order_cell,s->order_tick,s->order_count,impacts,s->shots,s->hits,s->deaths,projectiles,pops,trails,flashes,low,s->last_flash_tick,s->last_low_tick,s->last_death_tick,(int)s->world.biology,(int)s->world.player_faction,s->world.sun.radius,s->world.ambient.r,s->world.ambient.g,s->world.ambient.b);bool valid=!ferror(file);if (fclose(file)!=0) valid=false;return valid;
}
