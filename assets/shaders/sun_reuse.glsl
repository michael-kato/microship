// Current-frame specialization of Hedstrom et al. 2026, SPMIS Eqs. 16, 18, 19.
// Identity shifts in the uniform [0,1)^2 sun parameter domain (Jacobian = 1).
// One fresh uniform source candidate has UCW = 1. No prior-frame reservoirs.
struct SunSource
{
    vec4 point_roughness;
    vec4 normal_ior;
    vec4 view_metal;
    vec4 color;
    vec4 sample_cdf; // XY candidate, Z source proxy, W inclusive cell CDF.
};
#ifdef MICROSHIP_SUN_REUSE_PASS
layout(set = 0, binding = 8, std430) buffer SunSourceBuffer { SunSource sun_sources[]; };
#else
layout(set = 0, binding = 8, std430) readonly buffer SunSourceBuffer { SunSource sun_sources[]; };
#endif

float sun_proxy(vec3 point, vec3 normal, vec3 outgoing, vec3 color,
                float roughness, float ior, bool metal, vec2 sample_value)
{
    vec3 direction;
    float distance, light_pdf, bsdf_pdf;
    if (!sample_sun(point,sample_value,direction,distance,light_pdf)) return 0.0;
    vec3 bsdf = evaluate_bsdf(normal,outgoing,direction,color,roughness,ior,metal,bsdf_pdf);
    vec3 value = bsdf*render.sun_color.rgb*render.light.w*
        (max(0.0,dot(normal,direction))/light_pdf)*power_weight(light_pdf,bsdf_pdf);
    // Visibility and transmittance are evaluated once, after resampling. This
    // nonnegative unoccluded proxy can be positive where true transport is zero.
    return dot(value,vec3(0.2126,0.7152,0.0722));
}

float source_proxy(SunSource source, vec2 sample_value)
{
    return sun_proxy(source.point_roughness.xyz,source.normal_ior.xyz,source.view_metal.xyz,
        source.color.rgb,source.point_roughness.w,source.normal_ior.w,source.view_metal.w > 0.5,sample_value);
}

// N=2 stochastic neighbor draws, Nc=1 uniform canonical-correction draw, M=64.
// Noncanonical confidence is scaled by N/M, giving cSum=2 and cCanonical=1.
float reuse_sun(uvec2 pixel, Surface receiver, vec3 outgoing, PathSamples samples, inout vec2 selected)
{
    uint cells_x = (render.settings.x+7u)/8u;
    uint base = ((pixel.y/8u)*cells_x+pixel.x/8u)*64u;
    float total = sun_sources[base+63u].sample_cdf.w;
    uint state = sample_hash(samples.scramble ^ samples.index*0x9e3779b9u ^ 0x91e10da5u);
    float canonical_h = sun_proxy(receiver.point,receiver.normal,outgoing,receiver.color,
        receiver.roughness,receiver.ior,receiver.metal,selected);
    if (total <= 0.0 && canonical_h <= 0.0) return 0.0;
    uint correction = min(uint(random_float(state)*64.0),63u);
    float other_h = source_proxy(sun_sources[base+correction],selected);
    float canonical_m = 1.0/3.0;
    if (canonical_h > 0.0) canonical_m += (2.0/3.0)*canonical_h/(2.0*other_h+canonical_h);
    // Zero realized source weights do not prove their target functions have
    // empty support. Retain canonical correction even when no neighbor is drawn.
    if (total <= 0.0) return canonical_m;
    float sum_weight = canonical_m*canonical_h;
    float selected_h = canonical_h;

    for (uint draw = 0u; draw < 2u; ++draw)
    {
        float target = random_float(state)*total;
        uint low = 0u, high = 63u;
        while (low < high)
        {
            uint middle = (low+high)/2u;
            if (sun_sources[base+middle].sample_cdf.w <= target) low = middle+1u;
            else high = middle;
        }
        SunSource source = sun_sources[base+low];
        float h = source.sample_cdf.z;
        // Use the realized CDF interval, including FP scan rounding, as P(i).
        float previous = low == 0u ? 0.0 : sun_sources[base+low-1u].sample_cdf.w;
        float probability = (source.sample_cdf.w-previous)/total;
        if (h <= 0.0 || probability <= 0.0) continue;
        float receiver_h = sun_proxy(receiver.point,receiver.normal,outgoing,receiver.color,
            receiver.roughness,receiver.ior,receiver.metal,source.sample_cdf.xy);
        float mis = (2.0/(64.0*3.0*probability))*h/(2.0*h+receiver_h);
        float weight = mis*receiver_h;
        sum_weight += weight;
        if (weight > 0.0 && random_float(state)*sum_weight < weight)
        {
            selected = source.sample_cdf.xy;
            selected_h = receiver_h;
        }
    }
    return selected_h > 0.0 ? sum_weight/selected_h : 0.0;
}

#ifdef MICROSHIP_SUN_REUSE_PASS
shared float cell_cdf[64];
shared float cell_max[64];
vec3 film_direction(uvec2 pixel, vec2 jitter);

void build_sun_cell(uvec2 pixel)
{
    uint slot = gl_LocalInvocationIndex;
    uint base = (gl_WorkGroupID.y*gl_NumWorkGroups.x+gl_WorkGroupID.x)*64u;
    SunSource source = SunSource(vec4(0),vec4(0),vec4(0),vec4(0),vec4(0));
    uint state = sample_hash((pixel.y*render.settings.x+pixel.x) ^ render.scene.z ^ 0x51ed270bu);
    source.sample_cdf.xy = vec2(random_float(state),random_float(state));
    if (pixel.x < render.settings.x && pixel.y < render.settings.y)
    {
        vec3 direction = film_direction(pixel,vec2(0.5));
        uint triangle; vec2 barycentric; float distance;
        Surface surface;
        if (closest_hit(render.camera_position.xyz,direction,triangle,barycentric,distance) &&
            triangle != SUN_HIT && surface_at(triangle,barycentric,direction,surface))
        {
            source.point_roughness = vec4(surface.point,surface.roughness);
            source.normal_ior = vec4(surface.normal,surface.ior);
            source.view_metal = vec4(-direction,surface.metal ? 1.0 : 0.0);
            source.color = vec4(surface.color,0);
            source.sample_cdf.z = source_proxy(source,source.sample_cdf.xy);
        }
    }
    cell_max[slot] = source.sample_cdf.z;
    barrier();
    for (uint stride = 32u; stride > 0u; stride /= 2u)
    {
        if (slot < stride) cell_max[slot] = max(cell_max[slot],cell_max[slot+stride]);
        barrier();
    }
    // A small uniform mixture prevents positive targets disappearing through
    // CDF rounding. The realized selection PDF is used by the MIS estimator.
    cell_cdf[slot] = source.sample_cdf.z+cell_max[0]*0.0001;
    barrier();
    for (uint stride = 1u; stride < 64u; stride *= 2u)
    {
        float prefix = slot >= stride ? cell_cdf[slot-stride] : 0.0;
        barrier();
        cell_cdf[slot] += prefix;
        barrier();
    }
    source.sample_cdf.w = cell_cdf[slot];
    // Every padded slot is overwritten on every logical frame, even in vacuum.
    sun_sources[base+slot] = source;
}
#endif
