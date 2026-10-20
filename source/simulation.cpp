#include "simulation.h"
#include "vector_math.h"
#include "factory_shape.h"
#include "geometry.h"
#include "flight.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <algorithm>
#include <new>

static const float GOLDEN_ANGLE_RADIANS = 2.39996323f;
static const float BROOD_SPREAD_SCALE = 2.5f;
static const float BROOD_SPREAD_PHASE = 1.7f;
static const float UNIT_GRID_CELL_SIZE = 4;
static const float UNIT_GRID_MARGIN = 16;
static const float UNIT_GRID_CELL_GROWTH = 1.25f;
static const float UNIT_GRID_MOTION_MARGIN = 1;
static const uint32_t NEIGHBOR_HASH_X = 73856093u;
static const uint32_t NEIGHBOR_HASH_Y = 19349663u;
static const uint32_t NEIGHBOR_HASH_Z = 83492791u;
static const float UNIT_UP_AXIS_PARALLEL_LIMIT = .95f;
static const unsigned int BRANCH_RANDOM_MULTIPLIER = 1664525u;
static const unsigned int BRANCH_RANDOM_INCREMENT = 1013904223u;
static const int RANDOM_DISCARDED_BITS = 8;
static const int RANDOM_SIGNED_HALF_RANGE = 8388608;
static const unsigned int SEED_INDEX_MULTIPLIER = 747796405u;
static const int RANDOM_UNSIGNED_RANGE = 16777216;
static const int RESPONSE_PHASE_COUNT = 13;
static const int MAX_MOVE_COORDINATE = 1000000;
static const float POSITION_EPSILON = .0001f;
static const float ATTENTION_RETENTION_RADIUS_SCALE = 1.35f;
static const float FULL_TURN_RADIANS = 6.28318530718f;
static const float INITIAL_NEAREST_DISTANCE = 1e20f;
static const unsigned int TARGET_SEED_MULTIPLIER = 2891336453u;
static const float INITIAL_ENEMY_DISTANCE = 1e30f;
static const float INITIAL_FACTORY_GROWTH = .02f;
static const float FACTORY_AXIS_PARALLEL_LIMIT = .9f;
static const float CHILD_LIMB_LENGTH_SCALE = 1.8f;
static const float AXIAL_SPREAD = .12f;
static const float FAN_SPREAD = .8f;
static const float HYPHA_SPREAD = .5f;
static const float AXIAL_BEND = .04f;
static const float FAN_BEND = .14f;
static const float HYPHA_BEND = .3f;
static const float LIMB_RADIAL_DIRECTION_WEIGHT = 1.5f;
static const float BRANCH_JITTER_SCALE = .35f;
static const float BRANCH_MIN_HEIGHT_SCALE = .65f;
static const int BRANCH_HEIGHT_VARIANTS = 100;
static const float BRANCH_HEIGHT_STEP = .003f;
static const float BRANCH_NORMAL_JITTER_SCALE = .4f;
static const int HYPHA_CHAIN_LIMBS = 3;
static const int FIRE_COOLDOWN_PHASE_COUNT = 5;
static const float FIRE_COOLDOWN_PHASE_SECONDS = .04f;
static const float SHOT_ENDPOINT_EPSILON = .001f;
static const float UNIT_RATE_MIN_SCALE = .8f;
static const float UNIT_RATE_SCALE_RANGE = .4f;
static const int UNIT_RATE_VARIANTS = 7;
static const int UNIT_RATE_INTERVALS = 6;
static const float COLONIZE_SURFACE_OFFSET = .25f;
static const float LEGACY_ORBIT_SURFACE_OFFSET = 1.3f;
static const float ORBIT_AXIS_PARALLEL_LIMIT = .9f;
static const float LEGACY_ORBIT_SPREAD_PER_SQRT_INDEX = .06f;
static const float ORBIT_HEIGHT_PHASE_PER_GENERATION = .7f;
static const float STRAFE_ENTRY_RANGE_SCALE = 1.1f;
static const float ATTACK_STOP_RANGE_SCALE = .85f;
static const float STEERING_DIRECTION_EPSILON = .001f;
static const float STRAFE_PREFERRED_RANGE_SCALE = .72f;
static const float LANDING_APPROACH_RADIUS = 1.5f;
static const float CELL_AVOIDANCE_LOOKAHEAD_SECONDS = .35f;
static const float CELL_COLLISION_RADIUS_SCALE = 1.08f;
static const float CELL_AVOIDANCE_CLEARANCE = .6f;
static const float MIN_HEADING_SPEED = .02f;
static const int BANK_RESPONSE_GAIN = 5;
static const float CELL_COLLISION_CLEARANCE = .12f;
static const float MOVE_COMPLETION_RADIUS = .15f;
static const float ORBIT_HEIGHT_RADIUS_SCALE = .4f;
static const int FLIGHT_POSITION_GAIN = 3;
static const int ORBIT_RADIAL_WAVE_FREQUENCY = 2;
static const int ORBIT_HEIGHT_WAVE_FREQUENCY = 3;
static const int LAUNCH_GOAL_DISTANCE = 3;
static const float DEBRIS_VELOCITY_JITTER = .6f;
static const float DEBRIS_AXIS_JITTER = .35f;
static const float MIN_FRACTURE_SEGMENT_FRACTION = .001f;
static const float SPIKE_INDEX_AZIMUTH_RADIANS = 2.094395102f;
static const float CHILD_SUPPORT_GROWTH_FRACTION = .75f;
static const float HIT_ANCHOR_JITTER_SCALE = .18f;
static const float EMBEDDED_SPIKE_LENGTH_SCALE = .55f;
static const float LANDING_COMPLETION_RADIUS = .35f;
static const float PRODUCTION_AXIS_PARALLEL_LIMIT = .9f;
static const float NEW_UNIT_TIP_CLEARANCE_SCALE = .4f;
static const float SPIKE_HIT_OUTWARD_WEIGHT = .75f;
static const float SPIKE_HIT_TANGENT_WEIGHT = .65f;
static const float HIT_EFFECT_LIFETIME_SCALE = 1.8f;
static const float LEGACY_ORBIT_BASE_RADIUS_SCALE = .25f;

static float length(Vec3 v) { return std::sqrt(dot(v,v)); }
static void sound(Simulation* s,SoundKind kind,Vec3 position)
{
    ++s->sounds.totals[kind];
    if (kind!=SOUND_ORDER && kind!=SOUND_WIN && kind!=SOUND_LOSE && !simulation_in_sight(s,position,0)) return;
    if (s->sounds.count==MAX_SOUND_EVENTS) { ++s->sounds.dropped;return; }
    s->sounds.pending[s->sounds.count++]=SoundEvent{position,kind};
}

