#ifndef NANOVA_ASSET_LIMITS_H
#define NANOVA_ASSET_LIMITS_H

// Name and path capacities include the terminating NUL byte.
enum { ASSET_NAME_CAPACITY=32, ASSET_PATH_CAPACITY=256 };
enum { UNIT_STORAGE_CAPACITY=4096, SIMULATION_TICKS_PER_SECOND=60 };

#endif
