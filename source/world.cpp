#include "world.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include "vector_math.h"

enum { WORLD_LINE_SIZE = 512, SHAPE_VALUE_COUNT = 7 };

enum LineResult
{
    LINE_END,
    LINE_READY,
    LINE_TOO_LONG,
    LINE_ZERO_BYTE,
    LINE_IO_ERROR
};

static LineResult read_line(FILE* file, char* line)
{
    uint32_t length = 0;
    int character = fgetc(file);
    while (character != EOF && character != '\n')
    {
        if (character == 0)
            return LINE_ZERO_BYTE;
        if (length == WORLD_LINE_SIZE - 1)
            return LINE_TOO_LONG;
        line[length++] = (char)character;
        character = fgetc(file);
    }
    line[length] = 0;
    if (ferror(file))
        return LINE_IO_ERROR;
    return character == EOF && length == 0 ? LINE_END : LINE_READY;
}

static char* read_word(char** cursor)
{
    char* position = *cursor;
    while (isspace((unsigned char)*position))
        ++position;
    char* word = position;
    while (*position && !isspace((unsigned char)*position))
        ++position;
    if (*position)
        *position++ = 0;
    *cursor = position;
    return *word ? word : 0;
}

static bool read_values(char* cursor, float* values, uint32_t count = SHAPE_VALUE_COUNT)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        char* word = read_word(&cursor);
        if (!word)
            return false;
        char* end = 0;
        errno = 0;
        values[i] = strtof(word, &end);
        if (*end || errno == ERANGE || !std::isfinite(values[i]))
            return false;
    }
    return read_word(&cursor) == 0;
}

static bool read_shape(char* cursor, Vec3* position, float* radius, Color* color)
{
    float values[SHAPE_VALUE_COUNT];
    if (!read_values(cursor, values) || values[3] <= 0.0f)
        return false;
    for (uint32_t i = 4; i < SHAPE_VALUE_COUNT; ++i)
    {
        if (values[i] < 0.0f || values[i] > 1.0f)
            return false;
    }
    *position = Vec3{ values[0], values[1], values[2] };
    *radius = values[3];
    *color = Color{ values[4], values[5], values[6] };
    return true;
}

static bool read_owner(const char* word, Faction* owner)
{
    if (!word)
        return false;
    if (strcmp(word, "neutral") == 0)
        *owner = FACTION_NEUTRAL;
    else if (strcmp(word, "player") == 0 || strcmp(word,"virus")==0)
        *owner = FACTION_PLAYER;
    else if (strcmp(word, "enemy") == 0 || strcmp(word,"immune")==0)
        *owner = FACTION_ENEMY;
    else if (strcmp(word, "flowery") == 0)
        *owner = FACTION_FLOWERY;
    else if (strcmp(word, "crystalline") == 0)
        *owner = FACTION_CRYSTALLINE;
    else if (strcmp(word, "liquid_metal") == 0)
        *owner = FACTION_LIQUID_METAL;
    else if (strcmp(word, "insectoid") == 0)
        *owner = FACTION_INSECTOID;
    else if (strcmp(word, "rocky") == 0)
        *owner = FACTION_ROCKY;
    else if (strcmp(word, "robotic") == 0)
        *owner = FACTION_ROBOTIC;
    else
        return false;
    return true;
}