Vec3 simulation_spike_direction(uint32_t index,uint32_t count)
{
    float y=1-2*(index+.5f)/count,angle=index*GOLDEN_ANGLE_RADIANS,r=std::sqrt(std::fmax(0.0f,1-y*y));
    return Vec3{r*std::cos(angle),y,r*std::sin(angle)};
}
void simulation_unit_axes(const Unit* u,Vec3* right,Vec3* up,Vec3* forward)
{
    *forward=u->forward;Vec3 axis=std::fabs(forward->y)<UNIT_UP_AXIS_PARALLEL_LIMIT ? Vec3{0,1,0} : Vec3{1,0,0};
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

static const float SPAWN_SCALE_SECONDS = .25f;
static const float SPAWN_START_SCALE = .3f;
static const float SPAWN_GROWTH_SCALE = .7f;
static const float LANDING_SHRINK_SCALE = .8f;
float simulation_unit_scale(const Simulation* s,const Unit* unit)
{
    float size=simulation_unit_definition(s,unit)->scale;
    if (!s->world.combat.flight_enabled) return size;
    float t=unit->hatched ? 1 : std::fmin(1.0f,unit->age/SPAWN_SCALE_SECONDS);t=t*t*(3-2*t);size*=SPAWN_START_SCALE+SPAWN_GROWTH_SCALE*t;
    if (unit->landing>0) size*=1-LANDING_SHRINK_SCALE*std::fmin(1.0f,unit->landing/s->world.gameplay.landing_seconds);
    return size;
}

Vec3 simulation_trail_origin(const Simulation* s,const Unit* unit)
{
    Vec3 local=simulation_unit_definition(s,unit)->trail_origin;
    Vec3 offset=world_direction(unit,local);
    if (!s->world.combat.flight_enabled)
    {
        float yaw=simulation_unit_definition(s,unit)->yaw_degrees*(float)(3.141592653589793/180.0);
        offset=Vec3{std::cos(yaw)*local.x+std::sin(yaw)*local.z,local.y,-std::sin(yaw)*local.x+std::cos(yaw)*local.z};
    }
    return add(unit->position,scale(offset,simulation_unit_scale(s,unit)));
}

static int home_body(const Simulation* s, Faction faction)
{
    for (uint32_t i=0;i<s->world.body_count;++i)
        if (s->home_owner[i]==faction && s->world.content.bodies[s->world.bodies[i].type].hosts_site) return (int)i;
    return NO_BODY;
}

static int32_t grid_axis(const UnitGrid& g,float value,uint32_t axis)
{
    float low=axis==0 ? g.low.x : axis==1 ? g.low.y : g.low.z;
    float cell=std::floor((value-low)*g.inverse_cell);
    return cell<0 ? 0 : cell>=g.size[axis] ? g.size[axis]-1 : (int32_t)cell;
}
static uint32_t grid_key(const UnitGrid& g,const Unit& u)
{
    uint32_t cell=((uint32_t)grid_axis(g,u.position.z,2)*g.size[1]+grid_axis(g,u.position.y,1))*g.size[0]+grid_axis(g,u.position.x,0);
    return cell*g.slots+g.slot[u.faction];
}
// Covers the cells and a margin; units outside clamp to the border cells.
static void grid_bounds(Simulation* s)
{
    const World& w=s->world;Vec3 low=w.bodies[0].position,high=low;
    for (uint32_t p=0;p<w.body_count;++p)
    {
        Vec3 c=w.bodies[p].position;float r=w.bodies[p].radius;
        low=Vec3{std::fmin(low.x,c.x-r),std::fmin(low.y,c.y-r),std::fmin(low.z,c.z-r)};
        high=Vec3{std::fmax(high.x,c.x+r),std::fmax(high.y,c.y+r),std::fmax(high.z,c.z+r)};
    }
    UnitGrid& g=s->grid;g.low=subtract(low,Vec3{UNIT_GRID_MARGIN,UNIT_GRID_MARGIN,UNIT_GRID_MARGIN});g.slots=0;
    for (uint32_t f=1;f<MAX_FACTIONS;++f) if (w.content.factions[f].name[0]) { g.slot[f]=g.slots;g.slot_faction[g.slots++]=f; }
    Vec3 span=add(subtract(high,low),scale(Vec3{1,1,1},2*UNIT_GRID_MARGIN));
    for (float cell=UNIT_GRID_CELL_SIZE;;cell*=UNIT_GRID_CELL_GROWTH)
    {
        g.size[0]=(int32_t)std::ceil(span.x/cell);g.size[1]=(int32_t)std::ceil(span.y/cell);g.size[2]=(int32_t)std::ceil(span.z/cell);
        if ((uint32_t)g.size[0]*g.size[1]*g.size[2]*g.slots<=MAX_UNIT_GRID_KEYS) { g.inverse_cell=1/cell;break; }
    }
    float spacing=w.combat.separation*w.content.max_unit_separation;
    s->neighbors.inverse_cell=spacing>0 ? 1/spacing : 0;
}
static void grid_build(Simulation* s)
{
    UnitGrid& g=s->grid;uint32_t keys=(uint32_t)g.size[0]*g.size[1]*g.size[2]*g.slots;
    memset(g.start,0,(keys+1)*sizeof(g.start[0]));
    for (uint32_t i=0;i<MAX_UNITS;++i) if (s->units[i].health>0) ++g.start[grid_key(g,s->units[i])+1];
    for (uint32_t k=0;k<keys;++k) { g.start[k+1]+=g.start[k];g.cursor[k]=g.start[k]; }
    for (uint32_t i=0;i<MAX_UNITS;++i) if (s->units[i].health>0) g.units[g.cursor[grid_key(g,s->units[i])]++]=(uint16_t)i;
}
// Live units of the given factions (a bit per faction) in the cells overlapping
// the sphere: a superset that callers test by distance.
static uint32_t grid_query(const UnitGrid& g,Vec3 center,float radius,uint32_t factions,uint16_t* out)
{
    radius+=UNIT_GRID_MOTION_MARGIN;uint32_t count=0,wanted=0;
    for (uint32_t k=0;k<g.slots;++k) if (factions&(1u<<g.slot_faction[k])) wanted|=1u<<k;
    if (!wanted) return 0;
    bool all=wanted==(1u<<g.slots)-1;
    int32_t x0=grid_axis(g,center.x-radius,0),x1=grid_axis(g,center.x+radius,0);
    int32_t y0=grid_axis(g,center.y-radius,1),y1=grid_axis(g,center.y+radius,1);
    int32_t z0=grid_axis(g,center.z-radius,2),z1=grid_axis(g,center.z+radius,2);
    for (int32_t z=z0;z<=z1;++z) for (int32_t y=y0;y<=y1;++y)
    {
        uint32_t row=((uint32_t)z*g.size[1]+y)*g.size[0];
        if (all) { for (uint32_t k=g.start[(row+x0)*g.slots];k<g.start[(row+x1+1)*g.slots];++k) out[count++]=g.units[k];continue; }
        for (int32_t x=x0;x<=x1;++x) for (uint32_t slot=0;slot<g.slots;++slot) if (wanted&(1u<<slot))
        {
            uint32_t key=(row+x)*g.slots+slot;
            for (uint32_t k=g.start[key];k<g.start[key+1];++k) out[count++]=g.units[k];
        }
    }
    return count;
}
static uint32_t enemies_of(Faction faction) { return ~(1u<<faction); }
static uint32_t neighbor_bucket(int32_t x,int32_t y,int32_t z)
{ return ((uint32_t)x*NEIGHBOR_HASH_X^(uint32_t)y*NEIGHBOR_HASH_Y^(uint32_t)z*NEIGHBOR_HASH_Z)&(NEIGHBOR_BUCKETS-1); }
static int32_t neighbor_axis(const NeighborGrid& g,float value) { return (int32_t)std::floor(value*g.inverse_cell); }
static uint32_t neighbor_key(const NeighborGrid& g,Vec3 p) { return neighbor_bucket(neighbor_axis(g,p.x),neighbor_axis(g,p.y),neighbor_axis(g,p.z)); }
static void neighbors_build(Simulation* s)
{
    NeighborGrid& g=s->neighbors;if (g.inverse_cell<=0) return;
    memset(g.start,0,sizeof(g.start));
    for (uint32_t i=0;i<MAX_UNITS;++i) if (s->units[i].health>0) ++g.start[neighbor_key(g,s->units[i].position)+1];
    for (uint32_t k=0;k<NEIGHBOR_BUCKETS;++k) { g.start[k+1]+=g.start[k];g.cursor[k]=g.start[k]; }
    for (uint32_t i=0;i<MAX_UNITS;++i) if (s->units[i].health>0) g.units[g.cursor[neighbor_key(g,s->units[i].position)]++]=(uint16_t)i;
}
// Units in the 27 cells around the point, a superset of every unit within one
// cell width. Hash collisions within the block are visited once.
static uint32_t neighbors_query(const NeighborGrid& g,Vec3 center,uint16_t* out)
{
    if (g.inverse_cell<=0) return 0;
    int32_t x=neighbor_axis(g,center.x),y=neighbor_axis(g,center.y),z=neighbor_axis(g,center.z);
    uint32_t seen[27],seen_count=0,count=0;
    for (int32_t dz=-1;dz<=1;++dz) for (int32_t dy=-1;dy<=1;++dy) for (int32_t dx=-1;dx<=1;++dx)
    {
        uint32_t bucket=neighbor_bucket(x+dx,y+dy,z+dz);bool repeated=false;
        for (uint32_t k=0;k<seen_count;++k) repeated=repeated || seen[k]==bucket;
        if (repeated) continue;
        seen[seen_count++]=bucket;
        for (uint32_t k=g.start[bucket];k<g.start[bucket+1];++k) out[count++]=g.units[k];
    }
    return count;
}

bool simulation_in_sight(const Simulation* s,Vec3 position,float radius)
{
    if (s->world.sight_radius<=0) return true;
    float reach=s->world.sight_radius+radius;
    for (uint32_t p=0;p<s->world.body_count;++p)
        if (s->colonies[p].active && s->colonies[p].faction==s->world.player_faction
            && length(subtract(position,s->world.bodies[p].position))<reach+s->world.bodies[p].radius) return true;
    uint16_t near[MAX_UNITS];uint32_t count=grid_query(s->grid,position,reach,1u<<s->world.player_faction,near);
    for (uint32_t n=0;n<count;++n)
    {
        const Unit& u=s->units[near[n]];
        if (u.health>0 && u.faction==s->world.player_faction && length(subtract(position,u.position))<reach) return true;
    }
    return false;
}
static bool opening_orbit(const Simulation* s,const Unit& unit)
{
    return s->world.neutral_start && !s->faction_established[unit.faction] && !unit.commanded;
}
bool simulation_body_known(const Simulation* s,uint32_t p)
{ return p<s->world.body_count && (s->world.sight_radius<=0 || s->discovered[p]); }

void simulation_update_sight(Simulation* s)
{
    grid_build(s);
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
    Vec3 axis=std::fabs(normal->y)<UNIT_UP_AXIS_PARALLEL_LIMIT ? Vec3{0,1,0} : Vec3{0,0,1};
    *right=normalize(cross(axis,*normal));*up=cross(*normal,*right);
}
Vec3 simulation_structure_position(const Simulation* s,uint32_t p,uint32_t structure)
{
    if (simulation_producer_definition(s,p)->segment_path[0])
        return simulation_growth_point(s,p,structure,simulation_structure_growth(s,p,structure));
    const Colony* c=&s->colonies[p];Vec3 up=s->world.bodies[p].colony_normal;
    Vec3 base=c->surface;float growth=c->growth;
    if (s->world.gameplay.enabled)
    {
        Vec3 right,normal;simulation_producer_axes(simulation_producer_definition(s,p),&right,&up,&normal);
        if (structure) { base=add(base,c->branches[structure-1].offset);growth=c->branches[structure-1].growth; }
    }
    return add(base,scale(up,simulation_producer_definition(s,p)->height*std::fmax(INITIAL_FACTORY_GROWTH,growth)));
}

static uint32_t branch_random(uint32_t* seed)
{ *seed=*seed*BRANCH_RANDOM_MULTIPLIER+BRANCH_RANDOM_INCREMENT;return *seed; }
Vec3 simulation_growth_point(const Simulation* s,uint32_t p,uint32_t k,float growth)
{
    float t=std::fmax(0.0f,std::fmin(1.0f,growth))*GROWTH_SEGMENTS;
    uint32_t j=std::min((uint32_t)t,(uint32_t)GROWTH_SEGMENTS-1);
    const Vec3* points=s->colonies[p].stems[k];
    return add(points[j],scale(subtract(points[j+1],points[j]),t-j));
}
static float signed_random(uint32_t* seed) { return (branch_random(seed)>>RANDOM_DISCARDED_BITS)*(1.0f/RANDOM_SIGNED_HALF_RANGE)-1; }
float simulation_structure_growth(const Simulation* s,uint32_t p,uint32_t k)
{
    const Colony& c=s->colonies[p];const ProducerDefinition& d=*simulation_producer_definition(s,p);
    float growth=k ? c.branches[k-1].growth : c.growth;
    uint32_t wounds=k ? c.branches[k-1].wounds : c.wounds;
    uint32_t hits=c.parents[k]<0 ? d.hits : d.branches.hits;
    uint32_t lost=std::min((uint32_t)GROWTH_SEGMENTS,wounds*GROWTH_SEGMENTS/hits);
    return std::fmin(growth,(GROWTH_SEGMENTS-lost)/(float)GROWTH_SEGMENTS);
}
float simulation_production(const Simulation* s,uint32_t p)
{
    const Colony& c=s->colonies[p];const ProducerDefinition& d=*simulation_producer_definition(s,p);
    float production=0;
    for (uint32_t k=0;k<c.structure_count;++k)
    {
        if ((k ? c.branches[k-1].health : c.health)<=0) continue;
        float growth=simulation_structure_growth(s,p,k);
        production+=growth*growth*(c.parents[k]<0 ? 1.0f/c.root_count : d.branches.production);
    }
    return production;
}
static Vec3 factory_direction(Vec3 direction,const ProducerDefinition& definition)
{
    if (!definition.growth_plane_enabled) return direction;
    Vec3 n=definition.growth_plane_normal;float depth=dot(direction,n);
    Vec3 flat=subtract(direction,scale(n,depth));
    if (dot(flat,flat)<.000001f) flat=cross(n,std::fabs(n.y)<.9f ? Vec3{0,1,0} : Vec3{1,0,0});
    return normalize(add(normalize(flat),scale(n,depth*definition.growth_depth_ratio)));
}
float simulation_structure_scale(const Simulation* s,uint32_t p,uint32_t k)
{
    const Colony& c=s->colonies[p];const BranchRules& f=simulation_producer_definition(s,p)->branches;
    return f.grammar==GROWTH_TREE ? c.limb_scale[k] : c.parents[k]>=0 ? FACTORY_CHILD_STEM_WIDTH_SCALE : 1;
}
bool simulation_structure_terminal(const Simulation* s,uint32_t p,uint32_t k)
{
    const Colony& c=s->colonies[p];
    for (uint32_t j=k+1;j<c.structure_count;++j) if (c.parents[j]==(int32_t)k) return false;
    return true;
}
static uint32_t factory_count(const BranchRules& f,uint32_t roots)
{
    if (f.grammar!=GROWTH_TREE) return f.count;
    uint32_t count=0,level=roots;
    for (uint32_t i=0;i<f.levels;++i) { count+=level;level*=f.children; }
    return count;
}
static void depth_first(const Colony* c,uint32_t node,uint32_t* order,uint32_t* count)
{
    order[(*count)++]=node;
    for (uint32_t j=node+1;j<c->structure_count;++j) if (c->parents[j]==(int32_t)node) depth_first(c,j,order,count);
}
static uint32_t select_factory_roots(const Body& body,const BranchRules& f,uint32_t* indices)
{
    if (!f.organelle_roots) { indices[0]=0;return 1; }
    uint32_t count=0,limit=f.root_limit ? std::min(f.root_limit,body.factory_root_count) : body.factory_root_count;
    bool used[MAX_FACTORY_STRUCTURES]={};
    for (uint32_t k=0;k<limit;++k)
    {
        uint32_t best=body.factory_root_count;
        for (uint32_t j=0;j<body.factory_root_count;++j)
            if (!used[j] && body.factory_root_radii[j]>=f.root_minimum_radius
                && (best==body.factory_root_count || (f.root_limit && body.factory_root_radii[j]>body.factory_root_radii[best]))) best=j;
        if (best==body.factory_root_count) break;
        used[best]=true;indices[count++]=best;
    }
    return count;
}
static void build_factory_stem(Simulation* s,uint32_t p,uint32_t k,uint32_t* seed)
{
    Colony* c=&s->colonies[p];const BranchRules* f=&simulation_producer_definition(s,p)->branches;
    const Body& body=s->world.bodies[p];Vec3 right,up,normal;
    simulation_producer_axes(simulation_producer_definition(s,p),&right,&up,&normal);
    int32_t parent=c->parents[k];
    Vec3 start=parent>=0 ? c->stems[parent][GROWTH_SEGMENTS-1] : body.factory_roots[c->root_attachments[k]];
    c->stems[k][0]=start;
    Vec3 radial=normalize(subtract(start,body.position));
    Vec3 side=normalize(cross(std::fabs(radial.y)<FACTORY_AXIS_PARALLEL_LIMIT ? Vec3{0,1,0} : Vec3{1,0,0},radial));
    Vec3 other=cross(radial,side);float sign=k%2 ? -1.0f : 1.0f;
    float extent=parent>=0 ? f->spacing*CHILD_LIMB_LENGTH_SCALE : simulation_producer_definition(s,p)->height;
    float spread=f->grammar==GROWTH_AXIAL ? AXIAL_SPREAD : f->grammar==GROWTH_FAN ? FAN_SPREAD : HYPHA_SPREAD;
    float bend=f->grammar==GROWTH_AXIAL ? AXIAL_BEND : f->grammar==GROWTH_FAN ? FAN_BEND : HYPHA_BEND;
    if (f->grammar==GROWTH_TREE)
    {
        uint32_t depth=0;for (int32_t v=parent;v>=0;v=c->parents[v]) ++depth;
        extent=simulation_producer_definition(s,p)->height*std::pow(f->length_scale,(float)depth);bend=f->bend;spread=0;
        if (f->length_jitter>0) extent*=1+signed_random(seed)*f->length_jitter;
        if (f->width_jitter>0) c->limb_scale[k]=std::pow(f->width_scale,(float)depth)*(1+signed_random(seed)*f->width_jitter);
        if (parent>=0)
        {
            radial=normalize(subtract(c->stems[parent][GROWTH_SEGMENTS],c->stems[parent][GROWTH_SEGMENTS-1]));
            Vec3 axis=simulation_producer_definition(s,p)->growth_plane_normal;
            if (dot(axis,axis)<.01f) axis=normal;
            float angle=((k-c->root_count)%f->children-(f->children-1)*.5f)*2*f->angle;
            if (f->angle_jitter>0) angle+=signed_random(seed)*f->angle_jitter;
            radial=normalize(add(add(scale(radial,std::cos(angle)),scale(cross(axis,radial),std::sin(angle))),scale(axis,dot(axis,radial)*(1-std::cos(angle)))));
        }
        radial=factory_direction(radial,*simulation_producer_definition(s,p));
        Vec3 plane=simulation_producer_definition(s,p)->growth_plane_enabled ? simulation_producer_definition(s,p)->growth_plane_normal : normal;
        Vec3 tangent=cross(plane,radial);
        if (dot(tangent,tangent)<.000001f) tangent=cross(std::fabs(radial.y)<.9f ? Vec3{0,1,0} : Vec3{1,0,0},radial);
        side=normalize(tangent);other=cross(radial,side);
    }
    for (uint32_t j=1;j<=GROWTH_SEGMENTS;++j)
    {
        Vec3 direction=normalize(add(scale(radial,LIMB_RADIAL_DIRECTION_WEIGHT),add(scale(side,(parent>=0 ? sign*spread : 0)+signed_random(seed)*bend),scale(other,signed_random(seed)*bend))));
        direction=factory_direction(direction,*simulation_producer_definition(s,p));
        c->stems[k][j]=add(c->stems[k][j-1],scale(direction,extent/GROWTH_SEGMENTS));
        radial=f->grammar==GROWTH_TREE ? direction : normalize(subtract(c->stems[k][j],body.position));
    }
    if (k) c->branches[k-1].offset=subtract(start,c->surface);
}
static void regrow_factory_stems(Simulation* s,uint32_t p,uint32_t root,uint32_t generation)
{
    const BranchRules& f=simulation_producer_definition(s,p)->branches;
    Colony& c=s->colonies[p];
    for (uint32_t k=root;k<c.structure_count;++k)
    {
        int32_t ancestor=(int32_t)k;while (ancestor>(int32_t)root) ancestor=c.parents[ancestor];
        if (ancestor!=(int32_t)root) continue;
        uint32_t seed=f.seed^s->world.bodies[p].seed^(k*SEED_INDEX_MULTIPLIER)^(generation*TARGET_SEED_MULTIPLIER);
        build_factory_stem(s,p,k,&seed);
    }
}

static void prepare_factory(Simulation* s,uint32_t p)
{
    Colony* c=&s->colonies[p];const BranchRules* f=&simulation_producer_definition(s,p)->branches;
    Vec3 right,up,normal;simulation_producer_axes(simulation_producer_definition(s,p),&right,&up,&normal);
    uint32_t seed=f->seed+p*SEED_INDEX_MULTIPLIER;
    const Body& body=s->world.bodies[p];
    c->root_count=select_factory_roots(body,*f,c->root_attachments);
    c->structure_count=factory_count(*f,c->root_count);
    for (uint32_t k=0;k<c->structure_count;++k)
    {
        if (k<c->root_count) c->parents[k]=-1;
        else if (f->grammar==GROWTH_TREE) c->parents[k]=(k-c->root_count)/f->children;
        else if (!f->organelle_roots) c->parents[k]=(k-1)/2;
        else if (f->grammar==GROWTH_FAN) c->parents[k]=((k-c->root_count)/2)%c->root_count;
        else if (f->grammar==GROWTH_HYPHA && (k-c->root_count)%HYPHA_CHAIN_LIMBS) c->parents[k]=k-1;
        else c->parents[k]=f->grammar==GROWTH_AXIAL ? k-c->root_count : (k-c->root_count)%c->root_count;
        c->limb_scale[k]=c->parents[k]<0 ? 1 : c->limb_scale[c->parents[k]]*f->width_scale;
    }
    if (f->grammar==GROWTH_TREE)
    {
        uint32_t topology_seed=seed^body.seed^((c->generation-1)*TARGET_SEED_MULTIPLIER);
        c->structure_count=c->root_count;
        for (uint32_t parent=0;parent<c->structure_count;++parent)
        {
            uint32_t depth=0;for (int32_t node=c->parents[parent];node>=0;node=c->parents[node]) ++depth;
            // The first child of the first root stays a leaf, so production can start soon after colonization.
            if (depth+1>=f->levels || parent==c->root_count) continue;
            uint32_t children=1;
            for (uint32_t child=1;child<f->children;++child) if ((signed_random(&topology_seed)+1)*.5f<f->branch_probability) ++children;
            for (uint32_t child=0;child<children;++child)
            {
                uint32_t k=c->structure_count++;c->parents[k]=(int32_t)parent;
                c->limb_scale[k]=c->limb_scale[parent]*f->width_scale;
            }
        }
    }
    // Every root starts at colonization; later roots may start up to the authored offset after it.
    // Each structure grows for sprout_seconds. A root's limbs emerge depth first at even intervals,
    // so its first child follows the mature trunk and its last limb matures when establishment ends.
    float seconds=simulation_producer_definition(s,p)->establishment_seconds;
    float grow=std::fmin(simulation_producer_definition(s,p)->sprout_seconds,seconds);
    uint32_t timing_seed=seed^body.seed^(c->generation*TARGET_SEED_MULTIPLIER);
    for (uint32_t r=0;r<c->root_count;++r)
    {
        uint32_t order[MAX_FACTORY_STRUCTURES],count=0;depth_first(c,r,order,&count);
        float start=r ? (signed_random(&timing_seed)+1)*.5f*f->root_offset_seconds : 0;
        float first=start+grow,interval=count>2 ? std::fmax(0.0f,seconds-grow-first)/(count-2) : 0;
        for (uint32_t i=0;i<count;++i)
        { c->deployment_start[order[i]]=i ? first+(i-1)*interval : start;c->deployment_seconds[order[i]]=grow; }
    }
    if (simulation_producer_definition(s,p)->segment_path[0])
    {
        seed^=body.seed;
        seed^=(c->generation-1)*TARGET_SEED_MULTIPLIER;
        for (uint32_t k=0;k<c->structure_count;++k) build_factory_stem(s,p,k,&seed);
        return;
    }
    for (uint32_t j=1;j<c->structure_count;++j)
    {
        uint32_t parent=(j-1)/2,depth=0;for (uint32_t v=j+1;v>1;v>>=1) ++depth;
        float side=j%2 ? -1.0f : 1.0f;
        float width=f->spacing/std::sqrt((float)depth);
        float jitter=((branch_random(&seed)>>RANDOM_DISCARDED_BITS)*(1.0f/RANDOM_UNSIGNED_RANGE)-HYPHA_SPREAD)*width*BRANCH_JITTER_SCALE;
        Vec3 offset=add(scale(right,side*width+jitter),scale(up,width*(BRANCH_MIN_HEIGHT_SCALE+(branch_random(&seed)%BRANCH_HEIGHT_VARIANTS)*BRANCH_HEIGHT_STEP)));
        offset=add(offset,scale(normal,jitter*BRANCH_NORMAL_JITTER_SCALE));
        c->branches[j-1].offset=add(parent ? c->branches[parent-1].offset : Vec3{},offset);
    }
}

static void establish_colony(Simulation* s,uint32_t p,Faction faction)
{
    Colony* c=&s->colonies[p];Vec3 surface=c->surface;uint32_t generation=c->generation+1;
    *c=Colony();c->surface=surface;c->generation=generation;c->faction=faction;
    c->type=s->world.content.factions[faction].producer;
    c->health=simulation_producer_definition(s,p)->health;c->growth=INITIAL_FACTORY_GROWTH;c->priority=s->world.content.factions[faction].default_unit;
    c->fighter_percent=s->world.content.units[c->priority].group==0 ? 100 : 0;c->production_error=50;
    c->active=true;c->root_generation=1;
    s->world.bodies[p].owner=faction;s->faction_established[faction]=true;++s->faction_stats[faction].colonies_gained;
    if (s->world.gameplay.enabled) prepare_factory(s,p);
    if (simulation_producer_definition(s,p)->establishment_seconds>0) c->growth=0;
    else for (uint32_t k=1;k<c->root_count;++k) { c->branches[k-1].health=simulation_producer_definition(s,p)->health;c->branches[k-1].growth=INITIAL_FACTORY_GROWTH;c->branches[k-1].generation=1; }
    sound(s,SOUND_COLONIZE,c->surface);
}

static bool spawn(Simulation* s, uint32_t type, Vec3 position, int target,const Vec3* heading=0)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health<=0)
        {
            const UnitDefinition* definition=&s->world.content.units[type];
            Unit unit={};
            unit.latch=-1;
            unit.attention=-1;unit.response_timer=definition->response_seconds*((s->next_generation+1)%RESPONSE_PHASE_COUNT)/RESPONSE_PHASE_COUNT;
            unit.position=position;unit.forward=Vec3{0,0,1};unit.type=type;unit.faction=definition->faction;
            if (heading) { unit.forward=*heading;unit.hatched=true; }
            unit.health=definition->health;
            unit.target=target;unit.generation=++s->next_generation;
            for (uint32_t j=0;j<s->world.spikes.count;++j) unit.spikes[j]=1;
            for (uint32_t j=0;j<definition->payload_count;++j) unit.payloads[j]=1;
            unit.orbit_phase=unit.generation*GOLDEN_ANGLE_RADIANS;unit.cooldown=(unit.generation%FIRE_COOLDOWN_PHASE_COUNT)*FIRE_COOLDOWN_PHASE_SECONDS;
            s->trails[i]=Trail();s->trails[i].points[0]=simulation_trail_origin(s,&unit);s->trails[i].count=1;
            s->units[i]=unit;++s->births;++s->faction_stats[unit.faction].built;
            sound(s,SOUND_BIRTH,position);
            return true;
        }
    return false;
}

