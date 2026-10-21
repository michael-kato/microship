#include "flight.h"
#include "vector_math.h"
#include "factory_shape.h"

static const float MIN_OBSTACLE_SEGMENT_SQUARED_LENGTH = 1e-12f;
static const float OBSTACLE_DIRECTION_EPSILON = .0001f;
static const float MIN_LOOKAHEAD_SQUARED_LENGTH = .01f;
static const float CRAFT_COLLISION_RADIUS_SCALE = .8f;
static const float INITIAL_OBSTACLE_SQUARED_DISTANCE = 1e20f;
static const float PROBE_INTERVALS_FLOAT = 3.0f;
static const float SIDE_DIRECTION_EPSILON = .001f;
static const float TANGENTIAL_AVOIDANCE_GAIN = 1.2f;
static const int LOOKAHEAD_PROBE_COUNT = 4;
static const int RADIAL_AVOIDANCE_GAIN = 4;

static float length(Vec3 v) { return std::sqrt(dot(v,v)); }
static FlightObstacle capsule(Vec3 a,Vec3 b,float radius,uint32_t cell,uint32_t structure)
{ return FlightObstacle{a,b,scale(add(a,b),.5f),radius,length(subtract(b,a))*.5f+radius,cell,structure}; }
static float box_distance(Vec3 p,Vec3 low,Vec3 high)
{
    Vec3 d={std::fmax(0.0f,std::fmax(low.x-p.x,p.x-high.x)),std::fmax(0.0f,std::fmax(low.y-p.y,p.y-high.y)),std::fmax(0.0f,std::fmax(low.z-p.z,p.z-high.z))};
    return length(d);
}
static Vec3 nearest(Vec3 point,const FlightObstacle& o)
{
    Vec3 delta=subtract(o.end,o.start);float squared=dot(delta,delta);
    float t=squared>MIN_OBSTACLE_SEGMENT_SQUARED_LENGTH ? std::fmax(0.0f,std::fmin(1.0f,dot(subtract(point,o.start),delta)/squared)) : 0;
    return add(o.start,scale(delta,t));
}
// Contact attackers must reach their target. A digester attacking a structure
// ignores every limb of that colony: dense trees surround each head with other
// limbs, whose clearance would otherwise hold it outside its attach range.
static bool contact_task(const Simulation* s,const Unit& u,const FlightObstacle& o)
{
    const UnitDefinition& d=*simulation_unit_definition(s,&u);
    return d.attack==ATTACK_SPORE || (u.hatched && u.age<UNIT_LAUNCH_SECONDS)
        || (d.attack==ATTACK_DIGEST && u.task==TASK_ATTACK_STRUCTURE && u.attention==(int32_t)o.cell);
}
void flight_obstacles(const Simulation* s,FlightObstacles* out)
{
    out->count=out->cell_count=0;out->first[0]=0;
    if (s->world.combat.lookahead<=0) return;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        const Colony& c=s->colonies[p];if (!c.active) continue;
        uint32_t first=out->count;
        const ProducerDefinition& d=*simulation_producer_definition(s,p);
        if (!d.segment_path[0] || d.stem_radius<=0 || d.head_radius<=0) continue;
        for (uint32_t k=0;k<c.structure_count;++k)
        {
            if ((k ? c.branches[k-1].health : c.health)<=0) continue;
            float growth=simulation_structure_growth(s,p,k);
            for (uint32_t j=0;j<GROWTH_SEGMENTS;++j)
            {
                float extension=std::fmax(0.0f,std::fmin(1.0f,growth*GROWTH_SEGMENTS-j));
                if (extension<=OBSTACLE_DIRECTION_EPSILON) continue;
                float width=d.width*(1-FACTORY_STEM_TAPER_PER_SEGMENT*j)*simulation_structure_scale(s,p,k)*(FACTORY_STEM_START_WIDTH_SCALE+FACTORY_STEM_GROWTH_WIDTH_SCALE*growth);
                Vec3 a=c.stems[k][j],b=add(a,scale(subtract(c.stems[k][j+1],a),extension));
                out->values[out->count++]=capsule(a,b,width*d.stem_radius,p,k);
            }
            if (growth>=(k ? c.branches[k-1].growth : c.growth))
            {
                Vec3 tip=simulation_structure_position(s,p,k);
                float size=d.width*simulation_structure_head_scale(s,p,k)*(FACTORY_HEAD_START_WIDTH_SCALE+FACTORY_HEAD_GROWTH_WIDTH_SCALE*growth);
                out->values[out->count++]=capsule(tip,tip,size*d.head_radius,p,k);
            }
        }
        if (out->count==first) continue;
        Vec3 low=out->values[first].center,high=low;
        for (uint32_t i=first;i<out->count;++i)
        {
            const FlightObstacle& o=out->values[i];
            low=Vec3{std::fmin(low.x,o.center.x-o.bound),std::fmin(low.y,o.center.y-o.bound),std::fmin(low.z,o.center.z-o.bound)};
            high=Vec3{std::fmax(high.x,o.center.x+o.bound),std::fmax(high.y,o.center.y+o.bound),std::fmax(high.z,o.center.z+o.bound)};
        }
        out->low[out->cell_count]=low;out->high[out->cell_count]=high;out->first[++out->cell_count]=out->count;
    }
}
Vec3 flight_avoid(Simulation* s,const FlightObstacles& obstacles,const Unit& u,Vec3 desired,float speed)
{
    const CombatVisuals& f=s->world.combat;
    if (f.lookahead<=0) return desired;
    const UnitDefinition& d=*simulation_unit_definition(s,&u);
    Vec3 ahead=scale(u.velocity,f.lookahead),force={};
    if (dot(ahead,ahead)<MIN_LOOKAHEAD_SQUARED_LENGTH) ahead=scale(desired,f.lookahead);
    bool avoided=false;
    // Probes lie within |ahead| of the craft. Skip obstacles whose bound cannot come within safe distance.
    float reach=length(ahead)+d.scale*CRAFT_COLLISION_RADIUS_SCALE+f.obstacle_clearance;
    for (uint32_t c=0;c<obstacles.cell_count;++c)
    {
        if (box_distance(u.position,obstacles.low[c],obstacles.high[c])>=reach) continue;
        for (uint32_t i=obstacles.first[c];i<obstacles.first[c+1];++i)
        {
            const FlightObstacle& o=obstacles.values[i];
            if (length(subtract(u.position,o.center))>=o.bound+reach || contact_task(s,u,o)) continue;
            float safe=o.radius+d.scale*CRAFT_COLLISION_RADIUS_SCALE+f.obstacle_clearance;
            float best=INITIAL_OBSTACLE_SQUARED_DISTANCE;Vec3 away={};
            for (uint32_t j=0;j<LOOKAHEAD_PROBE_COUNT;++j)
            {
                Vec3 probe=add(u.position,scale(ahead,j/PROBE_INTERVALS_FLOAT));Vec3 delta=subtract(probe,nearest(probe,o));
                float squared=dot(delta,delta);if (squared<best) { best=squared;away=delta; }
            }
            if (best>=safe*safe) continue;
            float strength=1-std::sqrt(best)/safe;
            away=subtract(u.position,nearest(u.position,o));float distance=length(away);
            if (distance<OBSTACLE_DIRECTION_EPSILON)
            {
                away=cross(subtract(o.end,o.start),desired);
                if (dot(away,away)<OBSTACLE_DIRECTION_EPSILON) away=Vec3{1,0,0};
                away=normalize(away);
            }
            else away=scale(away,1/distance);
            Vec3 axis=subtract(o.end,o.start);
            if (dot(axis,axis)<OBSTACLE_DIRECTION_EPSILON) axis=s->world.bodies[o.cell].colony_normal;
            Vec3 side=cross(axis,away);
            if (dot(side,side)>OBSTACLE_DIRECTION_EPSILON)
            {
                side=normalize(side);float direction=dot(side,desired);
                if (direction<0 || (std::fabs(direction)<SIDE_DIRECTION_EPSILON && u.generation%2)) side=scale(side,-1);
            }
            force=add(force,scale(add(scale(away,RADIAL_AVOIDANCE_GAIN),scale(side,TANGENTIAL_AVOIDANCE_GAIN)),speed*strength));avoided=true;
        }
    }
    if (avoided) ++s->avoidance_events;
    return add(desired,force);
}
void flight_clip(Simulation* s,const FlightObstacles& obstacles,Unit* u,Vec3 previous)
{
    const UnitDefinition& d=*simulation_unit_definition(s,u);float craft=d.scale*CRAFT_COLLISION_RADIUS_SCALE;
    for (uint32_t c=0;c<obstacles.cell_count;++c)
    {
        if (box_distance(u->position,obstacles.low[c],obstacles.high[c])>=craft) continue;
        for (uint32_t i=obstacles.first[c];i<obstacles.first[c+1];++i)
        {
            const FlightObstacle& o=obstacles.values[i];
            if (length(subtract(u->position,o.center))>=o.bound+craft || contact_task(s,*u,o)) continue;
            float radius=o.radius+craft;
            Vec3 center=nearest(u->position,o),away=subtract(u->position,center);float distance=length(away);
            if (distance>=radius) continue;
            ++s->factory_contacts;
            // Stop at the obstacle if the previous point was outside. This bounds the correction.
            Vec3 before=subtract(previous,nearest(previous,o));
            if (dot(before,before)>=radius*radius)
            {
                u->position=previous;
                if (distance>OBSTACLE_DIRECTION_EPSILON)
                {
                    Vec3 normal=scale(away,1/distance);float inward=dot(u->velocity,normal);
                    if (inward<0) u->velocity=subtract(u->velocity,scale(normal,inward));
                }
            }
            else if (distance>OBSTACLE_DIRECTION_EPSILON)
            {
                // Growth can enclose a craft between ticks. Move it outward at its current speed limit.
                float step=std::fmin(radius-distance,d.speed/SIMULATION_TICKS_PER_SECOND);
                u->position=add(previous,scale(away,step/distance));
            }
        }
    }
}
