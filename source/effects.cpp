#include "effects.h"
#include "simulation.h"
#include "camera.h"
#include "render_config.h"
#include "vector_math.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>

struct Projection { Vec3 eye,forward,right,up; float pixels,cx,cy; };
static bool project(const Projection& p,Vec3 world,float* screen)
{
    Vec3 v=subtract(world,p.eye);float z=dot(v,p.forward);if (z<=.1f) return false;
    screen[0]=p.cx+dot(v,p.right)*p.pixels/z;screen[1]=p.cy-dot(v,p.up)*p.pixels/z;
    screen[2]=std::sqrt(dot(v,v));screen[3]=p.pixels/z;return true;
}
static void segment(EffectSegment* output,uint32_t* count,const Projection& p,Vec3 a,Vec3 b,float width,Color color,float alpha,int kind)
{
    if (*count>=MAX_EFFECT_SEGMENTS || (kind==0 && dot(subtract(a,b),subtract(a,b))<.000001f)) return;
    EffectSegment e={};if (!project(p,a,e.start) || !project(p,b,e.end)) return;
    e.start[3]=std::fmax(.65f,e.start[3]*width);e.end[3]=(float)kind;
    e.color[0]=color.r;e.color[1]=color.g;e.color[2]=color.b;e.color[3]=alpha;
    output[(*count)++]=e;
}
static bool tile_rectangle(const EffectSegment& e,uint32_t width,uint32_t height,int* r)
{
    float radius=e.start[3]+2;
    r[0]=(int)std::floor((std::fmin(e.start[0],e.end[0])-radius)/EFFECT_TILE);
    r[1]=(int)std::floor((std::fmin(e.start[1],e.end[1])-radius)/EFFECT_TILE);
    r[2]=(int)std::floor((std::fmax(e.start[0],e.end[0])+radius)/EFFECT_TILE);
    r[3]=(int)std::floor((std::fmax(e.start[1],e.end[1])+radius)/EFFECT_TILE);
    if (r[2]<0 || r[3]<0 || r[0]>=(int)width || r[1]>=(int)height) return false;
    r[0]=std::max(0,r[0]);r[1]=std::max(0,r[1]);r[2]=std::min((int)width-1,r[2]);r[3]=std::min((int)height-1,r[3]);return true;
}
bool effects_build(const Simulation* s,const Camera* camera,const RenderConfig* config,EffectSegment* output,uint32_t* count,uint32_t* tiles,uint32_t tile_count)
{
    *count=0;uint32_t width=(config->width+EFFECT_TILE-1)/EFFECT_TILE,height=(config->height+EFFECT_TILE-1)/EFFECT_TILE;
    if (tile_count!=width*height) return false;
    // Layout: prefix offsets, temporary counts/cursors, then compact indices.
    uint32_t* offsets=tiles;uint32_t* cursors=tiles+tile_count+1;uint32_t* indices=cursors+tile_count;
    memset(offsets,0,(tile_count*2+1)*sizeof(uint32_t));
    if (!s->world.combat.enabled) return true;
    Projection p={};p.eye=camera->position;p.forward=normalize(subtract(camera->target,camera->position));
    p.right=normalize(cross(p.forward,camera->up));p.up=cross(p.right,p.forward);
    p.cx=config->width*.5f;p.cy=config->height*.5f;p.pixels=config->height/(2*std::tan(config->vertical_fov_degrees*.00872664626f));
    const CombatVisuals* v=&s->world.combat;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* u=&s->units[i];const Trail* t=&s->trails[i];if (u->health<=0 || !simulation_unit_definition(s,u)->trails || !s->visible_units[i]) continue;
        Vec3 previous=u->position;
        for (uint32_t j=0;j<t->count;++j)
        {
            float fade=1-(float)j/TRAIL_POINTS;
            segment(output,count,p,previous,t->points[j],v->trail_width*fade,content_color(&s->world.content,u->faction),fade*.5f,0);
            previous=t->points[j];
        }
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* u=&s->units[i];if (u->health<=0 || !s->visible_units[i]) continue;
        Color color=content_color(&s->world.content,u->faction);
        if (u->selected)
            segment(output,count,p,u->position,u->position,simulation_unit_definition(s,u)->cue_radius,color,.45f,3);
    }
    for (uint32_t i=0;i<MAX_POPS;++i)
    {
        const Pop* pop=&s->pops[i];if (pop->duration<=0 || !simulation_in_sight(s,pop->position,0)) continue;float t=pop->age/pop->duration;
        float ease=1-(1-t)*(1-t);
        segment(output,count,p,pop->position,pop->position,v->pop_radius*(.2f+1.3f*ease),content_color(&s->world.content,pop->faction),(1-t)*(1-t),2);
        for (int j=0;j<7;++j)
        {
            float angle=j*6.2831853f/7+i*2.399963f;
            Vec3 direction=add(scale(p.right,std::cos(angle)),scale(p.up,std::sin(angle)));
            Vec3 a=add(pop->position,scale(direction,v->pop_radius*(.1f+ease*.8f)));
            Vec3 b=add(pop->position,scale(direction,v->pop_radius*(.2f+ease*1.7f)));
            segment(output,count,p,a,b,v->trail_width*.6f,content_color(&s->world.content,pop->faction),1-t,1);
        }
    }
    for (uint32_t i=0;i<MAX_POPS;++i)
    {
        const Impact* hit=&s->impacts[i];if (hit->duration<=0 || !simulation_in_sight(s,hit->position,0)) continue;
        float t=hit->age/hit->duration,radius=v->pop_radius*.55f*std::sqrt(t+.02f);
        for (int j=0;j<5;++j)
        {
            float angle=j*6.2831853f/5+i*2.399963f;
            Vec3 direction=add(scale(p.right,std::cos(angle)),scale(p.up,std::sin(angle)));
            segment(output,count,p,add(hit->position,scale(direction,radius*.4f)),add(hit->position,scale(direction,radius)),
                v->trail_width*.55f,v->flash,(1-t)*(1-t),1);
        }
    }
    for (uint32_t i=0;i<*count;++i)
    {
        int r[4];if (!tile_rectangle(output[i],width,height,r)) continue;
        for (int y=r[1];y<=r[3];++y) for (int x=r[0];x<=r[2];++x) ++cursors[y*width+x];
    }
    for (uint32_t i=0;i<tile_count;++i)
    { offsets[i+1]=offsets[i]+cursors[i];cursors[i]=offsets[i]; }
    if (offsets[tile_count]>MAX_EFFECT_REFERENCES) { fprintf(stderr,"Visible effects exceed tile-reference capacity.\n");return false; }
    for (uint32_t i=0;i<*count;++i)
    {
        int r[4];if (!tile_rectangle(output[i],width,height,r)) continue;
        for (int y=r[1];y<=r[3];++y) for (int x=r[0];x<=r[2];++x) indices[cursors[y*width+x]++]=i;
    }
    return true;
}
