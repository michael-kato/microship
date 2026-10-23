#include "microfacet.h"
#include "vector_math.h"

static const int BIT_REVERSE_HALF_WORD_BITS = 16;
static const unsigned int BIT_REVERSE_ALTERNATE_MASK = 0x55555555u;
static const unsigned int BIT_REVERSE_PAIR_MASK = 0x33333333u;
static const unsigned int BIT_REVERSE_NIBBLE_MASK = 0x0f0f0f0fu;
static const unsigned int BIT_REVERSE_BYTE_MASK = 0x00ff00ffu;
static const int BITS_PER_BYTE = 8;
static const float FLOAT_MANTISSA_SAMPLE_COUNT = 16777216.0f;
static const int ENERGY_INTEGRATION_SAMPLES = 1024;
static const float MIN_GGX_ALPHA = 0.002f;
static const float MIN_VIEW_NORMAL_COSINE = 0.001f;
static const float FULL_TURN_RADIANS = 6.28318530718f;

static float radical_inverse(unsigned int bits)
{
	bits = (bits << BIT_REVERSE_HALF_WORD_BITS) |
	       (bits >> BIT_REVERSE_HALF_WORD_BITS);
	bits = ((bits & BIT_REVERSE_ALTERNATE_MASK) << 1) |
	       ((bits >> 1) & BIT_REVERSE_ALTERNATE_MASK);
	bits = ((bits & BIT_REVERSE_PAIR_MASK) << 2) |
	       ((bits >> 2) & BIT_REVERSE_PAIR_MASK);
	bits = ((bits & BIT_REVERSE_NIBBLE_MASK) << 4) |
	       ((bits >> 4) & BIT_REVERSE_NIBBLE_MASK);
	bits = ((bits & BIT_REVERSE_BYTE_MASK) << BITS_PER_BYTE) |
	       ((bits >> BITS_PER_BYTE) & BIT_REVERSE_BYTE_MASK);
	return (float)(bits >> BITS_PER_BYTE) *
	       (1.0f / FLOAT_MANTISSA_SAMPLE_COUNT);
}

void microfacet_energy_table(float* values)
{
	const unsigned int samples = ENERGY_INTEGRATION_SAMPLES;
	for (unsigned int y = 0; y < MICROFACET_TABLE_SIZE; ++y)
	{
		float roughness = (float)y / (MICROFACET_TABLE_SIZE - 1);
		float alpha = roughness * roughness;
		if (alpha < MIN_GGX_ALPHA)
			alpha = MIN_GGX_ALPHA;
		for (unsigned int x = 0; x < MICROFACET_TABLE_SIZE; ++x)
		{
			float nv = (float)x / (MICROFACET_TABLE_SIZE - 1);
			nv *= nv; // Extra resolution near grazing angles.
			if (nv < MIN_VIEW_NORMAL_COSINE)
				nv = MIN_VIEW_NORMAL_COSINE;
			Vec3 view = {std::sqrt(1.0f - nv * nv), 0.0f, nv};
			Vec3 stretched = normalize(Vec3{alpha * view.x, 0.0f, view.z});
			Vec3 t1 = {0.0f, 1.0f, 0.0f};
			Vec3 t2 = cross(stretched, t1);
			double energy = 0.0, bias = 0.0;
			for (unsigned int i = 0; i < samples; ++i)
			{
				float radius = std::sqrt((i + 0.5f) / samples);
				float angle = FULL_TURN_RADIANS * radical_inverse(i);
				float a = radius * std::cos(angle),
				      b = radius * std::sin(angle);
				float blend = 0.5f * (1.0f + stretched.z);
				b = (1.0f - blend) * std::sqrt(1.0f - a * a) + blend * b;
				float z2 = 1.0f - a * a - b * b;
				Vec3 h =
				    add(add(scale(t1, a), scale(t2, b)),
					    scale(stretched, std::sqrt(z2 > 0.0f ? z2 : 0.0f)));
				h = normalize(
				    Vec3{alpha * h.x, alpha * h.y, h.z > 0.0f ? h.z : 0.0f});
				float nl = 2.0f * dot(view, h) * h.z - view.z;
				// With VNDF sampling and Fresnel=1, BRDF*cos/PDF = Smith G1(L).
				if (nl > 0.0f)
				{
					float g1 =
					    2.0f * nl /
					    (nl + std::sqrt(alpha * alpha +
						                (1.0f - alpha * alpha) * nl * nl));
					float grazing = 1.0f - dot(view, h);
					if (grazing < 0.0f)
						grazing = 0.0f;
					energy += g1;
					bias +=
					    g1 * grazing * grazing * grazing * grazing * grazing;
				}
			}
			values[y * MICROFACET_TABLE_SIZE + x] = (float)(energy / samples);
			values[(MICROFACET_TABLE_SIZE + y) * MICROFACET_TABLE_SIZE + x] =
			    (float)(bias / samples);
		}
	}
}
