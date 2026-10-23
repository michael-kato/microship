#include "content.h"
#include "asset_json.h"
#include "asset_limits.h"
#include "vector_math.h"
#include <cmath>
#include <stdio.h>
#include <string.h>

static const uint32_t DEFAULT_HYPHA_CHAIN_LENGTH = 3;
static const float DEFAULT_FACTORY_TINT_STRENGTH = .25f;
static const uint32_t MAX_FACTORY_STEM_BULGES = 8;
static const int MAX_BUILDING_SEED = 16777215;
static const int MAX_BUILDING_PRODUCTION = 10;
static const int MAX_BUILDING_SPACING = 10;
static const float MAX_BRANCH_SPREAD_SCALE = 3;
static const int MAX_BRANCH_EMERGENCE_SECONDS = 300;
static const int MAX_BUILDING_WIDTH = 10000;
static const int MAX_BUILDING_HEIGHT = 10000;
static const int MAX_BUILDING_COLLECTION = 10000;
static const int MAX_BUILDING_HEALTH = 10000;
static const int MAX_BUILDING_GROWTH_SECONDS = 10000;
static const int MAX_BUILDING_HEAD_RADIUS = 10;
static const int MAX_BUILDING_STEM_RADIUS = 10;
static const int MAX_UNIT_TARGETS = 2;
static const int MAX_UNIT_INTERVAL_FACTOR = 100;
static const int MAX_UNIT_CUE_RADIUS = 1000;
static const int MAX_UNIT_PICK_RADIUS = 1000;
static const int MAX_UNIT_SEPARATION = 10000;
static const int MAX_UNIT_SHOT_INTERVAL = 10000;
static const int MAX_UNIT_COST = 10000;
static const int MAX_UNIT_RESPONSE_SECONDS = 10;
static const int MAX_UNIT_LEASH_RADIUS = 100;
static const int MAX_UNIT_COLONIZE_RADIUS = 100;
static const int MAX_UNIT_STRUCTURE_RADIUS = 100;
static const int MAX_UNIT_TARGET_RADIUS = 100;
static const int MAX_UNIT_RANGE = 10000;
static const int MAX_UNIT_HEALTH = 10000;
static const int MAX_UNIT_SPEED = 10000;
static const int MAX_UNIT_YAW_DEGREES = 360000;
static const int MAX_UNIT_SCALE = 1000;
static const int MAX_UNIT_LIFETIME = 300;
static const int MAX_UNIT_CLOUD_RADIUS = 20;
static const int MAX_UNIT_CLOUD_SECONDS = 120;
static const int MAX_UNIT_CLOUD_DAMAGE = 1000;
static const int MAX_UNIT_BROOD = 64;
static const int MAX_UNIT_STUN_SECONDS = 60;
static const float MIN_CELL_DISPLACEMENT_WIDTH_DEGREES = 6;
static const double MAX_CELL_INTERIOR_VARIATION = .15;
static const double MAX_CELL_SHAPE_VARIATION = .12;
static const int MAX_NORMAL_COMPONENT = 100;
static const float MIN_NORMAL_SQUARED_LENGTH = .01f;
static const int MAX_NORMAL_SQUARED_LENGTH = 10000;
static const int MAX_ESTABLISHMENT_SECONDS = 10000;
static const int MAX_BRANCH_HEALTH = 10000;
static const int MAX_BRANCH_REGROW_SECONDS = 300;