static const char* read_record(World* world, bool* has_star, uint32_t* volume_fades, char* line)
{
    char* comment = strchr(line, '#');
    if (comment)
        *comment = 0;
    char* cursor = line;
    char* kind = read_word(&cursor);
    if (!kind)
        return 0;
    if (!strcmp(kind,"ambient"))
    {
        float v[3];
        if (world->ambient_enabled || *has_star || !read_values(cursor,v,3)) return "ambient requires one RGB record and no star";
        for (uint32_t i=0;i<3;++i) if (v[i]<0 || v[i]>10000) return "ambient radiance must be in [0,10000]";
        world->ambient_enabled=true;world->ambient=Color{v[0],v[1],v[2]};return 0;
    }
    if (!strcmp(kind,"biology"))
    {
        char* side=read_word(&cursor);
        if (world->biology || !side || read_word(&cursor)) return "biology requires one virus or immune side";
        if (!strcmp(side,"virus")) { world->biology=BIOLOGY_VIRUS;world->player_faction=FACTION_PLAYER; }
        else if (!strcmp(side,"immune")) { world->biology=BIOLOGY_IMMUNE;world->player_faction=FACTION_ENEMY; }
        else return "biology side must be virus or immune";
        return 0;
    }
    if (!strcmp(kind,"ambient_gradient"))
    {
        float v[6];
        if (world->ambient_gradient || !read_values(cursor,v,6)) return "ambient_gradient requires one top RGB and bottom RGB record";
        for (uint32_t i=0;i<6;++i) if (v[i]<0 || v[i]>10000) return "ambient gradient weights must be in [0,10000]";
        world->ambient_gradient=true;world->ambient_top=Color{v[0],v[1],v[2]};world->ambient_bottom=Color{v[3],v[4],v[5]};return 0;
    }
    if (!strcmp(kind,"nutrients"))
    {
        float v[2];
        if (!read_values(cursor,v,2) || v[0]<0 || v[0]>=world->body_count || std::floor(v[0])!=v[0] || v[1]<0 || v[1]>1)
            return "nutrients requires a preceding cell index and availability in [0,1]";
        Cell* cell=&world->bodies[(uint32_t)v[0]];
        if (cell->nutrients>=0) return "duplicate cell nutrients";
        cell->nutrients=v[1];return 0;
    }
    if (!strcmp(kind,"backdrop"))
    {
        char* model=read_word(&cursor);float v[7];
        if (world->backdrop.model_path[0] || !model || strlen(model)>=sizeof(world->backdrop.model_path)
            || !read_values(cursor,v,7) || v[3]<=0 || v[3]>10000) return "backdrop requires relative mesh x y z scale r g b";
        for (uint32_t i=4;i<7;++i) if (v[i]<0 || v[i]>1) return "backdrop tint must be in [0,1]";
        strcpy(world->backdrop.model_path,model);world->backdrop.position=Vec3{v[0],v[1],v[2]};
        world->backdrop.scale=v[3];world->backdrop.color=Color{v[4],v[5],v[6]};return 0;
    }
    if (!strcmp(kind,"studio_light"))
    {
        char* role=read_word(&cursor);float v[7];int index=role && !strcmp(role,"key") ? 0 : role && !strcmp(role,"fill") ? 1 : role && !strcmp(role,"rim") ? 2 : -1;
        if (index<0 || (world->studio_mask & (1u<<index)) || !read_values(cursor,v,7)) return "studio_light requires unique key/fill/rim, direction XYZ, RGB and irradiance";
        Vec3 direction={v[0],v[1],v[2]};float squared=dot(direction,direction);
        if (squared<.0001f || !std::isfinite(squared) || v[6]<0 || v[6]>100) return "invalid studio direction or irradiance";
        for (uint32_t i=3;i<6;++i) if (v[i]<0 || v[i]>1) return "studio RGB must be in [0,1]";
        StudioLight* light=&world->studio[index];light->direction=scale(direction,1/std::sqrt(squared));
        light->color=Color{v[3],v[4],v[5]};light->irradiance=v[6];world->studio_mask|=1u<<index;return 0;
    }
    if (!strcmp(kind,"cell_translucency"))
    {
        float v[2];if (world->cell_transmission_set || !read_values(cursor,v,2) || v[0]<0 || v[0]>.9f || v[1]<0 || v[1]>10)
            return "cell_translucency requires transmission [0,.9] and absorption [0,10] once";
        world->cell_transmission_set=true;world->cell_transmission=v[0];world->cell_absorption=v[1];return 0;
    }
    if (!strcmp(kind,"backdrop_wave"))
    {
        float v[3];if (world->backdrop_wave_set || !read_values(cursor,v,3) || v[0]<0 || v[0]>.1f || v[1]<=0 || v[1]>100 || v[2]<=0 || v[2]>10)
            return "backdrop_wave requires amplitude [0,.1], frequency (0,100] and speed (0,10] once";
        world->backdrop_wave_set=true;world->backdrop_wave=Vec3{v[0],v[1],v[2]};return 0;
    }
    if (!strcmp(kind,"combat_visuals"))
    {
        float v[8];if ((world->combat_fields&1) || !read_values(cursor,v,8)) return "combat_visuals requires eight positive values once";
        for (uint32_t i=0;i<8;++i) if (v[i]<=0 || v[i]>100) return "invalid combat visual value";
        if (v[5]>=1 || v[2]>1 || v[3]>5 || v[4]>2 || v[6]>2) return "combat visual lifetime/width/health fraction exceeds limits";
        CombatVisuals* c=&world->combat;c->enabled=true;c->projectile_speed=v[0];c->shot_interval=v[1];c->trail_width=v[2];c->trail_lifetime=v[3];
        c->flash_seconds=v[4];c->low_health=v[5];c->pop_seconds=v[6];c->pop_radius=v[7];world->combat_fields|=1;return 0;
    }
    if (!strcmp(kind,"spike_weapon"))
    {
        char* path=read_word(&cursor);float v[5];
        if (world->spikes.enabled || !path || strlen(path)>=sizeof(world->spikes.model) || !read_values(cursor,v,5)
            || v[0]<1 || v[0]>32 || v[0]!=std::floor(v[0]) || v[1]<=0 || v[1]>1 || v[2]<=0 || v[2]>2
            || v[3]<=0 || v[3]>60 || v[4]<=0 || v[4]>1) return "spike_weapon requires mesh, count [1,32], root (0,1], length (0,2], regrow seconds (0,60], projectile scale (0,1]";
        SpikeRules* r=&world->spikes;r->enabled=true;strcpy(r->model,path);r->count=(uint32_t)v[0];
        r->root=v[1];r->length=v[2];r->regrow=v[3];r->scale=v[4];return 0;
    }
    if (!strcmp(kind,"recovery_rules"))
    {
        float v[3];SpikeRules* r=&world->spikes;
        if (r->recovery_set || !read_values(cursor,v,3) || v[0]<=0 || v[0]>60 || v[1]<0 || v[1]>60 || v[2]<=0 || v[2]>10)
            return "recovery_rules requires heal interval (0,60], damage delay [0,60], orbit clearance (0,10] once";
        r->recovery_set=true;r->heal_interval=v[0];r->heal_delay=v[1];r->heal_clearance=v[2];return 0;
    }
    if (!strcmp(kind,"injury_colors"))
    {
        float v[6];if (world->spikes.injury_set || !read_values(cursor,v,6)) return "injury_colors requires two RGB triples once";
        for (uint32_t i=0;i<6;++i) if (v[i]<0 || v[i]>1) return "injury colors must be in [0,1]";
        world->spikes.injury_set=true;world->spikes.injury[0]=Color{v[0],v[1],v[2]};world->spikes.injury[1]=Color{v[3],v[4],v[5]};return 0;
    }
    if (!strcmp(kind,"cell_clarity"))
    {
        float v[1];if (world->cell_clarity_set || !read_values(cursor,v,1) || v[0]<0 || v[0]>1) return "cell_clarity requires [0,1] once";
        world->cell_clarity_set=true;world->cell_clarity=v[0];return 0;
    }
    if (!strcmp(kind,"ai_production"))
    {
        float v[2];if (world->ai_production_set || !read_values(cursor,v,2) || v[0]<=0 || v[0]>1 || v[1]<0 || v[1]>300)
            return "ai_production requires rate (0,1] and neutral-expansion delay [0,300] once";
        world->ai_production_set=true;world->ai_production=v[0];world->ai_expansion=v[1];return 0;
    }
    if (!strcmp(kind,"flight_rules"))
    {
        float v[4];
        if (world->combat.flight_enabled || !read_values(cursor,v,4) || v[0]<=0 || v[0]>100 || v[1]<=0 || v[1]>30
            || v[2]<=0 || v[2]>3 || v[3]<0 || v[3]>1) return "flight_rules requires acceleration (0,100], turn rate (0,30], separation (0,3], bank [0,1] once";
        world->combat.flight_enabled=true;world->combat.acceleration=v[0];world->combat.turn_rate=v[1];
        world->combat.separation=v[2];world->combat.bank=v[3];return 0;
    }
    if (!strcmp(kind,"orbit_rules"))
    {
        float v[2];if ((world->combat_fields&2) || !read_values(cursor,v,2) || v[0]<=0 || v[0]>5 || v[1]<=0 || v[1]>2) return "orbit_rules requires clearance (0,5] and speed (0,2] once";
        world->combat.orbit_clearance=v[0];world->combat.orbit_speed=v[1];world->combat_fields|=2;return 0;
    }
    if (!strcmp(kind,"combat_color") || !strcmp(kind,"damage_colors"))
    {
        bool damage=!strcmp(kind,"damage_colors");Faction faction=FACTION_NEUTRAL;
        if (!damage && (!read_owner(read_word(&cursor),&faction) || (faction!=FACTION_PLAYER && faction!=FACTION_ENEMY))) return "combat_color requires virus or immune RGB";
        uint32_t flag=damage ? 16u : 2u<<(uint32_t)faction;float v[6];
        if ((world->combat_fields&flag) || !read_values(cursor,v,damage ? 6 : 3)) return "duplicate or malformed combat palette";
        for (uint32_t i=0;i<(damage ? 6u : 3u);++i) if (v[i]<0 || v[i]>1) return "combat RGB must be in [0,1]";
        if (damage) { world->combat.flash=Color{v[0],v[1],v[2]};world->combat.injured=Color{v[3],v[4],v[5]}; }
        else world->combat.faction[faction]=Color{v[0],v[1],v[2]};
        world->combat_fields|=flag;return 0;
    }
    if (!strcmp(kind,"backdrop_soft"))
    {
        float v[5];
        if (world->backdrop_soft || !read_values(cursor,v,5)
            || v[0]<1 || v[0]>256 || v[1]<0 || v[1]>20 || v[2]<0 || v[2]>10
            || v[3]<0 || v[3]>.15f || v[4]<1 || v[4]>120)
            return "backdrop_soft requires blur [1,256], glow [0,20], ambient [0,10], pulse [0,.15], period [1,120]";
        world->backdrop_soft=true;world->backdrop_blur=v[0];world->backdrop_glow=v[1];
        world->backdrop_ambient=v[2];world->backdrop_pulse=v[3];world->backdrop_period=v[4];return 0;
    }
    if (!strcmp(kind,"backdrop_samples"))
    {
        float v;
        if (world->backdrop_samples || !read_values(cursor,&v,1) || v<1 || v>4096 || std::floor(v)!=v)
            return "backdrop_samples requires one integer in [1,4096]";
        world->backdrop_samples=(uint32_t)v;return 0;
    }

    GameplayRules* g=&world->gameplay;
    struct RuleRecord { const char* name; float* fields[5]; uint32_t count, flag; };
    RuleRecord rules[] = {
        {"fighter_rules", {&g->fighter_speed,&g->fighter_health,&g->fighter_damage,&g->fighter_range,&g->fighter_cost}, 5, 4},
        {"seedship_rules", {&g->seed_speed,&g->seed_health,&g->seed_cost,0,0}, 3, 8},
        {"stalk_rules", {&g->growth_seconds,&g->stalk_health,&g->collection_rate,&g->stalk_height,&g->stalk_width}, 5, 16},
        {"economy_rules", {&g->storage,&g->manufacture_seconds,&g->landing_seconds,&g->enemy_attack_seconds,0}, 4, 32}
    };
    for (uint32_t r = 0; r < 4; ++r)
        if (strcmp(kind, rules[r].name) == 0)
        {
            if (*volume_fades & rules[r].flag) return "duplicate gameplay rules";
            float values[5];
            if (!read_values(cursor, values, rules[r].count)) return "invalid gameplay rules";
            for (uint32_t i = 0; i < rules[r].count; ++i)
                if (values[i] <= 0 || values[i] > 10000) return "gameplay values must be finite in (0,10000]";
            for (uint32_t i=0;i<rules[r].count;++i) *rules[r].fields[i]=values[i];
            *volume_fades |= rules[r].flag;
            world->gameplay.enabled = true;
            return 0;
        }
    if (strcmp(kind, "stalk_model") == 0)
    {
        char* model = read_word(&cursor);
        if (world->gameplay.stalk_model[0]) return "duplicate stalk model";
        if (!model || strlen(model) >= sizeof(world->gameplay.stalk_model) || read_word(&cursor))
            return "stalk_model requires one relative mesh path";
        strcpy(world->gameplay.stalk_model,model);
        world->gameplay.enabled = true;
        return 0;
    }
    if (strcmp(kind, "colony_site") == 0)
    {
        float v[4];
        if (!read_values(cursor,v,4) || v[0] < 0 || v[0] >= world->body_count || std::floor(v[0]) != v[0])
            return "colony_site requires a preceding planet index and outward normal";
        Cell* planet = &world->bodies[(uint32_t)v[0]];
        Vec3 normal = {v[1],v[2],v[3]};
        float length2 = dot(normal,normal);
        if (dot(planet->colony_normal,planet->colony_normal) > 0 || length2 < 0.01f || length2 > 10000)
            return "invalid or duplicate colony site normal";
        planet->colony_normal = normalize(normal);
        return 0;
    }

    if (strcmp(kind, "sun_orbit") == 0)
    {
        float values[7];
        if (world->orbit_period != 0.0f) return "duplicate sun_orbit";
        if (!read_values(cursor, values, 7) || values[6] <= 0.0f)
            return "invalid sun_orbit: expected center_xyz axis_xyz positive_period_seconds";
        Vec3 axis = {values[3], values[4], values[5]};
        float length_squared = dot(axis, axis);
        if (!std::isfinite(length_squared) || length_squared < 1e-12f)
            return "sun_orbit axis must be finite and nonzero";
        world->orbit_center = Vec3{values[0], values[1], values[2]};
        world->orbit_axis = normalize(axis);
        world->orbit_period = values[6];
        return 0;
    }
    if (strcmp(kind, "ship") == 0)
    {
        if (world->ship_count == MAX_SHIPS) return "world exceeds ship capacity";
        Ship ship = {};
        if (!read_owner(read_word(&cursor), &ship.faction) || ship.faction == FACTION_NEUTRAL)
            return "invalid ship faction";
        char* size = read_word(&cursor);
        if (!size || (strcmp(size, "large") && strcmp(size, "small")))
            return "ship size must be large or small";
        ship.large = strcmp(size, "large") == 0;
        char* model = read_word(&cursor);
        float v[8];
        if (!model || strlen(model) >= sizeof(ship.model_path) || !read_values(cursor, v, 8)
            || v[3] <= 0 || v[3] > 1000000 || std::fabs(v[4]) > 360000
            || std::fabs(v[0]) > 1000000 || std::fabs(v[1]) > 1000000 || std::fabs(v[2]) > 1000000
            || v[5] < 0 || v[6] < 0 || v[7] < 0 || v[5] > 1 || v[6] > 1 || v[7] > 1)
            return "invalid ship: expected faction size relative_mesh_path x y z scale yaw_degrees r g b";
        strcpy(ship.model_path, model);
        ship.position = Vec3{v[0], v[1], v[2]};
        ship.scale = v[3];
        ship.yaw_degrees = v[4];
        ship.color = Color{v[5], v[6], v[7]};
        world->ships[world->ship_count++] = ship;
        return 0;
    }
    if (strcmp(kind, "haze_edge_fade") == 0 || strcmp(kind, "global_atmosphere_edge_fade") == 0)
    {
        bool global = strcmp(kind, "global_atmosphere_edge_fade") == 0;
        uint32_t flag = global ? 2u : 1u;
        float value;
        if ((global ? world->global_atmosphere_radius : world->haze_radius) <= 0.0f)
            return "volume edge fade must follow its haze or global_atmosphere volume";
        if (*volume_fades & flag) return "duplicate volume edge fade";
        if (!read_values(cursor, &value, 1) || value < 0.0f || value > 1.0f)
            return "volume edge fade must be one finite fraction in [0, 1]";
        if (global) world->global_atmosphere_edge_fade = value;
        else world->haze_edge_fade = value;
        *volume_fades |= flag;
        return 0;
    }
    if (strcmp(kind, "haze") == 0 || strcmp(kind, "global_atmosphere") == 0)
    {
        bool global = strcmp(kind, "global_atmosphere") == 0;
        float values[8];
        if ((global ? world->global_atmosphere_radius : world->haze_radius) != 0.0f)
            return "duplicate atmosphere/dust volume";
        if (!read_values(cursor, values, 8) || values[3] <= 0.0f
            || values[3] > 1000000.0f
            || values[4] < 0 || values[5] < 0 || values[6] < 0
            || values[4] > 1000 || values[5] > 1000 || values[6] > 1000
            || std::fabs(values[7]) >= 0.95f)
            return "invalid haze: expected center_xyz radius scattering_rgb anisotropy (-0.95, 0.95)";
        if (global)
        {
            world->global_atmosphere_center = Vec3{values[0], values[1], values[2]};
            world->global_atmosphere_radius = values[3];
            world->global_atmosphere_scattering = Color{values[4], values[5], values[6]};
            world->global_atmosphere_anisotropy = values[7];
        }
        else
        {
            world->haze_center = Vec3{values[0], values[1], values[2]};
            world->haze_radius = values[3];
            world->haze_scattering = Color{values[4], values[5], values[6]};
            world->haze_anisotropy = values[7];
        }
        return 0;
    }
    if (strcmp(kind, "model") == 0 || strcmp(kind, "atmosphere") == 0)
    {
        char* word = read_word(&cursor);
        if (!word || word[0] < '0' || word[0] > '9') return "expected an existing body index";
        char* end = 0;
        errno = 0;
        unsigned long index = strtoul(word, &end, 10);
        if (*end || errno || index >= world->body_count) return "expected an existing body index";
        Cell* planet = &world->bodies[index];
        if (strcmp(kind, "model") == 0)
        {
            char* path = read_word(&cursor);
            if (planet->model_path[0]) return "duplicate model for body";
            if (!path || strlen(path) >= sizeof(planet->model_path) || read_word(&cursor))
                return "invalid model: expected body_index relative_mesh_path";
            strcpy(planet->model_path, path);
        }
        else
        {
            float values[8];
            if (planet->atmosphere_height > 0.0f) return "duplicate atmosphere for body";
            if (!read_values(cursor, values, 8) || values[0] <= 0.0f || values[0] > 1.0f
                || values[1] <= 0.0f || values[1] > values[0]
                || values[2] < 0 || values[3] < 0 || values[4] < 0 || values[5] < 0
                || values[6] < 0 || std::fabs(values[7]) >= 0.95f)
                return "invalid atmosphere: expected index height scale_height rayleigh_rgb mie absorption anisotropy";
            for (uint32_t i = 2; i <= 6; ++i)
                if (values[i] > 1000.0f) return "atmosphere coefficients must be no greater than 1000";
            planet->atmosphere_height = values[0];
            planet->atmosphere_scale_height = values[1];
            planet->rayleigh = Color{values[2], values[3], values[4]};
            planet->mie = values[5];
            planet->absorption = values[6];
            planet->anisotropy = values[7];
        }
        return 0;
    }

    if (strcmp(kind, "star") == 0)
    {
        if (*has_star || world->ambient_enabled)
            return "duplicate star";
        if (!read_shape(cursor, &world->sun.position, &world->sun.radius, &world->sun.color))
            return "invalid star fields: expected x y z radius r g b";
        *has_star = true;
        return 0;
    }

    Cell planet = {};
    planet.nutrients=-1;
    if (strcmp(kind, "planet") == 0 || strcmp(kind,"cell")==0)
        planet.kind = BODY_CELL;
    else if (strcmp(kind, "asteroid") == 0)
        planet.kind = BODY_ASTEROID;
    else
        return "unknown record: expected star, planet, or asteroid";

    if (world->body_count == MAX_BODIES)
        return "world exceeds body capacity";
    if (!read_owner(read_word(&cursor), &planet.owner))
        return "invalid owner: expected neutral, player/enemy, or a named faction";
    if (!read_shape(cursor, &planet.position, &planet.radius, &planet.color))
        return "invalid body fields: expected x y z radius r g b";
    world->bodies[world->body_count++] = planet;
    return 0;
}