bool simulation_initialize(Simulation* s, const char* world_path,uint32_t seed)
{
    // Construct in place: a temporary of this size would not fit the 2 MB stack.
    s->~Simulation();new (s) Simulation();s->selected_body=s->order_body=NO_BODY;
    if (!world_load(&s->world,world_path,seed)) return false;
    grid_bounds(s);
    if (seed) for (uint32_t i=0;i<s->world.body_count;++i) s->world.bodies[i].seed^=seed;
    for (uint32_t i=0;i<s->world.body_count;++i) s->home_owner[i]=s->world.start_owner[i];
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
    {
        Body& b=s->world.bodies[i];const BodyDefinition& d=s->world.content.bodies[b.type];
        MeshSource source={b.model_path,b.position,b.radius,0,b.color,d.parts_path,b.seed,d.shape_variation,d.part_variation};
        if (d.hosts_site && !geometry_attachments(source,b.colony_normal,b.factory_roots,b.factory_root_radii,MAX_FACTORY_STRUCTURES,&b.factory_root_count)) return false;
        b.factory_root=b.factory_roots[0];
        for (uint32_t t=0;d.hosts_site && t<s->world.content.producer_count;++t)
            {
                const BranchRules& f=s->world.content.producers[t].branches;
                uint32_t indices[MAX_FACTORY_STRUCTURES];uint32_t roots=select_factory_roots(b,f,indices);
                if (!roots) { fprintf(stderr,"Factory root filter found no eligible large organelle.\n");return false; }
                uint32_t required=f.grammar==GROWTH_TREE ? factory_count(f,roots) : roots;
                if (f.count<required || required>MAX_FACTORY_STRUCTURES)
                { fprintf(stderr,"Factory structure capacity is smaller than the authored tree or attachment count.\n");return false; }
            }
        if (d.hosts_site && !geometry_surface_point(b.model_path,b.position,b.radius,b.colony_normal,&s->colonies[i].surface)) return false;
    }
    if (s->world.gameplay.enabled)
        for (uint32_t p=0;p<s->world.body_count;++p)
            if (s->world.bodies[p].owner!=FACTION_NEUTRAL && s->world.content.bodies[s->world.bodies[p].type].hosts_site)
            {
                establish_colony(s,p,s->home_owner[p]);s->colonies[p].growth=1;
                Colony& c=s->colonies[p];const ProducerDefinition& d=*simulation_producer_definition(s,p);
                c.establishment_age=d.establishment_seconds;
                for (uint32_t k=1;k<c.root_count;++k)
                { c.branches[k-1].health=d.health;c.branches[k-1].growth=1;c.branches[k-1].generation=1; }
            }
    if (s->world.auto_colonize && s->world.neutral_start)
        for (uint32_t i=0;i<MAX_UNITS;++i)
        {
            Unit& u=s->units[i];const UnitDefinition& d=*simulation_unit_definition(s,&u);
            if (u.health<=0 || u.faction!=s->world.player_faction || d.group!=0 || !d.colonizes) continue;
            u.commanded=true;u.task=TASK_COLONIZE;break;
        }
    simulation_update_sight(s);simulation_update_fog(s,0);
    snprintf(s->message,sizeof(s->message),"RIGHT-CLICK OPEN SPACE TO SCOUT. ORDER A KNOWN BODY TO INTERACT.");
    if (s->world.content.factions[s->world.player_faction].automatic)
    {
        s->selected_body=home_body(s,s->world.player_faction);
        snprintf(s->message,sizeof(s->message),"SEEKING SPORES LAUNCH AUTOMATICALLY FROM HOME.");
    }
    if (s->world.neutral_start)
    {
        s->selected_body=home_body(s,s->world.player_faction);
        snprintf(s->message,sizeof(s->message),s->world.auto_colonize ? "FIRST FIGHTER IS ESTABLISHING YOUR SITE" : "SELECT CRAFT / RIGHT-CLICK YOUR CELL TO BUILD");
    }
    return true;
}

void simulation_set_time(Simulation* s, double seconds)
{ s->time_seconds=seconds; }

void simulation_select(Simulation* s, int group)
{
    if (s->world.content.factions[s->world.player_faction].automatic) return;
    int home=home_body(s,s->world.player_faction);if (home>=0) sound(s,SOUND_ORDER,s->world.bodies[home].position);
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        u->selected=u->health>0 && u->faction==s->world.player_faction && !s->world.content.factions[u->faction].automatic && (group<0 || (int)simulation_unit_definition(s,u)->group==group);
        if (u->selected) { u->path_marker_tick=s->tick_count;++count; }
    }
    snprintf(s->message,sizeof(s->message),"SELECTED %u %s",count,group>=0 && group<MAX_GROUPS ? s->world.content.groups[group].plural : "UNITS");
}

void simulation_order(Simulation* s, uint32_t cell)
{
    if (!simulation_body_known(s,cell) || s->outcome<0) return;
    if (s->world.content.factions[s->world.player_faction].automatic) { s->selected_body=(int)cell;return; }
    sound(s,SOUND_ORDER,s->world.bodies[cell].position);
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].selected && s->units[i].health>0)
        {
            Unit& u=s->units[i];u.target=(int)cell;u.moving=false;u.landing=0;u.latch=-1;u.attention=-1;
            u.commanded=!s->colonies[cell].active || s->colonies[cell].faction!=u.faction;u.response_timer=0;u.path_marker_tick=s->tick_count;++count;
        }
    s->selected_body=(int)cell;
    if (count) { s->order_body=(int)cell;s->order_tick=s->tick_count;s->order_count=count; }
    if (count) snprintf(s->message,sizeof(s->message),"%u UNITS ORDERED TO %s %u",count,s->world.content.bodies[s->world.bodies[cell].type].label,cell+1);
    else snprintf(s->message,sizeof(s->message),"SELECT A GROUP FIRST");
}

void simulation_move_to(Simulation* s,Vec3 destination)
{
    if (s->world.content.factions[s->world.player_faction].automatic) return;
    if (s->outcome<0 || !std::isfinite(destination.x) || !std::isfinite(destination.y) || !std::isfinite(destination.z)
        || std::fabs(destination.x)>MAX_MOVE_COORDINATE || std::fabs(destination.y)>MAX_MOVE_COORDINATE || std::fabs(destination.z)>MAX_MOVE_COORDINATE) return;
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].selected)
        {
            Unit* u=&s->units[i];u->target=NO_BODY;u->destination=destination;u->moving=true;u->landing=0;u->latch=-1;++count;
            u->commanded=true;u->attention=-1;u->path_marker_tick=s->tick_count;
        }
    s->order_body=NO_BODY;s->order_count=count;s->order_tick=s->tick_count;
    snprintf(s->message,sizeof(s->message),"%u UNITS MOVING. SCOUT TO FIND CELLS.",count);
}

void simulation_priority_type(Simulation* s,uint32_t type)
{
    if (s->world.content.factions[s->world.player_faction].automatic) return;
    int p=s->selected_body;
    if (p<0 || !simulation_body_known(s,(uint32_t)p) || !s->colonies[p].active || s->colonies[p].faction!=s->world.player_faction)
    { snprintf(s->message,sizeof(s->message),"SELECT YOUR PRODUCTION SITE FIRST");return; }
    if (type>=s->world.content.unit_count || s->world.content.units[type].faction!=s->colonies[p].faction) return;
    Colony* c=&s->colonies[p];c->priority=type;c->ratio_enabled=false;
    c->fighter_percent=s->world.content.units[type].group==0 ? 100 : 0;c->production_error=50;
    snprintf(s->message,sizeof(s->message),"NEXT UNIT: %s",s->world.content.groups[s->world.content.units[type].group].singular);
}
void simulation_priority(Simulation* s,int group)
{
    int type=content_product(&s->world.content,s->world.player_faction,group);
    if (type>=0) simulation_priority_type(s,(uint32_t)type);
}

bool simulation_ratio_available(const Simulation* s,uint32_t p)
{
    if (!simulation_body_known(s,p)) return false;
    const Colony* c=&s->colonies[p];
    return c->active && c->faction==s->world.player_faction
        && !s->world.content.factions[c->faction].automatic
        && content_product(&s->world.content,c->faction,0)>=0 && content_product(&s->world.content,c->faction,1)>=0;
}

void simulation_production_ratio(Simulation* s,uint32_t percent)
{
    int p=s->selected_body;
    if (percent>100 || p<0 || !simulation_ratio_available(s,(uint32_t)p)) return;
    Colony* c=&s->colonies[p];
    if (!c->ratio_enabled || c->fighter_percent!=percent) c->production_error=50;
    c->fighter_percent=percent;c->ratio_enabled=true;
    snprintf(s->message,sizeof(s->message),"PRODUCTION: %u%% %.16s / %u%% %.16s",percent,s->world.content.groups[0].plural,100-percent,s->world.content.groups[1].plural);
}

static Vec3 colony_tip(const Simulation* s, uint32_t p)
{
    const Colony& c=s->colonies[p];
    for (uint32_t k=0;k<s->colonies[p].structure_count;++k)
        if ((k ? c.branches[k-1].health : c.health)>0) return simulation_structure_position(s,p,k);
    return c.surface;
}

static bool defended(const Simulation* s, uint32_t p, Faction faction)
{
    const Body* cell=&s->world.bodies[p];
    if (s->colonies[p].active && s->colonies[p].faction!=faction) return true;
    uint16_t near[MAX_UNITS];uint32_t count=grid_query(s->grid,cell->position,cell->radius+s->world.content.max_unit_range,enemies_of(faction),near);
    for (uint32_t n=0;n<count;++n)
    {
        const Unit& u=s->units[near[n]];
        if (u.health>0 && u.faction!=faction && length(subtract(u.position,cell->position))<cell->radius+simulation_unit_definition(s,&u)->range) return true;
    }
    return false;
}

// Digesters and spores must touch their target, so they may enter the cell they attack.
static bool contact_attack(const UnitDefinition& d) { return d.attack==ATTACK_DIGEST || d.attack==ATTACK_SPORE; }
static bool clear_shot(const Simulation* s, Vec3 from, Vec3 to,int32_t structure_cell=NO_BODY)
{
    Vec3 delta=subtract(to,from);float distance=length(delta);
    if (distance<POSITION_EPSILON) return true;
    Vec3 direction=scale(delta,1.0f/distance);
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        if ((int32_t)p==structure_cell) continue;
        Vec3 offset=subtract(from,s->world.bodies[p].position);float along=dot(offset,direction);
        float radius=s->world.bodies[p].radius,disc=along*along-dot(offset,offset)+radius*radius;
        if (disc>0 && -along+std::sqrt(disc)>SHOT_ENDPOINT_EPSILON && -along-std::sqrt(disc)<distance-SHOT_ENDPOINT_EPSILON) return false;
    }
    return true;
}

static float structure_health(const Colony*,uint32_t);
static uint32_t structure_generation(const Colony*,uint32_t);

