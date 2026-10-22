#include "game.h"
#include "renderer.h"
#include "render_config.h"
#include "simulation.h"
#include "presentation.h"
#include <memory>
#include <new>
#include "orders.h"
#include "audio.h"
#include "vector_math.h"
#include "math_constants.h"
#include "player_settings.h"
#include "pause_menu.h"
#include "asset_limits.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <chrono>
#include <thread>
#endif
#include "../assets/shaders/hud_layout.glsl"

static const float PICK_MISS_DISTANCE = 1e30f;
static const float CELL_PICK_RADIUS_SCALE = 1.08f;
static const float MOVE_PLANE_DIRECTION_EPSILON = .0001f;
static const float SELECTION_MARKER_RADIUS_SCALE = 1.14f;
static const int PERCENT_SCALE = 100;
static const double MILLISECONDS_PER_SECOND = 1000.0;
static const double MAX_FRAME_CATCHUP_SECONDS = 0.25;
static const int SMOKE_SIMULATION_TICKS = 3;

// Positions of moving things at the previous simulation tick. Interactive frames
// draw between that tick and the current one, so motion advances on every frame
// even when rendering outpaces the fixed 60 Hz simulation. Only drawing uses it.
struct MotionHistory
{
    Vec3 units[MAX_UNITS],debris_start[MAX_LIMB_DEBRIS],debris_end[MAX_LIMB_DEBRIS];
    uint32_t unit_generation[MAX_UNITS];
    float debris_age[MAX_LIMB_DEBRIS];
};
// Current positions held aside while the scene is built from blended ones.
struct MotionRestore
{
    Vec3 units[MAX_UNITS],projectiles[MAX_PROJECTILES],debris_start[MAX_LIMB_DEBRIS],debris_end[MAX_LIMB_DEBRIS];
};

struct Game
{
    Simulation simulation;
    Simulation initial_simulation;
    Renderer renderer;
    Camera camera;
    PointerInput pointer;
    bool ratio_dragging;
    int ratio_body;
    bool box_dragging,box_moved;
    int box[4];
    MotionHistory motion;
    MotionRestore current_motion;
};

static void record_motion(MotionHistory* h,const Simulation* s)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
    { h->units[i]=s->units[i].position;h->unit_generation[i]=s->units[i].health>0 ? s->units[i].generation : 0; }
    for (uint32_t i=0;i<MAX_LIMB_DEBRIS;++i)
    { h->debris_start[i]=s->debris[i].start;h->debris_end[i]=s->debris[i].end;h->debris_age[i]=s->debris[i].seconds>0 ? s->debris[i].age : -1.0f; }
}

static Vec3 blend(Vec3 previous,Vec3 current,float alpha)
{ return add(previous,scale(subtract(current,previous),alpha)); }

// Draw alpha of the way from the previous tick to the current one. Entities that
// did not exist at the previous tick keep their current pose.
static void blend_motion(Simulation* s,const MotionHistory* h,float alpha,MotionRestore* saved)
{
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit& u=s->units[i];saved->units[i]=u.position;
        if (u.health>0 && h->unit_generation[i]==u.generation) u.position=blend(h->units[i],u.position,alpha);
    }
    for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        Projectile& p=s->projectiles[i];saved->projectiles[i]=p.position;
        if (p.active) p.position=blend(p.previous,p.position,alpha);
    }
    for (uint32_t i=0;i<MAX_LIMB_DEBRIS;++i)
    {
        LimbDebris& d=s->debris[i];saved->debris_start[i]=d.start;saved->debris_end[i]=d.end;
        if (d.seconds>0 && h->debris_age[i]>=0 && h->debris_age[i]<d.age)
        { d.start=blend(h->debris_start[i],d.start,alpha);d.end=blend(h->debris_end[i],d.end,alpha); }
    }
}

static void restore_motion(Simulation* s,const MotionRestore* saved)
{
    for (uint32_t i=0;i<MAX_UNITS;++i) s->units[i].position=saved->units[i];
    for (uint32_t i=0;i<MAX_PROJECTILES;++i) s->projectiles[i].position=saved->projectiles[i];
    for (uint32_t i=0;i<MAX_LIMB_DEBRIS;++i) { s->debris[i].start=saved->debris_start[i];s->debris[i].end=saved->debris_end[i]; }
}

static double monotonic_seconds()
{
    return profiler_clock_ms()/MILLISECONDS_PER_SECOND;
}

static void print_world(const Simulation* simulation, const char* path)
{
    const World* world=&simulation->world;
    printf("Loaded world '%s': %u bodies.\n",path,world->body_count);
    printf("Ambient diffuse RGB: %g %g %g.\n",world->ambient.r,world->ambient.g,world->ambient.b);
    if (world->backdrop.model_path[0]) printf("Backdrop: %s.\n",world->backdrop.model_path);
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
    float scale=std::tan(config->vertical_fov_degrees*HALF_DEGREES_TO_RADIANS);
    return normalize(add(forward,add(::scale(right,(2.0f*(x+.5f)/config->width-1)*scale*config->width/config->height),
        ::scale(up,(1-2.0f*(y+.5f)/config->height)*scale))));
}

