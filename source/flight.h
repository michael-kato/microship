#ifndef NANOVORE_FLIGHT_H
#define NANOVORE_FLIGHT_H

#include "simulation.h"

void flight_obstacles(const Simulation*, FlightObstacles*);
Vec3 flight_avoid(Simulation*, const FlightObstacles&, const Unit&,
                  Vec3 desired, float speed);
void flight_clip(Simulation*, const FlightObstacles&, Unit*, Vec3 previous);

#endif