static bool within_leash(const Simulation* s,const Unit& u,Vec3 point)
{
    const UnitDefinition& d=*simulation_unit_definition(s,&u);
    return u.commanded || d.leash_radius<=0 || length(subtract(point,s->world.bodies[u.target].position))<=s->world.bodies[u.target].radius+d.leash_radius;
}
static bool attention_valid(const Simulation* s,const Unit& u,const Vec3* positions)
{
    if (u.attention<0) return false;
    const UnitDefinition& d=*simulation_unit_definition(s,&u);
    Vec3 point={};float radius=0;
    if (u.task==TASK_ATTACK_UNIT && u.attention<MAX_UNITS)
    {
        const Unit& other=s->units[u.attention];
        if (other.health<=0 || other.generation!=u.attention_generation || other.faction==u.faction) return false;
        point=positions[u.attention];radius=d.unit_radius;
    }
    else if (u.task==TASK_ATTACK_STRUCTURE && (uint32_t)u.attention<s->world.body_count)
    {
        const Colony& c=s->colonies[u.attention];
        if (!c.active || c.faction==u.faction || c.generation!=u.attention_colony_generation
            || structure_health(&c,u.attention_structure)<=0 || structure_generation(&c,u.attention_structure)!=u.attention_generation) return false;
        point=simulation_structure_position(s,u.attention,u.attention_structure);radius=d.structure_radius;
    }
    else return false;
    return within_leash(s,u,point) && length(subtract(point,u.position))<=radius*ATTENTION_RETENTION_RADIUS_SCALE;
}
static void local_task(Simulation* s,Unit* u,const Vec3* positions,const int32_t settlers[MAX_BODIES][MAX_FACTIONS],float dt)
{
    const UnitDefinition& d=*simulation_unit_definition(s,u);
    bool valid=attention_valid(s,*u,positions);
    if (!valid) { u->attention=-1;u->task=TASK_PATROL; }
    u->response_timer-=dt;
    if (u->response_timer>0) return;
    u->response_timer=d.response_seconds*(UNIT_RATE_MIN_SCALE+UNIT_RATE_SCALE_RANGE*(u->generation%UNIT_RATE_VARIANTS)/UNIT_RATE_INTERVALS);
    if (valid) return;
    for (uint32_t pass=0;pass<2;++pass)
    {
        uint32_t mask=pass ? d.secondary_targets : d.targets;float nearest=d.unit_radius;
        uint16_t near[MAX_UNITS];uint32_t count=mask&TARGET_UNITS ? grid_query(s->grid,u->position,nearest,enemies_of(u->faction),near) : 0;
        for (uint32_t n=0;n<count;++n)
        {
            uint32_t j=near[n];const Unit& other=s->units[j];if (other.health<=0 || other.faction==u->faction) continue;
            float distance=length(subtract(positions[j],u->position));
            if (distance>=nearest || !within_leash(s,*u,positions[j]) || !clear_shot(s,u->position,positions[j])) continue;
            nearest=distance;u->task=TASK_ATTACK_UNIT;u->attention=j;u->attention_generation=other.generation;
        }
        if (u->attention>=0) return;
        nearest=d.structure_radius;
        if (mask&TARGET_STRUCTURES) for (uint32_t p=0;p<s->world.body_count;++p)
        {
            const Colony& c=s->colonies[p];if (!c.active || c.faction==u->faction) continue;
            for (uint32_t k=0;k<s->colonies[p].structure_count;++k)
            {
                if (structure_health(&c,k)<=0) continue;
                Vec3 point=simulation_structure_position(s,p,k);float distance=length(subtract(point,u->position));
                if (distance>=nearest || !within_leash(s,*u,point) || !clear_shot(s,u->position,point,p)) continue;
                nearest=distance;u->task=TASK_ATTACK_STRUCTURE;u->attention=p;u->attention_structure=k;
                u->attention_generation=structure_generation(&c,k);u->attention_colony_generation=c.generation;
            }
        }
        if (u->attention>=0) return;
    }
    if (opening_orbit(s,*u) || u->commanded || !d.colonizes || d.colonize_radius<=0) return;
    float nearest=d.colonize_radius;int32_t chosen=-1;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        if (!s->world.content.bodies[s->world.bodies[p].type].hosts_site || s->colonies[p].active
            || (settlers[p][u->faction]>=0 && settlers[p][u->faction]!=(int32_t)(u-s->units))
            || (u->faction==s->world.player_faction && !simulation_body_known(s,p))) continue;
        float distance=length(subtract(u->position,s->world.bodies[p].position))-s->world.bodies[p].radius;
        if (distance<nearest && !defended(s,p,u->faction)) { nearest=distance;chosen=p; }
    }
    if (chosen>=0) { u->target=chosen;u->task=TASK_COLONIZE; }
}

bool simulation_attachment_valid(const Simulation* s,const Unit* u)
{
    if (u->health<=0 || u->latch<0) return false;
    if (u->latch_colony)
    {
        if ((uint32_t)u->latch>=s->world.body_count) return false;
        const Colony& c=s->colonies[u->latch];
        return c.active && c.generation==u->latch_root_generation && c.faction!=u->faction
            && u->latch_structure<c.structure_count && structure_health(&c,u->latch_structure)>0
            && structure_generation(&c,u->latch_structure)==u->latch_generation;
    }
    if (u->latch>=MAX_UNITS) return false;
    const Unit& target=s->units[u->latch];
    return target.health>0 && target.generation==u->latch_generation && target.faction!=u->faction;
}
int32_t simulation_held_by(const Simulation* s,uint32_t unit)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (!s->units[i].latch_colony && s->units[i].latch==(int32_t)unit && simulation_attachment_valid(s,&s->units[i])) return (int32_t)i;
    return -1;
}

static void move_units(Simulation* s, float dt)
{
    const CombatVisuals* flight=&s->world.combat;
    FlightObstacles obstacles;flight_obstacles(s,&obstacles);
    Vec3 positions[MAX_UNITS],velocities[MAX_UNITS];int32_t held_by[MAX_UNITS];
    for (uint32_t i=0;i<MAX_UNITS;++i)
    { positions[i]=s->units[i].position;velocities[i]=s->units[i].velocity;held_by[i]=-1;s->units[i].charging=false; }
    grid_build(s);neighbors_build(s);
    uint16_t near[MAX_UNITS];
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit& u=s->units[i];
        if (!u.latch_colony && simulation_attachment_valid(s,&u) && held_by[u.latch]<0) held_by[u.latch]=(int32_t)i;
    }
    // One approach slot per side and cell keeps a group from blocking its own landing point.
    int32_t settlers[MAX_BODIES][MAX_FACTIONS];
    for (uint32_t p=0;p<MAX_BODIES;++p) for (uint32_t f=0;f<MAX_FACTIONS;++f) settlers[p][f]=-1;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit& u=s->units[i];
        if (u.health<=0 || u.moving || u.target<0 || opening_orbit(s,u) || !simulation_unit_definition(s,&u)->colonizes) continue;
        uint32_t p=(uint32_t)u.target;
        if (s->colonies[p].active || defended(s,p,u.faction)) continue;
        int32_t old=settlers[p][u.faction];Vec3 goal=s->colonies[p].surface;
        if (old<0 || length(subtract(positions[i],goal))<length(subtract(positions[old],goal))) settlers[p][u.faction]=(int32_t)i;
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (u->health<=0) continue;
        if (held_by[i]>=0 || u->stun>0) { u->velocity=Vec3{};u->flight_goal=u->position;continue; }
        if (u->target<0 && !u->moving) continue;
        const UnitDefinition* definition=simulation_unit_definition(s,u);
        if (u->latch>=0 && !u->moving)
        {
            if (simulation_attachment_valid(s,u))
            {
                Vec3 point=u->latch_colony ? simulation_structure_position(s,u->latch,u->latch_structure) : positions[u->latch];
                u->position=add(point,u->latch_offset);u->velocity=Vec3{};u->flight_goal=point;continue;
            }
            u->latch=-1;
        }
        Vec3 combat_target={};bool strafe=false;
        uint32_t p=(uint32_t)u->target;
        if (!u->moving && s->colonies[p].active && s->colonies[p].faction==u->faction) u->commanded=false;
        bool colonizing=u->target>=0 && settlers[p][u->faction]==(int32_t)i && !opening_orbit(s,*u) && simulation_unit_definition(s,u)->colonizes && s->world.content.bodies[s->world.bodies[p].type].hosts_site
            && !s->colonies[p].active && !defended(s,p,u->faction);
        Vec3 goal=u->moving ? u->destination : add(s->colonies[p].surface,scale(s->world.bodies[p].colony_normal,colonizing ? COLONIZE_SURFACE_OFFSET : LEGACY_ORBIT_SURFACE_OFFSET));
        if (!u->moving && !colonizing)
        {
            Vec3 normal=s->world.bodies[p].colony_normal;
            Vec3 axis=std::fabs(normal.z)<ORBIT_AXIS_PARALLEL_LIMIT ? Vec3{0,0,1} : Vec3{1,0,0};
            Vec3 tangent=normalize(cross(normal,axis)),bitangent=cross(tangent,normal);
            float angle=u->generation*GOLDEN_ANGLE_RADIANS;
            if (s->world.combat.enabled)
            {
                float rate=(UNIT_RATE_MIN_SCALE+UNIT_RATE_SCALE_RANGE*(u->generation%UNIT_RATE_VARIANTS)/UNIT_RATE_INTERVALS)*(u->generation%3 ? 1 : -1);
                u->orbit_phase=std::fmod(u->orbit_phase+s->world.combat.orbit_speed*rate*dt,FULL_TURN_RADIANS);angle=u->orbit_phase;
            }
            float spread=s->world.bodies[p].radius*(LEGACY_ORBIT_BASE_RADIUS_SCALE+LEGACY_ORBIT_SPREAD_PER_SQRT_INDEX*std::sqrt((float)i+1));
            if (s->world.combat.enabled)
            {
                spread=s->world.bodies[p].radius+flight->orbit_clearance+flight->weave_radius*std::sin(ORBIT_RADIAL_WAVE_FREQUENCY*angle+u->generation);
                float height=ORBIT_HEIGHT_RADIUS_SCALE*s->world.bodies[p].radius+flight->weave_height*std::sin(ORBIT_HEIGHT_WAVE_FREQUENCY*angle+u->generation*ORBIT_HEIGHT_PHASE_PER_GENERATION);
                goal=add(s->world.bodies[p].position,scale(normal,height));
            }
            goal=add(goal,add(scale(tangent,std::cos(angle)*spread),scale(bitangent,std::sin(angle)*spread)));
            local_task(s,u,positions,settlers,dt);
            if (u->task==TASK_ATTACK_UNIT) goal=positions[u->attention];
            else if (u->task==TASK_ATTACK_STRUCTURE) goal=simulation_structure_position(s,u->attention,u->attention_structure);
            else if (u->task==TASK_COLONIZE) goal=add(s->colonies[u->target].surface,scale(s->world.bodies[u->target].colony_normal,COLONIZE_SURFACE_OFFSET));
            else if (u->commanded && s->colonies[p].active && s->colonies[p].faction!=u->faction)
            {
                float nearest=INITIAL_NEAREST_DISTANCE;
                for (uint32_t k=0;k<s->colonies[p].structure_count;++k)
                {
                    if (structure_health(&s->colonies[p],k)<=0) continue;
                    Vec3 point=simulation_structure_position(s,p,k);float distance=length(subtract(point,u->position));
                    if (distance<nearest)
                    {
                        nearest=distance;goal=point;u->task=TASK_ATTACK_STRUCTURE;u->attention=p;u->attention_structure=k;
                        u->attention_generation=structure_generation(&s->colonies[p],k);u->attention_colony_generation=s->colonies[p].generation;
                    }
                }
            }
            if (u->task==TASK_ATTACK_UNIT || u->task==TASK_ATTACK_STRUCTURE)
            {
                float nearest=length(subtract(goal,u->position));
                if (definition->attack==ATTACK_PROJECTILE && flight->flight_enabled && nearest<definition->range*STRAFE_ENTRY_RANGE_SCALE)
                { combat_target=goal;strafe=true; }
                else if (nearest<definition->range*ATTACK_STOP_RANGE_SCALE) goal=u->position;
            }
        }
        if (u->moving) u->task=TASK_MOVE;
        else if (colonizing) u->task=TASK_COLONIZE;
        if (u->hatched && u->age<UNIT_LAUNCH_SECONDS && !u->moving && !u->commanded && definition->attack!=ATTACK_SPORE)
        {
            Vec3 radial=subtract(u->position,s->world.bodies[p].position);
            if (dot(radial,radial)>STEERING_DIRECTION_EPSILON) goal=add(u->position,scale(normalize(radial),LAUNCH_GOAL_DISTANCE));
            u->task=TASK_LAUNCH;u->attention=-1;
        }
        u->flight_goal=goal;
        Vec3 delta=subtract(goal,u->position);float distance=length(delta);
        float speed=definition->speed*((u->task==TASK_PATROL || u->task==TASK_LAUNCH) ? definition->cruise_fraction : 1);
        u->charging=definition->attack==ATTACK_DIGEST && u->latch_recovery<=0
            && (u->task==TASK_ATTACK_UNIT || u->task==TASK_ATTACK_STRUCTURE) && distance>definition->range
            && distance<definition->charge_radius && clear_shot(s,u->position,goal,u->task==TASK_ATTACK_STRUCTURE ? u->attention : NO_BODY);
        if (u->charging) speed*=definition->charge_speed;
        float acceleration=flight->acceleration*(u->charging ? definition->charge_acceleration : 1);
        if (flight->flight_enabled)
        {
            Vec3 desired=distance>POSITION_EPSILON ? scale(delta,std::fmin(speed,distance*FLIGHT_POSITION_GAIN)/distance) : Vec3{};
            if (strafe)
            {
                Vec3 away=subtract(u->position,combat_target);float d=length(away);
                if (d>STEERING_DIRECTION_EPSILON)
                {
                    away=scale(away,1/d);Vec3 side=cross(s->world.bodies[p].colony_normal,away);
                    if (dot(side,side)>STEERING_DIRECTION_EPSILON) side=normalize(side);
                    desired=add(scale(side,speed*ORBIT_HEIGHT_PHASE_PER_GENERATION),scale(away,(simulation_unit_definition(s,u)->range*STRAFE_PREFERRED_RANGE_SCALE-d)*FLIGHT_POSITION_GAIN));
                }
            }
            // Read a stable pass input: separation must not depend on unit update order.
            uint32_t count=neighbors_query(s->neighbors,positions[i],near);float own=definition->separation;
            for (uint32_t n=0;n<count;++n)
            {
                uint32_t j=near[n];const Unit& other=s->units[j];
                if (j==i || other.health<=0 || (colonizing && other.faction==u->faction && other.target==u->target)) continue;
                Vec3 apart=subtract(positions[i],positions[j]);float squared=dot(apart,apart);
                float spacing=flight->separation*std::fmax(own,simulation_unit_definition(s,&other)->separation);
                if (squared>=spacing*spacing || squared<=POSITION_EPSILON*POSITION_EPSILON) continue;
                float d=std::sqrt(squared);desired=add(desired,scale(apart,speed*(1-d/spacing)/d));
            }
            // Steer around a cell before the hard collision shell becomes necessary.
            for (uint32_t j=0;j<s->world.body_count;++j)
            {
                if (colonizing && j==p && distance<LANDING_APPROACH_RADIUS) continue;
                if (contact_attack(*definition) && u->task==TASK_ATTACK_STRUCTURE && u->attention==(int32_t)j) continue;
                Vec3 probe=add(u->position,scale(velocities[i],CELL_AVOIDANCE_LOOKAHEAD_SECONDS));
                Vec3 away=subtract(probe,s->world.bodies[j].position);float d=length(away);
                float safe=s->world.bodies[j].radius*CELL_COLLISION_RADIUS_SCALE+CELL_AVOIDANCE_CLEARANCE;
                if (d<safe && d>STEERING_DIRECTION_EPSILON) desired=add(desired,scale(away,speed*FLIGHT_POSITION_GAIN*(1-d/safe)/d));
            }
            desired=flight_avoid(s,obstacles,*u,desired,speed);
            float desired_speed=length(desired);if (desired_speed>speed) desired=scale(desired,speed/desired_speed);
            Vec3 change=subtract(desired,velocities[i]);float change_length=length(change);
            if (change_length>acceleration*dt) change=scale(change,acceleration*dt/change_length);
            u->velocity=add(velocities[i],change);u->position=add(u->position,scale(u->velocity,dt));
            if (length(u->velocity)>MIN_HEADING_SPEED)
            {
                Vec3 heading=normalize(u->velocity),side=cross(Vec3{0,1,0},u->forward);
                float blend=1-std::exp(-flight->turn_rate*dt);
                float bank=-dot(subtract(heading,u->forward),side)*BANK_RESPONSE_GAIN;
                u->bank+=(std::fmax(-flight->bank,std::fmin(flight->bank,bank))-u->bank)*blend;
                Vec3 forward=add(scale(u->forward,1-blend),scale(heading,blend));
                if (length(forward)>STEERING_DIRECTION_EPSILON) u->forward=normalize(forward);
            }
        }
        else if (distance>POSITION_EPSILON) u->position=add(u->position,scale(delta,std::fmin(1.0f,speed*dt/distance)));
        // Conservative spherical collision keeps transit outside cell terrain.
        for (uint32_t p2=0;p2<s->world.body_count;++p2)
        {
            Vec3 offset=subtract(u->position,s->world.bodies[p2].position);float radial=length(offset);
            float clearance=s->world.bodies[p2].radius*CELL_COLLISION_RADIUS_SCALE+CELL_COLLISION_CLEARANCE;
            if (colonizing && p2==p && distance<LANDING_APPROACH_RADIUS) continue;
            if (contact_attack(*definition) && u->task==TASK_ATTACK_STRUCTURE && u->attention==(int32_t)p2) continue;
            if (radial<clearance && radial>POSITION_EPSILON)
                u->position=add(s->world.bodies[p2].position,scale(offset,clearance/radial));
        }
        if (flight->flight_enabled) flight_clip(s,obstacles,u,positions[i]);
        if (u->moving && length(subtract(u->destination,u->position))<MOVE_COMPLETION_RADIUS)
        {
            u->moving=false;u->commanded=false;u->attention=-1;u->response_timer=0;
            float nearest=INITIAL_NEAREST_DISTANCE;int32_t home=home_body(s,u->faction);
            for (uint32_t b=0;b<s->world.body_count;++b) if (u->faction!=s->world.player_faction || simulation_body_known(s,b))
            { float distance=length(subtract(u->position,s->world.bodies[b].position));if (distance<nearest) { nearest=distance;home=b; } }
            u->target=home;
        }
    }
}