static float pick_distance(Vec3 eye, Vec3 direction, Vec3 position, float radius)
{
    Vec3 offset=subtract(eye,position);float b=dot(offset,direction);
    float discriminant=b*b-dot(offset,offset)+radius*radius;
    return discriminant>=0 && -b+std::sqrt(discriminant)>0 ? std::fmax(0.0f,-b-std::sqrt(discriminant)) : PICK_MISS_DISTANCE;
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

static bool ratio_hit(const RenderConfig* config,int x,int y)
{
    return x>=(int)config->width-HUD_RATIO_LEFT_INSET && x<=(int)config->width-HUD_RATIO_RIGHT_INSET
        && y>=(int)config->height-HUD_RATIO_TOP && y<(int)config->height-HUD_RATIO_BOTTOM;
}

static void ratio_pointer(Game* game,PointerInput* input,const RenderConfig* config)
{
    Simulation* s=&game->simulation;
    if (input->restart) game->ratio_dragging=false;
    if (!input->pointer_cancelled && input->pointer_pressed && ratio_hit(config,input->press_x,input->press_y))
    { game->ratio_dragging=true;game->ratio_body=s->selected_body; }
    bool clicked=input->click && ratio_hit(config,input->click_x,input->click_y);
    if (!game->ratio_dragging && !clicked) return;
    input->drag_x=0;input->drag_y=0;
    int x=clicked ? input->click_x : input->pointer_x;
    if ((clicked || game->ratio_body==s->selected_body) && !input->pointer_cancelled)
    {
        int offset=x-((int)config->width-HUD_RATIO_LEFT_INSET);
        offset=offset<0 ? 0 : offset>HUD_RATIO_WIDTH ? HUD_RATIO_WIDTH : offset;
        uint32_t bombers=(uint32_t)((offset*PERCENT_SCALE+HUD_RATIO_WIDTH/2)/HUD_RATIO_WIDTH);
        simulation_production_ratio(s,PERCENT_SCALE-bombers);
    }
    input->click=false;
    if (input->pointer_released || input->pointer_cancelled || !input->pointer_held) game->ratio_dragging=false;
}

static void select_rectangle(Game* game,const RenderConfig* config,bool additive,int group,
    int left,int top,int right_edge,int bottom)
{
    Simulation* s=&game->simulation;
    if (s->world.content.factions[s->world.player_faction].automatic) return;
    Vec3 forward=normalize(subtract(game->camera.target,game->camera.position));
    Vec3 right=normalize(cross(forward,game->camera.up)),up=cross(right,forward);
    float focal=config->height/(2*std::tan(config->vertical_fov_degrees*HALF_DEGREES_TO_RADIANS));
    uint32_t count=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        Unit* u=&s->units[i];
        if (!additive) u->selected=false;
        if (u->health<=0 || u->faction!=s->world.player_faction || (group>=0 && (int)simulation_unit_definition(s,u)->group!=group)) continue;
        Vec3 offset=subtract(u->position,game->camera.position);float depth=dot(offset,forward);
        if (depth<=0) continue;
        float x=config->width*.5f+dot(offset,right)*focal/depth,y=config->height*.5f-dot(offset,up)*focal/depth;
        if (x>=left && x<=right_edge && y>=top && y<=bottom) { u->selected=true;u->path_marker_tick=s->tick_count;++count; }
    }
    if (group>=0) snprintf(s->message,sizeof(s->message),"SELECTED %u %s ON SCREEN",count,s->world.content.groups[group].plural);
}

static int alert_row_at(const RenderConfig*,int,int);
static bool notice_pointer_hit(const Game* game,const RenderConfig* config,int x,int y)
{
    int row=alert_row_at(config,x,y);
    if (row>=0 && communications_alert(&game->simulation,(uint32_t)row)>=0) return true;
    return communications_speaking(&game->simulation) && x>=(int)config->width-HUD_SITE_PANEL_LEFT_INSET
        && x<(int)config->width-HUD_PANEL_MARGIN && y>=(int)config->height-HUD_DIALOGUE_BOTTOM
        && y<(int)config->height-HUD_DIALOGUE_BOTTOM+HUD_DIALOGUE_HEIGHT;
}
static void selection_input(Game* game,PointerInput* input,const RenderConfig* config)
{
    if (!game->simulation.world.gameplay.enabled) return;
    if (input->pointer_pressed && notice_pointer_hit(game,config,input->press_x,input->press_y))
    { game->box_dragging=false;return; }
    if (input->restart || input->pointer_cancelled) { game->box_dragging=false;return; }
    if (input->group)
    { simulation_select(&game->simulation,input->group==3 ? -1 : (int)input->group-1);input->group=0; }
    if (input->pointer_pressed && input->press_x>=0 && input->press_x<(int)config->width
        && input->press_y>=0 && input->press_y<(int)config->height-HUD_BOTTOM_BAND_HEIGHT)
    {
        game->box_dragging=true;game->box_moved=false;
        game->box[0]=input->press_x;game->box[1]=input->press_y;
    }
    if (!game->box_dragging) return;
    game->box[2]=std::max(0,std::min((int)config->width-1,input->pointer_x));
    game->box[3]=std::max(0,std::min((int)config->height-HUD_BOTTOM_BAND_HEIGHT-1,input->pointer_y));
    game->box_moved=game->box_moved || input->pointer_moved;
    if (input->pointer_released)
    {
        if (game->box_moved)
        {
            select_rectangle(game,config,input->additive,-1,
                std::min(game->box[0],game->box[2]),std::min(game->box[1],game->box[3]),
                std::max(game->box[0],game->box[2]),std::max(game->box[1],game->box[3]));
            input->click=false;
        }
        game->box_dragging=false;
    }
}

