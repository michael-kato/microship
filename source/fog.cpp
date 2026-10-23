#include "simulation.h"
#include "vector_math.h"
#include <algorithm>
#include <cmath>

struct FogSource
{
	Vec3 center;
	float radius;
};

static float sample_field(const SightField& field, float x, float y)
{
	float u = (x - field.low_x) / field.spacing,
	      v = (y - field.low_y) / field.spacing;
	if (u < 0 || v < 0 || u > SIGHT_FIELD_SIDE - 1 || v > SIGHT_FIELD_SIDE - 1)
		return 0;
	int ix = std::min((int)u, SIGHT_FIELD_SIDE - 2),
	    iy = std::min((int)v, SIGHT_FIELD_SIDE - 2);
	float fx = u - ix, fy = v - iy;
	const float* row = &field.reveal[iy * SIGHT_FIELD_SIDE + ix];
	return (row[0] * (1 - fx) + row[1] * fx) * (1 - fy) +
	       (row[SIGHT_FIELD_SIDE] * (1 - fx) + row[SIGHT_FIELD_SIDE + 1] * fx) *
	           fy;
}

static void resize_field(SightField* field, float low_x, float low_y,
                         float span)
{
	SightField previous = *field;
	field->low_x = low_x;
	field->low_y = low_y;
	field->spacing = span / (SIGHT_FIELD_SIDE - 1);
	for (int y = 0; y < SIGHT_FIELD_SIDE; ++y)
		for (int x = 0; x < SIGHT_FIELD_SIDE; ++x)
			field->reveal[y * SIGHT_FIELD_SIDE + x] =
			    previous.spacing > 0
			        ? sample_field(previous, low_x + x * field->spacing,
			                       low_y + y * field->spacing)
			        : 0;
}

static void stamp_source(SightField* field, const FogSource& source,
                         float feather, float plane)
{
	float extent = source.radius + feather;
	int x0 =
	    std::max(0, (int)std::floor((source.center.x - extent - field->low_x) /
		                            field->spacing));
	int y0 =
	    std::max(0, (int)std::floor((source.center.y - extent - field->low_y) /
		                            field->spacing));
	int x1 = std::min(SIGHT_FIELD_SIDE - 1,
	                  (int)std::ceil((source.center.x + extent - field->low_x) /
	                                 field->spacing));
	int y1 = std::min(SIGHT_FIELD_SIDE - 1,
	                  (int)std::ceil((source.center.y + extent - field->low_y) /
	                                 field->spacing));
	float dz = plane - source.center.z;
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x)
		{
			float dx = field->low_x + x * field->spacing - source.center.x,
			      dy = field->low_y + y * field->spacing - source.center.y;
			float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			float t = std::fmax(
			    0.0f, std::fmin(1.0f, (distance - source.radius + feather) /
				                          (2 * feather)));
			float reveal = 1 - t * t * (3 - 2 * t);
			float& value = field->reveal[y * SIGHT_FIELD_SIDE + x];
			value = std::fmax(value, reveal);
		}
}

void simulation_update_fog(Simulation* s, float dt)
{
	const World& w = s->world;
	if (w.sight_radius <= 0 || w.fog_blur <= 0)
		return;
	SightField* field = &s->fog_memory;
	FogSource sources[MAX_UNITS + MAX_BODIES];
	uint32_t count = 0;
	for (uint32_t p = 0; p < w.body_count; ++p)
		if (s->colonies[p].active && s->colonies[p].faction == w.player_faction)
			sources[count++] = FogSource{w.bodies[p].position,
			                             w.sight_radius + w.bodies[p].radius};
	uint32_t colony_sources = count;
	for (uint32_t i = 0; i < MAX_UNITS; ++i)
		if (s->units[i].health > 0 && s->units[i].faction == w.player_faction)
			sources[count++] = FogSource{s->units[i].position, w.sight_radius};
	if (field->spacing <= 0)
	{
		float low_x = w.bodies[0].position.x, high_x = low_x,
		      low_y = w.bodies[0].position.y, high_y = low_y;
		for (uint32_t p = 0; p < w.body_count; ++p)
		{
			const Body& b = w.bodies[p];
			low_x = std::fmin(low_x, b.position.x - b.radius);
			high_x = std::fmax(high_x, b.position.x + b.radius);
			low_y = std::fmin(low_y, b.position.y - b.radius);
			high_y = std::fmax(high_y, b.position.y + b.radius);
		}
		for (uint32_t i = 0; i < count; ++i)
		{
			low_x = std::fmin(low_x, sources[i].center.x);
			high_x = std::fmax(high_x, sources[i].center.x);
			low_y = std::fmin(low_y, sources[i].center.y);
			high_y = std::fmax(high_y, sources[i].center.y);
		}
		float margin = 2 * (w.sight_radius + w.fog_feather),
		      span = std::fmax(1.0f, std::fmax(high_x - low_x, high_y - low_y) +
			                             2 * margin);
		resize_field(field, (low_x + high_x - span) * .5f,
		             (low_y + high_y - span) * .5f, span);
	}
	// Expand in whole powers of two only when a scout leaves the field. Camera
	// movement cannot move, create, or erase sight. Remapping preserves old
	// sight.
	float span = field->spacing * (SIGHT_FIELD_SIDE - 1), low_x = field->low_x,
	      low_y = field->low_y;
	for (uint32_t i = 0; i < count; ++i)
	{
		float radius = sources[i].radius + w.fog_feather;
		Vec3 c = sources[i].center;
		while (c.x - radius < low_x || c.y - radius < low_y ||
		       c.x + radius > low_x + span || c.y + radius > low_y + span)
		{
			low_x -= span * .5f;
			low_y -= span * .5f;
			span *= 2;
		}
	}
	if (span > field->spacing * (SIGHT_FIELD_SIDE - 1) * 1.001f)
		resize_field(field, low_x, low_y, span);
	float decay = w.fog_half_life > 0 ? std::exp2(-dt / w.fog_half_life) : 0;
	for (int i = 0; i < SIGHT_FIELD_CELLS; ++i)
		field->reveal[i] *= decay;
	// Units in one field cell share a stamp: the first one's disc stands for
	// the rest, which lie within one cell diagonal of it. Swarms stamp per
	// occupied cell.
	bool stamped[SIGHT_FIELD_CELLS] = {};
	for (uint32_t i = 0; i < count; ++i)
	{
		if (i >= colony_sources)
		{
			int x = std::min(
			    SIGHT_FIELD_SIDE - 1,
			    std::max(0, (int)((sources[i].center.x - field->low_x) /
				                  field->spacing)));
			int y = std::min(
			    SIGHT_FIELD_SIDE - 1,
			    std::max(0, (int)((sources[i].center.y - field->low_y) /
				                  field->spacing)));
			bool& cell = stamped[y * SIGHT_FIELD_SIDE + x];
			if (cell)
				continue;
			cell = true;
		}
		stamp_source(field, sources[i], w.fog_feather, w.fog_plane_z);
	}
}
