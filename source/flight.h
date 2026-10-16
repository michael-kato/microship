#ifndef MICROSHIP_FLIGHT_H
#define MICROSHIP_FLIGHT_H

#include "simulation.h"

struct FlightObstacle { Vec3 start,end;float radius;uint32_t cell,structure; };
struct FlightObstacles
{
    FlightObstacle values[MAX_BODIES*MAX_FACTORY_STRUCTURES*(GROWTH_SEGMENTS+1)];
    uint32_t count;
};
void flight_obstacles(const Simulation*,FlightObstacles*);
Vec3 flight_avoid(Simulation*,const FlightObstacles&,const Unit&,Vec3 desired,float speed);
void flight_clip(Simulation*,const FlightObstacles&,Unit*,Vec3 previous);

#endif
