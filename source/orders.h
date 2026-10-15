#ifndef MICROSHIP_ORDERS_H
#define MICROSHIP_ORDERS_H
#include <stdint.h>
struct Simulation;
// Replay supported gameplay controls before a frozen render, or in a CPU application run.
bool orders_run(Simulation* simulation, const char* path, uint32_t ticks);
#endif
