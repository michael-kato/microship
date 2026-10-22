#ifndef NANOVORE_DISPLAY_TRANSFORM_GLSL
#define NANOVORE_DISPLAY_TRANSFORM_GLSL

// AgX display transform for linear Rec.709 scene radiance (Troy Sobotka's AgX
// base look, minimal fit by Benjamin Wrensch). An inset matrix, a log2 encode
// over [-12.47, 4.03] EV around middle grey, a sigmoid and the outset matrix
// map scene light to display light. Bright saturated colors desaturate toward
// white instead of clipping or shifting hue per channel; middle grey 0.18
// stays near 0.18. The result is linear display light in [0, 1].
const float AGX_MIN_EV = -12.47393;
const float AGX_MAX_EV = 4.026069;
const float AGX_DISPLAY_GAMMA = 2.2;
const float AGX_MIN_INPUT = 1e-10;

vec3 agx_contrast(vec3 x)
{
    vec3 x2=x*x,x4=x2*x2;
    return 15.5*x4*x2-40.14*x4*x+31.96*x4-6.868*x2*x+0.4298*x2+0.1191*x-0.00232;
}

vec3 agx_display(vec3 linear)
{
    const mat3 inset=mat3(
        0.842479062253094,0.0423282422610123,0.0423756549057051,
        0.0784335999999992,0.878468636469772,0.0784336,
        0.0792237451477643,0.0791661274605434,0.879142973793104);
    const mat3 outset=mat3(
        1.19687900512017,-0.0528968517574562,-0.0529716355144438,
        -0.0980208811401368,1.15190312990417,-0.0980434501171241,
        -0.0990297440797205,-0.0989611768448433,1.15107367264116);
    vec3 encoded=clamp(log2(max(inset*linear,vec3(AGX_MIN_INPUT))),AGX_MIN_EV,AGX_MAX_EV);
    vec3 display=agx_contrast((encoded-AGX_MIN_EV)/(AGX_MAX_EV-AGX_MIN_EV));
    return clamp(pow(max(outset*display,vec3(0)),vec3(AGX_DISPLAY_GAMMA)),0.0,1.0);
}

#endif
