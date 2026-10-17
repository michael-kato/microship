#include "presentation.h"
#include "simulation.h"
#include "render_config.h"
#include "camera.h"
#include "effects.h"
#include "vector_math.h"
#include "math_constants.h"
#include "factory_shape.h"
#include <string.h>
#include <stdio.h>
#include <new>
#include <algorithm>

static const float MIN_VISIBLE_SPIKE_GROWTH = .001f;
static const float MIN_EMBEDDED_DIRECTION_SQUARED = .01f;
static const float EMBEDDED_SPIKE_LENGTH_SCALE = .45f;
static const float PROJECTILE_SHRINK_SECONDS = .06f;
static const float MIN_VISUAL_MOTION_SQUARED = .000001f;
static const float MIN_LEGACY_FACTORY_GROWTH = .02f;
static const double FULL_TURN_RADIANS = 6.283185307179586;
static const double BACKDROP_AXIS_PHASE_OFFSET = .7;
static const float SPAWN_SCALE_SECONDS = .25f;
static const float SPAWN_START_SCALE = .3f;
static const float SPAWN_GROWTH_SCALE = .7f;
static const float LANDING_SHRINK_SCALE = .8f;
static const float SPIKE_AXIS_PARALLEL_LIMIT = .95f;
static const float EMBEDDED_SPIKE_OUTWARD_SCALE = .7f;
static const float INJURY_TINT_STRENGTH = .95f;
static const float HIT_FLASH_TINT_STRENGTH = .9f;
static const float HIT_FLASH_EMISSION_STRENGTH = .6f;
static const float MIN_VISIBLE_STEM_EXTENSION = .0001f;
static const float FACTORY_AXIS_PARALLEL_LIMIT = .9f;
static const float FACTORY_FACTION_TINT_STRENGTH = .25f;
static const float BUD_START_SCALE = .08f;
static const float BUD_GROWTH_SCALE = .92f;

static void set_instance(Presentation* p,uint32_t slot,uint32_t mesh,Vec3 position,Vec3 x,Vec3 y,Vec3 z)
{
    RenderInstance* instance=&p->scene.instances[slot];instance->mesh=mesh;instance->mask=1;
    const float columns[3][3]={{x.x,x.y,x.z},{y.x,y.y,y.z},{z.x,z.y,z.z}};
    const float center[3]={position.x,position.y,position.z};
    for (uint32_t row=0;row<3;++row)
    {
        for (uint32_t column=0;column<3;++column) instance->transform[row][column]=columns[column][row];
        instance->transform[row][3]=center[row];
    }
}

