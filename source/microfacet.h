#ifndef NANOVA_MICROFACET_H
#define NANOVA_MICROFACET_H

enum { MICROFACET_TABLE_SIZE = 32 };

// Directional albedo of the same separable-Smith GGX used by the shader,
// with Fresnel = 1. Mathematical coefficients, not cached scene illumination.
void microfacet_energy_table(float* values);

#endif
