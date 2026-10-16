#include "game.h"
#include "renderer.h"
#include "render_config.h"
#include "simulation.h"
#include "presentation.h"
#include <memory>
#include <new>
#include "orders.h"
#include "vector_math.h"

#include <stdio.h>
#include <string.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct Game
{
    Simulation simulation;
    Simulation initial_simulation;
    Renderer renderer;
    Camera camera;
    PointerInput pointer;
};

static double monotonic_seconds()
{
    LARGE_INTEGER count, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / frequency.QuadPart;
}

static void print_world(const Simulation* simulation, const char* path)
{
    const World* world=&simulation->world;
    printf("Loaded world '%s': %u bodies.\n",path,world->body_count);
    printf("Ambient diffuse RGB: %g %g %g.\n",world->ambient.r,world->ambient.g,world->ambient.b);
    const Content* content=&world->content;
    for (uint32_t i = 0; i < world->body_count; ++i)
    {
        if (!simulation_body_known(simulation,i)) continue;
        const Body* cell = &world->bodies[i];
        printf("body %s %s %g %g %g %g %g %g %g\n",content->bodies[cell->type].name, cell->owner ? content->factions[cell->owner].name : "neutral",
               cell->position.x, cell->position.y, cell->position.z, cell->radius,
               cell->color.r, cell->color.g, cell->color.b);
    }
    printf("Placements: %u\n",world->spawn_count);
    for (uint32_t i=0;i<world->spawn_count;++i)
    {
        const Spawn* spawn=&world->spawns[i];const UnitDefinition* definition=&content->units[spawn->type];
        if (world->sight_radius>0 && definition->faction!=world->player_faction) continue;
        printf("spawn %s %g %g %g scale %g yaw %g\n",definition->name,spawn->position.x,spawn->position.y,spawn->position.z,definition->scale,definition->yaw_degrees);
    }
}

static Vec3 pointer_ray(const Camera* camera, const RenderConfig* config, int x, int y)
{
    Vec3 forward=normalize(subtract(camera->target,camera->position));
    Vec3 right=normalize(cross(forward,camera->up)),up=cross(right,forward);
    float scale=std::tan(config->vertical_fov_degrees*.00872664626f);
    return normalize(add(forward,add(::scale(right,(2.0f*(x+.5f)/config->width-1)*scale*config->width/config->height),
        ::scale(up,(1-2.0f*(y+.5f)/config->height)*scale))));
}

static float pick_distance(Vec3 eye, Vec3 direction, Vec3 position, float radius)
{
    Vec3 offset=subtract(eye,position);float b=dot(offset,direction);
    float discriminant=b*b-dot(offset,offset)+radius*radius;
    return discriminant>=0 && -b+std::sqrt(discriminant)>0 ? std::fmax(0.0f,-b-std::sqrt(discriminant)) : 1e30f;
}

static void startup_camera(Game* game,const RenderConfig* config)
{
    camera_initialize(&game->camera,config);
    if (game->simulation.world.sight_radius<=0) return;
    const World* w=&game->simulation.world;
    for (uint32_t p=0;p<w->body_count;++p) if (game->simulation.home_owner[p]==w->player_faction)
    {
        Vec3 offset=subtract(game->camera.position,game->camera.target);
        game->camera.target=w->bodies[p].position;game->camera.position=add(game->camera.target,offset);break;
    }
}

