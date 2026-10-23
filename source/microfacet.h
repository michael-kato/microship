#ifndef NANOVORE_MICROFACET_H
#define NANOVORE_MICROFACET_H

enum
{
	MICROFACET_TABLE_SIZE = 32
};

// Two tables of the same separable-Smith GGX used by the shader: directional
// albedo with Fresnel = 1, then the Schlick bias integral of (1 - v.h)^5.
// Albedo for reflectance F0 is F0*(E - B) + B. Mathematical coefficients, not
// cached scene illumination. Writes 2*MICROFACET_TABLE_SIZE^2 values.
void microfacet_energy_table(float* values);

#endif
