#include "world.h"
#include "asset_json.h"
#include "vector_math.h"
#include <stdio.h>
#include <string.h>
#include <cmath>
#include "asset_limits.h"
#include "geometry.h"

static const int MAX_PLACEMENT_COORDINATE = 1000000;
static const int MAX_MEDIUM_ABSORPTION = 1000;
static const int MAX_MEDIUM_SCATTERING = 1000;
static const int MAX_BODY_RADIUS = 1000000;
static const int MAX_BACKDROP_WAVE_SPEED = 10;
static const int MAX_BACKDROP_WAVE_FREQUENCY = 100;
static const float MAX_BACKDROP_WAVE_AMPLITUDE = .1f;
static const int MAX_BACKDROP_PERIOD_SECONDS = 120;
static const float MAX_BACKDROP_PULSE_AMPLITUDE = .15f;
static const int MAX_BACKDROP_AMBIENT = 10;
static const int MAX_BACKDROP_GLOW = 20;
static const int MAX_SURFACE_GLOW_STRENGTH = 8;
static const int MAX_BACKDROP_SCALE = 10000;
static const int MAX_BACKDROP_COORDINATE = 1000000;
static const int MAX_MEMBRANE_ABSORPTION = 10;
static const float MAX_MEMBRANE_TRANSMISSION = .9f;
static const int MAX_STUDIO_IRRADIANCE = 100;
static const int MAX_GRADIENT_BOTTOM_RADIANCE = 10000;
static const int MAX_GRADIENT_TOP_RADIANCE = 10000;
static const int MAX_AMBIENT_RADIANCE = 10000;
static const int MAX_SPIKE_LENGTH = 2;
static const int MAX_EFFECTS_POP_RADIUS = 100;
static const int MAX_EFFECTS_POP_SECONDS = 2;
static const int MAX_EFFECTS_FLASH_SECONDS = 2;
static const int MAX_EFFECTS_TRAIL_LIFETIME = 5;
static const int MAX_EFFECTS_SHOT_INTERVAL = 100;
static const int MAX_EFFECTS_PROJECTILE_SPEED = 100;
static const int MAX_DEBRIS_SPIN = 20;
static const int MAX_DEBRIS_DRAG = 10;
static const int MAX_DEBRIS_SPEED = 10;
static const int MAX_DEBRIS_SECONDS = 10;
static const int MAX_RULES_REGROW_SECONDS = 60;
static const int MAX_SPIKE_COUNT = 32;
static const int MAX_RULES_ORBIT_CLEARANCE = 10;
static const int MAX_RULES_DAMAGE_DELAY = 60;
static const int MAX_RULES_HEAL_INTERVAL = 60;
static const int MAX_ORBIT_SPEED = 2;
static const int MAX_ORBIT_CLEARANCE = 5;
static const int MAX_RULES_SEPARATION = 3;
static const int MAX_RULES_TURN_RATE = 30;
static const int MAX_RULES_ACCELERATION = 100;
static const int MAX_RULES_SIGHT_RADIUS = 1000;
static const int MAX_RULES_EXPANSION_DELAY = 300;
static const int MAX_RULES_ENEMY_ATTACK_SECONDS = 10000;
static const int MAX_RULES_LANDING_SECONDS = 10000;
static const int MAX_RULES_MANUFACTURE_SECONDS = 10000;
static const int MAX_RULES_STORAGE = 10000;
static const int MAX_RULES_RESOURCE_LABEL = 32;
static const int MAX_NORMAL_COMPONENT = 100;
static const float MIN_NORMAL_SQUARED_LENGTH = .01f;
static const int MAX_NORMAL_SQUARED_LENGTH = 10000;
static const int MAX_BACKDROP_BLUR_RADIUS = 256;
static const double MAX_LIGHT_DIRECTION_COMPONENT = 1e18;
static const float MIN_LIGHT_DIRECTION_SQUARED = .0001f;
static const int MAX_BACKDROP_SAMPLES = 4096;
static const int MAX_AUTHORED_SEED = 16777215;
static const int DEFAULT_BODY_SEED_MULTIPLIER = 4721;
static const float MAX_MEDIUM_ANISOTROPY = .95f;
static const int MAX_FLIGHT_LOOKAHEAD_SECONDS = 5;
static const int MAX_OBSTACLE_CLEARANCE = 5;
static const int MAX_ORBIT_WEAVE_RADIUS = 5;
static const int MAX_ORBIT_WEAVE_HEIGHT = 5;
static const float MIN_SITE_NORMAL_SQUARED_LENGTH = .9f;