bool presentation_initialize(Presentation* p,const Simulation* s,const RenderConfig* config)
{
    const World* w=&s->world;RenderScene* scene=&p->scene;
    std::vector<MeshSource> sources;
    try
    {
        sources.reserve(MAX_BODIES+MAX_UNIT_TYPES+MAX_PRODUCER_TYPES+2);
        for (uint32_t i=0;i<w->body_count;++i)
        {
            const Body* c=&w->bodies[i];p->body_meshes[i]=(uint32_t)sources.size();
            const BodyDefinition& d=w->content.bodies[c->type];
            sources.push_back(MeshSource{c->model_path,c->position,c->radius,0,c->color,d.parts_path,c->seed,d.shape_variation,d.part_variation});
        }
        for (uint32_t i=0;i<w->content.unit_count;++i)
        {
            const UnitDefinition* definition=&w->content.units[i];bool shared=false;
            for (uint32_t j=0;j<i;++j)
                if (!strcmp(definition->model_path,w->content.units[j].model_path)
                    && !memcmp(&definition->color,&w->content.units[j].color,sizeof(Color)))
                { p->unit_meshes[i]=p->unit_meshes[j];shared=true;break; }
            if (shared) continue;
            p->unit_meshes[i]=(uint32_t)sources.size();
            sources.push_back(MeshSource{definition->model_path,Vec3{},1,0,definition->color,0,0,0,0});
        }
        for (uint32_t i=0;i<w->content.producer_count;++i)
        {
            p->factory_meshes[i]=(uint32_t)sources.size();
            sources.push_back(MeshSource{w->content.producers[i].model_path,Vec3{},1,0,Color{1,1,1},0,0,0,0});
            const ProducerDefinition& d=w->content.producers[i];
            if (d.segment_path[0])
            {
                p->segment_meshes[i]=(uint32_t)sources.size();sources.push_back(MeshSource{d.segment_path,Vec3{},1,0,Color{1,1,1},0,0,0,0});
                p->tip_meshes[i]=(uint32_t)sources.size();sources.push_back(MeshSource{d.tip_path,Vec3{},1,0,Color{1,1,1},0,0,0,0});
            }
        }
        if (w->spikes.enabled)
        {
            p->spike_mesh=(uint32_t)sources.size();
            sources.push_back(MeshSource{w->spikes.model,Vec3{},1,0,Color{1,1,1},0,0,0,0});
        }
        if (w->backdrop.model_path[0])
        {
            p->background_mesh=(uint32_t)sources.size();const Background* b=&w->backdrop;
            sources.push_back(MeshSource{b->model_path,b->position,b->scale,0,b->color,0,0,0,0});
        }
        if (!geometry_build(&scene->geometry,sources.data(),(uint32_t)sources.size(),config)) return false;
        p->unit_base=0;p->body_base=w->gameplay.enabled ? (uint32_t)MAX_UNITS : w->spawn_count;
        p->factory_base=p->body_base+w->body_count;
        p->stem_base=p->factory_base+(w->gameplay.enabled ? w->body_count*MAX_FACTORY_STRUCTURES : 0);
        p->bud_base=p->stem_base+(w->gameplay.enabled ? w->body_count*MAX_FACTORY_STRUCTURES*GROWTH_SEGMENTS : 0);
        p->debris_base=p->bud_base+(w->gameplay.enabled ? w->body_count*MAX_FACTORY_STRUCTURES : 0);
        p->attached_base=p->debris_base+(w->gameplay.enabled ? MAX_LIMB_DEBRIS : 0);
        p->flying_base=p->attached_base+(w->spikes.enabled ? MAX_UNITS*w->spikes.count : 0);
        p->embedded_base=p->flying_base+(w->spikes.enabled ? MAX_PROJECTILES : 0);
        p->background_slot=p->embedded_base+(w->spikes.enabled ? MAX_EMBEDDED*(MAX_UNITS+MAX_BODIES*MAX_FACTORY_STRUCTURES) : 0);
        if (!presentation_prepare_membranes(p,s)) return false;
        scene->instances.resize(p->background_slot+1);
        scene->effects.resize(MAX_EFFECT_SEGMENTS);
        scene->tile_count=((config->width+EFFECT_TILE-1)/EFFECT_TILE)*((config->height+EFFECT_TILE-1)/EFFECT_TILE);
        scene->effect_tiles.resize(2*scene->tile_count+1+MAX_EFFECT_REFERENCES);
        for (uint32_t i=0;i<w->body_count;++i)
            if (w->bodies[i].interior_radius>0)
            { p->medium_bodies[scene->media.size()]=(int32_t)i;scene->media.push_back(RenderMedium{}); }
        if (w->atmosphere_enabled)
        { p->medium_bodies[scene->media.size()]=-1;scene->media.push_back(RenderMedium{}); }
    }
    catch (const std::bad_alloc&) { fprintf(stderr,"Cannot allocate game presentation storage.\n");return false; }
    scene->ambient=w->ambient;scene->ambient_top=w->ambient_top;scene->ambient_bottom=w->ambient_bottom;
    for (uint32_t i=0;i<3;++i) scene->lights[i]=RenderLight{w->studio[i].direction,w->studio[i].color,w->studio[i].irradiance};
    scene->lights_enabled=w->studio_mask!=0;scene->guides_required=w->combat.enabled;
    scene->thin_transmission=w->thin_transmission;scene->thin_absorption=w->thin_absorption;scene->thin_clarity=w->thin_clarity;
    scene->thin_edge_strength=w->thin_edge_strength;scene->thin_edge_power=w->thin_edge_power;
    scene->background_samples=w->backdrop_soft ? 0 : w->backdrop_samples;
    scene->background_triangles=w->backdrop_soft || !w->backdrop.model_path[0] ? 0 : scene->geometry.meshes[p->background_mesh].count;
    scene->background_blur=w->backdrop_soft ? w->backdrop_blur : 0;
    scene->background_glow=w->backdrop_glow;scene->background_ambient=w->backdrop_ambient;scene->background_wave=w->backdrop_wave;
    return true;
}