static int alert_row_at(const RenderConfig* config,int x,int y)
{
    int left=(int)config->width-HUD_PANEL_MARGIN-HUD_ALERT_WIDTH;
    if (x<left || x>=(int)config->width-HUD_PANEL_MARGIN || y<HUD_PANEL_MARGIN) return -1;
    int row=(y-HUD_PANEL_MARGIN)/HUD_ALERT_STRIDE;
    return row<HUD_ALERT_ROWS && (y-HUD_PANEL_MARGIN)%HUD_ALERT_STRIDE<HUD_ALERT_HEIGHT ? row : -1;
}
static bool notice_input(Game* game,PointerInput* input,const RenderConfig* config)
{
    if (!input->click && !input->command) return false;
    int x=input->click ? input->click_x : input->command_x,y=input->click ? input->click_y : input->command_y;
    int row=alert_row_at(config,x,y);Simulation* s=&game->simulation;
    int cell=row>=0 ? communications_alert(s,(uint32_t)row) : -1;
    if (cell>=0)
    {
        if (input->click && communications_acknowledge(s,(uint32_t)cell)
            && x<(int)config->width-HUD_PANEL_MARGIN-28)
        {
            Vec3 offset=subtract(game->camera.position,game->camera.target);float distance=std::sqrt(dot(offset,offset));
            float focus_distance=s->world.communications.focus_distance;
            if (distance>focus_distance) offset=scale(offset,focus_distance/distance);
            game->camera.target=s->world.bodies[cell].position;game->camera.position=add(game->camera.target,offset);
            s->selected_body=cell;
        }
        input->click=false;input->command=false;game->box_dragging=false;return true;
    }
    if (communications_speaking(s) && x>=(int)config->width-HUD_SITE_PANEL_LEFT_INSET && x<(int)config->width-HUD_PANEL_MARGIN
        && y>=(int)config->height-HUD_DIALOGUE_BOTTOM && y<(int)config->height-HUD_DIALOGUE_BOTTOM+HUD_DIALOGUE_HEIGHT)
    { input->click=false;input->command=false;game->box_dragging=false;return true; }
    return false;
}
static void notice_overlay(Game* game)
{
    Renderer* r=&game->renderer;const Simulation* s=&game->simulation;r->notice_mask=0;
    for (uint32_t line=HUD_NOTICE_FIRST_LINE;line<HUD_TEXT_LINES;++line) r->hud[line][0]=0;
    const DialogueEvent* e=communications_speaking(s);
    if (e)
    {
        r->notice_mask|=1;snprintf(r->hud[HUD_NOTICE_FIRST_LINE],HUD_LINE_CAPACITY,"%s / TRANSMISSION",s->world.content.factions[e->faction].label);
        const char* cursor=communications_line(s,*e);
        for (uint32_t row=1;row<=3 && *cursor;++row)
        {
            size_t length=std::min((size_t)40,strlen(cursor));
            if (cursor[length]) { while (length && cursor[length]!=' ') --length;if (!length) length=40; }
            snprintf(r->hud[HUD_NOTICE_FIRST_LINE+row],HUD_LINE_CAPACITY,"%.*s",(int)length,cursor);cursor+=length;while (*cursor==' ') ++cursor;
        }
        if (!r->hud[HUD_NOTICE_FIRST_LINE+3][0]) snprintf(r->hud[HUD_NOTICE_FIRST_LINE+3],HUD_LINE_CAPACITY,e->trigger==DIALOGUE_TERRITORY ? "CONTROLS %u CELLS" : e->trigger==DIALOGUE_SHIPS ? "FIRST SHIP CONTACT" : "FIRST CELL CONTACT",e->cells);
    }
    uint32_t pending=0;for (uint32_t p=0;p<s->world.body_count;++p) pending+=s->communications.alerts[p].pending;
    for (uint32_t row=0;row<HUD_ALERT_ROWS;++row)
    {
        int cell=communications_alert(s,row);if (cell<0) break;
        const TerritoryAlert& a=s->communications.alerts[cell];uint32_t faction=1;
        while (faction+1<MAX_FACTIONS && !(a.factions&(1u<<faction))) ++faction;
        uint32_t line=HUD_ALERT_FIRST_LINE+row*3;r->notice_mask|=2u<<row;
        snprintf(r->hud[line],HUD_LINE_CAPACITY,"HOSTILE CONTACT / %u %s",pending,pending==1 ? "ALERT" : "ALERTS");
        snprintf(r->hud[line+1],HUD_LINE_CAPACITY,"CELL %03d / %.16s",cell+1,s->world.content.factions[faction].label);
        snprintf(r->hud[line+2],HUD_LINE_CAPACITY,"CLICK TO FOCUS / %uS AGO",(s->tick_count-a.tick)/SIMULATION_TICKS_PER_SECOND);
    }
}

static void gameplay_input(Game* game, PointerInput* input, const RenderConfig* config)
{
    Simulation* s=&game->simulation;if (!s->world.gameplay.enabled) return;
    if (input->restart) { *s=game->initial_simulation;game->ratio_dragging=false;startup_camera(game,config); }
    if (input->pause) s->paused=!s->paused;
    if (notice_input(game,input,config)) return;
    if (input->group) simulation_select(s,input->group==3 ? -1 : (int)input->group-1);
    if (input->focus)
    {
        int p=s->selected_body;
        if (p<0) for (uint32_t j=0;j<s->world.body_count;++j)
            if (s->home_owner[j]==s->world.player_faction) { p=(int)j;break; }
        if (p>=0)
        { Vec3 offset=subtract(game->camera.position,game->camera.target);game->camera.target=s->world.bodies[p].position;game->camera.position=add(game->camera.target,offset); }
    }
    if (input->screen_group)
        select_rectangle(game,config,false,(int)input->screen_group-1,
            0,0,(int)config->width-1,(int)config->height-HUD_BOTTOM_BAND_HEIGHT-1);
    if (!input->click && !input->command) return;
    if ((input->click || input->command) && (input->click ? input->click_y : input->command_y)>=(int)config->height-HUD_BOTTOM_BAND_HEIGHT)
    {
        int x=input->click ? input->click_x : input->command_x;
        if (x>=HUD_PANEL_MARGIN && x<HUD_FLEET_PANEL_RIGHT && (input->click ? input->click_y : input->command_y)>=(int)config->height-HUD_PANEL_HEIGHT)
        {
            if (input->click && input->click_y>=(int)config->height-HUD_GROUP_BUTTON_TOP && input->click_y<(int)config->height-HUD_GROUP_BUTTON_BOTTOM)
                simulation_select(s,(input->click_x-HUD_PANEL_MARGIN)/HUD_GROUP_BUTTON_WIDTH==2 ? -1 : (input->click_x-HUD_PANEL_MARGIN)/HUD_GROUP_BUTTON_WIDTH);
            return;
        }
        if (x>=(int)config->width-HUD_SITE_PANEL_LEFT_INSET && x<(int)config->width-HUD_PANEL_MARGIN)
        {
            return;
        }
    }
    int x=input->command ? input->command_x : input->click_x,y=input->command ? input->command_y : input->click_y;
    Vec3 ray=pointer_ray(&game->camera,config,x,y);float nearest=PICK_MISS_DISTANCE;int cell=-1,unit=-1;
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        if (!simulation_body_known(s,p)) continue;
        float t=pick_distance(game->camera.position,ray,s->world.bodies[p].position,s->world.bodies[p].radius*CELL_PICK_RADIUS_SCALE);
        if (t<nearest) { nearest=t;cell=(int)p; }
    }
    if (input->command)
    {
        if (cell>=0) simulation_order(s,(uint32_t)cell);
        else if (s->world.sight_radius>0 && std::fabs(ray.z)>MOVE_PLANE_DIRECTION_EPSILON)
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
        if (s->units[i].health>0 && s->units[i].faction==s->world.player_faction && !s->world.content.factions[s->units[i].faction].automatic)
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
        if (s->units[unit].selected) s->units[unit].path_marker_tick=s->tick_count;
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
    float pixels=config->height/(2*depth*std::tan(config->vertical_fov_degrees*HALF_DEGREES_TO_RADIANS));
    marker[0]=(int)(config->width*.5f+dot(offset,right)*pixels);
    marker[1]=(int)(config->height*.5f-dot(offset,up)*pixels);
    marker[2]=(int)(body->radius*pixels*SELECTION_MARKER_RADIUS_SCALE);marker[3]=1;
}