static Vec3 read_normal(Json j)
{
    Vec3 n=json_vector(j,-MAX_NORMAL_COMPONENT,MAX_NORMAL_COMPONENT);float squared=dot(n,n);
    if (squared<MIN_NORMAL_SQUARED_LENGTH || squared>MAX_NORMAL_SQUARED_LENGTH) json_error(j,"normal must have squared length in [.01,10000]");
    return squared>0 ? normalize(n) : n;
}
static Faction read_owner(const World* world,Json j)
{
    char name[ASSET_NAME_CAPACITY]={};json_string(j,name,ASSET_NAME_CAPACITY);int owner=content_faction(&world->content,name);
    if (owner<0) { json_error(j,"unknown faction reference");return 0; }
    return (Faction)owner;
}
static bool load_rules(World* w,const char* path)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|resource_label|economy|ai|exploration|flight|orbit|recovery|spikes");
    json_string(json_get(root,"resource_label"),w->content.resource_label,MAX_RULES_RESOURCE_LABEL);
    Json economy=json_find(root,"economy");
    if (json_present(economy))
    {
        Json j=economy;json_object(j,"storage|manufacture_seconds|landing_seconds|enemy_attack_seconds");
        w->gameplay.storage=json_positive(json_get(j,"storage"),MAX_RULES_STORAGE);
        w->gameplay.manufacture_seconds=json_positive(json_get(j,"manufacture_seconds"),MAX_RULES_MANUFACTURE_SECONDS);
        w->gameplay.landing_seconds=json_positive(json_get(j,"landing_seconds"),MAX_RULES_LANDING_SECONDS);
        w->gameplay.enemy_attack_seconds=json_positive(json_get(j,"enemy_attack_seconds"),MAX_RULES_ENEMY_ATTACK_SECONDS);
        w->gameplay.enabled=true;
    }
    Json ai=json_find(root,"ai");
    if (json_present(ai))
    {
        Json j=ai;json_object(j,"production_rate|expansion_delay");
        w->ai_production=json_positive(json_get(j,"production_rate"),1);
        w->ai_expansion=json_float(json_get(j,"expansion_delay"),0,MAX_RULES_EXPANSION_DELAY);
        w->ai_production_set=true;
    }
    Json exploration=json_find(root,"exploration");
    if (json_present(exploration))
    {
        Json j=exploration;json_object(j,"sight_radius");
        w->sight_radius=json_positive(json_get(j,"sight_radius"),MAX_RULES_SIGHT_RADIUS);

    }
    Json flight=json_find(root,"flight");
    if (json_present(flight))
    {
        Json j=flight;json_object(j,"acceleration|turn_rate|separation|bank|lookahead_seconds|obstacle_clearance");
        w->combat.acceleration=json_positive(json_get(j,"acceleration"),MAX_RULES_ACCELERATION);
        w->combat.turn_rate=json_positive(json_get(j,"turn_rate"),MAX_RULES_TURN_RATE);
        w->combat.separation=json_positive(json_get(j,"separation"),MAX_RULES_SEPARATION);
        w->combat.bank=json_float(json_get(j,"bank"),0,1);
        Json lookahead=json_find(j,"lookahead_seconds"),clearance=json_find(j,"obstacle_clearance");
        w->combat.lookahead=json_present(lookahead) ? json_positive(lookahead,MAX_FLIGHT_LOOKAHEAD_SECONDS) : 0;
        w->combat.obstacle_clearance=json_present(clearance) ? json_positive(clearance,MAX_OBSTACLE_CLEARANCE) : 0;
        w->combat.flight_enabled=true;
    }
    Json orbit=json_find(root,"orbit");
    if (json_present(orbit))
    {
        Json j=orbit;json_object(j,"clearance|speed|weave_radius|weave_height");
        w->combat.orbit_clearance=json_positive(json_get(j,"clearance"),MAX_ORBIT_CLEARANCE);
        w->combat.orbit_speed=json_positive(json_get(j,"speed"),MAX_ORBIT_SPEED);
        Json radius=json_find(j,"weave_radius"),height=json_find(j,"weave_height");
        w->combat.weave_radius=json_present(radius) ? json_float(radius,0,MAX_ORBIT_WEAVE_RADIUS) : 0;
        w->combat.weave_height=json_present(height) ? json_float(height,0,MAX_ORBIT_WEAVE_HEIGHT) : 0;
        w->combat_fields|=COMBAT_FIELD_ORBIT;
    }
    Json recovery=json_find(root,"recovery");
    if (json_present(recovery))
    {
        Json j=recovery;json_object(j,"heal_interval|damage_delay|orbit_clearance");
        w->spikes.heal_interval=json_positive(json_get(j,"heal_interval"),MAX_RULES_HEAL_INTERVAL);
        w->spikes.heal_delay=json_float(json_get(j,"damage_delay"),0,MAX_RULES_DAMAGE_DELAY);
        w->spikes.heal_clearance=json_positive(json_get(j,"orbit_clearance"),MAX_RULES_ORBIT_CLEARANCE);
        w->spikes.recovery_set=true;
    }
    Json spikes=json_find(root,"spikes");
    if (json_present(spikes))
    {
        Json j=spikes;json_object(j,"count|regrow_seconds");
        w->spikes.count=json_uint(json_get(j,"count"),1,MAX_SPIKE_COUNT);
        w->spikes.regrow=json_positive(json_get(j,"regrow_seconds"),MAX_RULES_REGROW_SECONDS);
        w->spikes.enabled=true;
    }
    return d.valid;
}
static bool load_effects(World* w,const char* path)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|projectile|damage|injury|spikes|limb_break|target_line|spore_cloud|heal_aura");
    Json aura=json_find(root,"heal_aura");
    if (json_present(aura))
    {
        json_object(aura,"color|alpha");w->combat.aura_color=json_color(json_get(aura,"color"));
        w->combat.aura_alpha=json_float(json_get(aura,"alpha"),0,1);
    }
    Json cloud=json_find(root,"spore_cloud");
    if (json_present(cloud))
    {
        json_object(cloud,"color|alpha");w->combat.cloud_color=json_color(json_get(cloud,"color"));
        w->combat.cloud_alpha=json_float(json_get(cloud,"alpha"),0,1);
    }
    Json line=json_find(root,"target_line");
    if (json_present(line))
    {
        json_object(line,"width|alpha|hold_seconds|fade_seconds");w->combat.target_line_width=json_positive(json_get(line,"width"),1);
        w->combat.target_line_alpha=json_float(json_get(line,"alpha"),0,1);
        if (json_present(json_find(line,"hold_seconds"))) w->combat.target_line_hold=json_float(json_get(line,"hold_seconds"),0,60);
        if (json_present(json_find(line,"fade_seconds"))) w->combat.target_line_fade=json_positive(json_get(line,"fade_seconds"),60);
    }
    Json fracture=json_find(root,"limb_break");
    if (json_present(fracture))
    {
        json_object(fracture,"seconds|speed|drag|spin|shrink_seconds");
        w->combat.break_seconds=json_positive(json_get(fracture,"seconds"),MAX_DEBRIS_SECONDS);
        w->combat.break_speed=json_positive(json_get(fracture,"speed"),MAX_DEBRIS_SPEED);
        w->combat.break_drag=json_float(json_get(fracture,"drag"),0,MAX_DEBRIS_DRAG);
        w->combat.break_spin=json_float(json_get(fracture,"spin"),0,MAX_DEBRIS_SPIN);
        w->combat.break_shrink=json_positive(json_get(fracture,"shrink_seconds"),w->combat.break_seconds);
    }
    Json p=json_find(root,"projectile");
    if (json_present(p))
    {
        json_object(p,"projectile_speed|shot_interval|trail_width|trail_lifetime|trail_length_scale|flash_seconds|pop_seconds|pop_radius");
        w->combat.projectile_speed=json_positive(json_get(p,"projectile_speed"),MAX_EFFECTS_PROJECTILE_SPEED);
        w->combat.shot_interval=json_positive(json_get(p,"shot_interval"),MAX_EFFECTS_SHOT_INTERVAL);
        w->combat.trail_width=json_positive(json_get(p,"trail_width"),1);
        w->combat.trail_lifetime=json_positive(json_get(p,"trail_lifetime"),MAX_EFFECTS_TRAIL_LIFETIME);
        Json trail_scale=json_find(p,"trail_length_scale");
        w->combat.trail_length_scale=json_present(trail_scale) ? json_positive(trail_scale,1) : 1;

        w->combat.flash_seconds=json_positive(json_get(p,"flash_seconds"),MAX_EFFECTS_FLASH_SECONDS);
        w->combat.pop_seconds=json_positive(json_get(p,"pop_seconds"),MAX_EFFECTS_POP_SECONDS);
        w->combat.pop_radius=json_positive(json_get(p,"pop_radius"),MAX_EFFECTS_POP_RADIUS);
        w->combat.enabled=true;w->combat_fields|=COMBAT_FIELD_PROJECTILE;
    }
    Json damage=json_find(root,"damage");
    if (json_present(damage))
    {
        json_object(damage,"flash|injured");w->combat.flash=json_color(json_get(damage,"flash"));
        w->combat.injured=json_color(json_get(damage,"injured"));w->combat_fields|=COMBAT_FIELD_DAMAGE;
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
        w->spikes.length=json_positive(json_get(spikes,"length"),MAX_SPIKE_LENGTH);
        w->spikes.scale=json_positive(json_get(spikes,"projectile_scale"),1);
    }
    return d.valid;
}
static bool load_appearance(World* w,const char* path,uint32_t seed)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|ambient|gradient|lights|thin_surface|backdrop|atmosphere|fog|self_glow|organelle_appearance|animation");
    w->ambient=json_color(json_get(root,"ambient"),MAX_AMBIENT_RADIANCE);w->ambient_enabled=true;
    Json gradient=json_find(root,"gradient");
    if (json_present(gradient))
    {
        json_object(gradient,"top|bottom");w->ambient_top=json_color(json_get(gradient,"top"),MAX_GRADIENT_TOP_RADIANCE);
        w->ambient_bottom=json_color(json_get(gradient,"bottom"),MAX_GRADIENT_BOTTOM_RADIANCE);w->ambient_gradient=true;
    }
    Json atmosphere=json_find(root,"atmosphere");
    if (json_present(atmosphere))
    {
        json_object(atmosphere,"center|radius|edge_fade|scattering|absorption|anisotropy|shadows");
        Json shadows=json_find(atmosphere,"shadows");w->atmosphere_shadows=!json_present(shadows) || json_bool(shadows);
        w->atmosphere_center=json_vector(json_get(atmosphere,"center"),-MAX_BACKDROP_COORDINATE,MAX_BACKDROP_COORDINATE);
        w->atmosphere_radius=json_positive(json_get(atmosphere,"radius"),MAX_BACKDROP_SCALE);
        w->atmosphere_fade=json_float(json_get(atmosphere,"edge_fade"),0,1);
        w->atmosphere_scattering=json_color(json_get(atmosphere,"scattering"),1);
        w->atmosphere_absorption=json_float(json_get(atmosphere,"absorption"),0,1);
        w->atmosphere_phase=json_float(json_get(atmosphere,"anisotropy"),-.95f,.95f);w->atmosphere_enabled=true;
    }
    Json fog=json_find(root,"fog");
    if (json_present(fog))
    {
        json_object(fog,"blur_radius|sight_feather|plane_z|tint|memory_half_life_seconds");
        Json tint=json_find(fog,"tint");w->fog_tint=json_present(tint) ? json_color(tint) : Color{1,1,1};
        w->fog_blur=json_float(json_get(fog,"blur_radius"),1,256);
        w->fog_feather=json_positive(json_get(fog,"sight_feather"),100);
        Json half_life=json_find(fog,"memory_half_life_seconds");
        if (json_present(half_life)) w->fog_half_life=json_float(half_life,0,60);
        w->fog_plane_z=json_float(json_get(fog,"plane_z"),-MAX_BACKDROP_COORDINATE,MAX_BACKDROP_COORDINATE);
    }
    w->organelle_style[0]=1;w->organelle_style[1]=.5f;w->organelle_style[2]=1;w->organelle_style[3]=0;
    w->organelle_period=w->membrane_wavelength=w->membrane_period=w->factory_period=1;
    Json organelles=json_find(root,"organelle_appearance");
    if (json_present(organelles))
    {
        json_object(organelles,"edge_saturation|edge_width|center_glow|center_power");
        w->organelle_style[0]=json_float(json_get(organelles,"edge_saturation"),0,3);
        w->organelle_style[1]=json_float(json_get(organelles,"edge_width"),.01f,1);
        w->organelle_style[2]=json_float(json_get(organelles,"center_glow"),0,8);
        w->organelle_style[3]=json_float(json_get(organelles,"center_power"),0,16);
    }
    Json animation=json_find(root,"animation");
    if (json_present(animation))
    {
        json_object(animation,"organelles|membrane|factories");
        Json organs=json_get(animation,"organelles");json_object(organs,"squash|period_seconds");
        w->organelle_squash=json_float(json_get(organs,"squash"),0,.1f);
        w->organelle_period=json_float(json_get(organs,"period_seconds"),1,300);
        Json membrane=json_get(animation,"membrane");json_object(membrane,"ripple_strength|wavelength|period_seconds");
        w->membrane_ripple=json_float(json_get(membrane,"ripple_strength"),0,.2f);
        w->membrane_wavelength=json_float(json_get(membrane,"wavelength"),.1f,100);
        w->membrane_period=json_float(json_get(membrane,"period_seconds"),1,300);
        Json factories=json_get(animation,"factories");json_object(factories,"sway_degrees|period_seconds");
        w->factory_sway=json_float(json_get(factories,"sway_degrees"),0,5)*.01745329252f;
        w->factory_period=json_float(json_get(factories,"period_seconds"),1,300);
    }
    Json glow=json_find(root,"self_glow");
    if (json_present(glow))
    {
        json_object(glow,"cell_interior|organelles|factory_stems|factory_leaves|units");
        w->glow_interior=json_float(json_get(glow,"cell_interior"),0,MAX_SURFACE_GLOW_STRENGTH);
        w->glow_organelles=json_float(json_get(glow,"organelles"),0,MAX_SURFACE_GLOW_STRENGTH);
        w->glow_stems=json_float(json_get(glow,"factory_stems"),0,MAX_SURFACE_GLOW_STRENGTH);
        w->glow_leaves=json_float(json_get(glow,"factory_leaves"),0,MAX_SURFACE_GLOW_STRENGTH);
        w->glow_units=json_float(json_get(glow,"units"),0,MAX_SURFACE_GLOW_STRENGTH);
    }
    Json lights=json_find(root,"lights");
    if (json_present(lights))
    {
        json_object(lights,"key|fill|rim");const char* names[]={"key","fill","rim"};
        for (uint32_t i=0;i<3;++i)
        {
            Json light=json_get(lights,names[i]);json_object(light,"direction|color|irradiance");
            Json direction=json_get(light,"direction");Vec3 n=json_vector(direction,-MAX_LIGHT_DIRECTION_COMPONENT,MAX_LIGHT_DIRECTION_COMPONENT);float squared=dot(n,n);
            if (!std::isfinite(squared) || squared<MIN_LIGHT_DIRECTION_SQUARED) json_error(direction,"light direction must be nonzero");
            w->studio[i].direction=squared>0 ? scale(n,1/std::sqrt(squared)) : n;
            w->studio[i].color=json_color(json_get(light,"color"));
            w->studio[i].irradiance=json_float(json_get(light,"irradiance"),0,MAX_STUDIO_IRRADIANCE);w->studio_mask|=1u<<i;
        }
    }
    Json thin=json_find(root,"thin_surface");
    if (json_present(thin))
    {
        json_object(thin,"transmission|absorption|clarity|edge_fade|blur_radius");
        Json blur=json_find(thin,"blur_radius");if (json_present(blur)) w->thin_blur_radius=json_float(blur,0,32);
        Json edge=json_find(thin,"edge_fade");
        if (json_present(edge))
        {
            json_object(edge,"strength|power");
            w->thin_edge_strength=json_float(json_get(edge,"strength"),0,1);
            w->thin_edge_power=json_positive(json_get(edge,"power"),16);
        }
        w->thin_transmission=json_float(json_get(thin,"transmission"),0,MAX_MEMBRANE_TRANSMISSION);
        w->thin_absorption=json_float(json_get(thin,"absorption"),0,MAX_MEMBRANE_ABSORPTION);w->thin_transmission_set=true;
        Json clarity=json_find(thin,"clarity");
        if (json_present(clarity)) { w->thin_clarity=json_float(clarity,0,1);w->thin_clarity_set=true; }
    }
    Json backdrop=json_find(root,"backdrop");
    if (json_present(backdrop))
    {
        json_object(backdrop,"model|variants|position|scale|tint|samples|soft|wave");
        json_path(json_get(backdrop,"model"),path,w->backdrop.model_path);
        Json variants=json_find(backdrop,"variants");
        if (json_present(variants))
        {
            // Resolve the bank once. The odd live seed must still reach every entry.
            char paths[16][ASSET_PATH_CAPACITY]={};uint32_t count=1;
            memcpy(paths[0],w->backdrop.model_path,ASSET_PATH_CAPACITY);
            JsonArray entries=json_array(variants,15);Json entry={};
            while (json_next(&entries,&entry))
            {
                if (count>=16 || !json_path(entry,path,paths[count])) return false;
                for (uint32_t i=0;i<count;++i) if (!strcmp(paths[i],paths[count])) return json_error(entry,"duplicate backdrop model");
                ++count;
            }
            if (count==1) return json_error(variants,"backdrop variants must not be empty");
            // Reject malformed alternatives even when another model was selected.
            for (uint32_t i=0;i<count;++i)
                if (!geometry_validate_model(paths[i])) return json_error(variants,"invalid backdrop model");
            uint32_t mixed=seed; mixed=((mixed>>16)^mixed)*0x45d9f3bu;mixed=(mixed>>16)^mixed;
            memcpy(w->backdrop.model_path,paths[mixed%count],ASSET_PATH_CAPACITY);
        }
        w->backdrop.position=json_vector(json_get(backdrop,"position"),-MAX_BACKDROP_COORDINATE,MAX_BACKDROP_COORDINATE);
        w->backdrop.scale=json_positive(json_get(backdrop,"scale"),MAX_BACKDROP_SCALE);
        w->backdrop.color=json_color(json_get(backdrop,"tint"));
        Json samples=json_find(backdrop,"samples");if (json_present(samples)) w->backdrop_samples=json_uint(samples,1,MAX_BACKDROP_SAMPLES);
        Json soft=json_find(backdrop,"soft");
        if (json_present(soft))
        {
            json_object(soft,"blur_radius|glow|ambient|pulse_amplitude|period_seconds");
            w->backdrop_blur=json_float(json_get(soft,"blur_radius"),1,MAX_BACKDROP_BLUR_RADIUS);
            w->backdrop_glow=json_float(json_get(soft,"glow"),0,MAX_BACKDROP_GLOW);
            w->backdrop_ambient=json_float(json_get(soft,"ambient"),0,MAX_BACKDROP_AMBIENT);
            w->backdrop_pulse=json_float(json_get(soft,"pulse_amplitude"),0,MAX_BACKDROP_PULSE_AMPLITUDE);
            w->backdrop_period=json_float(json_get(soft,"period_seconds"),1,MAX_BACKDROP_PERIOD_SECONDS);w->backdrop_soft=true;
        }
        Json wave=json_find(backdrop,"wave");
        if (json_present(wave))
        {
            json_object(wave,"amplitude|frequency|speed");
            w->backdrop_wave=Vec3{json_float(json_get(wave,"amplitude"),0,MAX_BACKDROP_WAVE_AMPLITUDE),
                json_positive(json_get(wave,"frequency"),MAX_BACKDROP_WAVE_FREQUENCY),json_positive(json_get(wave,"speed"),MAX_BACKDROP_WAVE_SPEED)};
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
        json_object(j,"type|owner|position|radius|tint|site|medium|seed");Body b={};b.availability=-1;
        Json seed=json_find(j,"seed");b.seed=json_present(seed) ? json_uint(seed,0,MAX_AUTHORED_SEED) : (w->body_count+1)*DEFAULT_BODY_SEED_MULTIPLIER;
        char type[ASSET_NAME_CAPACITY]={};Json name=json_get(j,"type");json_string(name,type,ASSET_NAME_CAPACITY);int index=content_body(&w->content,type);
        if (index<0) return json_error(name,"unknown body type reference");
        b.type=(uint32_t)index;strcpy(b.model_path,w->content.bodies[index].model_path);b.owner=read_owner(w,json_get(j,"owner"));
        b.position=json_vector(json_get(j,"position"),-MAX_PLACEMENT_COORDINATE,MAX_PLACEMENT_COORDINATE);b.radius=json_positive(json_get(j,"radius"),MAX_BODY_RADIUS);
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
            json_object(medium,"radius_fraction|edge_fade|scattering|absorption|anisotropy|depth_blur");
            Json blur=json_find(medium,"depth_blur");
            if (json_present(blur))
            {
                json_object(blur,"pixels_per_unit|sharp_distance|max_radius|start_fraction|full_fraction");
                b.interior_blur_fraction=json_present(json_find(blur,"start_fraction")) || json_present(json_find(blur,"full_fraction"));
                if (b.interior_blur_fraction)
                {
                    b.interior_blur_rate=json_float(json_get(blur,"start_fraction"),0,1);
                    b.interior_blur_sharp=json_float(json_get(blur,"full_fraction"),0,1);
                    if (b.interior_blur_sharp<=b.interior_blur_rate || json_present(json_find(blur,"pixels_per_unit")) || json_present(json_find(blur,"sharp_distance")))
                        json_error(blur,"fractional blur requires start < full and no distance fields");
                }
                else
                {
                    b.interior_blur_rate=json_float(json_get(blur,"pixels_per_unit"),0,32);
                    b.interior_blur_sharp=json_float(json_get(blur,"sharp_distance"),0,100);
                }
                b.interior_blur_max=json_float(json_get(blur,"max_radius"),0,32);
            }
            b.interior_radius=json_positive(json_get(medium,"radius_fraction"),1);
            b.interior_fade=json_float(json_get(medium,"edge_fade"),0,1);
            b.interior_scattering=json_color(json_get(medium,"scattering"),MAX_MEDIUM_SCATTERING);
            b.interior_absorption=json_float(json_get(medium,"absorption"),0,MAX_MEDIUM_ABSORPTION);
            Json anisotropy=json_get(medium,"anisotropy");b.interior_anisotropy=json_float(anisotropy,-MAX_MEDIUM_ANISOTROPY,MAX_MEDIUM_ANISOTROPY);
            if (std::fabs(b.interior_anisotropy)>=MAX_MEDIUM_ANISOTROPY) json_error(anisotropy,"anisotropy must be in (-.95,.95)");
        }
        w->bodies[w->body_count++]=b;
    }
    JsonArray spawns=json_array(json_get(root,"spawns"),MAX_SPAWNS);
    while (json_next(&spawns,&j))
    {
        json_object(j,"type|position");Spawn s={};Json name=json_get(j,"type");json_string(name,s.type_name,ASSET_NAME_CAPACITY);
        int index=content_unit(&w->content,s.type_name);if (index<0) return json_error(name,"unknown unit type reference");
        s.type=(uint32_t)index;s.position=json_vector(json_get(j,"position"),-MAX_PLACEMENT_COORDINATE,MAX_PLACEMENT_COORDINATE);w->spawns[w->spawn_count++]=s;
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
        if (world->neutral_start)
            for (uint32_t side=0;side<2;++side)
            {
                Faction faction=side ? world->opponent_faction : world->player_faction;bool founder=false;
                for (uint32_t i=0;i<world->spawn_count;++i)
                { const UnitDefinition& u=world->content.units[world->spawns[i].type];founder=founder || (u.faction==faction && u.colonizes); }
                if (!founder) { fprintf(stderr,"%s: each neutral-start side requires a craft that can establish a site\n",path);return false; }
            }
        for (uint32_t f=1;f<MAX_FACTIONS;++f) if (world->content.factions[f].declared
            && !world->content.factions[f].producer_name[0])
        { fprintf(stderr,"%s: gameplay factions require production definitions\n",path);return false; }
        for (uint32_t t=0;t<world->content.unit_count;++t) if (world->content.units[t].cost>world->gameplay.storage)
        { fprintf(stderr,"%s: storage must cover declared unit costs\n",path);return false; }
        for (uint32_t i=0;i<world->body_count;++i)
            if (world->content.bodies[world->bodies[i].type].hosts_site && (dot(world->bodies[i].colony_normal,world->bodies[i].colony_normal)<MIN_SITE_NORMAL_SQUARED_LENGTH || world->bodies[i].availability<0))
            { fprintf(stderr,"%s: production hosts require site and resource records\n",path);return false; }
    }
    if (world->sight_radius>0 && (!world->gameplay.enabled || (world->backdrop.model_path[0] && !world->backdrop_soft)))
    { fprintf(stderr,"%s: exploration requires gameplay and a soft backdrop\n",path);return false; }
    if ((world->studio_mask && world->studio_mask!=STUDIO_LIGHTS_COMPLETE)
        || (world->combat_fields && (world->combat_fields!=COMBAT_FIELDS_COMPLETE || !world->gameplay.enabled))
        || (world->combat.flight_enabled && world->combat_fields!=COMBAT_FIELDS_COMPLETE)
        || (world->backdrop_wave_set && !world->backdrop_soft))
    { fprintf(stderr,"%s: incomplete studio or combat settings, or wave without a soft backdrop\n",path);return false; }
    if ((world->spikes.enabled || world->spikes.recovery_set || world->spikes.injury_set || world->spikes.model[0])
        && (!world->spikes.enabled || !world->spikes.model[0] || !world->spikes.recovery_set || !world->spikes.injury_set
            || !world->combat.flight_enabled || world->combat_fields!=COMBAT_FIELDS_COMPLETE))
    { fprintf(stderr,"%s: spike combat requires flight, combat, recovery and injury settings\n",path);return false; }
    if (world->thin_clarity_set && !world->thin_transmission_set)
    { fprintf(stderr,"%s: thin clarity requires thin_transmission\n",path);return false; }
    if ((world->backdrop_samples || world->backdrop_soft) && !world->backdrop.model_path[0])
    { fprintf(stderr,"%s: background settings require a backdrop mesh\n",path);return false; }
    return true;
}

