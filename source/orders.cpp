#include "orders.h"
#include "simulation.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

enum OrderKind { SELECT, TARGET, INSPECT, PRIORITY };
struct Order { uint32_t tick; OrderKind kind; uint32_t argument; };

static bool integer(const char* text, uint32_t maximum, uint32_t* value)
{
    if (!*text) return false;
    for (const char* p=text;*p;++p) if (*p<'0' || *p>'9') return false;
    errno=0;char* end=0;unsigned long long n=strtoull(text,&end,10);
    if (errno || *end || n>maximum) return false;
    *value=(uint32_t)n;return true;
}

bool orders_run(Simulation* s, const char* path, uint32_t ticks)
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
            if (fields!=3 || count==512) { valid=false;break; }
            Order order={};
            if (!integer(tick,ticks-1,&order.tick) || (count && order.tick<orders[count-1].tick)) { valid=false;break; }
            if (!strcmp(command,"select"))
            {
                order.kind=SELECT;
                if (!strcmp(argument,"fighters")) order.argument=0;
                else if (!strcmp(argument,"seedships")) order.argument=1;
                else if (!strcmp(argument,"all")) order.argument=2;
                else { valid=false;break; }
            }
            else if (!strcmp(command,"order") || !strcmp(command,"planet") || !strcmp(command,"cell"))
            {
                order.kind=!strcmp(command,"order") ? TARGET : INSPECT;
                if (!s->world.body_count || !integer(argument,s->world.body_count-1,&order.argument)) { valid=false;break; }
            }
            else if (!strcmp(command,"priority"))
            {
                order.kind=PRIORITY;
                if (!strcmp(argument,"fighter")) order.argument=0;
                else if (!strcmp(argument,"seedship")) order.argument=1;
                else { valid=false;break; }
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
            if (order->kind==SELECT) simulation_select(s,order->argument);
            else if (order->kind==TARGET) simulation_order(s,order->argument);
            else if (order->kind==INSPECT) s->selected_cell=(int)order->argument;
            else simulation_priority(s,(UnitKind)order->argument);
        }
        simulation_step(s);
    }
    if (path || ticks) printf("Gameplay replay: %u commands, %u ticks; births %u, landings %u, losses %u.\n",count,ticks,s->births,s->landings,s->losses);
    return true;
}