static void overlay_enabled(Game* game, const RenderConfig* config)
{
    Renderer* r=&game->renderer;const Simulation* s=&game->simulation;
    r->overlay_enabled=s->world.gameplay.enabled;if (!r->overlay_enabled) return;
    notice_overlay(game);
    uint32_t groups[MAX_GROUPS]={},fleet=0,selected=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
        if (s->units[i].health>0 && s->units[i].faction==s->world.player_faction)
        { ++groups[simulation_unit_definition(s,&s->units[i])->group];++fleet;if (s->units[i].selected) ++selected; }
    snprintf(r->hud[HUD_LINE_TITLE],HUD_LINE_CAPACITY,"NANOVA / %s     FLEET %u     SELECTED %u",s->outcome ? (s->outcome>0 ? "VICTORY" : "DEFEAT") : s->paused ? "PAUSED" : s->world.content.factions[s->world.player_faction].label,fleet,selected);
    snprintf(r->hud[HUD_LINE_GROUPS],HUD_LINE_CAPACITY,"1 %-10s %-5u 2 %-10s %-5u 3 ALL",s->world.content.groups[0].plural,groups[0],s->world.content.groups[1].plural,groups[1]);
    snprintf(r->hud[HUD_LINE_MESSAGE],HUD_LINE_CAPACITY,"%.55s",s->outcome ? (s->outcome>0 ? "YOU WON / KEEP PLAYING / R TO RESTART" : "COLONY LOST / R TO RESTART") : s->message);
    snprintf(r->hud[HUD_LINE_HELP],HUD_LINE_CAPACITY,"F/B: ON SCREEN   RIGHT CLICK: ORDER   ESC: MENU");
    snprintf(r->hud[HUD_LINE_SITE],HUD_LINE_CAPACITY,"SELECT A BODY");
    snprintf(r->hud[HUD_LINE_STRUCTURES],HUD_LINE_CAPACITY,"SCOUT TO FIND ENEMY PRODUCTION SITES");
    snprintf(r->hud[HUD_LINE_RESOURCE],HUD_LINE_CAPACITY,"CLEAR DEFENDERS / COLONIZE TO EXPAND");
    snprintf(r->hud[HUD_LINE_PRIORITY],HUD_LINE_CAPACITY,"SELECT YOUR SITE TO SET THE UNIT MIX");
    if (s->world.content.factions[s->world.player_faction].automatic)
    {
        snprintf(r->hud[HUD_LINE_TITLE],HUD_LINE_CAPACITY,"NANOVA / %s     SPORES %u",s->outcome ? (s->outcome>0 ? "VICTORY" : "DEFEAT") : s->paused ? "PAUSED" : s->world.content.factions[s->world.player_faction].label,fleet);
        snprintf(r->hud[HUD_LINE_GROUPS],HUD_LINE_CAPACITY,"AUTOMATIC EMISSION / NO UNIT ORDERS");
        snprintf(r->hud[HUD_LINE_HELP],HUD_LINE_CAPACITY,"HOME: FOCUS   ESC: MENU   R: RESTART");
    }
    for (int j=0;j<4;++j) r->hud_status[j]=0;
    r->hud_status[0]=-1;
    Color accent=content_color(&s->world.content,s->world.player_faction);
    r->hud_accent[0]=accent.r;r->hud_accent[1]=accent.g;r->hud_accent[2]=accent.b;
    for (uint32_t j=0;j<4;++j) r->selection_box[j]=-1;
    if (game->box_dragging && game->box_moved)
    {
        r->selection_box[0]=std::min(game->box[0],game->box[2]);r->selection_box[1]=std::min(game->box[1],game->box[3]);
        r->selection_box[2]=std::max(game->box[0],game->box[2]);r->selection_box[3]=std::max(game->box[1],game->box[3]);
    }
    r->hud_feedback[0]=-1;r->hud_feedback[1]=s->paused ? 2.0f : (float)s->outcome;
    r->hud_feedback[2]=(float)s->time_seconds;r->hud_feedback[3]=-1;
    int p=s->selected_body;selection_marker(game,config,p,r->selection_marker);
    if (p>=0 && simulation_body_known(s,(uint32_t)p) && s->visible_bodies[p])
    {
        const Colony* c=&s->colonies[p];
        bool ours=c->active && c->faction==s->world.player_faction;
        r->hud_accent[3]=ours ? 1.0f : c->active ? -1.0f : 0.0f;
        snprintf(r->hud[HUD_LINE_SITE],HUD_LINE_CAPACITY,"%s %02d / %s",s->world.content.bodies[s->world.bodies[p].type].label,p+1,c->active ? (ours ? "YOUR NODE" : "HOSTILE NODE") : "UNCLAIMED");
        if (c->building) snprintf(r->hud[HUD_LINE_SITE]+strlen(r->hud[HUD_LINE_SITE]),HUD_LINE_CAPACITY-strlen(r->hud[HUD_LINE_SITE]),"  %s %03.0f%%",s->world.content.factions[c->faction].automatic ? "SPORE" : "BUILD",c->manufacturing*PERCENT_SCALE);
        if (c->capacity_blocked) snprintf(r->hud[HUD_LINE_SITE],HUD_LINE_CAPACITY,"UNIT STORAGE FULL / BUILD HELD");
        if (s->world.gameplay.enabled)
        {
            uint32_t structures=c->health>0 ? 1 : 0;float production=simulation_production(s,p);
            for (uint32_t j=1;j<c->structure_count;++j) if (c->branches[j-1].health>0)
            { ++structures; }
            snprintf(r->hud[HUD_LINE_STRUCTURES],HUD_LINE_CAPACITY,"STRUCTURES %u / %u   RATE %.2fX",c->active ? structures : 0,c->active ? c->structure_count : simulation_producer_definition(s,p)->branches.count,production);
        }
        snprintf(r->hud[HUD_LINE_RESOURCE],HUD_LINE_CAPACITY,"%s %05.1f / %.0f",s->world.content.resource_label,c->energy,s->world.gameplay.storage);
        if (simulation_ratio_available(s,(uint32_t)p))
        {
            snprintf(r->hud[HUD_LINE_PRIORITY],HUD_LINE_CAPACITY,"%.10s %3u%%   %.10s %3u%%",s->world.content.groups[0].plural,c->fighter_percent,s->world.content.groups[1].plural,100-c->fighter_percent);
            r->hud_feedback[3]=(100-c->fighter_percent)/(float)PERCENT_SCALE;
        }
        if (s->world.content.factions[c->faction].automatic)
        {
            snprintf(r->hud[HUD_LINE_STRUCTURES],HUD_LINE_CAPACITY,"AUTOMATIC SEEKING SPORES");
            snprintf(r->hud[HUD_LINE_RESOURCE],HUD_LINE_CAPACITY,"SPORES CAN BE SHOT DOWN");
            snprintf(r->hud[HUD_LINE_PRIORITY],HUD_LINE_CAPACITY,"BLASTS ALSO DAMAGE NEARBY FRIENDLIES");
        }
        if (!ours)
        {
            const FactionDefinition& faction=s->world.content.factions[s->world.player_faction];
            const UnitDefinition& colonizer=s->world.content.units[faction.default_unit];
            snprintf(r->hud[HUD_LINE_PRIORITY],HUD_LINE_CAPACITY,"CAPTURE COST: %u %.16s",colonizer.colonization_units,s->world.content.groups[colonizer.group].plural);
            const UnitDefinition& bomber=s->world.content.units[faction.assault_unit];
            if (bomber.colonizes && !bomber.consume_colonists) snprintf(r->hud[HUD_LINE_RESOURCE],HUD_LINE_CAPACITY,"OR ONE BOMBER / BOMBER IS RETAINED");
        }
        r->hud_status[0]=-1;r->hud_status[1]=c->growth;
        r->hud_status[2]=c->energy/s->world.gameplay.storage;r->hud_status[3]=c->building ? c->manufacturing : 0;
        if (s->order_body==p && s->order_count) r->hud_feedback[0]=(s->tick_count-s->order_tick)/SIMULATION_TICKS_PER_SECOND_FLOAT;
    }
    else if (p>=0 && simulation_body_known(s,(uint32_t)p))
    { r->hud_accent[3]=0;snprintf(r->hud[HUD_LINE_SITE],HUD_LINE_CAPACITY,"%s %02d / OUT OF SIGHT",s->world.content.bodies[s->world.bodies[p].type].label,p+1); }
    int hover=-1;float nearest=PICK_MISS_DISTANCE;
    if (game->pointer.pointer_valid)
    {
        Vec3 ray=pointer_ray(&game->camera,config,game->pointer.pointer_x,game->pointer.pointer_y);
        for (uint32_t j=0;j<s->world.body_count;++j)
        {
            if (!simulation_body_known(s,j)) continue;
            float d=pick_distance(game->camera.position,ray,s->world.bodies[j].position,s->world.bodies[j].radius*CELL_PICK_RADIUS_SCALE);
            if (d<nearest) { nearest=d;hover=(int)j; }
        }
    }
    selection_marker(game,config,hover==p ? -1 : hover,r->hover_marker);
}

