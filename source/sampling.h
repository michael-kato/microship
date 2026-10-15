#ifndef MICROSHIP_SAMPLING_H
#define MICROSHIP_SAMPLING_H
#include <stdint.h>
// 256 dimensions, first twelve columns: application sample indices are <4096.
static const uint32_t SZ_COLUMN_COUNT = 256*12;
extern const uint32_t sz_columns[SZ_COLUMN_COUNT];
static const uint32_t SZ_DIAGONAL_COUNT = 7410;
extern const uint32_t sz_diagonals[SZ_DIAGONAL_COUNT];
// Scene-independent integer sequence values, generated only at renderer startup.
void make_sz_samples(uint32_t* output, uint32_t count);
#endif
