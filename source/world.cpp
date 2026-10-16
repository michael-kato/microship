#include "world.h"
#include "asset_json.h"
#include "vector_math.h"
#include <stdio.h>
#include <string.h>
#include <cmath>

static Vec3 read_normal(Json j)
{
    Vec3 n=json_vector(j,-100,100);float squared=dot(n,n);
    if (squared<.01f || squared>10000) json_error(j,"normal must have squared length in [.01,10000]");
    return squared>0 ? normalize(n) : n;
}
static Faction read_owner(const World* world,Json j)
{
    char name[32]={};json_string(j,name,32);int owner=content_faction(&world->content,name);
    if (owner<0) { json_error(j,"unknown faction reference");return 0; }
    return (Faction)owner;
}
static bool load_rules(World* w,const char* path)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|resource_label|economy|ai|exploration|flight|orbit|recovery|spikes");
    json_string(json_get(root,"resource_label"),w->content.resource_label,32);
    Json economy=json_find(root,"economy");
    if (json_present(economy))
    {
        Json j=economy;json_object(j,"storage|manufacture_seconds|landing_seconds|enemy_attack_seconds");
        w->gameplay.storage=json_positive(json_get(j,"storage"),10000);
        w->gameplay.manufacture_seconds=json_positive(json_get(j,"manufacture_seconds"),10000);
        w->gameplay.landing_seconds=json_positive(json_get(j,"landing_seconds"),10000);
        w->gameplay.enemy_attack_seconds=json_positive(json_get(j,"enemy_attack_seconds"),10000);
        w->gameplay.enabled=true;
    }
    Json ai=json_find(root,"ai");
    if (json_present(ai))
    {
        Json j=ai;json_object(j,"production_rate|expansion_delay");
        w->ai_production=json_positive(json_get(j,"production_rate"),1);
        w->ai_expansion=json_float(json_get(j,"expansion_delay"),0,300);
        w->ai_production_set=true;
    }
    Json exploration=json_find(root,"exploration");
    if (json_present(exploration))
    {
        Json j=exploration;json_object(j,"sight_radius");
        w->sight_radius=json_positive(json_get(j,"sight_radius"),1000);

    }
    Json flight=json_find(root,"flight");
    if (json_present(flight))
    {
        Json j=flight;json_object(j,"acceleration|turn_rate|separation|bank");
        w->combat.acceleration=json_positive(json_get(j,"acceleration"),100);
        w->combat.turn_rate=json_positive(json_get(j,"turn_rate"),30);
        w->combat.separation=json_positive(json_get(j,"separation"),3);
        w->combat.bank=json_float(json_get(j,"bank"),0,1);
        w->combat.flight_enabled=true;
    }
    Json orbit=json_find(root,"orbit");
    if (json_present(orbit))
    {
        Json j=orbit;json_object(j,"clearance|speed");
        w->combat.orbit_clearance=json_positive(json_get(j,"clearance"),5);
        w->combat.orbit_speed=json_positive(json_get(j,"speed"),2);
        w->combat_fields|=2;
    }
    Json recovery=json_find(root,"recovery");
    if (json_present(recovery))
    {
        Json j=recovery;json_object(j,"heal_interval|damage_delay|orbit_clearance");
        w->spikes.heal_interval=json_positive(json_get(j,"heal_interval"),60);
        w->spikes.heal_delay=json_float(json_get(j,"damage_delay"),0,60);
        w->spikes.heal_clearance=json_positive(json_get(j,"orbit_clearance"),10);
        w->spikes.recovery_set=true;
    }
    Json spikes=json_find(root,"spikes");
    if (json_present(spikes))
    {
        Json j=spikes;json_object(j,"count|regrow_seconds");
        w->spikes.count=json_uint(json_get(j,"count"),1,32);
        w->spikes.regrow=json_positive(json_get(j,"regrow_seconds"),60);
        w->spikes.enabled=true;
    }
    return d.valid;
}
static bool load_effects(World* w,const char* path)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|projectile|damage|injury|spikes");
    Json p=json_find(root,"projectile");
    if (json_present(p))
    {
        json_object(p,"projectile_speed|shot_interval|trail_width|trail_lifetime|flash_seconds|pop_seconds|pop_radius");
        w->combat.projectile_speed=json_positive(json_get(p,"projectile_speed"),100);
        w->combat.shot_interval=json_positive(json_get(p,"shot_interval"),100);
        w->combat.trail_width=json_positive(json_get(p,"trail_width"),1);
        w->combat.trail_lifetime=json_positive(json_get(p,"trail_lifetime"),5);
        w->combat.flash_seconds=json_positive(json_get(p,"flash_seconds"),2);
        w->combat.pop_seconds=json_positive(json_get(p,"pop_seconds"),2);
        w->combat.pop_radius=json_positive(json_get(p,"pop_radius"),100);
        w->combat.enabled=true;w->combat_fields|=1;
    }
    Json damage=json_find(root,"damage");
    if (json_present(damage))
    {
        json_object(damage,"flash|injured");w->combat.flash=json_color(json_get(damage,"flash"));
        w->combat.injured=json_color(json_get(damage,"injured"));w->combat_fields|=4;
    }
    Json injury=json_find(root,"injury");
    if (json_present(injury))
    {
        json_object(injury,"first|later");w->spikes.injury[0]=json_color(json_get(injury,"first"));
        w->spikes.injury[1]=json_color(json_get(injury,"later"));w->spikes.injury_set=true;
    }
    Json spikes=json_find(root,"spikes");
    if (json_present(spikes))
    {
        json_object(spikes,"model|root_radius|length|projectile_scale");
        json_path(json_get(spikes,"model"),path,w->spikes.model);
        w->spikes.root=json_positive(json_get(spikes,"root_radius"),1);
        w->spikes.length=json_positive(json_get(spikes,"length"),2);
        w->spikes.scale=json_positive(json_get(spikes,"projectile_scale"),1);
    }
    return d.valid;
}
static bool load_appearance(World* w,const char* path)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|ambient|gradient|lights|thin_surface|backdrop");
    w->ambient=json_color(json_get(root,"ambient"),10000);w->ambient_enabled=true;
    Json gradient=json_find(root,"gradient");
    if (json_present(gradient))
    {
        json_object(gradient,"top|bottom");w->ambient_top=json_color(json_get(gradient,"top"),10000);
        w->ambient_bottom=json_color(json_get(gradient,"bottom"),10000);w->ambient_gradient=true;
    }
    Json lights=json_find(root,"lights");
    if (json_present(lights))
    {
        json_object(lights,"key|fill|rim");const char* names[]={"key","fill","rim"};
        for (uint32_t i=0;i<3;++i)
        {
            Json light=json_get(lights,names[i]);json_object(light,"direction|color|irradiance");
            Json direction=json_get(light,"direction");Vec3 n=json_vector(direction,-1e18,1e18);float squared=dot(n,n);
            if (!std::isfinite(squared) || squared<.0001f) json_error(direction,"light direction must be nonzero");
            w->studio[i].direction=squared>0 ? scale(n,1/std::sqrt(squared)) : n;
            w->studio[i].color=json_color(json_get(light,"color"));
            w->studio[i].irradiance=json_float(json_get(light,"irradiance"),0,100);w->studio_mask|=1u<<i;
        }
    }
    Json thin=json_find(root,"thin_surface");
    if (json_present(thin))
    {
        json_object(thin,"transmission|absorption|clarity");
        w->thin_transmission=json_float(json_get(thin,"transmission"),0,.9f);
        w->thin_absorption=json_float(json_get(thin,"absorption"),0,10);w->thin_transmission_set=true;
        Json clarity=json_find(thin,"clarity");
        if (json_present(clarity)) { w->thin_clarity=json_float(clarity,0,1);w->thin_clarity_set=true; }
    }
    Json backdrop=json_find(root,"backdrop");
    if (json_present(backdrop))
    {
        json_object(backdrop,"model|position|scale|tint|samples|soft|wave");
        json_path(json_get(backdrop,"model"),path,w->backdrop.model_path);
        w->backdrop.position=json_vector(json_get(backdrop,"position"),-1000000,1000000);
        w->backdrop.scale=json_positive(json_get(backdrop,"scale"),10000);
        w->backdrop.color=json_color(json_get(backdrop,"tint"));
        Json samples=json_find(backdrop,"samples");if (json_present(samples)) w->backdrop_samples=json_uint(samples,1,4096);
        Json soft=json_find(backdrop,"soft");
        if (json_present(soft))
        {
            json_object(soft,"blur_radius|glow|ambient|pulse_amplitude|period_seconds");
            w->backdrop_blur=json_float(json_get(soft,"blur_radius"),1,256);
            w->backdrop_glow=json_float(json_get(soft,"glow"),0,20);
            w->backdrop_ambient=json_float(json_get(soft,"ambient"),0,10);
            w->backdrop_pulse=json_float(json_get(soft,"pulse_amplitude"),0,.15f);
            w->backdrop_period=json_float(json_get(soft,"period_seconds"),1,120);w->backdrop_soft=true;
        }
        Json wave=json_find(backdrop,"wave");
        if (json_present(wave))
        {
            json_object(wave,"amplitude|frequency|speed");
            w->backdrop_wave=Vec3{json_float(json_get(wave,"amplitude"),0,.1f),
                json_positive(json_get(wave,"frequency"),100),json_positive(json_get(wave,"speed"),10)};
            w->backdrop_wave_set=true;
        }
    }
    return d.valid;
}
static bool load_placements(World* w,Json root)
{
    JsonArray bodies=json_array(json_get(root,"bodies"),MAX_BODIES);Json j={};
    while (json_next(&bodies,&j))
    {
        json_object(j,"type|owner|position|radius|tint|site|medium");Body b={};b.availability=-1;
        char type[32]={};Json name=json_get(j,"type");json_string(name,type,32);int index=content_body(&w->content,type);
        if (index<0) return json_error(name,"unknown body type reference");
        b.type=(uint32_t)index;strcpy(b.model_path,w->content.bodies[index].model_path);b.owner=read_owner(w,json_get(j,"owner"));
        b.position=json_vector(json_get(j,"position"),-1000000,1000000);b.radius=json_positive(json_get(j,"radius"),1000000);
        b.color=json_color(json_get(j,"tint"));Json site=json_find(j,"site");
        if (json_present(site))
        {
            json_object(site,"normal|availability");b.colony_normal=read_normal(json_get(site,"normal"));
            b.availability=json_float(json_get(site,"availability"),0,1);
            if (!w->content.bodies[index].hosts_site) return json_error(site,"body type cannot host a production site");
        }
        Json medium=json_find(j,"medium");
        if (json_present(medium))
        {
            json_object(medium,"radius_fraction|edge_fade|scattering|absorption|anisotropy");
            b.interior_radius=json_positive(json_get(medium,"radius_fraction"),1);
            b.interior_fade=json_float(json_get(medium,"edge_fade"),0,1);
            b.interior_scattering=json_color(json_get(medium,"scattering"),1000);
            b.interior_absorption=json_float(json_get(medium,"absorption"),0,1000);
            Json anisotropy=json_get(medium,"anisotropy");b.interior_anisotropy=json_float(anisotropy,-.95f,.95f);
            if (std::fabs(b.interior_anisotropy)>=.95f) json_error(anisotropy,"anisotropy must be in (-.95,.95)");
        }
        w->bodies[w->body_count++]=b;
    }
    JsonArray spawns=json_array(json_get(root,"spawns"),MAX_SPAWNS);
    while (json_next(&spawns,&j))
    {
        json_object(j,"type|position");Spawn s={};Json name=json_get(j,"type");json_string(name,s.type_name,32);
        int index=content_unit(&w->content,s.type_name);if (index<0) return json_error(name,"unknown unit type reference");
        s.type=(uint32_t)index;s.position=json_vector(json_get(j,"position"),-1000000,1000000);w->spawns[w->spawn_count++]=s;
    }
    return root.document->valid;
}
static bool validate_world(const World* world,const char* path)
{
    if (!world->player_faction || !world->ambient_enabled || (!world->body_count && !world->spawn_count))
    { fprintf(stderr,"%s: expected match, ambient and at least one cell or spawn\n",path);return false; }
    if (world->gameplay.enabled)
    {
        if (!world->content.resource_label[0] || !world->content.producer_count || !world->spikes.enabled)
        { fprintf(stderr,"%s: gameplay requires producer and spike definitions\n",path);return false; }
        for (uint32_t f=1;f<MAX_FACTIONS;++f) if (world->content.factions[f].declared
            && !world->content.factions[f].producer_name[0])
        { fprintf(stderr,"%s: gameplay factions require production definitions\n",path);return false; }
        for (uint32_t t=0;t<world->content.unit_count;++t) if (world->content.units[t].cost>world->gameplay.storage)
        { fprintf(stderr,"%s: storage must cover declared unit costs\n",path);return false; }
        for (uint32_t i=0;i<world->body_count;++i)
            if (world->content.bodies[world->bodies[i].type].hosts_site && (dot(world->bodies[i].colony_normal,world->bodies[i].colony_normal)<.9f || world->bodies[i].availability<0))
            { fprintf(stderr,"%s: production hosts require site and resource records\n",path);return false; }
    }
    if (world->sight_radius>0 && (!world->gameplay.enabled || (world->backdrop.model_path[0] && !world->backdrop_soft)))
    { fprintf(stderr,"%s: exploration requires gameplay and a soft backdrop\n",path);return false; }
    if ((world->studio_mask && world->studio_mask!=7)
        || (world->combat_fields && (world->combat_fields!=7 || !world->gameplay.enabled))
        || (world->combat.flight_enabled && world->combat_fields!=7)
        || (world->backdrop_wave_set && !world->backdrop_soft))
    { fprintf(stderr,"%s: incomplete studio or combat settings, or wave without a soft backdrop\n",path);return false; }
    if ((world->spikes.enabled || world->spikes.recovery_set || world->spikes.injury_set || world->spikes.model[0])
        && (!world->spikes.enabled || !world->spikes.model[0] || !world->spikes.recovery_set || !world->spikes.injury_set
            || !world->combat.flight_enabled || world->combat_fields!=7))
    { fprintf(stderr,"%s: spike combat requires flight, combat, recovery and injury settings\n",path);return false; }
    if (world->thin_clarity_set && !world->thin_transmission_set)
    { fprintf(stderr,"%s: thin clarity requires thin_transmission\n",path);return false; }
    if ((world->backdrop_samples || world->backdrop_soft) && !world->backdrop.model_path[0])
    { fprintf(stderr,"%s: background settings require a backdrop mesh\n",path);return false; }
    return true;
}