static float unit_visual_scale(const Simulation* s,const Unit* unit)
{
    float size=simulation_unit_definition(s,unit)->scale;
    if (!s->world.combat.flight_enabled) return size;
    float t=unit->hatched ? 1 : std::fmin(1.0f,unit->age/SPAWN_SCALE_SECONDS);t=t*t*(3-2*t);size*=SPAWN_START_SCALE+SPAWN_GROWTH_SCALE*t;
    if (unit->landing>0) size*=1-LANDING_SHRINK_SCALE*std::fmin(1.0f,unit->landing/s->world.gameplay.landing_seconds);
    return size;
}

static Vec3 posed_direction(Vec3 local,Vec3 x,Vec3 y,Vec3 z)
{ return add(add(scale(x,local.x),scale(y,local.y)),scale(z,local.z)); }
static uint32_t factory_instance_slot(const Presentation* state,uint32_t cell,uint32_t structure)
{ return state->factory_base+cell*MAX_FACTORY_STRUCTURES+structure; }
static void spike_instance(Presentation* state,uint32_t slot,Vec3 root,Vec3 direction,float length,float width,Color color)
{
    Vec3 axis=std::fabs(direction.y)<SPIKE_AXIS_PARALLEL_LIMIT ? Vec3{0,1,0} : Vec3{1,0,0};
    Vec3 x=normalize(cross(axis,direction)),y=cross(direction,x);
    set_instance(state,slot,state->spike_mesh,root,scale(x,width),scale(y,width),scale(direction,length));
    float* style=state->scene.instances[slot].style;style[0]=color.r;style[1]=color.g;style[2]=color.b;style[3]=1;
}
static void update_spike_instances(Presentation* state,const Simulation* simulation)
{
    const Simulation* s=simulation;const SpikeRules* r=&s->world.spikes;if (!r->enabled) return;
    state->attached_spikes=state->flying_spikes=state->embedded_spikes=0;
    uint32_t flying=state->flying_base,embedded=state->embedded_base;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* u=&s->units[i];if (u->health<=0 || !s->visible_units[i]) continue;
        float size=unit_visual_scale(s,u);Vec3 x,y,z;simulation_unit_axes(u,&x,&y,&z);
        Color color=u->flash>0 ? s->world.combat.flash : u->wounds ? r->injury[std::min(1u,u->wounds-1)] : content_color(&s->world.content,u->faction);
        if (simulation_unit_definition(s,u)->detaches)
            for (uint32_t j=0;j<r->count;++j)
            {
                float growth=u->spikes[j];if (growth<=MIN_VISIBLE_SPIKE_GROWTH) continue;
                Vec3 dir=posed_direction(simulation_spike_direction(j,r->count),x,y,z);
                spike_instance(state,state->attached_base+i*r->count+j,add(u->position,scale(dir,r->root*size)),dir,
                    size*r->length*growth,size*.5f*growth,color);++state->attached_spikes;
            }
        for (uint32_t j=0;j<u->wounds && j<MAX_EMBEDDED;++j)
        {
            if (dot(u->embedded[j].direction,u->embedded[j].direction)<MIN_EMBEDDED_DIRECTION_SQUARED) continue;
            Vec3 outward=posed_direction(u->embedded[j].direction,x,y,z);float length=std::fmax(r->scale,size*EMBEDDED_SPIKE_LENGTH_SCALE);
            Vec3 root=add(add(u->position,scale(posed_direction(u->embedded[j].anchor,x,y,z),size)),scale(outward,length*EMBEDDED_SPIKE_OUTWARD_SCALE));
            spike_instance(state,embedded+i*MAX_EMBEDDED+j,root,scale(outward,-1),length,length*.5f,content_color(&s->world.content,u->embedded[j].faction));++state->embedded_spikes;
        }
    }
    for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        const Projectile* p=&s->projectiles[i];if (!p->active || !simulation_in_sight(s,p->position,0)) continue;
        float blend=std::fmin(1.0f,p->age/PROJECTILE_SHRINK_SECONDS),length=p->launch_scale+(r->scale-p->launch_scale)*blend;
        spike_instance(state,flying+i,p->position,p->direction,length,length*.5f,content_color(&s->world.content,p->faction));++state->flying_spikes;
    }
    for (uint32_t i=0;i<s->world.body_count;++i)
    {
        const Colony* c=&s->colonies[i];if (!c->active || !s->visible_bodies[i]) continue;
        uint32_t count=s->colonies[i].structure_count;
        for (uint32_t k=0;k<count;++k)
        {
            const FactoryStructure* b=k ? &c->branches[k-1] : 0;
            if ((k ? b->health : c->health)<=0) continue;
            uint32_t wounds=k ? b->wounds : c->wounds;
            const EmbeddedSpike* spikes=k ? b->embedded : c->embedded;
            Vec3 tip=simulation_structure_position(s,i,k);
            for (uint32_t j=0;j<wounds && j<MAX_EMBEDDED;++j)
            {
                if (dot(spikes[j].direction,spikes[j].direction)<MIN_EMBEDDED_DIRECTION_SQUARED) continue;
                Vec3 outward=spikes[j].direction;float length=r->scale*2;
                spike_instance(state,embedded+MAX_UNITS*MAX_EMBEDDED+(i*MAX_FACTORY_STRUCTURES+k)*MAX_EMBEDDED+j,
                    add(add(tip,spikes[j].anchor),scale(outward,length*EMBEDDED_SPIKE_OUTWARD_SCALE)),scale(outward,-1),length,length*.5f,
                    content_color(&s->world.content,spikes[j].faction));++state->embedded_spikes;
            }
            if (wounds)
            {
                Color tint=r->injury[std::min(1u,wounds-1)];float* style=state->scene.instances[factory_instance_slot(state,i,k)].style;
                style[0]=tint.r;style[1]=tint.g;style[2]=tint.b;style[3]=INJURY_TINT_STRENGTH;
            }
        }
    }
}