static void pop_body(Simulation* s,Vec3 position,Faction faction,bool unit_death,float blast_radius=0)
{
    sound(s,SOUND_DEATH,position);
    uint32_t slot=0;
    while (slot<MAX_POPS && s->pops[slot].duration>0) ++slot;
    if (slot==MAX_POPS) slot=s->deaths%MAX_POPS;
    Pop* p=&s->pops[slot];p->position=position;p->age=0;
    p->duration=s->world.combat.pop_seconds;p->faction=faction;
    p->blast_radius=blast_radius;
    if (unit_death) { ++s->deaths;s->last_death_tick=s->tick_count; }
}

static void advance_visuals(Simulation* s,float dt)
{
    const CombatVisuals* v=&s->world.combat;
    for (uint32_t i=0;i<MAX_LIMB_DEBRIS;++i)
    {
        LimbDebris& d=s->debris[i];if (d.seconds<=0) continue;
        d.age+=dt;if (d.age>=d.seconds) { d.seconds=0;continue; }
        Vec3 midpoint=add(scale(add(d.start,d.end),.5f),scale(d.velocity,dt));
        Vec3 half=scale(subtract(d.end,d.start),.5f);float angle=d.spin*dt,c=std::cos(angle),sn=std::sin(angle);
        half=add(add(scale(half,c),scale(cross(d.axis,half),sn)),scale(d.axis,dot(d.axis,half)*(1-c)));
        d.start=subtract(midpoint,half);d.end=add(midpoint,half);d.velocity=scale(d.velocity,std::exp(-v->break_drag*dt));
    }
    for (uint32_t i=0;i<MAX_POPS;++i)
        if (s->impacts[i].duration>0 && (s->impacts[i].age+=dt)>=s->impacts[i].duration) s->impacts[i].duration=0;
    for (uint32_t i=0;i<MAX_POPS;++i)
        if (s->pops[i].duration>0 && (s->pops[i].age+=dt)>=s->pops[i].duration) s->pops[i].duration=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health>0) u->age+=dt;u->flash=std::fmax(0.0f,u->flash-dt);u->cooldown=std::fmax(0.0f,u->cooldown-dt);u->latch_recovery=std::fmax(0.0f,u->latch_recovery-dt);
        u->blob_cooldown=std::fmax(0.0f,u->blob_cooldown-dt);u->stun=std::fmax(0.0f,u->stun-dt);u->aura=std::fmax(0.0f,u->aura-dt);
        Trail* t=&s->trails[i];if (u->health<=0 || !simulation_unit_definition(s,u)->trails) { t->count=0;continue; }
        t->timer+=dt;if (t->timer<v->trail_lifetime/(TRAIL_POINTS-1)) continue;
        t->timer=0;uint32_t count=t->count<TRAIL_POINTS ? t->count : TRAIL_POINTS-1;
        for (uint32_t j=count;j>0;--j) t->points[j]=t->points[j-1];
        t->points[0]=simulation_trail_origin(s,u);t->count=count+1;
    }
}

static void recover_units(Simulation* s,float dt)
{
    const SpikeRules* r=&s->world.spikes;if (!r->enabled) return;
    // A unit glowing after devouring heals friendly units near it, even in combat. The
    // shortest interval among overlapping auras applies.
    float aura_interval[MAX_UNITS]={};uint16_t near[MAX_UNITS];
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit& e=s->units[i];if (e.health<=0 || e.aura<=0) continue;
        const UnitDefinition& d=*simulation_unit_definition(s,&e);
        uint32_t count=grid_query(s->grid,e.position,d.aura_radius,1u<<e.faction,near);
        for (uint32_t n=0;n<count;++n)
        {
            const Unit& f=s->units[near[n]];float& interval=aura_interval[near[n]];
            if (f.health>0 && length(subtract(f.position,e.position))<d.aura_radius && (interval<=0 || d.aura_interval<interval)) interval=d.aura_interval;
        }
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health<=0) continue;
        u->damage_age+=dt;
        for (uint32_t j=0;j<r->count;++j)
            if (u->spikes[j]<1)
            { float before=u->spikes[j];u->spikes[j]=std::fmin(1.0f,before+dt/r->regrow);if (u->spikes[j]==1) ++s->regrown; }
        const UnitDefinition* definition=simulation_unit_definition(s,u);
        for (uint32_t j=0;j<definition->payload_count;++j)
            u->payloads[j]=std::fmin(1.0f,u->payloads[j]+dt/definition->payload_reload);
        bool friendly=false;
        if (u->target>=0)
        {
            const Body* c=&s->world.bodies[u->target];
            friendly=c->owner==u->faction && length(subtract(u->position,c->position))<c->radius+r->heal_clearance;
        }
        bool aura=aura_interval[i]>0;float interval=aura ? aura_interval[i] : r->heal_interval;
        if (!u->wounds || (!aura && (!friendly || u->damage_age<r->heal_delay))) { u->heal_timer=0;continue; }
        u->heal_timer+=dt;
        if (u->heal_timer>=interval)
        {
            s->aura_heals+=aura;u->heal_timer=0;--u->wounds;if (u->wounds<MAX_EMBEDDED) u->embedded[u->wounds]=EmbeddedSpike();
            u->health=simulation_unit_definition(s,u)->health*(simulation_unit_definition(s,u)->hits-u->wounds)/simulation_unit_definition(s,u)->hits;++s->healed;
            sound(s,SOUND_HEAL,u->position);
        }
    }
}

static float structure_health(const Colony* c,uint32_t k)
{ return k ? c->branches[k-1].health : c->health; }
static uint32_t structure_generation(const Colony* c,uint32_t k)
{ return k ? c->branches[k-1].generation : c->root_generation; }
static uint32_t structure_wounds(const Colony* c,uint32_t k)
{ return k ? c->branches[k-1].wounds : c->wounds; }
static uint32_t structure_slot(uint32_t cell,uint32_t structure)
{ return MAX_UNITS+cell*MAX_FACTORY_STRUCTURES+structure; }
static void fracture_limb(Simulation* s,uint32_t p,uint32_t k,float old_growth,float retained)
{
    const Colony& c=s->colonies[p];const ProducerDefinition& definition=*simulation_producer_definition(s,p);
    if (!definition.segment_path[0]) return;
    ++s->limbs_broken;++s->faction_stats[s->colonies[p].faction].limbs_lost;sound(s,SOUND_BREAK,simulation_structure_position(s,p,k));
    const CombatVisuals& fx=s->world.combat;if (fx.break_seconds<=0) return;
    float growth=old_growth;
    uint32_t seed=s->world.bodies[p].seed^s->tick_count^(k*SEED_INDEX_MULTIPLIER);
    for (uint32_t j=0;j<=GROWTH_SEGMENTS;++j)
    {
        bool tip=j==GROWTH_SEGMENTS;
        float amount=std::fmax(0.0f,std::fmin(1.0f,growth*GROWTH_SEGMENTS-j));
        if (tip)
        { if (growth<(k ? c.branches[k-1].growth : c.growth) || (definition.branches.grammar==GROWTH_TREE && !simulation_structure_terminal(s,p,k))) continue; }
        else if (j<retained*GROWTH_SEGMENTS || amount<=MIN_FRACTURE_SEGMENT_FRACTION) continue;
        uint32_t slot=0;while (slot<MAX_LIMB_DEBRIS && s->debris[slot].seconds>0) ++slot;
        if (slot==MAX_LIMB_DEBRIS)
        {
            slot=0;for (uint32_t i=1;i<MAX_LIMB_DEBRIS;++i) if (s->debris[i].age>s->debris[slot].age) slot=i;
            ++s->debris_replaced;
        }
        LimbDebris d={};d.tip=tip;
        if (tip)
        {
            d.start=simulation_growth_point(s,p,k,growth);
            d.end=add(d.start,normalize(subtract(d.start,c.stems[k][0])));
        }
        else { d.start=c.stems[k][j];d.end=add(d.start,scale(subtract(c.stems[k][j+1],d.start),amount)); }
        Vec3 radial=normalize(subtract(d.start,s->world.bodies[p].position));
        Vec3 jitter={signed_random(&seed),signed_random(&seed),signed_random(&seed)};
        d.velocity=scale(add(radial,scale(jitter,DEBRIS_VELOCITY_JITTER)),fx.break_speed);
        // This axis always has a nonzero radial component.
        d.axis=normalize(add(radial,scale(jitter,DEBRIS_AXIS_JITTER)));d.spin=signed_random(&seed)*fx.break_spin;
        d.radius=tip ? definition.width*(definition.branches.grammar==GROWTH_TREE ? simulation_structure_scale(s,p,k) : (c.parents[k]>=0 ? FACTORY_CHILD_HEAD_WIDTH_SCALE : 1))*(FACTORY_HEAD_START_WIDTH_SCALE+DEBRIS_VELOCITY_JITTER*growth)
            : definition.width*(1-FACTORY_STEM_TAPER_PER_SEGMENT*j)*simulation_structure_scale(s,p,k)*(FACTORY_STEM_START_WIDTH_SCALE+FACTORY_STEM_GROWTH_WIDTH_SCALE*growth);
        d.seconds=fx.break_seconds;d.producer=c.type;d.faction=c.faction;s->debris[slot]=d;
    }
}

// Cloud damage below one hit waits in the target's exposure; each whole hit's
// worth becomes a hit this tick.
static uint32_t expose(float* exposure,float damage,float per_hit)
{
    *exposure+=damage;uint32_t hits=0;
    while (*exposure>=per_hit) { *exposure-=per_hit;++hits; }
    return hits;
}
static void spore_clouds(Simulation* s,float dt,uint32_t* hit_count)
{
    uint16_t near[MAX_UNITS];
    for (uint32_t c=0;c<MAX_SPORE_CLOUDS;++c)
    {
        SporeCloud& cloud=s->clouds[c];if (cloud.age>=cloud.seconds) continue;
        cloud.age+=dt;float damage=cloud.damage*dt;
        uint32_t count=grid_query(s->grid,cloud.position,cloud.radius,cloud.friendly_fire ? ~0u : enemies_of(cloud.faction),near);
        for (uint32_t n=0;n<count;++n)
        {
            Unit& u=s->units[near[n]];const UnitDefinition& d=*simulation_unit_definition(s,&u);
            if (u.health<=0 || length(subtract(u.position,cloud.position))>=cloud.radius || !clear_shot(s,cloud.position,u.position)) continue;
            uint32_t hits=expose(&u.exposure,damage,d.health/d.hits);hit_count[near[n]]+=hits;s->hits+=hits;
        }
        for (uint32_t p=0;p<s->world.body_count;++p)
        {
            Colony& colony=s->colonies[p];if (!colony.active || (!cloud.friendly_fire && colony.faction==cloud.faction)) continue;
            const ProducerDefinition& d=*simulation_producer_definition(s,p);
            for (uint32_t k=0;k<colony.structure_count;++k)
            {
                Vec3 point=simulation_structure_position(s,p,k);
                if (structure_health(&colony,k)<=0 || length(subtract(point,cloud.position))>=cloud.radius || !clear_shot(s,cloud.position,point,(int32_t)p)) continue;
                bool root=colony.parents[k]<0;float* exposure=k ? &colony.branches[k-1].exposure : &colony.exposure;
                uint32_t hits=expose(exposure,damage,root ? d.health/d.hits : d.branches.health/d.branches.hits);
                hit_count[structure_slot(p,k)]+=hits;s->hits+=hits;
            }
        }
    }
}

static void launch_blob(Simulation* s,Unit* u,const UnitDefinition& d,uint32_t victim)
{
    uint32_t slot=0;while (slot<MAX_PROJECTILES && s->projectiles[slot].active) ++slot;
    if (slot==MAX_PROJECTILES) return; // Wait for capacity.
    const Unit& t=s->units[victim];Vec3 aim=subtract(t.position,u->position);float distance=length(aim);
    Projectile p={};p.blob=true;p.stun=d.blob_stun;p.unit_type=u->type;p.position=p.previous=u->position;p.target=(int32_t)victim;
    p.faction=u->faction;p.direction=distance>POSITION_EPSILON ? scale(aim,1/distance) : u->forward;p.launch_scale=d.blob_scale;
    p.generation=t.generation;p.active=true;s->projectiles[slot]=p;
    u->blob_cooldown=d.blob_interval;++s->blobs;sound(s,SOUND_SHOT,u->position);
    // Without a move order, the cell goes to devour what it incapacitated.
    if (!u->moving) { u->task=TASK_ATTACK_UNIT;u->attention=(int32_t)victim;u->attention_generation=t.generation; }
}