int content_faction(const Content* c, const char* name)
{
	if (!name)
		return -1;
	if (!strcmp(name, "neutral"))
		return 0;
	for (uint32_t i = 1; i < MAX_FACTIONS; ++i)
		if (c->factions[i].declared && !strcmp(c->factions[i].name, name))
			return (int)i;
	return -1;
}
int content_group(const Content* c, const char* name)
{
	if (!name)
		return -1;
	for (uint32_t i = 0; i < MAX_GROUPS; ++i)
		if (c->groups[i].declared && (!strcmp(c->groups[i].key, name) ||
		                              !strcmp(c->groups[i].selection, name)))
			return (int)i;
	return -1;
}
int content_unit(const Content* c, const char* name)
{
	for (uint32_t i = 0; name && i < c->unit_count; ++i)
		if (!strcmp(c->units[i].name, name))
			return (int)i;
	return -1;
}
int content_body(const Content* c, const char* name)
{
	for (uint32_t i = 0; name && i < c->body_count; ++i)
		if (!strcmp(c->bodies[i].name, name))
			return (int)i;
	return -1;
}
int content_product(const Content* c, Faction faction, int group)
{
	const FactionDefinition* f = &c->factions[faction];
	if (f->producer_name[0] && (int)c->units[f->default_unit].group == group)
		return (int)f->default_unit;
	if (f->producer_name[0] && (int)c->units[f->assault_unit].group == group)
		return (int)f->assault_unit;
	return -1;
}
int content_producer(const Content* c, const char* name)
{
	for (uint32_t i = 0; name && i < c->producer_count; ++i)
		if (!strcmp(c->producers[i].name, name))
			return (int)i;
	return -1;
}
Color content_color(const Content* c, Faction faction)
{
	return c->factions[faction].color;
}
static bool definition_name(Json j, char* name)
{
	return json_string(json_get(j, "name"), name, ASSET_NAME_CAPACITY);
}
static Vec3 normal(Json j)
{
	Vec3 n = json_vector(j, -MAX_NORMAL_COMPONENT, MAX_NORMAL_COMPONENT);
	float length = dot(n, n);
	if (length < MIN_NORMAL_SQUARED_LENGTH ||
	    length > MAX_NORMAL_SQUARED_LENGTH)
		json_error(j, "normal must have squared length in [.01,10000]");
	return length > 0 ? normalize(n) : n;
}
static bool load_bodies(Content* c, Json root, const char* path)
{
	json_object(root, "$schema|shared|bodies");
	Json shared = json_find(root, "shared");
	JsonArray a = json_array(json_get(root, "bodies"), MAX_BODY_TYPES);
	Json j = {};
	while (json_next(&a, &j))
	{
		json_object(j, "name|label|geometry|model|hosts_site|variation");
		BodyDefinition b = {};
		char geometry[ASSET_NAME_CAPACITY] = {};
		definition_name(j, b.name);
		json_string(json_get(j, "label"), b.label, ASSET_NAME_CAPACITY);
		json_string(json_get(j, "geometry"), geometry, ASSET_NAME_CAPACITY);
		b.hosts_site = json_bool(json_get(j, "hosts_site"));
		if (!strcmp(geometry, "mesh"))
			json_path(json_get(j, "model"), path, b.model_path);
		else if (strcmp(geometry, "sphere") ||
		         json_present(json_find(j, "model")))
			json_error(j, "geometry must be mesh with a model, or sphere "
			              "without a model");
		Json variation = json_find(j, "variation");
		if (json_present(variation))
		{
			json_object(variation, "parts");
			json_object(shared,
			            "shape|interior|wall_noise|layout_rotation_degrees");
			Json noise = json_find(shared, "wall_noise");
			if (json_present(noise))
			{
				json_object(noise, "strength|frequency|warm|cool");
				b.wall_noise_strength =
				    json_float(json_get(noise, "strength"), 0, 1);
				b.wall_noise_frequency =
				    json_positive(json_get(noise, "frequency"), 32);
				b.wall_noise_warm = json_color(json_get(noise, "warm"));
				b.wall_noise_cool = json_color(json_get(noise, "cool"));
			}
			json_path(json_get(variation, "parts"), path, b.parts_path);
			b.shape_variation = json_float(json_get(shared, "shape"), 0,
			                               MAX_CELL_SHAPE_VARIATION);
			b.part_variation = json_float(json_get(shared, "interior"), 0,
			                              MAX_CELL_INTERIOR_VARIATION);
			Json rotation = json_find(shared, "layout_rotation_degrees");
			if (json_present(rotation))
				b.layout_rotation =
				    json_float(rotation, 0, 180) * .01745329252f;
			if (!b.model_path[0])
				json_error(variation, "variation requires a mesh");
		}
		if (content_body(c, b.name) >= 0)
			return json_error(j, "duplicate body definition");
		c->bodies[c->body_count++] = b;
	}
	return root.document->valid;
}
static bool load_personality(FactionDefinition* f, Json reference,
                             const char* origin)
{
	char path[ASSET_PATH_CAPACITY] = {};
	if (!json_path(reference, origin, path))
		return false;
	JsonDocument doc;
	if (!json_load(&doc, path))
		return false;
	Json root = json_root(&doc);
	json_object(root, "$schema|motion|dialogue|sound_bank");
	json_string(json_get(root, "sound_bank"), f->audio_bank,
	            sizeof(f->audio_bank));
	Json motion = json_get(root, "motion");
	json_object(
	    motion,
	    "acceleration|turn_rate|burst|wander|roll_degrees|period|cadence|"
	    "organelle_amount|organelle_period|factory_amount|factory_period");
	struct Field
	{
		const char* name;
		float* value;
		float low, high;
	};
	FactionMotion& m = f->motion;
	Field fields[] = {{"acceleration", &m.acceleration, .1f, 4},
	                  {"turn_rate", &m.turn_rate, .1f, 4},
	                  {"burst", &m.burst, 0, .8f},
	                  {"wander", &m.wander, 0, 5},
	                  {"roll_degrees", &m.roll, 0, 30},
	                  {"period", &m.period, .1f, 30},
	                  {"cadence", &m.cadence, 0, 1},
	                  {"organelle_amount", &m.organelle_amount, 0, 3},
	                  {"organelle_period", &m.organelle_period, .1f, 4},
	                  {"factory_amount", &m.factory_amount, 0, 3},
	                  {"factory_period", &m.factory_period, .1f, 4}};
	for (uint32_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
		*fields[i].value = json_float(json_get(motion, fields[i].name),
		                              fields[i].low, fields[i].high);
	m.roll *= .01745329252f;
	m.enabled = true;
	Json voice = json_get(root, "dialogue");
	json_object(voice, "ships|cells|milestones");
	json_string(json_get(voice, "ships"), f->dialogue.ships,
	            FACTION_LINE_CAPACITY);
	json_string(json_get(voice, "cells"), f->dialogue.cells,
	            FACTION_LINE_CAPACITY);
	JsonArray a = json_array(json_get(voice, "milestones"), FACTION_MILESTONES);
	Json line = {};
	uint32_t seen = 0;
	while (json_next(&a, &line))
	{
		json_object(line, "cells|line");
		uint32_t cells = json_uint(json_get(line, "cells"), 10, 90);
		if (cells % 10)
			return json_error(
			    line, "dialogue cell thresholds must be multiples of ten");
		uint32_t index = cells / 10 - 1;
		if (seen & (1u << index))
			return json_error(line, "duplicate dialogue threshold");
		seen |= 1u << index;
		json_string(json_get(line, "line"), f->dialogue.milestones[index],
		            FACTION_LINE_CAPACITY);
	}
	if (seen != (1u << FACTION_MILESTONES) - 1)
		return json_error(voice, "dialogue requires thresholds 10 through 90");
	f->dialogue.enabled = true;
	return doc.valid;
}

static bool load_factions(Content* c, Json root, const char* path)
{
	json_object(root, "$schema|factions");
	JsonArray a = json_array(json_get(root, "factions"), MAX_FACTIONS - 1);
	Json j = {};
	while (json_next(&a, &j))
	{
		json_object(
		    j, "id|name|label|color|production|cell_appearance|personality");
		FactionDefinition f = {};
		Json personality = json_find(j, "personality");
		if (json_present(personality) &&
		    !load_personality(&f, personality, path))
			return false;
		Json appearance = json_find(j, "cell_appearance");
		if (json_present(appearance))
		{
			json_object(appearance,
			            "tint|strength|roundness|displacement");
			f.cell_tint = json_color(json_get(appearance, "tint"));
			f.cell_strength =
			    json_float(json_get(appearance, "strength"), 0, 1);
			Json roundness = json_find(appearance, "roundness");
			if (json_present(roundness))
				f.cell_roundness = json_float(roundness, 0, 1);
			Json displacement = json_find(appearance, "displacement");
			if (json_present(displacement))
			{
				json_object(displacement,
				            "features|height|width_degrees|profile|seed");
				f.displacement_features =
				    json_uint(json_get(displacement, "features"), 1,
					          MAX_CELL_DISPLACEMENT_FEATURES);
				f.displacement_seed =
				    json_uint(json_get(displacement, "seed"), 0, 16777215);
				f.displacement_height =
				    json_positive(json_get(displacement, "height"), .2f);
				f.displacement_width =
				    json_float(json_get(displacement, "width_degrees"),
					           MIN_CELL_DISPLACEMENT_WIDTH_DEGREES, 75) *
				    .01745329252f;
				char profile[ASSET_NAME_CAPACITY] = {};
				json_string(json_get(displacement, "profile"), profile,
				            sizeof(profile));
				f.displacement_pointed = !strcmp(profile, "pointed");
				if (!f.displacement_pointed && strcmp(profile, "rounded"))
					json_error(displacement,
					           "profile must be pointed or rounded");
			}
		}
		uint32_t id = json_uint(json_get(j, "id"), 1, MAX_FACTIONS - 1);
		definition_name(j, f.name);
		json_string(json_get(j, "label"), f.label, ASSET_NAME_CAPACITY);
		f.color = json_color(json_get(j, "color"));
		if (c->factions[id].declared || content_faction(c, f.name) >= 0)
			return json_error(j, "duplicate or reserved faction ID or name");
		Json p = json_find(j, "production");
		if (json_present(p))
		{
			json_object(p, "building|default_unit|assault_unit|mode");
			Json mode = json_find(p, "mode");
			if (json_present(mode))
			{
				char value[ASSET_NAME_CAPACITY] = {};
				json_string(mode, value, ASSET_NAME_CAPACITY);
				if (!strcmp(value, "spores"))
					f.automatic = true;
				else if (strcmp(value, "manufacture"))
					json_error(mode,
					           "production mode must be manufacture or spores");
			}
			json_string(json_get(p, "building"), f.producer_name,
			            ASSET_NAME_CAPACITY);
			json_string(json_get(p, "default_unit"), f.default_name,
			            ASSET_NAME_CAPACITY);
			json_string(json_get(p, "assault_unit"), f.assault_name,
			            ASSET_NAME_CAPACITY);
		}
		f.declared = true;
		c->factions[id] = f;
	}
	return root.document->valid;
}
static bool load_groups(Content* c, Json root)
{
	json_object(root, "$schema|groups");
	JsonArray a = json_array(json_get(root, "groups"), MAX_GROUPS);
	Json j = {};
	while (json_next(&a, &j))
	{
		json_object(j, "id|key|selection|singular|plural");
		GroupDefinition g = {};
		uint32_t id = json_uint(json_get(j, "id"), 0, MAX_GROUPS - 1);
		json_string(json_get(j, "key"), g.key, ASSET_NAME_CAPACITY);
		json_string(json_get(j, "selection"), g.selection, ASSET_NAME_CAPACITY);
		json_string(json_get(j, "singular"), g.singular, ASSET_NAME_CAPACITY);
		json_string(json_get(j, "plural"), g.plural, ASSET_NAME_CAPACITY);
		if (c->groups[id].declared || !strcmp(g.key, "all") ||
		    !strcmp(g.selection, "all") || content_group(c, g.key) >= 0 ||
		    content_group(c, g.selection) >= 0)
			return json_error(j, "duplicate or reserved group ID or name");
		g.declared = true;
		c->groups[id] = g;
	}
	return root.document->valid;
}
static bool load_units(Content* c, Json root, const char* path)
{
	json_object(root, "$schema|shared|units");
	Json shared = json_get(root, "shared");
	json_object(shared, "secondary_interval_factor");
	float secondary_interval =
	    json_float(json_get(shared, "secondary_interval_factor"), 1,
		           MAX_UNIT_INTERVAL_FACTOR);
	JsonArray a = json_array(json_get(root, "units"), MAX_UNIT_TYPES);
	Json j = {};
	while (json_next(&a, &j))
	{
		json_object(
		    j, "name|faction|group|model|scale|yaw_degrees|tint|speed|health|"
		       "range|cost|shot_interval|separation|targets|colonizes|detaches|"
		       "trails|subsurface|trail_origin|pick_radius|cue_radius|hits|"
		       "brood|manufacture_seconds|attack|secondary_attack|autonomy|"
		       "payload|rear_tint|colonization|carrier");
		UnitDefinition u = {};
		definition_name(j, u.name);
		u.charge_speed = u.charge_acceleration = 1;
		Json manufacture = json_find(j, "manufacture_seconds");
		if (json_present(manufacture))
			u.manufacture_seconds =
			    json_positive(manufacture, MAX_BUILDING_GROWTH_SECONDS);
		Json brood = json_find(j, "brood");
		u.brood = json_present(brood) ? json_uint(brood, 1, MAX_UNIT_BROOD) : 1;
		Json rear = json_find(j, "rear_tint");
		if (json_present(rear))
		{
			json_object(rear, "color|strength");
			u.rear_tint = json_color(json_get(rear, "color"));
			u.rear_tint_strength = json_float(json_get(rear, "strength"), 0, 1);
		}
		Json attack = json_find(j, "attack");
		if (json_present(attack))
		{
			json_object(attack,
			            "mode|lifetime|friendly_fire|damage|cloud|charge_"
			            "radius|charge_speed|charge_acceleration|release_hits|"
			            "release_seconds|blob|aura");
			char mode[ASSET_NAME_CAPACITY] = {};
			json_string(json_get(attack, "mode"), mode, ASSET_NAME_CAPACITY);
			if (!strcmp(mode, "digest"))
				u.attack = ATTACK_DIGEST;
			else if (!strcmp(mode, "spore"))
			{
				u.attack = ATTACK_SPORE;
				Json lifetime = json_find(attack, "lifetime");
				if (json_present(lifetime))
					u.lifetime = json_positive(lifetime, MAX_UNIT_LIFETIME);
				u.friendly_fire = json_bool(json_get(attack, "friendly_fire"));
				u.burst_damage = json_float(json_get(attack, "damage"), 0,
				                            MAX_UNIT_CLOUD_DAMAGE);
				Json cloud = json_get(attack, "cloud");
				json_object(cloud, "radius|seconds|damage");
				u.cloud_radius = json_positive(json_get(cloud, "radius"),
				                               MAX_UNIT_CLOUD_RADIUS);
				u.cloud_seconds = json_positive(json_get(cloud, "seconds"),
				                                MAX_UNIT_CLOUD_SECONDS);
				u.cloud_damage = json_positive(json_get(cloud, "damage"),
				                               MAX_UNIT_CLOUD_DAMAGE);
			}
			else if (strcmp(mode, "projectile"))
				json_error(attack, "unknown attack mode");
			Json radius = json_find(attack, "charge_radius"),
			     speed = json_find(attack, "charge_speed"),
			     acceleration = json_find(attack, "charge_acceleration");
			Json hits = json_find(attack, "release_hits"),
			     seconds = json_find(attack, "release_seconds");
			if (u.attack != ATTACK_DIGEST &&
			    (json_present(radius) || json_present(speed) ||
			     json_present(acceleration) || json_present(hits) ||
			     json_present(seconds)))
				json_error(attack,
				           "charge and release parameters require digest mode");
			if (json_present(radius))
				u.charge_radius = json_positive(radius, 100);
			if (json_present(speed))
				u.charge_speed = json_float(speed, 1, 8);
			if (json_present(acceleration))
				u.charge_acceleration = json_float(acceleration, 1, 8);
			if (json_present(hits))
				u.release_hits = json_uint(hits, 1, MAX_HIT_COUNT);
			if (json_present(seconds))
				u.release_seconds = json_positive(seconds, 10);
			if (json_present(hits) != json_present(seconds))
				json_error(attack, "release requires both hits and seconds");
			Json blob = json_find(attack, "blob"),
			     aura = json_find(attack, "aura");
			if (u.attack != ATTACK_DIGEST &&
			    (json_present(blob) || json_present(aura)))
				json_error(attack, "blob and aura require digest mode");
			if (json_present(blob))
			{
				json_object(blob, "model|scale|range|interval|stun_seconds");
				json_path(json_get(blob, "model"), path, u.blob_model);
				u.blob_scale =
				    json_positive(json_get(blob, "scale"), MAX_UNIT_SCALE);
				u.blob_range =
				    json_positive(json_get(blob, "range"), MAX_UNIT_RANGE);
				u.blob_interval = json_positive(json_get(blob, "interval"),
				                                MAX_UNIT_SHOT_INTERVAL);
				u.blob_stun = json_positive(json_get(blob, "stun_seconds"),
				                            MAX_UNIT_STUN_SECONDS);
			}
			if (json_present(aura))
			{
				json_object(aura, "radius|seconds|heal_interval");
				u.aura_radius = json_positive(json_get(aura, "radius"),
				                              MAX_UNIT_CLOUD_RADIUS);
				u.aura_seconds = json_positive(json_get(aura, "seconds"),
				                               MAX_UNIT_CLOUD_SECONDS);
				u.aura_interval = json_positive(json_get(aura, "heal_interval"),
				                                MAX_UNIT_STUN_SECONDS);
			}
			if (u.attack != ATTACK_SPORE &&
			    (json_present(json_find(attack, "cloud")) ||
			     json_present(json_find(attack, "damage")) ||
			     json_present(json_find(attack, "lifetime")) ||
			     json_present(json_find(attack, "friendly_fire"))))
				json_error(attack, "spore parameters require spore mode");
		}
		json_string(json_get(j, "faction"), u.faction_name,
		            ASSET_NAME_CAPACITY);
		json_string(json_get(j, "group"), u.group_name, ASSET_NAME_CAPACITY);
		json_path(json_get(j, "model"), path, u.model_path);
		u.scale = json_positive(json_get(j, "scale"), MAX_UNIT_SCALE);
		u.yaw_degrees = json_float(json_get(j, "yaw_degrees"),
		                           -MAX_UNIT_YAW_DEGREES, MAX_UNIT_YAW_DEGREES);
		u.color = json_color(json_get(j, "tint"));
		u.speed = json_positive(json_get(j, "speed"), MAX_UNIT_SPEED);
		u.health = json_positive(json_get(j, "health"), MAX_UNIT_HEALTH);
		u.range = json_positive(json_get(j, "range"), MAX_UNIT_RANGE);
		u.unit_radius = u.structure_radius = u.range;
		u.cruise_fraction = 1;
		Json autonomy = json_find(j, "autonomy");
		if (json_present(autonomy))
		{
			json_object(autonomy,
			            "unit_radius|structure_radius|colonize_radius|leash_"
			            "radius|response_seconds|cruise_fraction");
			u.unit_radius = json_float(json_get(autonomy, "unit_radius"), 0,
			                           MAX_UNIT_TARGET_RADIUS);
			u.structure_radius =
			    json_float(json_get(autonomy, "structure_radius"), 0,
				           MAX_UNIT_STRUCTURE_RADIUS);
			u.colonize_radius =
			    json_float(json_get(autonomy, "colonize_radius"), 0,
				           MAX_UNIT_COLONIZE_RADIUS);
			u.leash_radius = json_positive(json_get(autonomy, "leash_radius"),
			                               MAX_UNIT_LEASH_RADIUS);
			u.response_seconds =
			    json_float(json_get(autonomy, "response_seconds"), 0,
				           MAX_UNIT_RESPONSE_SECONDS);
			u.cruise_fraction =
			    json_positive(json_get(autonomy, "cruise_fraction"), 1);
		}
		u.cost = json_positive(json_get(j, "cost"), MAX_UNIT_COST);
		u.shot_interval =
		    json_positive(json_get(j, "shot_interval"), MAX_UNIT_SHOT_INTERVAL);
		u.separation =
		    json_positive(json_get(j, "separation"), MAX_UNIT_SEPARATION);
		u.pick_radius =
		    json_positive(json_get(j, "pick_radius"), MAX_UNIT_PICK_RADIUS);
		u.cue_radius =
		    json_positive(json_get(j, "cue_radius"), MAX_UNIT_CUE_RADIUS);
		u.hits = json_uint(json_get(j, "hits"), 1, MAX_HIT_COUNT);
		Json trail_origin = json_find(j, "trail_origin");
		if (json_present(trail_origin))
			u.trail_origin = json_vector(trail_origin, -10, 10);
		u.colonizes = json_bool(json_get(j, "colonizes"));
		u.detaches = json_bool(json_get(j, "detaches"));
		u.trails = json_bool(json_get(j, "trails"));
		{
			Json sss = json_find(j, "subsurface");
			u.subsurface = !json_present(sss) || json_bool(sss);
		}
		u.colonization_units = 1;
		u.consume_colonists = true;
		Json colonization = json_find(j, "colonization");
		if (json_present(colonization))
		{
			json_object(colonization, "required_units|consume_units|seed");
			if (!u.colonizes)
				json_error(colonization,
				           "colonization requires colonizes true");
			u.colonization_units =
			    json_uint(json_get(colonization, "required_units"), 1,
				          MAX_COLONIZATION_UNITS);
			u.consume_colonists =
			    json_bool(json_get(colonization, "consume_units"));
			Json seed = json_find(colonization, "seed");
			if (u.consume_colonists == json_present(seed))
				json_error(colonization,
				           "only reusable colonizers require a seed");
			if (json_present(seed))
			{
				json_object(seed, "model|scale|reload_seconds");
				if (u.colonization_units != 1)
					json_error(seed, "seed launch requires one unit");
				json_path(json_get(seed, "model"), path, u.seed_model);
				u.seed_scale =
				    json_positive(json_get(seed, "scale"), MAX_UNIT_SCALE);
				u.seed_reload =
				    json_positive(json_get(seed, "reload_seconds"), 3600);
			}
		}
		Json carrier = json_find(j, "carrier");
		if (json_present(carrier))
		{
			json_object(carrier, "unit|interval_seconds|launch_offset");
			json_string(json_get(carrier, "unit"), u.carrier_name,
			            ASSET_NAME_CAPACITY);
			u.carrier_seconds =
			    json_positive(json_get(carrier, "interval_seconds"), 3600);
			u.carrier_offset =
			    json_vector(json_get(carrier, "launch_offset"), -10, 10);
		}
		Json payload = json_find(j, "payload");
		if (json_present(payload))
		{
			json_object(payload, "model|reload_seconds|sockets");
			json_path(json_get(payload, "model"), path, u.payload_model);
			u.payload_reload =
			    json_positive(json_get(payload, "reload_seconds"), 3600);
			JsonArray sockets =
			    json_array(json_get(payload, "sockets"), MAX_UNIT_PAYLOADS);
			Json socket = {};
			while (json_next(&sockets, &socket))
				u.payload_sockets[u.payload_count++] =
				    json_vector(socket, -10, 10);
			if (!u.payload_count || u.detaches || u.attack != ATTACK_PROJECTILE)
				json_error(payload, "payload requires sockets, projectile "
				                    "attack and detaches false");
		}
		JsonArray targets =
		    json_array(json_get(j, "targets"), MAX_UNIT_TARGETS);
		Json target = {};
		while (json_next(&targets, &target))
		{
			char name[ASSET_NAME_CAPACITY] = {};
			json_string(target, name, ASSET_NAME_CAPACITY);
			uint32_t bit = !strcmp(name, "units")        ? TARGET_UNITS
			               : !strcmp(name, "structures") ? TARGET_STRUCTURES
			                                             : 0;
			if (!bit || (u.targets & bit))
				return json_error(target,
				                  "unknown or duplicate target category");
			u.targets |= bit;
		}
		Json secondary = json_find(j, "secondary_attack");
		u.secondary_interval = secondary_interval;
		if (json_present(secondary))
		{
			json_object(secondary, "targets");
			JsonArray a =
			    json_array(json_get(secondary, "targets"), MAX_UNIT_TARGETS);
			while (json_next(&a, &target))
			{
				char name[ASSET_NAME_CAPACITY] = {};
				json_string(target, name, ASSET_NAME_CAPACITY);
				uint32_t bit = !strcmp(name, "units")        ? TARGET_UNITS
				               : !strcmp(name, "structures") ? TARGET_STRUCTURES
				                                             : 0;
				if (!bit || (bit & (u.targets | u.secondary_targets)))
					return json_error(target, "secondary targets must be "
					                          "distinct from primary targets");
				u.secondary_targets |= bit;
			}
		}
		if (content_unit(c, u.name) >= 0)
			return json_error(j, "duplicate unit definition");
		c->units[c->unit_count++] = u;
	}
	return root.document->valid;
}
static bool load_buildings(Content* c, Json root, const char* path)
{
	json_object(root, "$schema|shared|buildings");
	Json shared = json_get(root, "shared"),
	     shared_branches = json_get(shared, "branches");
	json_object(shared, "growth_seconds|sprout_seconds|establishment_seconds|"
	                    "collection|height|width|repair|growth_plane|branches");
	json_object(
	    shared_branches,
	    "seconds|seed|regrow_seconds|roots|root_limit|root_minimum_radius|"
	    "additional_root_scale|additional_root_width_scale|additional_root_"
	    "branch|surface_clearance|all_leaf_nodes|root_offset_seconds");
	JsonArray a = json_array(json_get(root, "buildings"), MAX_PRODUCER_TYPES);
	Json j = {};
	while (json_next(&a, &j))
	{
		json_object(j, "name|faction|model|health|hits|setup_speed|branches|"
		               "growth_meshes|navigation|haze");
		ProducerDefinition p = {};
		p.factory_glow_scale = 1;
		p.factory_tint_strength = DEFAULT_FACTORY_TINT_STRENGTH;
		Json repair = json_find(shared, "repair");
		if (json_present(repair))
		{
			json_object(repair,
			            "health_per_second|nutrients_per_health|delay_seconds");
			p.repair_health_per_second = json_positive(
			    json_get(repair, "health_per_second"), MAX_BUILDING_HEALTH);
			p.repair_nutrients_per_health = json_positive(
			    json_get(repair, "nutrients_per_health"), MAX_BUILDING_HEALTH);
			p.repair_delay_seconds =
			    json_float(json_get(repair, "delay_seconds"), 0,
				           MAX_BUILDING_GROWTH_SECONDS);
		}
		Json haze = json_find(j, "haze");
		if (json_present(haze))
		{
			json_object(haze, "reach|damage_per_second|heal_per_second");
			p.haze_reach =
			    json_positive(json_get(haze, "reach"), MAX_UNIT_CLOUD_RADIUS);
			p.haze_damage = json_positive(json_get(haze, "damage_per_second"),
			                              MAX_UNIT_CLOUD_DAMAGE);
			p.haze_heal = json_positive(json_get(haze, "heal_per_second"),
			                            MAX_UNIT_CLOUD_DAMAGE);
		}
		Json plane = json_find(shared, "growth_plane");
		if (json_present(plane))
		{
			json_object(plane, "normal|depth_ratio|spread_roots");
			Json spread = json_find(plane, "spread_roots");
			if (json_present(spread))
				p.spread_roots = json_bool(spread);
			p.growth_plane_normal = normal(json_get(plane, "normal"));
			p.growth_depth_ratio =
			    json_float(json_get(plane, "depth_ratio"), 0, 1);
			p.growth_plane_enabled = true;
		}
		Json navigation = json_find(j, "navigation");
		if (json_present(navigation))
		{
			json_object(navigation, "stem_radius|head_radius");
			p.stem_radius = json_positive(json_get(navigation, "stem_radius"),
			                              MAX_BUILDING_STEM_RADIUS);
			p.head_radius = json_positive(json_get(navigation, "head_radius"),
			                              MAX_BUILDING_HEAD_RADIUS);
		}
		Json growth = json_find(j, "growth_meshes");
		if (json_present(growth))
		{
			json_object(growth, "segment|tip|rounded|bulges|glow_scale|tint_"
			                    "strength|length_color");
			Json pigment = json_find(growth, "length_color");
			if (json_present(pigment))
			{
				json_object(pigment, "color|distance|strength");
				p.length_color = json_color(json_get(pigment, "color"));
				p.length_color_distance = json_positive(
				    json_get(pigment, "distance"), MAX_BUILDING_HEIGHT);
				p.length_color_strength =
				    json_float(json_get(pigment, "strength"), 0, 1);
			}
			Json glow_scale = json_find(growth, "glow_scale"),
			     tint = json_find(growth, "tint_strength");
			if (json_present(glow_scale))
				p.factory_glow_scale = json_float(glow_scale, 0, 1);
			if (json_present(tint))
				p.factory_tint_strength = json_float(tint, 0, 1);
			Json bulges = json_find(growth, "bulges");
			if (json_present(bulges))
				p.stem_bulges = json_uint(bulges, 0, MAX_FACTORY_STEM_BULGES);
			Json rounded = json_find(growth, "rounded");
			if (json_present(rounded))
				p.rounded_stem = json_bool(rounded);
			json_path(json_get(growth, "segment"), path, p.segment_path);
			json_path(json_get(growth, "tip"), path, p.tip_path);
		}
		definition_name(j, p.name);
		json_string(json_get(j, "faction"), p.faction_name,
		            ASSET_NAME_CAPACITY);
		json_path(json_get(j, "model"), path, p.model_path);
		p.growth_seconds = json_positive(json_get(shared, "growth_seconds"),
		                                 MAX_BUILDING_GROWTH_SECONDS);
		Json sprout = json_find(shared, "sprout_seconds");
		p.sprout_seconds =
		    json_present(sprout)
		        ? json_positive(sprout, MAX_BUILDING_GROWTH_SECONDS)
		        : p.growth_seconds;
		Json establishment = json_find(shared, "establishment_seconds");
		if (json_present(establishment))
			p.establishment_seconds =
			    json_positive(establishment, MAX_ESTABLISHMENT_SECONDS);
		p.health = json_positive(json_get(j, "health"), MAX_BUILDING_HEALTH);
		p.collection = json_positive(json_get(shared, "collection"),
		                             MAX_BUILDING_COLLECTION);
		p.height =
		    json_positive(json_get(shared, "height"), MAX_BUILDING_HEIGHT);
		p.width = json_positive(json_get(shared, "width"), MAX_BUILDING_WIDTH);
		p.hits = json_uint(json_get(j, "hits"), 1, MAX_STRUCTURE_HIT_COUNT);
		Json b = json_get(j, "branches");
		json_object(b,
		            "count|spacing|spread_scale|chain_length|production|"
		            "production_leaf_limits|production_reference_count|health|"
		            "hits|root_height|root_interval_seconds|grammar|tree");
		char roots[ASSET_NAME_CAPACITY] = {}, grammar[ASSET_NAME_CAPACITY] = {};
		Json roots_json = json_find(shared_branches, "roots"),
		     grammar_json = json_find(b, "grammar");
		if (json_present(roots_json))
		{
			json_string(roots_json, roots, ASSET_NAME_CAPACITY);
			p.branches.organelle_roots = !strcmp(roots, "organelles");
			if (!p.branches.organelle_roots && strcmp(roots, "single"))
				json_error(roots_json, "roots must be organelles or single");
		}
		Json root_limit = json_find(shared_branches, "root_limit"),
		     root_radius = json_find(shared_branches, "root_minimum_radius");
		if (json_present(root_limit))
			p.branches.root_limit =
			    json_uint(root_limit, 1, MAX_FACTORY_STRUCTURES);
		if (json_present(root_radius))
			p.branches.root_minimum_radius = json_float(root_radius, 0, 1.5f);
		if (!p.branches.organelle_roots &&
		    (json_present(root_limit) || json_present(root_radius)))
			json_error(b, "root filters require organelle roots");
		Json additional = json_find(shared_branches, "additional_root_scale");
		if (json_present(additional))
		{
			p.branches.additional_root_scale =
			    json_float(additional, .05f, .5f);
			if (!p.branches.organelle_roots)
				json_error(additional,
				           "additional roots require organelle roots");
		}
		p.branches.additional_root_width_scale = 1;
		Json small_width =
		         json_find(shared_branches, "additional_root_width_scale"),
		     small_branch =
		         json_find(shared_branches, "additional_root_branch");
		if (json_present(small_width))
			p.branches.additional_root_width_scale =
			    json_float(small_width, 1, 4);
		if (json_present(small_branch))
			p.branches.additional_root_branch = json_bool(small_branch);
		if ((json_present(small_width) || json_present(small_branch)) &&
		    !json_present(additional))
			json_error(
			    shared_branches,
			    "small root width and branching require additional roots");
		Json clearance = json_find(shared_branches, "surface_clearance"),
		     all_leaves = json_find(shared_branches, "all_leaf_nodes");
		if (json_present(clearance))
			p.branches.surface_clearance =
			    json_positive(clearance, MAX_BUILDING_SPACING);
		if (json_present(all_leaves))
			p.branches.all_leaf_nodes = json_bool(all_leaves);
		Json root_offset = json_find(shared_branches, "root_offset_seconds");
		if (json_present(root_offset))
			p.branches.root_offset_seconds =
			    json_float(root_offset, 0, MAX_ESTABLISHMENT_SECONDS);
		Json root_height = json_find(b, "root_height");
		if (json_present(root_height))
			p.branches.root_height =
			    json_positive(root_height, MAX_BUILDING_HEIGHT);
		Json root_interval = json_find(b, "root_interval_seconds");
		if (json_present(root_interval))
			p.branches.root_interval_seconds =
			    json_float(root_interval, 0, MAX_ESTABLISHMENT_SECONDS);
		p.branches.branch_probability = 1;
		if (json_present(grammar_json))
		{
			json_string(grammar_json, grammar, ASSET_NAME_CAPACITY);
			if (!strcmp(grammar, "fan"))
				p.branches.grammar = GROWTH_FAN;
			else if (!strcmp(grammar, "hypha"))
				p.branches.grammar = GROWTH_HYPHA;
			else if (!strcmp(grammar, "tree"))
				p.branches.grammar = GROWTH_TREE;
			else if (strcmp(grammar, "axial"))
				json_error(grammar_json,
				           "grammar must be axial, fan, hypha or tree");
		}
		p.branches.chain_length = DEFAULT_HYPHA_CHAIN_LENGTH;
		Json chain = json_find(b, "chain_length");
		if (json_present(chain))
		{
			if (p.branches.grammar != GROWTH_HYPHA)
				json_error(chain, "chain length requires the hypha grammar");
			p.branches.chain_length =
			    json_uint(chain, 1, MAX_FACTORY_STRUCTURES);
		}
		Json tree = json_find(b, "tree");
		if (p.branches.grammar == GROWTH_TREE)
		{
			json_object(tree,
			            "children|levels|length_scale|width_scale|angle_"
			            "degrees|bend|branch_probability|angle_jitter_degrees|"
			            "length_jitter|width_jitter|curvature|curve_jitter");
			p.branches.children = json_uint(json_get(tree, "children"), 2, 3);
			p.branches.levels = json_uint(json_get(tree, "levels"), 2, 5);
			p.branches.length_scale =
			    json_float(json_get(tree, "length_scale"), .2, .9);
			p.branches.width_scale =
			    json_float(json_get(tree, "width_scale"), .2, .9);
			p.branches.angle =
			    json_float(json_get(tree, "angle_degrees"), 10, 65) *
			    .01745329252f;
			p.branches.bend = json_float(json_get(tree, "bend"), .05, .5);
			Json curvature = json_find(tree, "curvature");
			p.branches.smooth_path = json_present(curvature);
			if (p.branches.smooth_path)
				p.branches.curvature = json_float(curvature, 0, 2);
			Json curve_jitter = json_find(tree, "curve_jitter");
			if (json_present(curve_jitter))
			{
				if (!p.branches.smooth_path)
					json_error(curve_jitter, "curve jitter requires curvature");
				p.branches.curve_jitter = json_float(curve_jitter, 0, 1);
			}
			Json probability = json_find(tree, "branch_probability"),
			     angle = json_find(tree, "angle_jitter_degrees");
			Json length = json_find(tree, "length_jitter"),
			     width = json_find(tree, "width_jitter");
			if (json_present(probability))
				p.branches.branch_probability = json_float(probability, 0, 1);
			if (json_present(angle))
				p.branches.angle_jitter =
				    json_float(angle, 0, 30) * .01745329252f;
			if (json_present(length))
				p.branches.length_jitter = json_float(length, 0, .4);
			if (json_present(width))
				p.branches.width_jitter = json_float(width, 0, .3);
		}
		else if (json_present(tree))
			json_error(tree, "tree settings require the tree grammar");
		Json branch_health = json_find(b, "health"),
		     branch_hits = json_find(b, "hits");
		p.branches.health =
		    json_present(branch_health)
		        ? json_positive(branch_health, MAX_BRANCH_HEALTH)
		        : p.health;
		p.branches.hits =
		    json_present(branch_hits)
		        ? json_uint(branch_hits, 1, MAX_STRUCTURE_HIT_COUNT)
		        : p.hits;
		p.branches.seconds = json_positive(json_get(shared_branches, "seconds"),
		                                   MAX_BRANCH_EMERGENCE_SECONDS);
		Json regrow = json_find(shared_branches, "regrow_seconds");
		p.branches.regrow_seconds =
		    json_present(regrow)
		        ? json_positive(regrow, MAX_BRANCH_REGROW_SECONDS)
		        : p.branches.seconds;
		p.branches.count =
		    json_uint(json_get(b, "count"), 1, MAX_FACTORY_STRUCTURES);
		p.branches.spacing =
		    json_positive(json_get(b, "spacing"), MAX_BUILDING_SPACING);
		p.branches.production =
		    json_positive(json_get(b, "production"), MAX_BUILDING_PRODUCTION);
		Json spread = json_find(b, "spread_scale"),
		     limits = json_find(b, "production_leaf_limits");
		p.branches.spread_scale =
		    json_present(spread)
		        ? json_float(spread, .1f, MAX_BRANCH_SPREAD_SCALE)
		        : 1;
		if (json_present(limits))
		{
			JsonArray entries = json_array(limits, MAX_FACTORY_STRUCTURES);
			Json entry = {};
			while (json_next(&entries, &entry))
			{
				json_object(entry, "roots|leaves|establishing_leaves");
				uint32_t roots = json_uint(json_get(entry, "roots"), 1,
				                           MAX_FACTORY_STRUCTURES);
				if (p.branches.production_leaf_limits[roots - 1])
					json_error(entry, "duplicate root count in leaf limits");
				p.branches.production_leaf_limits[roots - 1] = json_uint(
				    json_get(entry, "leaves"), 1, MAX_FACTORY_STRUCTURES);
				Json early = json_find(entry, "establishing_leaves");
				if (json_present(early))
					p.branches.establishing_leaf_limits[roots - 1] =
					    json_uint(early, 1, MAX_FACTORY_STRUCTURES);
			}
		}
		Json reference = json_find(b, "production_reference_count");
		if (json_present(reference))
			p.branches.production_reference_count =
			    json_uint(reference, 1, MAX_FACTORY_STRUCTURES);
		p.branches.seed =
		    json_uint(json_get(shared_branches, "seed"), 0, MAX_BUILDING_SEED);
		p.branches.normal = p.growth_plane_normal;
		p.branches.fields = BRANCH_FIELDS_COMPLETE;
		Json setup = json_find(j, "setup_speed");
		float setup_speed =
		    json_present(setup) ? json_float(setup, .25f, 4.0f) : 1;
		p.growth_seconds /= setup_speed;
		p.sprout_seconds /= setup_speed;
		p.establishment_seconds /= setup_speed;
		p.branches.seconds /= setup_speed;
		p.branches.root_offset_seconds /= setup_speed;
		p.branches.root_interval_seconds /= setup_speed;
		if (content_producer(c, p.name) >= 0)
			return json_error(j, "duplicate building definition");
		c->producers[c->producer_count++] = p;
	}
	return root.document->valid;
}
bool content_load(Content* content, const char* path)
{
	JsonDocument manifest;
	if (!json_load(&manifest, path))
		return false;
	Json root = json_root(&manifest);
	json_object(root, "$schema|bodies|factions|groups|units|buildings");
	const char* keys[] = {"bodies", "factions", "groups", "units", "buildings"};
	for (uint32_t i = 0; i < sizeof(keys) / sizeof(keys[0]) && manifest.valid;
	     ++i)
	{
		char file[ASSET_PATH_CAPACITY] = {};
		if (!json_path(json_get(root, keys[i]), path, file))
			return false;
		JsonDocument document;
		if (!json_load(&document, file))
			return false;
		Json data = json_root(&document);
		bool valid = i == 0   ? load_bodies(content, data, file)
		             : i == 1 ? load_factions(content, data, file)
		             : i == 2 ? load_groups(content, data)
		             : i == 3 ? load_units(content, data, file)
		                      : load_buildings(content, data, file);
		if (!valid)
			return false;
	}
	return manifest.valid && content_finish(content, path);
}

bool content_finish(Content* c, const char* path)
{
	bool valid = true;
	for (uint32_t i = 0; i < c->unit_count; ++i)
	{
		UnitDefinition* u = &c->units[i];
		int faction = content_faction(c, u->faction_name),
		    group = content_group(c, u->group_name);
		if (faction <= 0 || group < 0)
		{
			valid = false;
			break;
		}
		u->faction = (Faction)faction;
		u->group = (uint32_t)group;
		c->max_unit_range = std::fmax(c->max_unit_range, u->range);
		c->max_unit_separation =
		    std::fmax(c->max_unit_separation, u->separation);
	}
	for (uint32_t i = 0; valid && i < c->unit_count; ++i)
		if (c->units[i].carrier_seconds > 0)
		{
			UnitDefinition& u = c->units[i];
			int child = content_unit(c, u.carrier_name);
			if (child < 0 || c->units[child].faction != u.faction ||
			    c->units[child].group != 0 ||
			    c->units[child].carrier_seconds > 0)
			{
				valid = false;
				break;
			}
			u.carrier_unit = (uint32_t)child;
		}
	for (uint32_t i = 0; valid && i < c->producer_count; ++i)
	{
		ProducerDefinition* p = &c->producers[i];
		int faction = content_faction(c, p->faction_name);
		if (faction <= 0 || p->branches.fields != BRANCH_FIELDS_COMPLETE)
		{
			valid = false;
			break;
		}
		if (p->branches.organelle_roots && !p->segment_path[0])
		{
			valid = false;
			break;
		}
		p->faction = (Faction)faction;
	}
	for (uint32_t i = 1; valid && i < MAX_FACTIONS; ++i)
	{
		FactionDefinition* f = &c->factions[i];
		if (!f->declared || !f->producer_name[0])
			continue;
		int p = content_producer(c, f->producer_name),
		    u = content_unit(c, f->default_name),
		    a = content_unit(c, f->assault_name);
		if (p < 0 || u < 0 || a < 0 || c->producers[p].faction != i ||
		    c->units[u].faction != i || c->units[a].faction != i)
		{
			valid = false;
			break;
		}
		f->producer = (uint32_t)p;
		f->default_unit = (uint32_t)u;
		f->assault_unit = (uint32_t)a;
		if (f->automatic && (u != a || c->units[u].attack != ATTACK_SPORE ||
		                     c->units[u].colonizes))
			valid = false;
	}
	if (!valid)
		fprintf(stderr, "%s: content has incomplete or invalid references\n",
		        path);
	return valid;
}