static void update_gameplay_instances(Presentation* state,const Simulation* simulation)
{
    const Simulation* s=simulation;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* unit=&s->units[i];if (unit->health<=0 || !s->visible_units[i]) continue;
        const UnitDefinition* definition=simulation_unit_definition(s,unit);float yaw=definition->yaw_degrees*DEGREES_TO_RADIANS;
        if (unit->target>=0)
        {
            Vec3 delta=subtract(s->world.bodies[unit->target].position,unit->position);
            yaw=std::atan2(delta.x,delta.z);
        }
        if (s->world.combat.enabled)
        {
            Vec3 velocity=subtract(unit->position,s->trails[i].points[0]);
            if (dot(velocity,velocity)>MIN_VISUAL_MOTION_SQUARED) yaw=std::atan2(velocity.x,velocity.z);
            const CombatVisuals* fx=&s->world.combat;
            bool flash=unit->flash>0;float flash_strength=flash ? std::sqrt(unit->flash/fx->flash_seconds) : 0;Color tint=flash ? fx->flash : fx->injured;
            if (!flash && unit->wounds) tint=s->world.spikes.injury[std::min(1u,unit->wounds-1)];
            float* style=state->scene.instances[state->unit_base+i].style;style[0]=tint.r;style[1]=tint.g;style[2]=tint.b;
            style[3]=flash ? HIT_FLASH_TINT_STRENGTH*flash_strength : (unit->wounds ? INJURY_TINT_STRENGTH : 0);
            style[4]=flash ? HIT_FLASH_EMISSION_STRENGTH*flash_strength : 0;
        }
        if (s->world.combat.flight_enabled)
        {
            float size=unit_visual_scale(s,unit);Vec3 x,y,z;simulation_unit_axes(unit,&x,&y,&z);
            set_instance(state,state->unit_base+i,state->unit_meshes[unit->type],unit->position,scale(x,size),scale(y,size),scale(z,size));
            continue;
        }
        float c=std::cos(yaw)*definition->scale,v=std::sin(yaw)*definition->scale;
        set_instance(state,state->unit_base+i,state->unit_meshes[unit->type],unit->position,
            Vec3{c,0,-v},Vec3{0,definition->scale,0},Vec3{v,0,c});
    }
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        const Colony* colony=&s->colonies[p];if (!colony->active || !s->visible_bodies[p]) continue;

        if (s->world.gameplay.enabled)
        {
            const ProducerDefinition* definition=simulation_producer_definition(s,p);
            if (definition->segment_path[0])
            {
                Color color=content_color(&s->world.content,colony->faction);
                for (uint32_t k=0;k<colony->structure_count;++k)
                {
                    if ((k ? colony->branches[k-1].health : colony->health)<=0) continue;
                    float growth=simulation_structure_growth(s,p,k);
                    for (uint32_t j=0;j<GROWTH_SEGMENTS;++j)
                    {
                        float extension=std::fmax(0.0f,std::fmin(1.0f,growth*GROWTH_SEGMENTS-j));
                        if (extension<=MIN_VISIBLE_STEM_EXTENSION) continue;
                        Vec3 start=colony->stems[k][j],delta=scale(subtract(colony->stems[k][j+1],start),extension);
                        Vec3 y=normalize(delta),x=normalize(cross(std::fabs(y.z)<FACTORY_AXIS_PARALLEL_LIMIT ? Vec3{0,0,1} : Vec3{1,0,0},y)),z=cross(x,y);
                        float width=definition->width*(1-FACTORY_STEM_TAPER_PER_SEGMENT*j)*simulation_structure_scale(s,p,k)*(FACTORY_STEM_START_WIDTH_SCALE+FACTORY_STEM_GROWTH_WIDTH_SCALE*growth);
                        uint32_t slot=state->stem_base+(p*MAX_FACTORY_STRUCTURES+k)*GROWTH_SEGMENTS+j;
                        set_instance(state,slot,state->segment_meshes[colony->type],start,scale(x,width),delta,scale(z,width));
                        uint32_t wounds=k ? colony->branches[k-1].wounds : colony->wounds;
                        Color tint=wounds ? s->world.spikes.injury[std::min(1u,wounds-1)] : color;
                        float* style=state->scene.instances[slot].style;style[0]=tint.r;style[1]=tint.g;style[2]=tint.b;style[3]=wounds ? INJURY_TINT_STRENGTH : FACTORY_FACTION_TINT_STRENGTH;
                    }
                    float size=definition->width*(definition->branches.grammar==GROWTH_TREE ? simulation_structure_scale(s,p,k) : (colony->parents[k]>=0 ? FACTORY_CHILD_HEAD_WIDTH_SCALE : 1))*(FACTORY_HEAD_START_WIDTH_SCALE+FACTORY_HEAD_GROWTH_WIDTH_SCALE*growth);
                    uint32_t slot=factory_instance_slot(state,p,k);
                    Vec3 tip=simulation_structure_position(s,p,k),y=normalize(subtract(tip,colony->stems[k][0]));
                    Vec3 x=normalize(cross(std::fabs(y.z)<FACTORY_AXIS_PARALLEL_LIMIT ? Vec3{0,0,1} : Vec3{1,0,0},y)),z=cross(x,y);
                    if (growth>=(k ? colony->branches[k-1].growth : colony->growth) && (definition->branches.grammar!=GROWTH_TREE || simulation_structure_terminal(s,p,k)))
                        set_instance(state,slot,state->tip_meshes[colony->type],tip,scale(x,size),scale(y,size),scale(z,size));
                    float* style=state->scene.instances[slot].style;style[0]=color.r;style[1]=color.g;style[2]=color.b;style[3]=FACTORY_FACTION_TINT_STRENGTH;
                }
                for (uint32_t k=0;k<colony->structure_count;++k) if (colony->leaves[k].active)
                {
                    const LeafProduction& job=colony->leaves[k];
                    const UnitDefinition& u=s->world.content.units[job.type];float t=job.progress;
                    float size=u.scale*(BUD_START_SCALE+BUD_GROWTH_SCALE*t*t*(3-2*t));Vec3 tip=simulation_structure_position(s,p,k);
                    Vec3 z=normalize(subtract(tip,s->world.bodies[p].position));
                    Vec3 x=normalize(cross(std::fabs(z.y)<FACTORY_AXIS_PARALLEL_LIMIT ? Vec3{0,1,0} : Vec3{1,0,0},z)),y=cross(z,x);
                    set_instance(state,state->bud_base+p*MAX_FACTORY_STRUCTURES+k,state->unit_meshes[job.type],tip,scale(x,size),scale(y,size),scale(z,size));
                }
                continue;
            }
            Vec3 right,up,normal;simulation_producer_axes(simulation_producer_definition(s,p),&right,&up,&normal);
            for (uint32_t k=0;k<colony->structure_count;++k)
            {
                const FactoryStructure* b=k ? &colony->branches[k-1] : 0;if ((k ? b->health : colony->health)<=0) continue;
                float growth=std::fmax(MIN_LEGACY_FACTORY_GROWTH,k ? b->growth : colony->growth);
                Vec3 position=k ? add(colony->surface,b->offset) : colony->surface;
                float width=simulation_producer_definition(s,p)->width*growth;
                set_instance(state,factory_instance_slot(state,p,k),state->factory_meshes[colony->type],position,
                    scale(right,width),scale(up,simulation_producer_definition(s,p)->height*growth),scale(normal,width));
            }
            continue;
        }
    }
    for (uint32_t i=0;i<MAX_LIMB_DEBRIS;++i)
    {
        const LimbDebris& d=s->debris[i];if (d.seconds<=0 || !simulation_in_sight(s,d.start,0)) continue;
        float shrink=std::fmin(1.0f,(d.seconds-d.age)/s->world.combat.break_shrink);
        Vec3 delta=subtract(d.end,d.start),y=normalize(delta),x=normalize(cross(std::fabs(y.z)<FACTORY_AXIS_PARALLEL_LIMIT ? Vec3{0,0,1} : Vec3{1,0,0},y)),z=cross(x,y);
        uint32_t slot=state->debris_base+i;
        set_instance(state,slot,d.tip ? state->tip_meshes[d.producer] : state->segment_meshes[d.producer],d.start,
            scale(x,d.radius*shrink),d.tip ? scale(y,d.radius*shrink) : scale(delta,shrink),scale(z,d.radius*shrink));
        Color color=content_color(&s->world.content,d.faction);float* style=state->scene.instances[slot].style;
        style[0]=color.r;style[1]=color.g;style[2]=color.b;style[3]=FACTORY_FACTION_TINT_STRENGTH;
    }
    update_spike_instances(state,simulation);
}