static bool read_world(FILE* file, const char* path, World* world)
{
    static const char* line_errors[] = {
        "", "", "line exceeds 511 characters", "NUL byte in world file", "file read failed"
    };
    bool has_star = false;
    uint32_t volume_fades = 0;
    uint32_t line_number = 0;
    char line[WORLD_LINE_SIZE];
    for (;;)
    {
        LineResult result = read_line(file, line);
        if (result == LINE_END)
            break;
        ++line_number;
        const char* error = result == LINE_READY
            ? read_record(world, &has_star, &volume_fades, line) : line_errors[result];
        if (error)
        {
            fprintf(stderr, "%s:%u: %s\n", path, line_number, error);
            return false;
        }
    }
    if (world->gameplay.enabled)
    {
        if ((volume_fades & 60u) != 60u || !world->gameplay.stalk_model[0])
        {
            fprintf(stderr,"%s: gameplay requires fighter_rules, seedship_rules, stalk_rules, economy_rules and stalk_model\n",path);
            return false;
        }
        if (world->gameplay.storage<std::fmax(world->gameplay.fighter_cost,world->gameplay.seed_cost))
        { fprintf(stderr,"%s: resource storage must cover either unit cost\n",path);return false; }
        for (uint32_t i = 0; i < world->body_count; ++i)
            if (dot(world->bodies[i].colony_normal,world->bodies[i].colony_normal) < .9f)
            {
                fprintf(stderr,"%s: gameplay planets require colony_site metadata\n",path);
                return false;
            }
    }
    if (world->ambient_enabled && world->orbit_period>0)
    { fprintf(stderr,"%s: ambient-only scenes cannot have a sun orbit\n",path);return false; }
    if ((world->studio_mask && (world->studio_mask!=7 || !world->ambient_enabled))
        || (world->combat_fields && (world->combat_fields!=31 || !world->gameplay.enabled))
        || (world->combat.flight_enabled && world->combat_fields!=31)
        || (world->backdrop_wave_set && !world->backdrop_soft))
    { fprintf(stderr,"%s: incomplete studio lights/combat settings, or wave without a soft backdrop\n",path);return false; }
    if ((world->spikes.enabled || world->spikes.recovery_set || world->spikes.injury_set)
        && (!world->spikes.enabled || !world->spikes.recovery_set || !world->spikes.injury_set
            || !world->biology || !world->combat.flight_enabled || world->combat_fields!=31))
    { fprintf(stderr,"%s: spike combat requires biology, flight, combat, recovery and injury settings\n",path);return false; }
    if (world->cell_clarity_set && !world->cell_transmission_set)
    { fprintf(stderr,"%s: cell clarity requires cell_translucency\n",path);return false; }
    if (world->ai_production_set && !world->biology)
    { fprintf(stderr,"%s: AI production multiplier requires biology\n",path);return false; }
    if (world->cell_transmission_set && !world->ambient_enabled)
    { fprintf(stderr,"%s: cell translucency requires an ambient/studio scene\n",path);return false; }
    if (world->ambient_gradient && !world->ambient_enabled)
    { fprintf(stderr,"%s: ambient gradient needs ambient lighting\n",path);return false; }
    if (world->backdrop_samples && !world->backdrop.model_path[0])
    { fprintf(stderr,"%s: backdrop_samples requires a backdrop model\n",path);return false; }
    if (world->backdrop_soft && (!world->backdrop.model_path[0] || !world->ambient_enabled))
    { fprintf(stderr,"%s: backdrop_soft requires a backdrop mesh and ambient lighting\n",path);return false; }
    if (world->biology)
    {
        if (!world->ambient_enabled || !world->gameplay.enabled)
        { fprintf(stderr,"%s: biology needs ambient lighting and gameplay rules\n",path);return false; }
        for (uint32_t i=0;i<world->body_count;++i)
            if (world->bodies[i].kind!=BODY_CELL || world->bodies[i].nutrients<0)
            { fprintf(stderr,"%s: biological cells need nutrient availability\n",path);return false; }
    }
    if ((!has_star && !world->ambient_enabled) || (world->body_count == 0 && world->ship_count == 0))
    {
        fprintf(stderr, "%s:%u: expected one star and at least one body or ship\n", path, line_number);
        return false;
    }
    return true;
}

