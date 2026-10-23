#ifndef NANOVORE_ORDERS_H
#define NANOVORE_ORDERS_H
#include <stdint.h>
struct Simulation;
struct Audio;
// Replay supported gameplay controls before a frozen render, or in a CPU
// application run.
bool orders_run(Simulation* simulation, const char* path, uint32_t ticks,
                Audio* audio = 0);
#endif
