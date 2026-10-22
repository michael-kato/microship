#ifndef NANOVA_MAP_LAYOUT_H
#define NANOVA_MAP_LAYOUT_H

#include <stdint.h>
#include "types.h"
#include "asset_limits.h"

enum { MAX_LAYOUT_CELLS=CELL_STORAGE_CAPACITY };

// A circular field: homes sit evenly on its circle and an optional center cell
// sits near its middle. Every other cell sits at the centroid of its seeded
// Voronoi region, clipped to the field disk.
struct VoronoiLayout
{
    float field_radius,first_home_degrees,minimum_gap;
    uint32_t cells,homes,centroid_steps;
    float home_radius,center_radius,center_jitter; // Zero center radius: no center cell.
    float neutral_radius[2]; // Smallest and largest; larger regions get larger cells.
};
struct LayoutCell { Vec3 position;float radius; };

// Cells are ordered homes, then the center cell, then neutral cells. Returns false
// when no seeded attempt keeps every cell at least minimum_gap from its neighbours.
bool voronoi_layout(const VoronoiLayout& layout,uint32_t seed,LayoutCell* cells);

#endif
