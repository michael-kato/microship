#ifndef NANOVA_FLIGHT_H
#define NANOVA_FLIGHT_H

#include "simulation.h"

// Bound: distance from center to the farthest point of the capsule. Each cell's
// obstacles are contiguous, with a box around their bounding spheres.
struct FlightObstacle { Vec3 start,end,center;float radius,bound;uint32_t cell,structure; };
struct FlightObstacles
{
    FlightObstacle values[MAX_BODIES*MAX_FACTORY_STRUCTURES*(GROWTH_SEGMENTS+1)];
    uint32_t count,cell_count,first[MAX_BODIES+1];
    Vec3 low[MAX_BODIES],high[MAX_BODIES];
};
void flight_obstacles(const Simulation*,FlightObstacles*);
Vec3 flight_avoid(Simulation*,const FlightObstacles&,const Unit&,Vec3 desired,float speed);
void flight_clip(Simulation*,const FlightObstacles&,Unit*,Vec3 previous);

#endif
