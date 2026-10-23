#include "effects.h"
#include "camera.h"
#include "math_constants.h"
#include "render_config.h"
#include "simulation.h"
#include "vector_math.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>

static const float EFFECT_NEAR_CLIP_DISTANCE = .1f;
static const float MIN_TRAIL_SEGMENT_SQUARED_LENGTH = .000001f;
static const float MIN_EFFECT_RADIUS_PIXELS = .65f;
static const float SELECTION_CUE_ALPHA = .85f;
// Selection rings stay readable when the craft itself is a pixel or less.
static const float MIN_SELECTION_RADIUS_PIXELS = 5;
static const float BLAST_CORE_RADIUS_SCALE = .18f;
static const int BLAST_RING_SEGMENTS = 16;
static const float FULL_TURN_RADIANS = 6.2831853f;
static const int DEATH_BURST_RAYS = 7;
static const float BURST_GOLDEN_ANGLE_RADIANS = 2.399963f;
static const float HIT_RADIUS_SCALE = .55f;
static const float HIT_RADIUS_START_TIME = .02f;
static const int HIT_BURST_RAYS = 5;
static const float HIT_RAY_INNER_RADIUS_SCALE = .4f;
static const float DEATH_CORE_START_RADIUS_SCALE = .2f;
static const float DEATH_CORE_GROWTH_RADIUS_SCALE = 1.3f;
static const float DEATH_RAY_INNER_GROWTH_SCALE = .8f;
static const float DEATH_RAY_OUTER_GROWTH_SCALE = 1.7f;
static const float DEATH_RAY_WIDTH_SCALE = .6f;
static const float DEATH_RAY_INNER_START_RADIUS_SCALE = .1f;
static const float HIT_RAY_WIDTH_SCALE = .55f;
static const int EFFECT_TILE_RADIUS_MARGIN_PIXELS = 2;
static const float DEATH_RAY_OUTER_START_RADIUS_SCALE = .2f;
static const float CLOUD_RISE_FRACTION = .1f;
static const float CLOUD_FADE_FRACTION = .4f;
static const int CLOUD_EFFECT_KIND = 5;
static const float AURA_FADE_SECONDS = 1;
// Clouds and auras are visual only: past this many tile references, further
// ones are not drawn.
static const uint32_t CLOUD_REFERENCE_BUDGET = MAX_EFFECT_REFERENCES / 2;

