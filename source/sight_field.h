#ifndef NANOVORE_SIGHT_FIELD_H
#define NANOVORE_SIGHT_FIELD_H
// World-space visibility memory. This is simulation data, not rendered history.
enum
{
	SIGHT_FIELD_SIDE = 128,
	SIGHT_FIELD_CELLS = SIGHT_FIELD_SIDE * SIGHT_FIELD_SIDE
};
struct SightField
{
	float low_x, low_y, spacing;
	float reveal[SIGHT_FIELD_CELLS];
};
#endif