static void gameplay_input(Game* game, const PointerInput* input, const RenderConfig* config)
{
    Simulation* s=&game->simulation;if (!s->world.gameplay.enabled) return;
    if (input->restart) { *s=game->initial_simulation;startup_camera(game,config); }
    if (input->pause) s->paused=!s->paused;
    if (input->group) simulation_select(s,input->group==3 ? -1 : (int)input->group-1);
    if (input->priority) simulation_priority(s,(int)input->priority-1);
    if (input->focus)
    {
        int p=s->selected_body;
        if (p<0) for (uint32_t j=0;j<s->world.body_count;++j)
            if (s->world.bodies[j].owner==s->world.player_faction) { p=(int)j;break; }
        if (p>=0)
        { Vec3 offset=subtract(game->camera.position,game->camera.target);game->camera.target=s->world.bodies[p].position;game->camera.position=add(game->camera.target,offset); }
    }
    if (!input->click && !input->command) return;
    if ((input->click || input->command) && (input->click ? input->click_y : input->command_y)>=(int)config->height-164)
    {
        int x=input->click ? input->click_x : input->command_x;
        if (x>=24 && x<604 && (input->click ? input->click_y : input->command_y)>=(int)config->height-144)
        {
            if (input->click && input->click_y>=(int)config->height-108 && input->click_y<(int)config->height-86)
                simulation_select(s,(input->click_x-24)/194==2 ? -1 : (input->click_x-24)/194);
            return;
        }
        if (x>=(int)config->width-464 && x<(int)config->width-24)
        {
            if (input->click && input->click_y>=(int)config->height-80 && input->click_y<(int)config->height-58)
                simulation_priority(s,input->click_x<(int)config->width-244 ? 0 : 1);
            return;
        }
    }
    int x=input->command ? input->command_x : input->click_x,y=input->command ? input->command_y : input->click_y;
    Vec3 ray=pointer_ray(&game->camera,config,x,y);float nearest=1e30f;int cell=-1,unit=-1;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        if (!simulation_body_known(s,p)) continue;
        float t=pick_distance(game->camera.position,ray,s->world.bodies[p].position,s->world.bodies[p].radius*1.08f);
        if (t<nearest) { nearest=t;cell=(int)p; }
    }
    if (input->command)
    {
        if (cell>=0) simulation_order(s,(uint32_t)cell);
        else if (s->world.sight_radius>0 && std::fabs(ray.z)>.0001f)
        {
            float plane=0;
            for (uint32_t p=0;p<s->world.body_count;++p)
                if (s->home_owner[p]==s->world.player_faction) { plane=s->world.bodies[p].position.z;break; }
            float distance=(plane-game->camera.position.z)/ray.z;
            if (distance>0) simulation_move_to(s,add(game->camera.position,scale(ray,distance)));
        }
        return;
    }
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].faction==s->world.player_faction)
        {
            float radius=std::fmax(simulation_unit_definition(s,&s->units[i])->pick_radius,simulation_unit_definition(s,&s->units[i])->scale);
            float t=pick_distance(game->camera.position,ray,s->units[i].position,radius);
            if (t<nearest) { nearest=t;unit=(int)i; }
        }
    if (unit>=0)
    {
        bool selected=s->units[unit].selected;
        if (!input->additive) for (uint32_t i=0;i<MAX_UNITS;++i) s->units[i].selected=false;
        s->units[unit].selected=input->additive ? !selected : true;
    }
    else s->selected_body=cell;
}

static void selection_marker(const Game* game,const RenderConfig* config,int cell,int32_t* marker)
{
    marker[3]=0;if (cell<0 || !simulation_body_known(&game->simulation,(uint32_t)cell)) return;
    const Body* body=&game->simulation.world.bodies[cell];
    Vec3 f=normalize(subtract(game->camera.target,game->camera.position)),right=normalize(cross(f,game->camera.up));
    Vec3 up=cross(right,f),offset=subtract(body->position,game->camera.position);float depth=dot(offset,f);
    if (depth<=0) return;
    float pixels=config->height/(2*depth*std::tan(config->vertical_fov_degrees*.00872664626f));
    marker[0]=(int)(config->width*.5f+dot(offset,right)*pixels);
    marker[1]=(int)(config->height*.5f-dot(offset,up)*pixels);
    marker[2]=(int)(body->radius*pixels*1.14f);marker[3]=1;
}

