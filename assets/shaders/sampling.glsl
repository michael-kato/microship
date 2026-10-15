// SZ matrix decomposition and NILE O2 element adapted from Abdalla G. M. Ahmed.
// MIT license: third_party/nile/LICENSE. Mathematical coefficients, no lighting.
layout(set = 0, binding = 7, std430) readonly buffer SamplingBuffer { uint sz_columns[]; };

struct PathSamples { uint index; uint scramble; };

uint sample_hash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

#ifdef MICROSHIP_REFERENCE_SAMPLING
#include "../../tests/e2e/reference/sampling_diagonals.glsl"
#elif defined(MICROSHIP_COLUMN_SAMPLING)
// Previous sparse-column path for matched real-application timing comparisons.
uint sz_sample(uint index, uint dimension)
{
    uint result = 0u;
    while (index != 0u)
    {
        result ^= sz_columns[dimension*12u+uint(findLSB(index))];
        index &= index-1u;
    }
    return result;
}
#else
uint sz_sample(uint index, uint dimension)
{
    // The exact SZ sequence for the configured sample range. Generated from
    // matrix columns at startup, independent of pixels, scene, seed and frames.
    // Prefix: 3072 matrix columns + 7410 author reference diagonals.
    return sz_columns[10482u+index*256u+dimension];
}
#endif

uint nile_x(uint value)
{
    value ^= (value & 0x33333333u) << 1u;
    value ^= (value & 0x0f0f0f0fu) << 2u;
    value ^= (value & 0x00ff00ffu) << 4u;
    value ^= (value & 0x0000ffffu) << 8u;
    return value;
}

float sample_1d(PathSamples samples, uint dimension)
{
    uint value = sz_sample(samples.index,dimension) ^
                 sample_hash(samples.scramble ^ (dimension*0x9e3779b9u));
    return float(value >> 8u)*(1.0/16777216.0);
}

vec2 sample_2d(PathSamples samples, uint dimension)
{
    uint value = sz_sample(samples.index,dimension);
    uvec2 point = uvec2(nile_x(value),nile_x(value ^ ((value & 0x55555555u) << 1u)));
    // Independent per-pixel digital shifts keep marginal samples randomized.
    // They preserve each pixel's NILE element, not the paper's superpixel order.
    point ^= uvec2(sample_hash(samples.scramble ^ (dimension*0x9e3779b9u)),
                   sample_hash(samples.scramble ^ (dimension*0x9e3779b9u) ^ 0x68bc21ebu));
    return vec2(point >> 8u)*(1.0/16777216.0);
}
