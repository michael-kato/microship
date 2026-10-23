#ifndef NANOVORE_ORDERS_H
#define NANOVORE_ORDERS_H
#include <stdint.h>
struct Simulation;
struct Audio;
// Replay supported gameplay controls before a frozen render, or in a CPU
// application run.
// Plain decimal digits within [minimum, maximum], as replay files and the
// command line write them.
bool parse_integer(const char* text, uint32_t minimum, uint32_t maximum,
                   uint32_t* value);
bool orders_run(Simulation* simulation, const char* path, uint32_t ticks,
                Audio* audio = 0);
#endif