static void projectile_combat(Simulation* s,float dt)
{
    const CombatVisuals* v=&s->world.combat;
    CombatScratch& scratch=s->combat_scratch;memset(&scratch,0,offsetof(CombatScratch,landed));
    uint32_t* hit_count=scratch.hit_count;uint32_t* incoming=scratch.incoming;EmbeddedSpike (*landed)[2]=scratch.landed;
    bool* digest_heal=scratch.digest_heal;bool* burst=scratch.burst;bool* expired=scratch.expired;bool* held=scratch.held;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (!s->units[i].latch_colony && simulation_attachment_valid(s,&s->units[i]))
        { held[s->units[i].latch]=true;scratch.holder[s->units[i].latch]=(uint16_t)(i+1); }
    grid_build(s);uint16_t near[MAX_UNITS];
    if (s->world.spikes.enabled) for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        const Projectile* p=&s->projectiles[i];if (!p->active) continue;
        bool valid=p->colony ? s->colonies[p->target].generation==p->generation
            && structure_generation(&s->colonies[p->target],p->structure)==p->structure_generation : s->units[p->target].generation==p->generation;
        if (valid && !p->blob) ++incoming[p->colony ? structure_slot(p->target,p->structure) : p->target];
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health<=0) continue;
        const UnitDefinition* definition=simulation_unit_definition(s,u);
        if (definition->attack==ATTACK_SPORE && definition->lifetime>0 && u->age>=definition->lifetime)
        { expired[i]=true;continue; }
        // A held victim cannot fire. Supporting units must break the hold.
        // Read the phase input: a newly captured victim stops on the next tick.
        if (held[i] || u->stun>0) continue;
        // A free white blood cell first incapacitates a unit beyond contact with a blob, then devours it.
        if (definition->blob_range>0 && u->blob_cooldown<=0 && !simulation_attachment_valid(s,u))
        {
            int32_t victim=-1;float nearest=definition->blob_range;
            uint32_t count=grid_query(s->grid,u->position,nearest,enemies_of(u->faction),near);
            for (uint32_t n=0;n<count;++n)
            {
                const Unit& t=s->units[near[n]];if (t.health<=0 || t.stun>0 || held[near[n]]) continue;
                float d=length(subtract(t.position,u->position));
                if (d>definition->range && d<nearest && clear_shot(s,u->position,t.position)) { nearest=d;victim=(int32_t)near[n]; }
            }
            if (victim>=0) launch_blob(s,u,*definition,(uint32_t)victim);
        }
        if (u->cooldown>0 || (definition->attack==ATTACK_DIGEST && u->latch_recovery>0)) continue;
        int target=-1;bool colony=false;uint32_t structure=0;float nearest=simulation_unit_definition(s,u)->range;
        bool secondary=false;
        if (definition->attack==ATTACK_DIGEST && simulation_attachment_valid(s,u))
        { target=u->latch;colony=u->latch_colony;structure=u->latch_structure;secondary=colony ? !(definition->targets&TARGET_STRUCTURES) : !(definition->targets&TARGET_UNITS); }
        for (uint32_t pass=0;pass<2 && target<0;++pass)
        {
        uint32_t targets=pass ? definition->secondary_targets : definition->targets;
        secondary=pass!=0;
        uint32_t count=targets&TARGET_UNITS ? grid_query(s->grid,u->position,nearest,enemies_of(u->faction),near) : 0;
        for (uint32_t n=0;n<count;++n)
            if (s->units[near[n]].health>0 && s->units[near[n]].faction!=u->faction)
            {
                uint32_t j=near[n];
                float d=length(subtract(u->position,s->units[j].position));
                if (d<nearest && clear_shot(s,u->position,s->units[j].position)) { nearest=d;target=(int)j; }
            }
        if (target<0 && (targets&TARGET_STRUCTURES)) for (uint32_t j=0;j<s->world.body_count;++j)
            if (s->colonies[j].active && s->colonies[j].faction!=u->faction)
            {
                uint32_t count=s->colonies[j].structure_count;
                for (uint32_t k=0;k<count;++k) if (structure_health(&s->colonies[j],k)>0)
                {
                    Vec3 point=simulation_structure_position(s,j,k);float d=length(subtract(u->position,point));
                    if (d<nearest && clear_shot(s,u->position,point,(int32_t)j)) { nearest=d;target=(int)j;colony=true;structure=k; }
                }
            }
        }
        if (target<0) continue;
        if (definition->attack==ATTACK_DIGEST)
        {
            ++hit_count[colony ? structure_slot(target,structure) : (uint32_t)target];
            digest_heal[i]=true;++s->digestions;++s->hits;
            if (!simulation_attachment_valid(s,u)) { u->latch_hits=0;++s->captures; }
            u->latch=target;u->latch_colony=colony;u->latch_structure=structure;
            u->latch_generation=colony ? structure_generation(&s->colonies[target],structure) : s->units[target].generation;
            u->latch_root_generation=colony ? s->colonies[target].generation : 0;
            u->latch_offset=subtract(u->position,colony ? simulation_structure_position(s,target,structure) : s->units[target].position);
            u->cooldown=definition->shot_interval*(secondary ? definition->secondary_interval : 1);sound(s,SOUND_HIT,u->position);continue;
        }
        if (definition->attack==ATTACK_SPORE)
        {
            // Contact bursts the spore: a small hit on what it touched, then a toxic cloud from this tick.
            burst[i]=true;++s->spore_bursts;sound(s,SOUND_HIT,u->position);
            if (colony)
            {
                Colony& c=s->colonies[target];const ProducerDefinition& d=*simulation_producer_definition(s,(uint32_t)target);bool root=c.parents[structure]<0;
                float* exposure=structure ? &c.branches[structure-1].exposure : &c.exposure;
                uint32_t hits=expose(exposure,definition->burst_damage,root ? d.health/d.hits : d.branches.health/d.branches.hits);
                hit_count[structure_slot((uint32_t)target,structure)]+=hits;s->hits+=hits;
            }
            else
            {
                const UnitDefinition& d=*simulation_unit_definition(s,&s->units[target]);
                uint32_t hits=expose(&s->units[target].exposure,definition->burst_damage,d.health/d.hits);hit_count[target]+=hits;s->hits+=hits;
            }
            uint32_t slot=0;while (slot<MAX_SPORE_CLOUDS && s->clouds[slot].age<s->clouds[slot].seconds) ++slot;
            if (slot==MAX_SPORE_CLOUDS) { slot=s->cloud_cursor;s->cloud_cursor=(s->cloud_cursor+1)%MAX_SPORE_CLOUDS; }
            s->clouds[slot]=SporeCloud{u->position,definition->cloud_radius,0,definition->cloud_seconds,definition->cloud_damage,u->faction,definition->friendly_fire};
            continue;
        }
        uint32_t slot=0;while (slot<MAX_PROJECTILES && s->projectiles[slot].active) ++slot;
        if (slot==MAX_PROJECTILES) continue; // Wait for capacity; never apply invisible damage.
        Vec3 destination=colony ? simulation_structure_position(s,(uint32_t)target,structure) : s->units[target].position;
        Vec3 start=u->position;float launch_scale=s->world.spikes.scale;
        if (s->world.spikes.enabled && simulation_unit_definition(s,u)->detaches)
        {
            int spike=-1;float best=-2;Vec3 x,y,z;simulation_unit_axes(u,&x,&y,&z);
            Vec3 aim=subtract(destination,start);float aim_length=length(aim);aim=aim_length>POSITION_EPSILON ? scale(aim,1/aim_length) : u->forward;
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
        if (definition->payload_count)
        {
            uint32_t pod=0;while (pod<definition->payload_count && u->payloads[pod]<1) ++pod;
            if (pod==definition->payload_count) continue;
            start=add(u->position,scale(world_direction(u,definition->payload_sockets[pod]),definition->scale));
            launch_scale=definition->scale;u->payloads[pod]=0;++s->detached;
        }
        Projectile p={};p.unit_type=u->type;p.payload=definition->payload_count>0;p.position=p.previous=start;p.target=target;p.colony=colony;p.faction=u->faction;p.structure=structure;
        if (s->world.spikes.enabled)
        {
            uint32_t index=colony ? structure_slot(target,structure) : (uint32_t)target;
            uint32_t wounds=colony ? structure_wounds(&s->colonies[target],structure) : s->units[target].wounds;
            uint32_t socket=(wounds+incoming[index]++)%3;
            Vec3 outward=subtract(start,destination);float distance=length(outward);
            outward=distance>POSITION_EPSILON ? scale(outward,1/distance) : Vec3{0,0,1};
            Vec3 axis=std::fabs(outward.y)<UNIT_UP_AXIS_PARALLEL_LIMIT ? Vec3{0,1,0} : Vec3{1,0,0};
            Vec3 x=normalize(cross(axis,outward)),y=cross(outward,x);float angle=socket*SPIKE_INDEX_AZIMUTH_RADIANS;
            Vec3 normal=normalize(add(scale(outward,SPIKE_HIT_OUTWARD_WEIGHT),add(scale(x,SPIKE_HIT_TANGENT_WEIGHT*std::cos(angle)),scale(y,SPIKE_HIT_TANGENT_WEIGHT*std::sin(angle)))));
            p.anchor=colony ? scale(normal,simulation_producer_definition(s,(uint32_t)target)->width*HIT_ANCHOR_JITTER_SCALE) : local_direction(&s->units[target],scale(normal,EMBEDDED_SPIKE_LENGTH_SCALE));
            destination=add(destination,colony ? p.anchor : scale(world_direction(&s->units[target],p.anchor),simulation_unit_definition(s,&s->units[target])->scale));
        }
        Vec3 aim=subtract(destination,start);float aim_length=length(aim);p.direction=aim_length>POSITION_EPSILON ? scale(aim,1/aim_length) : u->forward;p.launch_scale=launch_scale;
        p.generation=colony ? s->colonies[target].generation : s->units[target].generation;
        p.structure_generation=colony ? structure_generation(&s->colonies[target],structure) : 0;
        p.active=true;s->projectiles[slot]=p;
        u->cooldown=v->shot_interval*simulation_unit_definition(s,u)->shot_interval*(secondary ? definition->secondary_interval : 1);++s->shots;
        sound(s,SOUND_SHOT,u->position);
    }
    for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        Projectile* p=&s->projectiles[i];if (!p->active) continue;
        bool valid=p->colony ? s->colonies[p->target].active && s->colonies[p->target].faction!=p->faction && s->colonies[p->target].generation==p->generation
            && structure_health(&s->colonies[p->target],p->structure)>0 && structure_generation(&s->colonies[p->target],p->structure)==p->structure_generation
            : s->units[p->target].health>0 && s->units[p->target].generation==p->generation;
        p->age+=dt;if (!valid || p->age>4) { p->active=false;continue; }
        Vec3 target=p->colony ? simulation_structure_position(s,p->target,p->structure) : s->units[p->target].position;
        if (s->world.spikes.enabled) target=add(target,p->colony ? p->anchor :
            scale(world_direction(&s->units[p->target],p->anchor),simulation_unit_definition(s,&s->units[p->target])->scale));
        Vec3 delta=subtract(target,p->position);float d=length(delta),step=v->projectile_speed*dt;
        Vec3 next=d<=step ? target : add(p->position,scale(delta,step/d));
        if (!clear_shot(s,p->position,next,p->colony ? p->target : NO_BODY)) { p->active=false;continue; }
        p->previous=p->position;p->position=next;
        if (d>POSITION_EPSILON) p->direction=scale(delta,1/d);
        if (d<=step)
        {
            if (p->blob) { Unit& t=s->units[p->target];t.stun=std::fmax(t.stun,p->stun);t.velocity=Vec3{}; }
            else if (s->world.spikes.enabled)
            {
                uint32_t index=p->colony ? structure_slot(p->target,p->structure) : (uint32_t)p->target;
                uint32_t slot=hit_count[index]++;
                if (slot<2 && !p->payload)
                {
                    scratch.landed_mask[index]|=(uint8_t)(1u<<slot);
                    Vec3 outward=scale(p->direction,-1);
                    landed[index][slot].direction=p->colony ? outward : local_direction(&s->units[p->target],outward);
                    landed[index][slot].faction=p->faction;landed[index][slot].anchor=p->anchor;
                }
            }
            uint32_t slot=s->hits%MAX_POPS;
            Impact impact={};impact.position=target;impact.duration=v->flash_seconds*HIT_EFFECT_LIFETIME_SCALE;impact.faction=p->faction;
            s->impacts[slot]=impact;
            p->active=false;++s->hits;sound(s,SOUND_HIT,target);
        }
    }
    spore_clouds(s,dt,hit_count);
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && (hit_count[i]>0 || digest_heal[i] || burst[i] || expired[i]))
        {
            Unit* u=&s->units[i];
            if (s->world.spikes.enabled)
            {
                uint32_t old=u->wounds;u->wounds=std::min(simulation_unit_definition(s,u)->hits,old+hit_count[i]);
                if (burst[i] || expired[i]) u->wounds=simulation_unit_definition(s,u)->hits;
                for (uint32_t j=old;j<u->wounds && j<2;++j) u->embedded[j]=(scratch.landed_mask[i]>>(j-old))&1u ? landed[i][j-old] : EmbeddedSpike();
                if (digest_heal[i] && u->wounds>0 && u->wounds<simulation_unit_definition(s,u)->hits)
                {
                    --u->wounds;if (u->wounds<MAX_EMBEDDED) u->embedded[u->wounds]=EmbeddedSpike{};
                    ++s->digest_heals;sound(s,SOUND_HEAL,u->position);
                }
                float maximum=simulation_unit_definition(s,u)->health;
                u->health=maximum*(simulation_unit_definition(s,u)->hits-u->wounds)/simulation_unit_definition(s,u)->hits;
                if (hit_count[i]>0) u->damage_age=u->heal_timer=0;
            }
            if (hit_count[i]>0 && u->latch>=0 && simulation_unit_definition(s,u)->release_hits)
            {
                u->latch_hits+=hit_count[i];
                if (u->health<=0 || u->latch_hits>=simulation_unit_definition(s,u)->release_hits)
                {
                    u->latch=-1;u->latch_hits=0;u->charging=false;
                    u->latch_recovery=simulation_unit_definition(s,u)->release_seconds;++s->releases;
                }
            }
            if (hit_count[i]>0) u->flash=v->flash_seconds;
            if (u->health>0 && hit_count[i]>0)
            {
                s->last_flash_tick=s->tick_count;
                if (u->wounds+1>=simulation_unit_definition(s,u)->hits) s->last_low_tick=s->tick_count;
            }
            if (u->health==0 && scratch.holder[i])
            {
                // Devoured: the holder glows and heals friendly units near it.
                Unit& eater=s->units[scratch.holder[i]-1];eater.aura=simulation_unit_definition(s,&eater)->aura_seconds;++s->devoured;
            }
            if (u->health==0)
            {
                pop_body(s,u->position,u->faction,true,burst[i] ? simulation_unit_definition(s,u)->cloud_radius : 0);
                ++s->losses;++s->faction_stats[u->faction].lost;u->selected=false;s->trails[i].count=0;
            }
        }
    for (uint32_t i=0;i<s->world.body_count;++i)
        for (uint32_t k=0;k<s->colonies[i].structure_count;++k)
        if (s->colonies[i].active && structure_health(&s->colonies[i],k)>0 && hit_count[structure_slot(i,k)]>0)
        {
            Colony* c=&s->colonies[i];FactoryStructure* branch=k ? &c->branches[k-1] : 0;
            float before=simulation_structure_growth(s,i,k);
            float* health=k ? &branch->health : &c->health;uint32_t* wounds=k ? &branch->wounds : &c->wounds;
            EmbeddedSpike* embedded=k ? branch->embedded : c->embedded;
            uint32_t index=structure_slot(i,k);
            const ProducerDefinition& definition=*simulation_producer_definition(s,i);
            bool root=c->parents[k]<0;
            uint32_t maximum=root ? definition.hits : definition.branches.hits;
            float maximum_health=root ? definition.health : definition.branches.health;
            uint32_t old=*wounds;*wounds=std::min(maximum,old+hit_count[index]);
            for (uint32_t j=old;j<*wounds && j<2;++j) embedded[j]=(scratch.landed_mask[index]>>(j-old))&1u ? landed[index][j-old] : EmbeddedSpike();
            *health=maximum_health*(maximum-*wounds)/maximum;
            float retained=simulation_structure_growth(s,i,k);
            if (retained<before) fracture_limb(s,i,k,before,retained);
            if (*health==0)
            {
                if (s->world.spikes.enabled) pop_body(s,simulation_structure_position(s,i,k),c->faction,false);
                if (k) { branch->growth=0;branch->regrow_timer=definition.branches.regrow_seconds; }
                else { c->growth=0;c->root_regrow_timer=definition.branches.regrow_seconds; }
                c->branch_timer=0;
            }
            // Children attach at the start of the last segment. Loss of that support detaches them.
            if (retained<=CHILD_SUPPORT_GROWTH_FRACTION)
                for (uint32_t j=k+1;j<c->structure_count;++j)
                {
                    int32_t ancestor=c->parents[j];
                    while (ancestor>(int32_t)k) ancestor=c->parents[ancestor];
                    if (ancestor!=(int32_t)k) continue;
                    FactoryStructure& child=c->branches[j-1];
                    if (child.health>0) fracture_limb(s,i,j,simulation_structure_growth(s,i,j),0);
                    child.health=0;child.growth=0;child.regrow_timer=definition.branches.regrow_seconds;
                }
            bool was_active=c->active;c->active=c->health>0;
            for (uint32_t j=1;j<c->structure_count;++j) c->active=c->active || c->branches[j-1].health>0;
            if (was_active && !c->active) ++s->faction_stats[c->faction].colonies_lost;
            if (!c->active)
            { c->energy=0;c->building=false;c->capacity_blocked=false;memset(c->leaves,0,sizeof(c->leaves));s->world.bodies[i].owner=FACTION_NEUTRAL; }

        }
}

