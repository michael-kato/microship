#include "orders.h"
#include "simulation.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <cmath>

enum OrderKind { SELECT, TARGET, INSPECT, PRIORITY, MOVE };
struct Order { uint32_t tick; OrderKind kind; uint32_t argument; Vec3 destination; };

static bool integer(const char* text, uint32_t maximum, uint32_t* value)
{
    if (!*text) return false;
    for (const char* p=text;*p;++p) if (*p<'0' || *p>'9') return false;
    errno=0;char* end=0;unsigned long long n=strtoull(text,&end,10);
    if (errno || *end || n>maximum) return false;
    *value=(uint32_t)n;return true;
}

bool orders_run(Simulation* s, const char* path, uint32_t ticks,Audio* audio)
{
    Order orders[512]={};uint32_t count=0;
    if (path)
    {
        if (!s->world.gameplay.enabled || !ticks) { fprintf(stderr,"Replay requires a gameplay world and positive --ticks.\n");return false; }
        FILE* file=fopen(path,"rb");if (!file) { fprintf(stderr,"Cannot open orders '%s'.\n",path);return false; }
        char line[256];bool valid=true;
        while (fgets(line,sizeof(line),file))
        {
            char* comment=strchr(line,'#');if (comment) *comment=0;
            char tick[32],command[32],argument[32],extra;int fields=sscanf(line,"%31s %31s %31s %c",tick,command,argument,&extra);
            if (fields<=0) continue;
            if (count==512 || fields<3) { valid=false;break; }
            Order order={};
            if (!integer(tick,ticks-1,&order.tick) || (count && order.tick<orders[count-1].tick)) { valid=false;break; }
            if (!strcmp(command,"move"))
            {
                order.kind=MOVE;Vec3* v=&order.destination;
                if (sscanf(line,"%31s %31s %f %f %f %c",tick,command,&v->x,&v->y,&v->z,&extra)!=5
                    || !std::isfinite(v->x) || !std::isfinite(v->y) || !std::isfinite(v->z)
                    || std::fabs(v->x)>1000000 || std::fabs(v->y)>1000000 || std::fabs(v->z)>1000000) { valid=false;break; }
            }
            else if (fields!=3) { valid=false;break; }
            else if (!strcmp(command,"select"))
            {
                order.kind=SELECT;
                int group=content_group(&s->world.content,argument);
                if (!strcmp(argument,"all")) order.argument=UINT32_MAX;
                else if (group>=0) order.argument=(uint32_t)group;
                else { valid=false;break; }
            }
            else if (!strcmp(command,"order") || !strcmp(command,"cell") || !strcmp(command,"body"))
            {
                order.kind=!strcmp(command,"order") ? TARGET : INSPECT;
                if (!s->world.body_count || !integer(argument,s->world.body_count-1,&order.argument)) { valid=false;break; }
            }
            else if (!strcmp(command,"priority"))
            {
                order.kind=PRIORITY;
                int type=content_unit(&s->world.content,argument);
                if (type<0)
                {
                    int group=content_group(&s->world.content,argument);
                    type=content_product(&s->world.content,s->world.player_faction,group);
                }
                if (type<0 || s->world.content.units[type].faction!=s->world.player_faction) { valid=false;break; }
                order.argument=(uint32_t)type;
            }
            else { valid=false;break; }
            orders[count++]=order;
        }
        valid=valid && !ferror(file);if (fclose(file)!=0) valid=false;
        if (!valid) { fprintf(stderr,"Invalid orders in '%s': tick command argument, sorted ticks within --ticks.\n",path);return false; }
    }
    uint32_t next=0;
    for (uint32_t tick=0;tick<ticks;++tick)
    {
        while (next<count && orders[next].tick==tick)
        {
            const Order* order=&orders[next++];
            if (order->kind==SELECT) simulation_select(s,order->argument==UINT32_MAX ? -1 : (int)order->argument);
            else if (order->kind==TARGET) simulation_order(s,order->argument);
            else if (order->kind==INSPECT)
            { if (simulation_body_known(s,order->argument)) s->selected_body=(int)order->argument; }
            else if (order->kind==MOVE) simulation_move_to(s,order->destination);
            else simulation_priority_type(s,order->argument);
        }
        simulation_step(s);
        if (audio && audio->capture)
        {
            Vec3 listener={};for (uint32_t i=0;i<s->world.body_count;++i) if (s->home_owner[i]==s->world.player_faction) listener=s->world.bodies[i].position;
            audio_events(audio,s,listener);if (!audio_tick(audio)) return false;
        }
        else s->sounds.count=0;
    }
    if (path || ticks) printf("Gameplay replay: %u commands, %u ticks; births %u, landings %u, losses %u.\n",count,ticks,s->births,s->landings,s->losses);
    return true;
}