bool world_load(World* world, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "%s: cannot open world: %s\n", path, strerror(errno));
        return false;
    }
    World loaded = {};
    loaded.player_faction=FACTION_PLAYER;
    loaded.ambient_top=loaded.ambient_bottom=Color{1,1,1};
    bool success = read_world(file, path, &loaded);
    if (fclose(file) != 0)
    {
        fprintf(stderr, "%s: cannot close world: %s\n", path, strerror(errno));
        return false;
    }
    if (success)
    {
        const char* slash = strrchr(path, '/');
        const char* backslash = strrchr(path, '\\');
        if (backslash && (!slash || backslash > slash)) slash = backslash;
        size_t directory_length = slash ? (size_t)(slash - path + 1) : 0;
        for (uint32_t i = 0; i < loaded.body_count + loaded.ship_count + 3; ++i)
        {
            char* model = i < loaded.body_count ? loaded.bodies[i].model_path
                : i < loaded.body_count + loaded.ship_count ? loaded.ships[i - loaded.body_count].model_path
                : i==loaded.body_count+loaded.ship_count ? loaded.gameplay.stalk_model : i==loaded.body_count+loaded.ship_count+1 ? loaded.backdrop.model_path : loaded.spikes.model;
            if (!model[0]) continue;
            char resolved[sizeof(loaded.bodies[i].model_path)];
            if (model[0] == '/' || model[0] == '\\' || strchr(model, ':')
                || directory_length + strlen(model) >= sizeof(resolved))
            {
                fprintf(stderr, "%s: model paths must be relative and fit 255 characters\n", path);
                return false;
            }
            memcpy(resolved, path, directory_length);
            strcpy(resolved + directory_length, model);
            strcpy(model, resolved);
        }
        *world = loaded;
    }
    return success;
}