static void colonize_cells(Simulation* s, float dt)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (u->health<=0 || u->target<0 || u->task!=TASK_COLONIZE || opening_orbit(s,*u) || !simulation_unit_definition(s,u)->colonizes
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
        if (c->active) continue;
        Vec3 goal=add(c->surface,scale(s->world.bodies[p].colony_normal,COLONIZE_SURFACE_OFFSET));
        if (length(subtract(u->position,goal))>LANDING_COMPLETION_RADIUS) continue;
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
        Colony* c=&s->colonies[p];if (!c->active) continue;
        const ProducerDefinition& d=*simulation_producer_definition(s,p);
        const BranchRules& f=d.branches;
        // Initial construction has one time slot per structure. Damage keeps its regrowth delay.
        bool establishing=c->establishment_age<d.establishment_seconds;
        c->establishment_age=std::fmin(d.establishment_seconds,c->establishment_age+dt);
        c->branch_timer+=dt;
        bool mature=false,emerged=false;
        for (uint32_t k=0;k<c->structure_count;++k)
        {
            FactoryStructure* b=k ? &c->branches[k-1] : 0;
            float* health=k ? &b->health : &c->health;
            float* growth=k ? &b->growth : &c->growth;
            float* timer=k ? &b->regrow_timer : &c->root_regrow_timer;
            uint32_t* wounds=k ? &b->wounds : &c->wounds;
            uint32_t* generation=k ? &b->generation : &c->root_generation;
            float* exposure=k ? &b->exposure : &c->exposure;
            EmbeddedSpike* embedded=k ? b->embedded : c->embedded;
            int32_t parent=c->parents[k];
            *timer=std::fmax(0.0f,*timer-dt);
            bool ready=parent<0 || ((parent ? c->branches[parent-1].health : c->health)>0 && simulation_structure_growth(s,p,parent)>=1);
            bool first=establishing && *generation==0;
            bool due=first ? c->establishment_age>c->deployment_start[k] : c->branch_timer>=f.seconds;
            if (*health<=0 && *timer<=0 && ready && due)
            {
                if (f.grammar!=GROWTH_TREE) c->branch_timer=0;
                if (*generation>0 && d.segment_path[0]) regrow_factory_stems(s,p,k,*generation+1);
                emerged=true;*health=parent<0 ? d.health : f.health;*growth=first ? 0 : INITIAL_FACTORY_GROWTH;*wounds=0;*exposure=0;++*generation;
                memset(embedded,0,sizeof(EmbeddedSpike)*MAX_EMBEDDED);sound(s,SOUND_GROW,simulation_structure_position(s,p,k));
            }
            if (*health<=0) continue;
            float old=*growth;
            if (establishing && *generation==1)
                *growth=std::fmin(1.0f,*growth+std::fmin(dt,std::fmax(0.0f,c->establishment_age-c->deployment_start[k]))/c->deployment_seconds[k]);
            else *growth=std::fmin(1.0f,*growth+dt/d.growth_seconds);
            if (old<1 && *growth>=1) sound(s,SOUND_MATURE,simulation_structure_position(s,p,k));
            mature=mature || *growth>=1;
        }
        if (emerged) c->branch_timer=0;
        float production=simulation_production(s,p);
        c->uptake=s->world.bodies[p].availability;
        c->energy=std::fmin(rules->storage,c->energy+dt*d.collection*c->uptake*production);
        if (!mature)
        { memset(c->leaves,0,sizeof(c->leaves));c->building=false;c->manufacturing=0;c->capacity_blocked=false;continue; }
        uint32_t tips[MAX_FACTORY_STRUCTURES],count=0;
        for (uint32_t k=0;k<c->structure_count;++k)
        {
            if ((k ? c->branches[k-1].health : c->health)<=0 || simulation_structure_growth(s,p,k)<1) continue;
            bool child=false;
            for (uint32_t j=k+1;j<c->structure_count;++j)
                child=child || (c->parents[j]==(int32_t)k && c->branches[j-1].health>0);
            if (!child && (f.grammar!=GROWTH_TREE || simulation_structure_terminal(s,p,k))) tips[count++]=k;
        }
        // Each ready leaf owns a job. All jobs draw from the same cell nutrients.
        bool ready[MAX_FACTORY_STRUCTURES]={};
        for (uint32_t i=0;i<count;++i) ready[tips[i]]=true;
        for (uint32_t k=0;k<c->structure_count;++k) if (!ready[k] && c->leaves[k].active)
        {
            // A new child can replace a tip. Move its paid job to another ready leaf.
            // Damage removes the job with the damaged support.
            if (structure_health(c,k)<=0 || simulation_structure_growth(s,p,k)<1)
            { c->leaves[k]=LeafProduction();continue; }
            for (uint32_t j=0;j<count;++j) if (!c->leaves[tips[j]].active)
            { c->leaves[tips[j]]=c->leaves[k];c->leaves[k]=LeafProduction();break; }
            // If all ready leaves are busy, retain the paid job until one becomes free.
        }
        for (uint32_t i=0;i<c->structure_count;++i)
        {
            uint32_t k=(c->production_cursor+i)%c->structure_count;LeafProduction* job=&c->leaves[k];
            if (!ready[k] || job->active) continue;
            uint32_t type=c->priority,error=c->production_error+c->fighter_percent;
            if (c->ratio_enabled) type=(uint32_t)content_product(&s->world.content,c->faction,error>=100 ? 0 : 1);
            float cost=s->world.content.units[type].cost;
            bool automatic=s->world.content.factions[c->faction].automatic;
            if (!automatic && c->energy<cost) break;
            if (!automatic) c->energy-=cost;
            job->active=true;job->type=type;job->progress=0;
            if (c->ratio_enabled) c->production_error=error%100;
            c->production_cursor=(k+1)%c->structure_count;
            // Continue from the same cursor on the next tick. Do not skip other leaves.
            break;
        }
        float rate=s->world.ai_production_set && (c->faction!=s->world.player_faction || s->world.player_ai) ? s->world.ai_production : 1;
        float step=dt*rate/rules->manufacture_seconds;
        c->building=false;c->manufacturing=0;c->capacity_blocked=false;
        for (uint32_t k=0;k<c->structure_count;++k)
        {
            LeafProduction* job=&c->leaves[k];if (!job->active || !ready[k]) continue;
            job->progress=std::fmin(1.0f,job->progress+step);
            if (job->progress>=1)
            {
                const UnitDefinition* definition=&s->world.content.units[job->type];
                Vec3 normal=s->world.bodies[p].colony_normal;
                Vec3 axis=std::fabs(normal.z)<PRODUCTION_AXIS_PARALLEL_LIMIT ? Vec3{0,0,1} : Vec3{1,0,0};
                Vec3 tangent=normalize(cross(normal,axis));
                float angle=(s->next_generation+1)*GOLDEN_ANGLE_RADIANS,spread=definition->scale*2;
                Vec3 position=add(add(colony_tip(s,p),scale(normal,1.0f)),
                    add(scale(tangent,std::cos(angle)*spread),scale(cross(tangent,normal),std::sin(angle)*spread)));
                if (d.segment_path[0])
                {
                    Vec3 tip=simulation_structure_position(s,p,k);
                    normal=normalize(subtract(tip,s->world.bodies[p].position));
                    position=add(tip,scale(normal,definition->scale*NEW_UNIT_TIP_CLEARANCE_SCALE));
                }
                // A brood hatches together around the leaf. A partial brood completes the job.
                uint32_t hatched=0;
                for (uint32_t b=0;b<definition->brood;++b)
                {
                    Vec3 offset=b ? scale(Vec3{std::cos(b*GOLDEN_ANGLE_RADIANS),std::sin(b*GOLDEN_ANGLE_RADIANS),std::cos(b*BROOD_SPREAD_PHASE)},definition->scale*BROOD_SPREAD_SCALE*std::sqrt((float)b)) : Vec3{};
                    hatched+=spawn(s,job->type,add(position,offset),(int)p,&normal);
                }
                if (hatched)
                { if (definition->group<2) c->produced[definition->group]+=hatched;c->leaf_births[k]+=hatched;*job=LeafProduction();c->production_tip=k; }
                else c->capacity_blocked=true;
            }
            if (job->active && (!c->building || job->progress>c->manufacturing))
            { c->building=true;c->manufacturing=job->progress;c->building_kind=job->type;c->production_tip=k; }
        }
    }
}

// Built-in strategy for one side: expand to the nearest site, then assault.
static void ai_orders(Simulation* s,Faction enemy)
{
    int home=home_body(s,enemy),target=-1;float nearest=INITIAL_ENEMY_DISTANCE;bool assault_ready=false;
    bool attack=s->time_seconds>=s->world.gameplay.enemy_attack_seconds;
    for (uint32_t p=0;p<s->world.body_count;++p) if (s->world.content.bodies[s->world.bodies[p].type].hosts_site && s->world.bodies[p].owner!=enemy
        && (s->home_owner[p]==FACTION_NEUTRAL || attack))
    {
        float d=length(subtract(s->world.bodies[p].position,s->world.bodies[home].position));
        if (d<nearest) { nearest=d;target=(int)p; }
    }
    for (uint32_t i=0;i<MAX_UNITS;++i) assault_ready=assault_ready || (s->units[i].health>0 && s->units[i].faction==enemy && (simulation_unit_definition(s,&s->units[i])->targets&TARGET_STRUCTURES));
    for (uint32_t p=0;p<s->world.body_count;++p) if (s->colonies[p].active && s->colonies[p].faction==enemy)
        s->colonies[p].priority=!assault_ready && target>=0 ? s->world.content.factions[enemy].assault_unit : s->world.content.factions[enemy].default_unit;
    bool expand=s->time_seconds>=(s->world.ai_production_set ? s->world.ai_expansion : s->world.gameplay.enemy_attack_seconds);
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];if (u->health<=0 || u->faction!=enemy) continue;
        bool establishing=s->world.neutral_start && !s->faction_established[enemy];
        int32_t next=!establishing && expand && target>=0 ? target : home;
        if (u->target!=next) { u->attention=-1;u->response_timer=0; }
        u->target=next;u->commanded=expand && (establishing || target>=0);
    }
}

// An automatic player faction sends its colonizers home once expansion opens.
static void automatic_player_orders(Simulation* s)
{
    Faction player=s->world.player_faction;
    bool expand=s->time_seconds>=(s->world.ai_production_set ? s->world.ai_expansion : s->world.gameplay.enemy_attack_seconds);
    if (s->world.neutral_start && !s->faction_established[player] && s->world.content.factions[player].automatic && expand)
        for (uint32_t i=0;i<MAX_UNITS;++i)
            if (s->units[i].health>0 && s->units[i].faction==player && simulation_unit_definition(s,&s->units[i])->colonizes)
            { s->units[i].target=home_body(s,player);s->units[i].commanded=true; }
}

static void outcome(Simulation* s)
{
    if (s->outcome) return;
    bool friendly=false,hostile=false,units=false,enemy_units=false;
    for (uint32_t p=0;p<s->world.body_count;++p) if (s->colonies[p].active)
    {
        friendly=friendly || s->colonies[p].faction==s->world.player_faction;
        hostile=hostile || s->colonies[p].faction!=s->world.player_faction;
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        units=units || (s->units[i].health>0 && s->units[i].faction==s->world.player_faction);
        enemy_units=enemy_units || (s->units[i].health>0 && s->units[i].faction==s->world.opponent_faction);
    }
    if (friendly && !hostile && (s->faction_established[s->world.opponent_faction] || !enemy_units)) { s->outcome=1;s->outcome_tick=s->tick_count;sound(s,SOUND_WIN,s->world.bodies[0].position);snprintf(s->message,sizeof(s->message),"YOU WON. CONTINUE PLAYING OR PRESS R TO RESTART."); }
    else if (!friendly && !units) { s->outcome=-1;s->outcome_tick=s->tick_count;sound(s,SOUND_LOSE,s->world.bodies[0].position);snprintf(s->message,sizeof(s->message),"COLONY LOST. R TO RESTART."); }
}