static void overlay_enabled(Game* game, const RenderConfig* config)
{
    Renderer* r=&game->renderer;const Simulation* s=&game->simulation;
    r->overlay_enabled=s->world.gameplay.enabled;if (!r->overlay_enabled) return;
    uint32_t groups[MAX_GROUPS]={},fleet=0,selected=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].faction==s->world.player_faction)
        { ++groups[simulation_unit_definition(s,&s->units[i])->group];++fleet;if (s->units[i].selected) ++selected; }
    snprintf(r->hud[0],96,"MICROSHIP / %s     FLEET %u     SELECTED %u",s->outcome ? (s->outcome>0 ? "VICTORY" : "DEFEAT") : s->paused ? "PAUSED" : s->world.content.factions[s->world.player_faction].label,fleet,selected);
    snprintf(r->hud[1],96,"1 %-10s %-5u 2 %-10s %-5u 3 ALL",s->world.content.groups[0].plural,groups[0],s->world.content.groups[1].plural,groups[1]);
    snprintf(r->hud[2],96,"%.55s",s->outcome ? (s->outcome>0 ? "ENEMY PRODUCTION SITES DESTROYED / R TO RESTART" : "COLONY LOST / R TO RESTART") : s->message);
    snprintf(r->hud[3],96,"RIGHT CLICK: ORDER   HOME: FOCUS   SPACE: PAUSE");
    snprintf(r->hud[4],96,"SELECT A BODY");
    snprintf(r->hud[5],96,"SCOUT TO FIND ENEMY PRODUCTION SITES");
    snprintf(r->hud[6],96,"CLEAR DEFENDERS / COLONIZE TO EXPAND");
    snprintf(r->hud[7],96,"F: %-15s S: %s",s->world.content.groups[0].plural,s->world.content.groups[1].plural);
    for (int j=0;j<4;++j) r->hud_status[j]=0;
    r->hud_status[0]=-1;
    Color accent=content_color(&s->world.content,s->world.player_faction);
    r->hud_accent[0]=accent.r;r->hud_accent[1]=accent.g;r->hud_accent[2]=accent.b;
    r->hud_feedback[0]=-1;r->hud_feedback[1]=s->paused ? 2.0f : (float)s->outcome;
    r->hud_feedback[2]=(float)s->time_seconds;
    int p=s->selected_body;selection_marker(game,config,p,r->selection_marker);
    if (p>=0 && simulation_body_known(s,(uint32_t)p) && s->visible_bodies[p])
    {
        const Colony* c=&s->colonies[p];
        bool ours=c->health>0 && c->faction==s->world.player_faction;
        r->hud_accent[3]=ours ? 1.0f : c->health>0 ? -1.0f : 0.0f;
        snprintf(r->hud[4],96,"%s %02d / %s",s->world.content.bodies[s->world.bodies[p].type].label,p+1,c->health>0 ? (ours ? "YOUR NODE" : "HOSTILE NODE") : "UNCLAIMED");
        if (c->building) snprintf(r->hud[4]+strlen(r->hud[4]),96-strlen(r->hud[4]),"  BUILD %03.0f%%",c->manufacturing*100);
        if (c->capacity_blocked) snprintf(r->hud[4],96,"UNIT STORAGE FULL / BUILD HELD");
        if (s->world.gameplay.enabled)
        {
            uint32_t structures=1;float production=1;
            for (uint32_t j=0;j<simulation_producer_definition(s,p)->branches.count-1;++j) if (c->branches[j].health>0)
            { ++structures;production+=simulation_producer_definition(s,p)->branches.production*c->branches[j].growth*c->branches[j].growth; }
            snprintf(r->hud[5],96,"STRUCTURES %u / %u   RATE %.2fX",c->health>0 ? structures : 0,simulation_producer_definition(s,p)->branches.count,production);
        }
        snprintf(r->hud[6],96,"%s %05.1f / %.0f",s->world.content.resource_label,c->energy,s->world.gameplay.storage);
        snprintf(r->hud[7],96,"F: %.16s %s  S: %.16s %s",s->world.content.groups[0].plural,s->world.content.units[c->priority].group==0 ? "*" : " ",s->world.content.groups[1].plural,s->world.content.units[c->priority].group==1 ? "*" : " ");
        if (!ours) snprintf(r->hud[7],96,"CLEAR DEFENDERS TO ESTABLISH A SITE");
        r->hud_status[0]=-1;r->hud_status[1]=c->growth;
        r->hud_status[2]=c->energy/s->world.gameplay.storage;r->hud_status[3]=c->building ? c->manufacturing : 0;
        if (s->order_body==p && s->order_count) r->hud_feedback[0]=(s->tick_count-s->order_tick)/60.0f;
    }
    else if (p>=0 && simulation_body_known(s,(uint32_t)p))
    { r->hud_accent[3]=0;snprintf(r->hud[4],96,"%s %02d / OUT OF SIGHT",s->world.content.bodies[s->world.bodies[p].type].label,p+1); }
    int hover=-1;float nearest=1e30f;
    if (game->pointer.pointer_valid)
    {
        Vec3 ray=pointer_ray(&game->camera,config,game->pointer.pointer_x,game->pointer.pointer_y);
        for (uint32_t j=0;j<s->world.body_count;++j)
        {
            if (!simulation_body_known(s,j)) continue;
            float d=pick_distance(game->camera.position,ray,s->world.bodies[j].position,s->world.bodies[j].radius*1.08f);
            if (d<nearest) { nearest=d;hover=(int)j; }
        }
    }
    selection_marker(game,config,hover==p ? -1 : hover,r->hover_marker);
}