// Plays seeded matches back to back without audio or rendering and prints one summary line each.
static int run_matches(const GameOptions* options)
{
    std::unique_ptr<Simulation> simulation(new (std::nothrow) Simulation());if (!simulation) return 1;
    std::unique_ptr<double[]> timings;
    if (options->cpu_benchmark)
    {
        timings.reset(new (std::nothrow) double[options->ticks]);
        if (!timings) { fprintf(stderr,"Cannot allocate tick timing records.\n");return 1; }
    }
    for (uint32_t i=0;i<options->matches;++i)
    {
        uint32_t seed=options->world_seed+i;
        double setup_start=options->cpu_benchmark ? profiler_clock_ms() : 0;
        if (!simulation_initialize(simulation.get(),options->world_path,seed,options->player_faction)) return 1;
        double setup_ms=options->cpu_benchmark ? profiler_clock_ms()-setup_start : 0;
        uint32_t count=0,peak_units=0,worst_tick=0;double total_ms=0,maximum_ms=0;
        SimulationTimings phase_total={},worst_phases={};
        while (!simulation->outcome && simulation->tick_count<options->ticks)
        {
            double start=options->cpu_benchmark ? profiler_clock_ms() : 0;
            SimulationTimings phases;
            simulation_step(simulation.get(),options->cpu_benchmark ? &phases : 0);
            if (options->cpu_benchmark)
            {
                double elapsed=profiler_clock_ms()-start;timings[count++]=elapsed;total_ms+=elapsed;
                for (uint32_t p=0;p<SIM_PHASE_COUNT;++p) phase_total.ms[p]+=phases.ms[p];
                if (elapsed>maximum_ms) { maximum_ms=elapsed;worst_tick=simulation->tick_count;worst_phases=phases; }
                uint32_t live=0;for (uint32_t u=0;u<MAX_UNITS;++u) live+=simulation->units[u].health>0;
                peak_units=std::max(peak_units,live);
            }
        }
        simulation_write_summary(simulation.get(),seed,stdout);fflush(stdout);
        if (options->cpu_benchmark && count)
        {
            std::sort(timings.get(),timings.get()+count);
            // Nearest-rank percentiles, with integer arithmetic and one-based ranks.
            fprintf(stdout,"{\"cpu_benchmark\":true,\"seed\":%u,\"ticks\":%u,\"setup_ms\":%.6f,\"total_tick_ms\":%.6f,\"mean_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f,\"max_ms\":%.6f,\"peak_units\":%u}\n",
                seed,count,setup_ms,total_ms,total_ms/count,timings[((uint64_t)count*50+99)/100-1],
                timings[((uint64_t)count*95+99)/100-1],timings[((uint64_t)count*99+99)/100-1],timings[count-1],peak_units);
            static const char* phases[]={"ai","visuals","recovery","movement","combat","growth","sight","appearance","fog"};
            fprintf(stdout,"{\"cpu_phases\":true,\"seed\":%u,\"worst_tick\":%u,\"mean_ms\":{",seed,worst_tick);
            for (uint32_t p=0;p<SIM_PHASE_COUNT;++p) fprintf(stdout,"%s\"%s\":%.6f",p ? "," : "",phases[p],phase_total.ms[p]/count);
            fprintf(stdout,"},\"worst_tick_ms\":{");
            for (uint32_t p=0;p<SIM_PHASE_COUNT;++p) fprintf(stdout,"%s\"%s\":%.6f",p ? "," : "",phases[p],worst_phases.ms[p]);
            fprintf(stdout,"}}\n");fflush(stdout);
        }
    }
    return 0;
}