bool world_load(World* world,const char* path)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|content|rules|appearance|effects|match|bodies|spawns");
    World loaded={};loaded.ambient_top=loaded.ambient_bottom=Color{1,1,1};
    if (!json_path(json_get(root,"content"),path,loaded.content_path) || !content_load(&loaded.content,loaded.content_path)) return false;
    char file[256]={};
    Json rules=json_find(root,"rules");if (json_present(rules) && (!json_path(rules,path,file) || !load_rules(&loaded,file))) return false;
    Json effects=json_find(root,"effects");if (json_present(effects) && (!json_path(effects,path,file) || !load_effects(&loaded,file))) return false;
    if (!json_path(json_get(root,"appearance"),path,file) || !load_appearance(&loaded,file)) return false;
    Json match=json_get(root,"match");json_object(match,"player|opponent");
    loaded.player_faction=read_owner(&loaded,json_get(match,"player"));loaded.opponent_faction=read_owner(&loaded,json_get(match,"opponent"));
    if (!loaded.player_faction || !loaded.opponent_faction || loaded.player_faction==loaded.opponent_faction)
        return json_error(match,"match requires two distinct non-neutral factions");
    if (!load_placements(&loaded,root) || !validate_world(&loaded,path)) return false;
    *world=loaded;return true;
}