bool world_load(World* world,const char* path,uint32_t seed)
{
    JsonDocument d;if (!json_load(&d,path)) return false;Json root=json_root(&d);
    json_object(root,"$schema|content|rules|appearance|effects|match|bodies|spawns");
    World loaded={};loaded.ambient_top=loaded.ambient_bottom=Color{1,1,1};
    if (!json_path(json_get(root,"content"),path,loaded.content_path) || !content_load(&loaded.content,loaded.content_path)) return false;
    char file[ASSET_PATH_CAPACITY]={};
    Json rules=json_find(root,"rules");if (json_present(rules) && (!json_path(rules,path,file) || !load_rules(&loaded,file))) return false;
    Json effects=json_find(root,"effects");if (json_present(effects) && (!json_path(effects,path,file) || !load_effects(&loaded,file))) return false;
    if (!json_path(json_get(root,"appearance"),path,file) || !load_appearance(&loaded,file,seed)) return false;
    Json match=json_get(root,"match");json_object(match,"player|opponent|starts|auto_colonize|player_ai");
    Json automatic=json_find(match,"auto_colonize");if (json_present(automatic)) loaded.auto_colonize=json_bool(automatic);
    Json player_ai=json_find(match,"player_ai");if (json_present(player_ai)) loaded.player_ai=json_bool(player_ai);
    loaded.player_faction=read_owner(&loaded,json_get(match,"player"));loaded.opponent_faction=read_owner(&loaded,json_get(match,"opponent"));
    if (!loaded.player_faction || !loaded.opponent_faction || loaded.player_faction==loaded.opponent_faction)
        return json_error(match,"match requires two distinct non-neutral factions");
    if (!load_placements(&loaded,root)) return false;
    for (uint32_t p=0;p<loaded.body_count;++p) loaded.start_owner[p]=loaded.bodies[p].owner;
    Json starts=json_find(match,"starts");
    if (json_present(starts))
    {
        loaded.neutral_start=true;bool seen[MAX_FACTIONS]={};bool cells[MAX_BODIES]={};
        JsonArray entries=json_array(starts,2);Json entry={};uint32_t count=0;
        while (json_next(&entries,&entry))
        {
            json_object(entry,"faction|cell");Faction faction=read_owner(&loaded,json_get(entry,"faction"));
            uint32_t cell=json_uint(json_get(entry,"cell"),0,loaded.body_count ? loaded.body_count-1 : 0);
            if (!loaded.body_count || (faction!=loaded.player_faction && faction!=loaded.opponent_faction)
                || seen[faction] || cells[cell] || loaded.bodies[cell].owner!=FACTION_NEUTRAL
                || !loaded.content.bodies[loaded.bodies[cell].type].hosts_site) return json_error(entry,"starts require distinct match factions and neutral production cells");
            seen[faction]=true;cells[cell]=true;loaded.start_owner[cell]=faction;++count;
        }
        if (count!=2) return json_error(starts,"starts require both match factions");
        for (uint32_t p=0;p<loaded.body_count;++p) if (loaded.bodies[p].owner!=FACTION_NEUTRAL) return json_error(starts,"neutral starts cannot contain established factories");
    }
    if (!validate_world(&loaded,path)) return false;
    *world=loaded;return true;
}