static void apply_camera_preferences(PointerInput* input,const PlayerSettings* settings)
{
    input->drag_x*=settings->drag_sensitivity;input->drag_y*=settings->drag_sensitivity;
    input->wheel*=settings->zoom_sensitivity*(settings->invert_zoom ? -1.0f : 1.0f);
}

static RenderConfig settings_config(const PlayerSettings* settings)
{
    RenderConfig config=settings->graphics;config.borderless=settings->borderless;
#ifdef _WIN32
    if (settings->borderless)
    {
        DEVMODEA mode={};mode.dmSize=sizeof(mode);
        bool found=EnumDisplaySettingsA(0,ENUM_CURRENT_SETTINGS,&mode)!=0;
        config.width=found ? mode.dmPelsWidth : 0;config.height=found ? mode.dmPelsHeight : 0;
    }
#endif
    return config;
}

static void renderer_preferences(Renderer* renderer,const PlayerSettings* settings,const GameOptions* options,bool debug_views)
{
    renderer->debug_stats=settings->show_stats;renderer->debug_views=debug_views;
    renderer->frame_log=options->frames!=0;
    renderer->disabled_features=player_settings_disabled_features(settings);
}

static bool replace_renderer(Game* game,Presentation* presentation,const RenderConfig* config,
    const PlayerSettings* settings,const GameOptions* options,bool debug_views)
{
    if (!renderer_shutdown(&game->renderer)) return false;
    if (!presentation_build(presentation,&game->simulation,&game->camera,config)
        || !renderer_initialize(&game->renderer,&presentation->scene,config,options->shader_path,
            options->builtin_shader_path,options->headless,debug_views,options->no_activate)) return false;
    renderer_preferences(&game->renderer,settings,options,debug_views);return true;
}

static bool graphics_changed(const PlayerSettings* a,const PlayerSettings* b)
{
    if (a->borderless!=b->borderless) return true;
    size_t count;const SettingField* fields=player_setting_fields(&count);
    for (size_t i=0;i<count;++i)
        if (fields[i].offset<sizeof(RenderConfig) && player_setting_value(a,fields[i])!=player_setting_value(b,fields[i])) return true;
    return false;
}

// The simulation, camera and audio stay in this session while graphics resources
// can be rebuilt at this boundary. No elapsed menu time reaches the tick clock.
static PauseAction pause_session(Game* game,Presentation* presentation,Audio* audio,RenderConfig* config,
    PlayerSettings* settings,const PlayerSettings* defaults,const char* settings_path,const GameOptions* options,bool rendered)
{
    uint32_t tick=game->simulation.tick_count;double time=game->simulation.time_seconds;
    bool previously_paused=game->simulation.paused;game->simulation.paused=true;
    audio_set_volume(audio,0,0);
    if (rendered)
    {
        uint32_t mask=game->renderer.disabled_features;
        settings->subsurface=!(mask&RENDER_SSS);settings->volume=!(mask&RENDER_VOLUME);
        settings->cell_blur=!(mask&RENDER_CELL_BLUR);settings->tilt_shift=!(mask&RENDER_TILT_SHIFT);
        settings->fog_blur=!(mask&RENDER_FOG_BLUR);settings->glow=!(mask&RENDER_GLOW);
        settings->show_stats=game->renderer.debug_stats;
    }
    PlayerSettings draft=*settings;int page=-1;char status[256]={};PauseAction action=PAUSE_ERROR;
    for (;;)
    {
        if (rendered)
        {
            // Present the frozen scene before opening the modal menu, including
            // after Apply or rollback replaces the renderer and its swapchain.
            profiler_begin(&game->renderer.profiler,profiler_clock_ms());
            overlay_enabled(game,config);
            if (!presentation_build(presentation,&game->simulation,&game->camera,config)
                || !renderer_render(&game->renderer,&presentation->scene,&game->camera,options->capture_path))
            { action=PAUSE_ERROR;break; }
        }
        action=pause_menu(rendered ? renderer_window(&game->renderer) : 0,&draft,defaults,&page,status);
        if (action!=PAUSE_APPLY) break;
        const char* error=player_settings_error(&draft);
        if (error) { snprintf(status,sizeof(status),"%s",error);continue; }
        RenderConfig candidate=settings_config(&draft),previous=*config;
        error=render_config_error(&candidate);
        if (error) { snprintf(status,sizeof(status),"%s",error);continue; }
        bool rebuild=graphics_changed(settings,&draft),debug_views=game->renderer.debug_views;
        bool applied=!rendered || !rebuild || replace_renderer(game,presentation,&candidate,&draft,options,debug_views);
        bool keep=applied;
        if (keep) keep=player_settings_save(&draft,settings_path);
        if (!keep)
        {
            if (rendered && rebuild && !replace_renderer(game,presentation,&previous,settings,options,debug_views))
            { action=PAUSE_ERROR;break; }
            draft=*settings;
            snprintf(status,sizeof(status),"Settings were not saved. Previous settings restored.");
            continue;
        }
        *config=candidate;*settings=draft;
        if (rendered) renderer_preferences(&game->renderer,settings,options,debug_views);
        snprintf(status,sizeof(status),"Settings applied and saved.");
        printf("Player settings applied: %ux%u; %u film samples; master %.3g; effects %.3g.\n",
            config->width,config->height,config->samples_per_pixel,settings->master_volume,settings->effects_volume);fflush(stdout);
    }
    bool unchanged=tick==game->simulation.tick_count && time==game->simulation.time_seconds;
    printf("Pause session: tick %u -> %u; time %.9g -> %.9g; action %d.\n",tick,game->simulation.tick_count,time,game->simulation.time_seconds,(int)action);fflush(stdout);
    game->simulation.paused=previously_paused;
    audio_set_volume(audio,settings->master_volume,settings->effects_volume);
    game->box_dragging=false;game->ratio_dragging=false;game->pointer=PointerInput();
    return unchanged ? action : PAUSE_ERROR;
}