struct Projection
{
	Vec3 eye, forward, right, up;
	float pixels, cx, cy;
};
static bool project(const Projection& p, Vec3 world, float* screen)
{
	Vec3 v = subtract(world, p.eye);
	float z = dot(v, p.forward);
	if (z <= EFFECT_NEAR_CLIP_DISTANCE)
		return false;
	screen[0] = p.cx + dot(v, p.right) * p.pixels / z;
	screen[1] = p.cy - dot(v, p.up) * p.pixels / z;
	screen[2] = std::sqrt(dot(v, v));
	screen[3] = p.pixels / z;
	return true;
}
static void segment(EffectSegment* output, uint32_t* count, const Projection& p,
                    Vec3 a, Vec3 b, float width, Color color, float alpha,
                    int kind)
{
	if (*count >= MAX_EFFECT_SEGMENTS ||
	    (kind == 0 && dot(subtract(a, b), subtract(a, b)) <
	                      MIN_TRAIL_SEGMENT_SQUARED_LENGTH))
		return;
	if (kind == 4)
	{
		float from = dot(subtract(a, p.eye), p.forward),
		      to = dot(subtract(b, p.eye), p.forward);
		float near = EFFECT_NEAR_CLIP_DISTANCE * 1.1f;
		if (from <= near)
			return;
		if (to < near)
			b = add(a, scale(subtract(b, a), (from - near) / (from - to)));
	}
	EffectSegment e = {};
	if (!project(p, a, e.start) || !project(p, b, e.end))
		return;
	e.start[3] = std::fmax(kind == 3 ? MIN_SELECTION_RADIUS_PIXELS
	                                 : MIN_EFFECT_RADIUS_PIXELS,
	                       e.start[3] * width);
	e.end[3] = (float)kind;
	e.color[0] = color.r;
	e.color[1] = color.g;
	e.color[2] = color.b;
	e.color[3] = alpha;
	output[(*count)++] = e;
}
static bool target_point(const Simulation* s, const Unit& u, Vec3* point)
{
	if (simulation_attachment_valid(s, &u))
		return false;
	if (u.moving)
	{
		*point = u.destination;
		return true;
	}
	if (u.task == TASK_ATTACK_UNIT)
	{
		if (u.attention < 0 || u.attention >= MAX_UNITS)
			return false;
		const Unit& target = s->units[u.attention];
		if (target.health <= 0 || target.generation != u.attention_generation ||
		    !s->visible_units[u.attention])
			return false;
		*point = target.position;
		return true;
	}
	if (u.task == TASK_ATTACK_STRUCTURE)
	{
		if (u.attention < 0 || (uint32_t)u.attention >= s->world.body_count ||
		    !s->visible_bodies[u.attention])
			return false;
		const Colony& c = s->colonies[u.attention];
		uint32_t k = u.attention_structure;
		if (!c.active || c.generation != u.attention_colony_generation ||
		    k >= c.structure_count)
			return false;
		if ((k ? c.branches[k - 1].health : c.health) <= 0 ||
		    (k ? c.branches[k - 1].generation : c.root_generation) !=
		        u.attention_generation)
			return false;
		*point = simulation_structure_position(s, u.attention, k);
		return true;
	}
	*point = u.flight_goal;
	return u.age > 0;
}
// A tile circle encloses its square. Reject tiles far from a segment so long
// target lines do not fill their entire screen-space bounding rectangle.
static bool tile_intersects(const EffectSegment& e, int x, int y)
{
	float ax = e.start[0], ay = e.start[1], dx = e.end[0] - ax,
	      dy = e.end[1] - ay;
	float cx = (x + .5f) * EFFECT_TILE, cy = (y + .5f) * EFFECT_TILE;
	float t = std::fmax(
	    0.0f, std::fmin(1.0f, ((cx - ax) * dx + (cy - ay) * dy) /
		                          std::fmax(MIN_TRAIL_SEGMENT_SQUARED_LENGTH,
		                                    dx * dx + dy * dy)));
	float radius = EFFECT_TILE * .70710679f + e.start[3] +
	               EFFECT_TILE_RADIUS_MARGIN_PIXELS;
	dx = cx - (ax + t * dx);
	dy = cy - (ay + t * dy);
	return dx * dx + dy * dy <= radius * radius;
}
static void tile_row(const EffectSegment& e, int y, int* low, int* high)
{
	float dy = e.end[1] - e.start[1];
	if (e.end[3] != 4 || std::fabs(dy) < .0001f)
		return;
	float radius = e.start[3] + EFFECT_TILE_RADIUS_MARGIN_PIXELS;
	float t0 = (y * EFFECT_TILE - radius - e.start[1]) / dy,
	      t1 = ((y + 1) * EFFECT_TILE + radius - e.start[1]) / dy;
	t0 = std::fmax(0.0f, std::fmin(1.0f, t0));
	t1 = std::fmax(0.0f, std::fmin(1.0f, t1));
	float x0 = e.start[0] + t0 * (e.end[0] - e.start[0]),
	      x1 = e.start[0] + t1 * (e.end[0] - e.start[0]);
	*low = std::max(
	    *low, (int)std::floor((std::fmin(x0, x1) - radius) / EFFECT_TILE));
	*high = std::min(
	    *high, (int)std::floor((std::fmax(x0, x1) + radius) / EFFECT_TILE));
}
static bool tile_rectangle(const EffectSegment& e, uint32_t width,
                           uint32_t height, int* r)
{
	float radius = e.start[3] + EFFECT_TILE_RADIUS_MARGIN_PIXELS;
	r[0] = (int)std::floor((std::fmin(e.start[0], e.end[0]) - radius) /
	                       EFFECT_TILE);
	r[1] = (int)std::floor((std::fmin(e.start[1], e.end[1]) - radius) /
	                       EFFECT_TILE);
	r[2] = (int)std::floor((std::fmax(e.start[0], e.end[0]) + radius) /
	                       EFFECT_TILE);
	r[3] = (int)std::floor((std::fmax(e.start[1], e.end[1]) + radius) /
	                       EFFECT_TILE);
	if (r[2] < 0 || r[3] < 0 || r[0] >= (int)width || r[1] >= (int)height)
		return false;
	r[0] = std::max(0, r[0]);
	r[1] = std::max(0, r[1]);
	r[2] = std::min((int)width - 1, r[2]);
	r[3] = std::min((int)height - 1, r[3]);
	return true;
}
bool effects_build(const Simulation* s, const Camera* camera,
                   const RenderConfig* config, EffectSegment* output,
                   uint32_t* count, uint32_t* tiles, uint32_t tile_count)
{
	*count = 0;
	uint32_t width = (config->width + EFFECT_TILE - 1) / EFFECT_TILE,
	         height = (config->height + EFFECT_TILE - 1) / EFFECT_TILE;
	if (tile_count != width * height)
		return false;
	// Layout: prefix offsets, temporary counts/cursors, then compact indices.
	uint32_t* offsets = tiles;
	uint32_t* cursors = tiles + tile_count + 1;
	uint32_t* indices = cursors + tile_count;
	memset(offsets, 0, (tile_count * 2 + 1) * sizeof(uint32_t));
	if (!s->world.combat.enabled)
		return true;
	Projection p = {};
	p.eye = camera->position;
	p.forward = normalize(subtract(camera->target, camera->position));
	p.right = normalize(cross(p.forward, camera->up));
	p.up = cross(p.right, p.forward);
	p.cx = config->width * .5f;
	p.cy = config->height * .5f;
	p.pixels =
	    config->height /
	    (2 * std::tan(config->vertical_fov_degrees * HALF_DEGREES_TO_RADIANS));
	const CombatVisuals* v = &s->world.combat;
	bool held[MAX_UNITS] = {};
	for (uint32_t i = 0; i < MAX_UNITS; ++i)
		if (!s->units[i].latch_colony &&
		    simulation_attachment_valid(s, &s->units[i]))
			held[s->units[i].latch] = true;
	for (uint32_t i = 0; i < MAX_UNITS; ++i)
	{
		const Unit* u = &s->units[i];
		if (u->health <= 0 || !s->visible_units[i])
			continue;
		Color color = content_color(&s->world.content, u->faction);
		if (u->selected)
		{
			Vec3 point;
			float age = (s->tick_count - u->path_marker_tick) /
			            SIMULATION_TICKS_PER_SECOND_FLOAT;
			float fade =
			    v->target_line_fade > 0
			        ? 1 - std::fmin(
			                  1.0f,
			                  std::fmax(0.0f, (age - v->target_line_hold) /
							                      v->target_line_fade))
			        : 1;
			if (v->target_line_alpha * fade > 0 && !held[i] &&
			    target_point(s, *u, &point))
				segment(output, count, p, u->position, point,
				        v->target_line_width, color,
				        v->target_line_alpha * fade, 4);
			segment(output, count, p, u->position, u->position,
			        simulation_unit_definition(s, u)->cue_radius, color,
			        SELECTION_CUE_ALPHA, 3);
		}
	}
	for (uint32_t i = 0; i < MAX_POPS; ++i)
	{
		const Pop* pop = &s->pops[i];
		if (pop->duration <= 0 || !simulation_in_sight(s, pop->position, 0))
			continue;
		float t = pop->age / pop->duration;
		float ease = 1 - (1 - t) * (1 - t);
		if (pop->blast_radius > 0)
		{
			float radius = pop->blast_radius * ease;
			Color color = content_color(&s->world.content, pop->faction);
			segment(output, count, p, pop->position, pop->position,
			        pop->blast_radius * BLAST_CORE_RADIUS_SCALE, color,
			        (1 - t) * (1 - t), 2);
			for (uint32_t j = 0; j < BLAST_RING_SEGMENTS; ++j)
			{
				float a = j * FULL_TURN_RADIANS / BLAST_RING_SEGMENTS,
				      b = (j + 1) * FULL_TURN_RADIANS / BLAST_RING_SEGMENTS;
				Vec3 x =
				    add(pop->position, scale(add(scale(p.right, std::cos(a)),
					                             scale(p.up, std::sin(a))),
					                         radius));
				Vec3 y =
				    add(pop->position, scale(add(scale(p.right, std::cos(b)),
					                             scale(p.up, std::sin(b))),
					                         radius));
				segment(output, count, p, x, y, v->trail_width, color, 1 - t,
				        1);
			}
			continue;
		}
		segment(output, count, p, pop->position, pop->position,
		        v->pop_radius * (DEATH_CORE_START_RADIUS_SCALE +
		                         DEATH_CORE_GROWTH_RADIUS_SCALE * ease),
		        content_color(&s->world.content, pop->faction),
		        (1 - t) * (1 - t), 2);
		for (int j = 0; j < DEATH_BURST_RAYS; ++j)
		{
			float angle = j * FULL_TURN_RADIANS / DEATH_BURST_RAYS +
			              i * BURST_GOLDEN_ANGLE_RADIANS;
			Vec3 direction = add(scale(p.right, std::cos(angle)),
			                     scale(p.up, std::sin(angle)));
			Vec3 a = add(
			    pop->position,
			    scale(direction,
				      v->pop_radius * (DEATH_RAY_INNER_START_RADIUS_SCALE +
				                       ease * DEATH_RAY_INNER_GROWTH_SCALE)));
			Vec3 b = add(
			    pop->position,
			    scale(direction,
				      v->pop_radius * (DEATH_RAY_OUTER_START_RADIUS_SCALE +
				                       ease * DEATH_RAY_OUTER_GROWTH_SCALE)));
			segment(output, count, p, a, b,
			        v->trail_width * DEATH_RAY_WIDTH_SCALE,
			        content_color(&s->world.content, pop->faction), 1 - t, 1);
		}
	}
	uint32_t cloud_references = 0;
	for (uint32_t i = 0; i < s->world.body_count && v->haze_alpha > 0; ++i)
	{
		const Colony& c = s->colonies[i];
		if (!c.active || !s->visible_bodies[i])
			continue;
		const ProducerDefinition& d = *simulation_producer_definition(s, i);
		if (d.haze_reach <= 0)
			continue;
		const Body& b = s->world.bodies[i];
		float age = (s->tick_count - c.established_tick) /
		            (float)SIMULATION_TICKS_PER_SECOND;
		float fade = v->haze_fade_seconds > 0
		                 ? std::fmin(1.0f, age / v->haze_fade_seconds)
		                 : 1;
		uint32_t before = *count;
		segment(output, count, p, b.position, b.position,
		        b.radius + d.haze_reach, v->haze_color, v->haze_alpha * fade,
		        CLOUD_EFFECT_KIND);
		int r[4];
		if (*count == before ||
		    !tile_rectangle(output[before], width, height, r))
			continue;
		cloud_references += (uint32_t)((r[2] - r[0] + 1) * (r[3] - r[1] + 1));
		if (cloud_references > CLOUD_REFERENCE_BUDGET)
		{
			*count = before;
			break;
		}
	}
	for (uint32_t i = 0; i < MAX_UNITS && v->aura_alpha > 0; ++i)
	{
		const Unit& u = s->units[i];
		if (u.health <= 0 || u.aura <= 0 || !s->visible_units[i])
			continue;
		float fade = std::fmin(1.0f, u.aura / AURA_FADE_SECONDS);
		uint32_t before = *count;
		segment(output, count, p, u.position, u.position,
		        simulation_unit_definition(s, &u)->aura_radius, v->aura_color,
		        v->aura_alpha * fade, CLOUD_EFFECT_KIND);
		int r[4];
		if (*count == before ||
		    !tile_rectangle(output[before], width, height, r))
			continue;
		cloud_references += (uint32_t)((r[2] - r[0] + 1) * (r[3] - r[1] + 1));
		if (cloud_references > CLOUD_REFERENCE_BUDGET)
		{
			*count = before;
			break;
		}
	}
	for (uint32_t i = 0; i < MAX_SPORE_CLOUDS && v->cloud_alpha > 0; ++i)
	{
		const SporeCloud& c = s->clouds[i];
		if (c.age >= c.seconds || !simulation_in_sight(s, c.position, c.radius))
			continue;
		float t = c.age / c.seconds,
		      fade = std::fmin(1.0f, t / CLOUD_RISE_FRACTION) *
			         std::fmin(1.0f, (1 - t) / CLOUD_FADE_FRACTION);
		uint32_t before = *count;
		segment(output, count, p, c.position, c.position, c.radius,
		        v->cloud_color, v->cloud_alpha * fade, CLOUD_EFFECT_KIND);
		int r[4];
		if (*count == before ||
		    !tile_rectangle(output[before], width, height, r))
			continue;
		cloud_references += (uint32_t)((r[2] - r[0] + 1) * (r[3] - r[1] + 1));
		if (cloud_references > CLOUD_REFERENCE_BUDGET)
		{
			*count = before;
			break;
		}
	}
	for (uint32_t i = 0; i < MAX_POPS; ++i)
	{
		const Impact* hit = &s->impacts[i];
		if (hit->duration <= 0 || !simulation_in_sight(s, hit->position, 0))
			continue;
		float t = hit->age / hit->duration,
		      radius = v->pop_radius * HIT_RADIUS_SCALE *
			           std::sqrt(t + HIT_RADIUS_START_TIME);
		for (int j = 0; j < HIT_BURST_RAYS; ++j)
		{
			float angle = j * FULL_TURN_RADIANS / HIT_BURST_RAYS +
			              i * BURST_GOLDEN_ANGLE_RADIANS;
			Vec3 direction = add(scale(p.right, std::cos(angle)),
			                     scale(p.up, std::sin(angle)));
			segment(output, count, p,
			        add(hit->position,
			            scale(direction, radius * HIT_RAY_INNER_RADIUS_SCALE)),
			        add(hit->position, scale(direction, radius)),
			        v->trail_width * HIT_RAY_WIDTH_SCALE, v->flash,
			        (1 - t) * (1 - t), 1);
		}
	}
	// Trails fill the segment pool last, so a crowded late game drops trail
	// segments rather than selection cues, target lines and combat effects.
	for (uint32_t i = 0; i < MAX_UNITS; ++i)
	{
		const Unit* u = &s->units[i];
		const Trail* t = &s->trails[i];
		if (u->health <= 0 || !simulation_unit_definition(s, u)->trails ||
		    !s->visible_units[i])
			continue;
		Vec3 origin = simulation_trail_origin(s, u), previous = origin;
		float total = 0;
		for (uint32_t j = 0; j < t->count; ++j)
		{
			Vec3 d = subtract(t->points[j], previous);
			total += std::sqrt(dot(d, d));
			previous = t->points[j];
		}
		float remaining = total * v->trail_length_scale, target = remaining;
		previous = origin;
		for (uint32_t j = 0; j < t->count && remaining > 0; ++j)
		{
			Vec3 delta = subtract(t->points[j], previous);
			float distance = std::sqrt(dot(delta, delta));
			if (distance <= 0)
				continue;
			float used = std::fmin(remaining, distance),
			      fade = (remaining - .5f * used) / target;
			Vec3 end = add(previous, scale(delta, used / distance));
			segment(output, count, p, previous, end, v->trail_width * fade,
			        content_color(&s->world.content, u->faction), fade * .5f,
			        0);
			previous = end;
			remaining -= used;
		}
	}
	for (uint32_t i = 0; i < *count; ++i)
	{
		int r[4];
		if (!tile_rectangle(output[i], width, height, r))
			continue;
		for (int y = r[1]; y <= r[3]; ++y)
		{
			int low = r[0], high = r[2];
			tile_row(output[i], y, &low, &high);
			for (int x = low; x <= high; ++x)
				if (tile_intersects(output[i], x, y))
					++cursors[y * width + x];
		}
	}
	for (uint32_t i = 0; i < tile_count; ++i)
	{
		offsets[i + 1] = offsets[i] + cursors[i];
		cursors[i] = offsets[i];
	}
	if (offsets[tile_count] > MAX_EFFECT_REFERENCES)
	{
		fprintf(stderr, "Visible effects exceed tile-reference capacity.\n");
		return false;
	}
	for (uint32_t i = 0; i < *count; ++i)
	{
		int r[4];
		if (!tile_rectangle(output[i], width, height, r))
			continue;
		for (int y = r[1]; y <= r[3]; ++y)
		{
			int low = r[0], high = r[2];
			tile_row(output[i], y, &low, &high);
			for (int x = low; x <= high; ++x)
				if (tile_intersects(output[i], x, y))
					indices[cursors[y * width + x]++] = i;
		}
	}
	return true;
}