static void add_sight(RenderScene* scene,Vec3 center,float radius)
{
    // Keep the same union of spheres. Remove only spheres fully contained in another.
    for (uint32_t i=0;i<scene->sight_count;)
    {
        const float* v=scene->sight[i].center_radius;
        Vec3 delta=subtract(center,Vec3{v[0],v[1],v[2]});float distance=std::sqrt(dot(delta,delta));
        if (distance+radius<=v[3]) return;
        if (distance+v[3]<=radius) scene->sight[i]=scene->sight[--scene->sight_count];
        else ++i;
    }
    RenderSight& out=scene->sight[scene->sight_count++];
    out.center_radius[0]=center.x;out.center_radius[1]=center.y;out.center_radius[2]=center.z;out.center_radius[3]=radius;
}

bool presentation_build(Presentation* p,const Simulation* s,const Camera* camera,const RenderConfig* config)
{
    RenderScene* scene=&p->scene;const World* w=&s->world;
    static_assert(MAX_RENDER_SIGHT>=MAX_UNITS+MAX_BODIES,"Sight source capacity");
    if (!presentation_update_membranes(p,s)) return false;
    scene->sight_count=0;scene->fog_blur=w->sight_radius>0 ? w->fog_blur : 0;
    scene->fog_tint=w->fog_tint;scene->fog_feather=w->fog_feather;scene->fog_plane_z=w->fog_plane_z;
    if (scene->fog_blur>0)
    {
        for (uint32_t i=0;i<w->body_count;++i)
            if (s->colonies[i].active && s->colonies[i].faction==w->player_faction)
                add_sight(scene,w->bodies[i].position,w->sight_radius+w->bodies[i].radius);
        for (uint32_t i=0;i<MAX_UNITS;++i)
            if (s->units[i].health>0 && s->units[i].faction==w->player_faction) add_sight(scene,s->units[i].position,w->sight_radius);
    }
    memset(scene->instances.data(),0,scene->instances.size()*sizeof(RenderInstance));
    for (uint32_t i=0;i<scene->instances.size();++i)
        for (uint32_t axis=0;axis<3;++axis) scene->instances[i].transform[axis][axis]=1;
    for (uint32_t i=0;i<w->body_count;++i)
        if (simulation_body_known(s,i) || scene->fog_blur>0)
        {
            set_instance(p,p->body_base+i,p->body_meshes[i],Vec3{},Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1});
            const CellAppearance& a=s->cell_appearance[i];float* style=scene->instances[p->body_base+i].style;
            style[0]=a.tint.r;style[1]=a.tint.g;style[2]=a.tint.b;style[3]=s->visible_bodies[i] ? a.strength : 0;
            float weight=0,roughness=0;
            if (s->visible_bodies[i]) for (uint32_t f=1;f<MAX_FACTIONS;++f)
                if (w->content.factions[f].cell_material)
                { weight+=a.factions[f];roughness+=a.factions[f]*w->content.factions[f].cell_roughness; }
            if (weight>0) { style[5]=roughness/weight;style[6]=std::fmin(1.0f,weight); }

        }
    for (uint32_t i=0;i<scene->media.size();++i)
    {
        RenderMedium* medium=&scene->media[i];*medium=RenderMedium();
        if (p->medium_bodies[i]<0)
        {
            medium->center_radius[0]=w->atmosphere_center.x;medium->center_radius[1]=w->atmosphere_center.y;medium->center_radius[2]=w->atmosphere_center.z;medium->center_radius[3]=w->atmosphere_radius;
            medium->scattering[0]=w->atmosphere_scattering.r;medium->scattering[1]=w->atmosphere_scattering.g;medium->scattering[2]=w->atmosphere_scattering.b;medium->scattering[3]=w->atmosphere_fade;
            medium->absorption_phase[0]=medium->absorption_phase[1]=medium->absorption_phase[2]=w->atmosphere_absorption;medium->absorption_phase[3]=w->atmosphere_phase;medium->lighting[0]=w->atmosphere_shadows ? 1.0f : 0.0f;
            continue;
        }
        const Body* c=&w->bodies[p->medium_bodies[i]];
        medium->center_radius[0]=c->position.x;medium->center_radius[1]=c->position.y;medium->center_radius[2]=c->position.z;
        medium->center_radius[3]=(simulation_body_known(s,p->medium_bodies[i]) || scene->fog_blur>0) ? c->radius*c->interior_radius : 0;
        medium->scattering[0]=c->interior_scattering.r;medium->scattering[1]=c->interior_scattering.g;medium->scattering[2]=c->interior_scattering.b;
        medium->scattering[3]=c->interior_fade;
        medium->absorption_phase[0]=medium->absorption_phase[1]=medium->absorption_phase[2]=c->interior_absorption;
        medium->blur[0]=c->interior_blur_rate;medium->blur[1]=c->interior_blur_sharp;medium->blur[2]=c->interior_blur_max;
        medium->absorption_phase[3]=c->interior_anisotropy;medium->lighting[0]=1;medium->lighting[1]=1;
    }
    if (w->gameplay.enabled) update_gameplay_instances(p,s);
    else
        for (uint32_t i=0;i<w->spawn_count;++i)
        {
            const Spawn* spawn=&w->spawns[i];const UnitDefinition* definition=&w->content.units[spawn->type];
            float c=std::cos(definition->yaw_degrees*DEGREES_TO_RADIANS)*definition->scale;
            float v=std::sin(definition->yaw_degrees*DEGREES_TO_RADIANS)*definition->scale;
            set_instance(p,p->unit_base+i,p->unit_meshes[spawn->type],spawn->position,Vec3{c,0,-v},Vec3{0,definition->scale,0},Vec3{v,0,c});
        }
    scene->time_seconds=s->time_seconds;
    if (w->backdrop.model_path[0])
    {
        double phase=s->time_seconds*FULL_TURN_RADIANS/(w->backdrop_soft ? w->backdrop_period : 1);
        scene->background_animation[0]=(float)std::sin(phase);scene->background_animation[1]=(float)std::cos(phase);
        scene->background_animation[2]=(float)std::fmod(phase,FULL_TURN_RADIANS);
        float pulse=w->backdrop_soft ? w->backdrop_pulse : 0;
        Vec3 stretch={1+pulse*(float)std::sin(phase),1+pulse*(float)std::sin(phase+BACKDROP_AXIS_PHASE_OFFSET),1},center=w->backdrop.position;
        set_instance(p,p->background_slot,p->background_mesh,Vec3{center.x*(1-stretch.x),center.y*(1-stretch.y),0},
            Vec3{stretch.x,0,0},Vec3{0,stretch.y,0},Vec3{0,0,1});
        scene->instances[p->background_slot].mask=w->backdrop_soft ? 4 : 1;
        scene->instances[p->background_slot].background=true;
        scene->instances[p->background_slot].style[7]=1;
    }
    render_scene_bounds(scene);
    return effects_build(s,camera,config,scene->effects.data(),&scene->effect_count,scene->effect_tiles.data(),scene->tile_count);
}