int game_run(const GameOptions* options)
{
    if (options->matches) return run_matches(options);
    std::unique_ptr<Game> storage(new (std::nothrow) Game());if (!storage) return 1;
    Game& game=*storage;
    Audio audio;
    std::unique_ptr<Presentation> presentation(new (std::nothrow) Presentation());
    if (!presentation) return 1;
    uint32_t seed=options->world_seed;
    if (!options->world_seed_set && !options->smoke && !options->headless && !options->ticks && !options->capture_path)
    {
#ifdef _WIN32
        seed=GetTickCount()|1u;
#else
        seed=(uint32_t)std::chrono::steady_clock::now().time_since_epoch().count()|1u;
#endif
    }
    printf("World variation seed: %u. Restart keeps this layout.\n",seed);
    if (!simulation_initialize(&game.simulation,options->world_path,seed,options->player_faction))
        return 1;
    if (!audio_load(&audio,options->audio_path,options->audio_capture_path,&game.simulation.world.content)) return 1;
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
    PlayerSettings defaults,settings;
    if (!player_settings_defaults(&defaults,&config,options->settings_defaults_path)) return 1;
    settings=defaults;char user_path[ASSET_PATH_CAPACITY]={};const char* settings_path=options->settings_path;
    bool preferences=!options->smoke && !options->headless && !options->frames && !options->capture_path;
    if (preferences && !settings_path)
    {
        if (!player_settings_user_path(user_path,sizeof(user_path)))
        { fprintf(stderr,"Cannot locate the player settings folder.\n");return 1; }
        settings_path=user_path;
    }
    if (settings_path && !player_settings_load(&settings,settings_path,true)) return 1;
    if (options->samples) settings.graphics.samples_per_pixel=options->samples;
    if (options->vsync) settings.graphics.vsync=true;
    if (options->stats_set) settings.show_stats=options->show_stats;
    config=settings_config(&settings);
    const char* config_error=render_config_error(&config);
    if (config_error) { fprintf(stderr,"Player graphics settings: %s.\n",config_error);return 1; }
    audio_set_volume(&audio,settings.master_volume,settings.effects_volume);
    printf("Render configuration: direct ray tracing; %u film samples; %u transmission layers; %u volume rays; %u integration steps.\n",
        config.samples_per_pixel,config.max_transmission_layers,config.volume_rays_per_pixel,config.volume_steps);
    startup_camera(&game,&config);
    if (!orders_run(&game.simulation,options->replay_path,options->ticks,&audio)) return 1;
    game.pointer=options->camera_input;
    PointerInput initial_input=options->camera_input;
    apply_camera_preferences(&initial_input,&settings);
    ratio_pointer(&game,&initial_input,&config);selection_input(&game,&initial_input,&config);camera_apply_input(&game.camera,&initial_input,&config);
    gameplay_input(&game,&initial_input,&config);
    printf("Initial camera: %g %g %g; target: %g %g %g.\n",game.camera.position.x,game.camera.position.y,game.camera.position.z,game.camera.target.x,game.camera.target.y,game.camera.target.z);
    if (!presentation_initialize(presentation.get(),&game.simulation,&config)) return 1;
    if (options->smoke && options->pause_on_start)
    {
        PauseAction action=pause_session(&game,presentation.get(),&audio,&config,&settings,&defaults,settings_path,options,false);
        if (action==PAUSE_ERROR) return 1;
        if (action==PAUSE_MAIN_MENU || action==PAUSE_QUIT)
        {
            if (options->state_path && !simulation_write_state(&game.simulation,options->state_path)) return 1;
            return action==PAUSE_MAIN_MENU ? GAME_RETURN_TO_MENU : 0;
        }
    }
    if (options->smoke)
    {
        if (!options->ticks)
        { for (uint32_t i=0;i<SMOKE_SIMULATION_TICKS;++i) simulation_step(&game.simulation);if (game.simulation.tick_count!=SMOKE_SIMULATION_TICKS) return 1; }
        if (!presentation_build(presentation.get(),&game.simulation,&game.camera,&config)
            || !render_scene_validate(&presentation->scene)) return 1;
        if (options->state_path && !simulation_write_state(&game.simulation,options->state_path)) return 1;
        if (options->scene_state_path && !render_scene_write(&presentation->scene,options->scene_state_path)) return 1;
        printf("Application lifecycle smoke passed: assets, simulation and render scene; GPU rendering excluded.\n");
        return audio_close(&audio) ? 0 : 1;
    }
    if (!presentation_build(presentation.get(),&game.simulation,&game.camera,&config)
        || !renderer_initialize(&game.renderer,&presentation->scene,&config,options->shader_path,
            options->builtin_shader_path,options->headless,options->debug_views,options->no_activate)) return 1;
    renderer_preferences(&game.renderer,&settings,options,options->debug_views);
    // Console writes are synchronous; interactive play keeps them out of the frame.
    game.renderer.frame_log = options->frames!=0;
    if (!options->headless && !audio_open(&audio)) { renderer_shutdown(&game.renderer);return 1; }
    game.renderer.debug_views = options->debug_views;
#ifdef DEVELOPMENT
    game.renderer.wireframe=options->wireframe;
#endif
    printf("Left-drag to select; middle-drag or WASD/arrows to pan; wheel to zoom. Gameplay: F/B select fighters/bombers on screen; 1 fighters, 2 bombers, 3 all; right-click to move or order; drag the site slider to set production; Space pause; Esc pause menu; R restart.\n");
#ifdef DEVELOPMENT
    printf("Development: F1 profiler; F2 render buffers; F3 wireframe; F5 SSS; F6 volume; F7 cell blur; F8 tilt shift; F9 fog blur; F10 self-glow and halo.\n");
#endif
    const double tick_seconds = 1.0 / SIMULATION_TICKS_PER_SECOND_DOUBLE;
    // Bounded diagnostics and captures draw the exact current tick.
    bool interpolate=!options->frames && !options->headless;
    record_motion(&game.motion,&game.simulation);
    double previous = monotonic_seconds();
    double accumulator = 0.0;
    double frame_total = 0.0;
    uint32_t frames = 0;
    bool success = true,return_to_menu=false,pause_pending=options->pause_on_start;
    PointerInput input = {};
    while (!options->frames || frames < options->frames)
    {
        if (frames && options->frame_delay_ms)
        {
#ifdef _WIN32
            Sleep(options->frame_delay_ms);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(options->frame_delay_ms));
#endif
        }
        double start = monotonic_seconds();
        Profiler* profile=&game.renderer.profiler;profiler_begin(profile,start*MILLISECONDS_PER_SECOND);
        if (!renderer_poll(&game.renderer,&input)) break;
        if (input.menu || pause_pending)
        {
            pause_pending=false;
            PauseAction action=pause_session(&game,presentation.get(),&audio,&config,&settings,&defaults,settings_path,options,true);
            if (action==PAUSE_ERROR) { success=false;break; }
            if (action==PAUSE_MAIN_MENU || action==PAUSE_QUIT)
            { return_to_menu=action==PAUSE_MAIN_MENU;break; }
            accumulator=0;previous=monotonic_seconds();input=PointerInput();
            record_motion(&game.motion,&game.simulation);continue;
        }
        double elapsed = start - previous;
        previous = start;
        game.renderer.previous_frame_ms = frames ? elapsed*MILLISECONDS_PER_SECOND : 0.0;
        if (input.toggle_stats) game.renderer.debug_stats = !game.renderer.debug_stats;
#ifdef DEVELOPMENT
        if (input.toggle_buffers) game.renderer.debug_views = !game.renderer.debug_views;
        if (input.toggle_wireframe) game.renderer.wireframe=!game.renderer.wireframe;
#endif
        if (input.toggle_render_features) renderer_toggle_features(&game.renderer,input.toggle_render_features);
        if (input.restart && game.simulation.world.gameplay.enabled) { accumulator=0;elapsed=0; }
        input.pan_seconds=(float)std::fmin(elapsed,MAX_FRAME_CATCHUP_SECONDS);
        apply_camera_preferences(&input,&settings);
        game.pointer=input;ratio_pointer(&game,&input,&config);selection_input(&game,&input,&config);
        camera_apply_input(&game.camera,&input,&config);gameplay_input(&game,&input,&config);
        double input_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_INPUT,start*MILLISECONDS_PER_SECOND,input_end);
        accumulator += elapsed < MAX_FRAME_CATCHUP_SECONDS ? elapsed : MAX_FRAME_CATCHUP_SECONDS;
        if (options->headless) accumulator=0;
        if (options->frame_ticks) accumulator=options->frame_ticks*tick_seconds;
        if (input.restart) record_motion(&game.motion,&game.simulation);
        while (accumulator >= tick_seconds)
        {
            if (interpolate) record_motion(&game.motion,&game.simulation);
            simulation_step(&game.simulation);
            audio_events(&audio,&game.simulation,game.camera.target);
            accumulator -= tick_seconds;
        }
        if ((options->headless && !options->ticks) || options->time_set)
            simulation_set_time(&game.simulation, options->time_seconds + frames * options->step_seconds);
        double simulation_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_SIMULATION,input_end,simulation_end);
        profile->pointer_x=input.pointer_x;profile->pointer_y=input.pointer_y;
        overlay_enabled(&game,&config);
        audio_events(&audio,&game.simulation,game.camera.target);
        if (!audio_running(&audio)) { success=false;break; }
        if (game.renderer.frame_log)
        {
            if (input.pan_x || input.pan_y)
                printf("Camera keyboard: pan %g %g; seconds %.9g.\n",input.pan_x,input.pan_y,input.pan_seconds);
            if (input.drag_x || input.drag_y || input.wheel)
                printf("Camera input: drag %g %g; wheel %g.\n", input.drag_x, input.drag_y, input.wheel);
            printf("Scene time: %.6f; camera: %g %g %g.\n",game.simulation.time_seconds,
                game.camera.position.x,game.camera.position.y,game.camera.position.z);
            fflush(stdout); // Input checks wait for this frame boundary.
        }
        bool final_frame = options->frames && frames + 1 == options->frames;
        if (interpolate) blend_motion(&game.simulation,&game.motion,(float)(accumulator/tick_seconds),&game.current_motion);
        bool built=presentation_build(presentation.get(),&game.simulation,&game.camera,&config);
        if (interpolate) restore_motion(&game.simulation,&game.current_motion);
        if (!built) { success=false;break; }
        profiler_cpu(profile,PROFILE_SCENE,simulation_end,profiler_clock_ms());
        if (!renderer_render(&game.renderer,&presentation->scene,&game.camera,final_frame ? options->capture_path : 0))
        {
            success = false;
            break;
        }
        double frame_end=profiler_clock_ms();
        profiler_cpu(profile,PROFILE_CPU_FRAME,start*MILLISECONDS_PER_SECOND,frame_end);
        profiler_finish(profile,options->frames && game.renderer.debug_stats);
        double frame_ms=frame_end-start*MILLISECONDS_PER_SECOND;
        game.renderer.work_frame_ms=frame_ms-game.renderer.acquire_ms-game.renderer.present_ms;
        frame_total += frame_ms;
        ++frames;
        if (!game.renderer.frame_log) continue;
        printf("Application frame: %.3f ms.\n", frame_ms);
        printf("Application cadence: %.3f ms.\n",game.renderer.previous_frame_ms);
        printf("Application work: %.3f ms; explicit acquire/present waits: %.3f ms.\n",game.renderer.work_frame_ms,game.renderer.acquire_ms+game.renderer.present_ms);
    }
    if (!renderer_shutdown(&game.renderer)) success = false;
    if (!audio_close(&audio)) success=false;
    if (options->state_path && !simulation_write_state(&game.simulation,options->state_path)) success=false;
    if (options->scene_state_path && !render_scene_write(&presentation->scene,options->scene_state_path)) success=false;
    if (frames)
        printf("Rendered %u frames: mean application %.3f ms (%.2f FPS).\n",
               frames, frame_total / frames, MILLISECONDS_PER_SECOND * frames / frame_total);
    return !success ? 1 : return_to_menu ? GAME_RETURN_TO_MENU : 0;
}