void simulation_step(Simulation* s)
{
    if (s->paused) return;
    const float dt=1.0f/SIMULATION_TICKS_PER_SECOND_FLOAT;
    if (s->outcome<0)
    {
        if (s->world.combat.enabled) { advance_visuals(s,dt);recover_units(s,dt);for (uint32_t i=0;i<MAX_PROJECTILES;++i) s->projectiles[i].active=false;simulation_set_time(s,s->time_seconds+1.0/SIMULATION_TICKS_PER_SECOND_DOUBLE); }
        simulation_update_fog(s,dt);return;
    }
    if (s->world.gameplay.enabled)
    {
        ai_orders(s,s->world.opponent_faction);
        if (s->world.player_ai) ai_orders(s,s->world.player_faction);
        else automatic_player_orders(s);
        if (s->world.combat.enabled) advance_visuals(s,dt);
        recover_units(s,dt);move_units(s,dt);
        projectile_combat(s,dt);
        colonize_cells(s,dt);grow_colonies(s,dt);outcome(s);simulation_update_sight(s);
        uint32_t alive[MAX_FACTIONS]={};
        for (uint32_t i=0;i<MAX_UNITS;++i) if (s->units[i].health>0) ++alive[s->units[i].faction];
        for (uint32_t f=0;f<MAX_FACTIONS;++f) s->faction_stats[f].peak_units=std::max(s->faction_stats[f].peak_units,alive[f]);
        for (uint32_t p=0;p<s->world.body_count;++p)
        {
            CellAppearance& appearance=s->cell_appearance[p];
            const FactionDefinition& f=s->world.content.factions[s->world.bodies[p].owner];
            for (uint32_t faction=1;faction<MAX_FACTIONS;++faction)
            {
                const FactionDefinition& profile=s->world.content.factions[faction];
                if (profile.morph_seconds<=0) continue;
                float target=s->world.bodies[p].owner==faction ? 1.0f : 0.0f,step=dt/profile.morph_seconds;
                float* blend=&appearance.factions[faction];*blend+=std::fmax(-step,std::fmin(step,target-*blend));
            }
            if (f.cell_strength>0)
            {
                if (appearance.strength<=0) appearance.tint=f.cell_tint;
                appearance.seconds=f.transform_seconds;
                float t=dt/appearance.seconds;
                appearance.tint.r+=(f.cell_tint.r-appearance.tint.r)*t;
                appearance.tint.g+=(f.cell_tint.g-appearance.tint.g)*t;
                appearance.tint.b+=(f.cell_tint.b-appearance.tint.b)*t;
            }
            if (appearance.seconds>0)
            {
                float change=dt/appearance.seconds;
                appearance.strength+=std::fmax(-change,std::fmin(change,f.cell_strength-appearance.strength));
            }
        }
    }
    simulation_update_fog(s,dt);
    ++s->tick_count;simulation_set_time(s,s->time_seconds+1.0/SIMULATION_TICKS_PER_SECOND_DOUBLE);
}

// One entry per named faction: its current colonies and units, then match totals.
static void write_faction_stats(const Simulation* s,FILE* file)
{
    uint32_t colonies[MAX_FACTIONS]={},units[MAX_FACTIONS]={};
    for (uint32_t p=0;p<s->world.body_count;++p) if (s->colonies[p].active) ++colonies[s->colonies[p].faction];
    for (uint32_t i=0;i<MAX_UNITS;++i) if (s->units[i].health>0) ++units[s->units[i].faction];
    fprintf(file,"\"faction_stats\":[");
    bool listed=false;
    for (uint32_t f=1;f<MAX_FACTIONS;++f)
    {
        if (!s->world.content.factions[f].name[0]) continue;
        const Simulation::FactionStats& t=s->faction_stats[f];
        fprintf(file,"%s{\"faction\":\"%s\",\"colonies\":%u,\"units\":%u,\"built\":%u,\"lost\":%u,\"limbs_lost\":%u,\"colonies_gained\":%u,\"colonies_lost\":%u,\"peak_units\":%u}",
            listed ? "," : "",s->world.content.factions[f].name,colonies[f],units[f],t.built,t.lost,t.limbs_lost,t.colonies_gained,t.colonies_lost,t.peak_units);
        listed=true;
    }
    fprintf(file,"]");
}
void simulation_write_summary(const Simulation* s,uint32_t seed,FILE* file)
{
    uint32_t blocked=0;for (uint32_t p=0;p<s->world.body_count;++p) blocked+=s->colonies[p].capacity_blocked;
    fprintf(file,"{\"seed\":%u,\"tick\":%u,\"unit_capacity\":%u,\"outcome\":%d,\"outcome_tick\":%u,\"capacity_blocked\":%u,",seed,s->tick_count,(uint32_t)MAX_UNITS,s->outcome,s->outcome_tick,blocked);
    write_faction_stats(s,file);fprintf(file,"}\n");
}

bool simulation_write_state(const Simulation* s, const char* path)
{
    FILE* file=fopen(path,"wb");if (!file) { fprintf(stderr,"Cannot write state '%s'.\n",path);return false; }
    fprintf(file,"{\"tick\":%u,\"time\":%.9f,\"unit_capacity\":%u,\"outcome\":%d,\"outcome_tick\":%u,\"births\":%u,\"losses\":%u,\"landings\":%u,\"blocked_landings\":%u,",
        s->tick_count,s->time_seconds,(uint32_t)MAX_UNITS,s->outcome,s->outcome_tick,s->births,s->losses,s->landings,s->blocked_landings);
    write_faction_stats(s,file);
    uint32_t clouds=0;for (uint32_t i=0;i<MAX_SPORE_CLOUDS;++i) clouds+=s->clouds[i].age<s->clouds[i].seconds;
    fprintf(file,",\"faction_events\":{\"digestions\":%u,\"digest_heals\":%u,\"spore_bursts\":%u,\"spore_clouds\":%u,\"captures\":%u,\"releases\":%u,\"blobs\":%u,\"devoured\":%u,\"aura_heals\":%u},\"sound_events\":{",s->digestions,s->digest_heals,s->spore_bursts,clouds,s->captures,s->releases,s->blobs,s->devoured,s->aura_heals);
    for (uint32_t i=0;i<SOUND_COUNT;++i) fprintf(file,"%s\"%s\":%u",i ? "," : "",sound_name(i),s->sounds.totals[i]);
    uint32_t fragments=0;for (uint32_t i=0;i<MAX_LIMB_DEBRIS;++i) fragments+=s->debris[i].seconds>0;
    fprintf(file,"},\"sound_dropped\":%u,\"fractures\":{\"limbs\":%u,\"fragments\":%u,\"replaced\":%u},\"colonies\":[",s->sounds.dropped,s->limbs_broken,fragments,s->debris_replaced);
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        const Colony* c=&s->colonies[p];
        fprintf(file,"%s{\"%s\":%u,\"faction\":%d,\"health\":%.6f,\"growth\":%.6f,\"%s\":%.6f,\"energy\":%.6f,\"building\":%s,\"priority\":%d,\"manufacturing\":%.6f,\"capacity_blocked\":%s,\"wounds\":%u",
            p ? "," : "","cell",p,(int)c->faction,c->health,c->growth,"nutrients",c->uptake,c->energy,c->building ? "true" : "false",(int)(c->faction ? s->world.content.units[c->priority].group : 0),c->manufacturing,c->capacity_blocked ? "true" : "false",c->wounds);
        fprintf(file,",\"fighter_percent\":%u,\"ratio_enabled\":%s,\"produced\":[%u,%u],\"building_type\":\"%s\"",c->fighter_percent,c->ratio_enabled ? "true" : "false",c->produced[0],c->produced[1],c->building ? s->world.content.units[c->building_kind].name : "");
        const Body& body=s->world.bodies[p];
        fprintf(file,",\"active\":%s,\"establishment_age\":%.6f,\"root_count\":%u,\"production\":%.6f,\"attachments\":[",c->active ? "true" : "false",c->establishment_age,c->root_count,c->active ? simulation_production(s,p) : 0);
        for (uint32_t k=0;k<body.factory_root_count;++k) { Vec3 v=body.factory_roots[k];fprintf(file,"%s[%.6f,%.6f,%.6f]",k ? "," : "",v.x,v.y,v.z); }
        fprintf(file,"],\"attachment_radii\":[");
        for (uint32_t k=0;k<body.factory_root_count;++k) fprintf(file,"%s%.9g",k ? "," : "",body.factory_root_radii[k]);
        fprintf(file,"],\"root_attachments\":[");
        for (uint32_t k=0;k<c->root_count;++k) fprintf(file,"%s%u",k ? "," : "",c->root_attachments[k]);
        fprintf(file,"]");
        fprintf(file,",\"appearance\":{\"tint\":[%.6f,%.6f,%.6f],\"strength\":%.6f,\"factions\":[",s->cell_appearance[p].tint.r,s->cell_appearance[p].tint.g,s->cell_appearance[p].tint.b,s->cell_appearance[p].strength);
        for (uint32_t f=0;f<MAX_FACTIONS;++f) fprintf(file,"%s%.6f",f ? "," : "",s->cell_appearance[p].factions[f]);
        fprintf(file,"]}");
        fprintf(file,",\"leaf_births\":[");
        for (uint32_t k=0;k<c->structure_count;++k) fprintf(file,"%s%u",k ? "," : "",c->leaf_births[k]);
        fprintf(file,"],\"leaf_jobs\":[");
        bool comma=false;
        for (uint32_t k=0;k<c->structure_count;++k) if (c->leaves[k].active)
        { fprintf(file,"%s{\"leaf\":%u,\"progress\":%.6f,\"type\":\"%s\"}",comma ? "," : "",k,c->leaves[k].progress,s->world.content.units[c->leaves[k].type].name);comma=true; }
        fprintf(file,"]");
        fprintf(file,",\"seed\":%u,\"root\":[%.6f,%.6f,%.6f],\"production_tip\":%u,\"discovered\":%s,\"visible\":%s,\"structures\":[",body.seed,body.factory_root.x,body.factory_root.y,body.factory_root.z,c->production_tip,s->discovered[p] ? "true" : "false",s->visible_bodies[p] ? "true" : "false");
        uint32_t count=s->world.gameplay.enabled && c->faction!=FACTION_NEUTRAL && s->world.content.bodies[s->world.bodies[p].type].hosts_site ? s->colonies[p].structure_count : 0;
        for (uint32_t k=0;k<count;++k)
        {
            const FactoryStructure* b=k ? &c->branches[k-1] : 0;Vec3 position=simulation_structure_position(s,p,k);
            fprintf(file,"%s{\"health\":%.6f,\"growth\":%.6f,\"wounds\":%u,\"generation\":%u,\"position\":[%.6f,%.6f,%.6f]",k ? "," : "",
                k ? b->health : c->health,k ? b->growth : c->growth,k ? b->wounds : c->wounds,k ? b->generation : c->root_generation,position.x,position.y,position.z);
            fprintf(file,",\"exposure\":%.6f,\"parent\":%d,\"visible_growth\":%.6f,\"scale\":%.6f,\"terminal\":%s",k ? b->exposure : c->exposure,c->parents[k],simulation_structure_growth(s,p,k),simulation_structure_scale(s,p,k),simulation_structure_terminal(s,p,k) ? "true" : "false");
            fprintf(file,",\"stem\":[");
            for (uint32_t j=0;j<=GROWTH_SEGMENTS;++j) { Vec3 v=c->stems[k][j];fprintf(file,"%s[%.6f,%.6f,%.6f]",j ? "," : "",v.x,v.y,v.z); }
            fprintf(file,"]}");
        }
        fprintf(file,"]}");
    }
    fprintf(file,"],\"flight\":{\"avoidance_events\":%u,\"contacts\":%u,\"obstacles\":[",s->avoidance_events,s->factory_contacts);
    FlightObstacles obstacles;flight_obstacles(s,&obstacles);
    for (uint32_t i=0;i<obstacles.count;++i)
    {
        const FlightObstacle& o=obstacles.values[i];
        fprintf(file,"%s{\"cell\":%u,\"structure\":%u,\"start\":[%.6f,%.6f,%.6f],\"end\":[%.6f,%.6f,%.6f],\"radius\":%.6f}",
            i ? "," : "",o.cell,o.structure,o.start.x,o.start.y,o.start.z,o.end.x,o.end.y,o.end.z,o.radius);
    }
    fprintf(file,"]},\"units\":[");bool first=true;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0)
        {
            const Unit* u=&s->units[i];
            uint32_t ready=0;for (uint32_t j=0;j<s->world.spikes.count;++j) ready+=u->spikes[j]>=1;
            fprintf(file,"%s{\"slot\":%u,\"generation\":%u,\"faction\":%d,\"kind\":%d,\"health\":%.6f,\"target\":%d,\"position\":[%.6f,%.6f,%.6f],\"velocity\":[%.6f,%.6f,%.6f],\"bank\":%.6f,\"selected\":%s,\"wounds\":%u,\"ready_spikes\":%u,\"flash\":%.6f,\"visible\":%s,\"moving\":%s",
                first ? "" : ",",i,u->generation,(int)u->faction,(int)simulation_unit_definition(s,u)->group,u->health,u->target,u->position.x,u->position.y,u->position.z,u->velocity.x,u->velocity.y,u->velocity.z,u->bank,u->selected ? "true" : "false",u->wounds,ready,u->flash,s->visible_units[i] ? "true" : "false",u->moving ? "true" : "false");
            fprintf(file,",\"exposure\":%.6f,\"stun\":%.6f,\"aura\":%.6f",u->exposure,u->stun,u->aura);
            uint32_t pods=0;const UnitDefinition* definition=simulation_unit_definition(s,u);
            for (uint32_t j=0;j<definition->payload_count;++j) pods+=u->payloads[j]>=1;
            fprintf(file,",\"ready_payloads\":%u,\"payload_growth\":[",pods);
            for (uint32_t j=0;j<definition->payload_count;++j) fprintf(file,"%s%.6f",j ? "," : "",u->payloads[j]);
            fprintf(file,"]");
            static const char* tasks[]={"patrol","move","attack-unit","attack-structure","colonize","launch"};
            fprintf(file,",\"task\":\"%s\",\"attention\":%d,\"attention_generation\":%u,\"commanded\":%s,\"flight_goal\":[%.6f,%.6f,%.6f]",
                tasks[u->task],u->attention,u->attention_generation,u->commanded ? "true" : "false",u->flight_goal.x,u->flight_goal.y,u->flight_goal.z);
            fprintf(file,",\"held_by\":%d,\"charging\":%s,\"latch_hits\":%u,\"latch_recovery\":%.6f",simulation_held_by(s,i),u->charging ? "true" : "false",u->latch_hits,u->latch_recovery);
            fprintf(file,",\"type\":\"%s\",\"latch\":%d,\"latch_generation\":%u,\"latch_colony\":%s}",simulation_unit_definition(s,u)->name,u->latch,u->latch_generation,u->latch_colony ? "true" : "false");first=false;
        }
    fprintf(file,"],\"projectile_state\":[");first=true;
    for (uint32_t i=0;i<MAX_PROJECTILES;++i) if (s->projectiles[i].active)
    {
        const Projectile& p=s->projectiles[i];
        fprintf(file,"%s{\"type\":\"%s\",\"payload\":%s,\"position\":[%.6f,%.6f,%.6f],\"previous\":[%.6f,%.6f,%.6f],\"scale\":%.6f}",
            first ? "" : ",",s->world.content.units[p.unit_type].name,p.payload ? "true" : "false",p.position.x,p.position.y,p.position.z,p.previous.x,p.previous.y,p.previous.z,p.launch_scale);first=false;
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
