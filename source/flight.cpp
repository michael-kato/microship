#include "flight.h"
#include "vector_math.h"

static float length(Vec3 v) { return std::sqrt(dot(v,v)); }
static Vec3 nearest(Vec3 point,const FlightObstacle& o)
{
    Vec3 delta=subtract(o.end,o.start);float squared=dot(delta,delta);
    float t=squared>1e-12f ? std::fmax(0.0f,std::fmin(1.0f,dot(subtract(point,o.start),delta)/squared)) : 0;
    return add(o.start,scale(delta,t));
}
static bool contact_task(const Simulation* s,const Unit& u,const FlightObstacle& o)
{
    const UnitDefinition& d=*simulation_unit_definition(s,&u);
    return d.attack==ATTACK_SPORE || (u.hatched && u.age<2)
        || (d.attack==ATTACK_DIGEST && u.task==TASK_ATTACK_STRUCTURE && u.attention==(int32_t)o.cell && u.attention_structure==o.structure);
}
void flight_obstacles(const Simulation* s,FlightObstacles* out)
{
    out->count=0;
    if (s->world.combat.lookahead<=0) return;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        const Colony& c=s->colonies[p];if (!c.active) continue;
        const ProducerDefinition& d=*simulation_producer_definition(s,p);
        if (!d.segment_path[0] || d.stem_radius<=0 || d.head_radius<=0) continue;
        for (uint32_t k=0;k<d.branches.count;++k)
        {
            if ((k ? c.branches[k-1].health : c.health)<=0) continue;
            float growth=simulation_structure_growth(s,p,k);
            for (uint32_t j=0;j<GROWTH_SEGMENTS;++j)
            {
                float extension=std::fmax(0.0f,std::fmin(1.0f,growth*GROWTH_SEGMENTS-j));
                if (extension<=.0001f) continue;
                float width=d.width*(1-.12f*j)*(c.parents[k]>=0 ? .65f : 1)*(.55f+.45f*growth);
                Vec3 a=c.stems[k][j],b=add(a,scale(subtract(c.stems[k][j+1],a),extension));
                out->values[out->count++]=FlightObstacle{a,b,width*d.stem_radius,p,k};
            }
            if (growth>=(k ? c.branches[k-1].growth : c.growth))
            {
                Vec3 tip=simulation_structure_position(s,p,k);
                float size=d.width*(c.parents[k]>=0 ? .7f : 1)*(.4f+.6f*growth);
                out->values[out->count++]=FlightObstacle{tip,tip,size*d.head_radius,p,k};
            }
        }
    }
}
Vec3 flight_avoid(Simulation* s,const FlightObstacles& obstacles,const Unit& u,Vec3 desired,float speed)
{
    const CombatVisuals& f=s->world.combat;
    if (f.lookahead<=0) return desired;
    const UnitDefinition& d=*simulation_unit_definition(s,&u);
    Vec3 ahead=scale(u.velocity,f.lookahead),force={};
    if (dot(ahead,ahead)<.01f) ahead=scale(desired,f.lookahead);
    bool avoided=false;
    for (uint32_t i=0;i<obstacles.count;++i)
    {
        const FlightObstacle& o=obstacles.values[i];if (contact_task(s,u,o)) continue;
        float safe=o.radius+d.scale*.8f+f.obstacle_clearance;
        float best=1e20f;Vec3 away={};
        for (uint32_t j=0;j<4;++j)
        {
            Vec3 probe=add(u.position,scale(ahead,j/3.0f));Vec3 delta=subtract(probe,nearest(probe,o));
            float squared=dot(delta,delta);if (squared<best) { best=squared;away=delta; }
        }
        if (best>=safe*safe) continue;
        float strength=1-std::sqrt(best)/safe;
        away=subtract(u.position,nearest(u.position,o));float distance=length(away);
        if (distance<.0001f)
        {
            away=cross(subtract(o.end,o.start),desired);
            if (dot(away,away)<.0001f) away=Vec3{1,0,0};
            away=normalize(away);
        }
        else away=scale(away,1/distance);
        Vec3 axis=subtract(o.end,o.start);
        if (dot(axis,axis)<.0001f) axis=s->world.bodies[o.cell].colony_normal;
        Vec3 side=cross(axis,away);
        if (dot(side,side)>.0001f)
        {
            side=normalize(side);float direction=dot(side,desired);
            if (direction<0 || (std::fabs(direction)<.001f && u.generation%2)) side=scale(side,-1);
        }
        force=add(force,scale(add(scale(away,4),scale(side,1.2f)),speed*strength));avoided=true;
    }
    if (avoided) ++s->avoidance_events;
    return add(desired,force);
}
void flight_clip(Simulation* s,const FlightObstacles& obstacles,Unit* u,Vec3 previous)
{
    const UnitDefinition& d=*simulation_unit_definition(s,u);
    for (uint32_t i=0;i<obstacles.count;++i)
    {
        const FlightObstacle& o=obstacles.values[i];if (contact_task(s,*u,o)) continue;
        float radius=o.radius+d.scale*.8f;
        Vec3 center=nearest(u->position,o),away=subtract(u->position,center);float distance=length(away);
        if (distance>=radius) continue;
        ++s->factory_contacts;
        // Stop at the obstacle if the previous point was outside. This bounds the correction.
        Vec3 before=subtract(previous,nearest(previous,o));
        if (dot(before,before)>=radius*radius)
        {
            u->position=previous;
            if (distance>.0001f)
            {
                Vec3 normal=scale(away,1/distance);float inward=dot(u->velocity,normal);
                if (inward<0) u->velocity=subtract(u->velocity,scale(normal,inward));
            }
        }
        else if (distance>.0001f)
        {
            // Growth can enclose a craft between ticks. Move it outward at its current speed limit.
            float step=std::fmin(radius-distance,d.speed/60);
            u->position=add(previous,scale(away,step/distance));
        }
    }
}
