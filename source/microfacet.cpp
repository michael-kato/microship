#include "microfacet.h"
#include "vector_math.h"

static float radical_inverse(unsigned int bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits >> 1) & 0x55555555u);
    bits = ((bits & 0x33333333u) << 2) | ((bits >> 2) & 0x33333333u);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits >> 4) & 0x0f0f0f0fu);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits >> 8) & 0x00ff00ffu);
    return (float)(bits >> 8) * (1.0f / 16777216.0f);
}

void microfacet_energy_table(float* values)
{
    const unsigned int samples = 1024;
    for (unsigned int y = 0; y < MICROFACET_TABLE_SIZE; ++y)
    {
        float roughness = (float)y / (MICROFACET_TABLE_SIZE - 1);
        float alpha = roughness * roughness;
        if (alpha < 0.002f) alpha = 0.002f;
        for (unsigned int x = 0; x < MICROFACET_TABLE_SIZE; ++x)
        {
            float nv = (float)x / (MICROFACET_TABLE_SIZE - 1);
            nv *= nv; // Extra resolution near grazing angles.
            if (nv < 0.001f) nv = 0.001f;
            Vec3 view = {std::sqrt(1.0f - nv*nv), 0.0f, nv};
            Vec3 stretched = normalize(Vec3{alpha*view.x, 0.0f, view.z});
            Vec3 t1 = {0.0f, 1.0f, 0.0f};
            Vec3 t2 = cross(stretched, t1);
            double energy = 0.0;
            for (unsigned int i = 0; i < samples; ++i)
            {
                float radius = std::sqrt((i + 0.5f) / samples);
                float angle = 6.28318530718f * radical_inverse(i);
                float a = radius * std::cos(angle), b = radius * std::sin(angle);
                float blend = 0.5f * (1.0f + stretched.z);
                b = (1.0f-blend)*std::sqrt(1.0f-a*a) + blend*b;
                float z2 = 1.0f-a*a-b*b;
                Vec3 h = add(add(scale(t1, a), scale(t2, b)),
                    scale(stretched, std::sqrt(z2 > 0.0f ? z2 : 0.0f)));
                h = normalize(Vec3{alpha*h.x, alpha*h.y, h.z > 0.0f ? h.z : 0.0f});
                float nl = 2.0f*dot(view,h)*h.z-view.z;
                // With VNDF sampling and Fresnel=1, BRDF*cos/PDF = Smith G1(L).
                if (nl > 0.0f)
                    energy += 2.0f*nl / (nl + std::sqrt(alpha*alpha + (1.0f-alpha*alpha)*nl*nl));
            }
            values[y*MICROFACET_TABLE_SIZE+x] = (float)(energy/samples);
        }
    }
}