int game_run(const GameOptions* options)
{
    Game game = {};
    std::unique_ptr<Presentation> presentation(new (std::nothrow) Presentation());
    if (!presentation) return 1;
    if (!simulation_initialize(&game.simulation, options->world_path))
        return 1;
    game.initial_simulation=game.simulation;
    print_world(&game.simulation, options->world_path);
    RenderConfig config;
    if (!render_config_load(&config, options->config_path))
        return 1;
    config.vsync=options->vsync;
    if (options->samples)
        config.samples_per_pixel = options->samples;
    if (options->seed_set)
        config.seed = options->seed;
    printf("Render configuration: direct ray tracing; %u film samples; %u transmission layers; %u volume rays; %u integration steps.\n",
        config.samples_per_pixel,config.max_transmission_layers,config.volume_rays_per_pixel,config.volume_steps);
    startup_camera(&game,&config);
    camera_apply_input(&game.camera, &options->camera_input, &config);
    if (!orders_run(&game.simulation,options->replay_path,options->ticks)) return 1;
    game.pointer=options->camera_input;
    gameplay_input(&game,&options->camera_input,&config);
    printf("Initial camera: %g %g %g; target: %g %g %g.\n",game.camera.position.x,game.camera.position.y,game.camera.position.z,game.camera.target.x,game.camera.target.y,game.camera.target.z);
    if (!presentation_initialize(presentation.get(),&game.simulation,&config)) return 1;
    if (options->smoke)
    {
        if (!options->ticks)
        { for (uint32_t i=0;i<3;++i) simulation_step(&game.simulation);if (game.simulation.tick_count!=3) return 1; }
        if (!presentation_build(presentation.get(),&game.simulation,&game.camera,&config)
            || !render_scene_validate(&presentation->scene)) return 1;
        if (options->state_path && !simulation_write_state(&game.simulation,options->state_path)) return 1;
        if (options->scene_state_path && !render_scene_write(&presentation->scene,options->scene_state_path)) return 1;
        printf("Application lifecycle smoke passed: assets, simulation and render scene; GPU rendering excluded.\n");
        return 0;
    }
    if (!presentation_build(presentation.get(),&game.simulation,&game.camera,&config)
        || !renderer_initialize(&game.renderer,&presentation->scene,&config,options->shader_path,
            options->builtin_shader_path,options->headless,options->debug_views,options->no_activate)) return 1;
    game.renderer.debug_stats = options->stats_set ? options->show_stats : !options->headless || options->debug_views;
    game.renderer.debug_views = options->debug_views;
    printf("Left-drag to pan; wheel to zoom. F1/F2 debug. Gameplay: 1 fighters, 2 bombers, 3 all; right-click to move or order; F/S factory production; Space pause; R restart.\n");
    const double tick_seconds = 1.0 / 60.0;
    double previous = monotonic_seconds();
    double accumulator = 0.0;
    double frame_total = 0.0;
    uint32_t frames = 0;
    bool success = true;
    PointerInput input = {};
    while ((!options->frames || frames < options->frames) && renderer_poll(&game.renderer, &input))
    {
        double start = monotonic_seconds();
        double elapsed = start - previous;
        previous = start;
        game.renderer.previous_frame_ms = frames ? elapsed*1000.0 : 0.0;
        if (input.toggle_stats) game.renderer.debug_stats = !game.renderer.debug_stats;
        if (input.toggle_buffers) game.renderer.debug_views = !game.renderer.debug_views;
        camera_apply_input(&game.camera,&input,&config);
        if (input.restart && game.simulation.world.gameplay.enabled) { accumulator=0;elapsed=0; }
        game.pointer=input;gameplay_input(&game,&input,&config);
        accumulator += elapsed < 0.25 ? elapsed : 0.25;
        if (options->headless) accumulator=0;
        while (accumulator >= tick_seconds)
        {
            simulation_step(&game.simulation);
            accumulator -= tick_seconds;
        }
        if ((options->headless && !options->ticks) || options->time_set)
            simulation_set_time(&game.simulation, options->time_seconds + frames * options->step_seconds);
        overlay_enabled(&game,&config);
        if (input.drag_x || input.drag_y || input.wheel)
            printf("Camera input: drag %g %g; wheel %g.\n", input.drag_x, input.drag_y, input.wheel);
        printf("Scene time: %.6f; camera: %g %g %g.\n",game.simulation.time_seconds,
            game.camera.position.x,game.camera.position.y,game.camera.position.z);
        bool final_frame = options->frames && frames + 1 == options->frames;
        if (!presentation_build(presentation.get(),&game.simulation,&game.camera,&config)
            || !renderer_render(&game.renderer,&presentation->scene,&game.camera,final_frame ? options->capture_path : 0))
        {
            success = false;
            break;
        }
        double frame_ms = (monotonic_seconds() - start) * 1000.0;
        game.renderer.work_frame_ms=frame_ms-game.renderer.acquire_ms-game.renderer.present_ms;
        frame_total += frame_ms;
        ++frames;
        printf("Application frame: %.3f ms.\n", frame_ms);
        printf("Application work: %.3f ms; explicit acquire/present waits: %.3f ms.\n",game.renderer.work_frame_ms,game.renderer.acquire_ms+game.renderer.present_ms);
    }
    if (!renderer_shutdown(&game.renderer)) success = false;
    if (options->state_path && !simulation_write_state(&game.simulation,options->state_path)) success=false;
    if (options->scene_state_path && !render_scene_write(&presentation->scene,options->scene_state_path)) success=false;
    if (frames)
        printf("Rendered %u frames: mean application %.3f ms (%.2f FPS).\n",
               frames, frame_total / frames, 1000.0 * frames / frame_total);
    return success ? 0 : 1;
}
