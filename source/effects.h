#ifndef MICROSHIP_EFFECTS_H
#define MICROSHIP_EFFECTS_H
#include <stdint.h>
struct Simulation;
struct Camera;
struct RenderConfig;
// Worst case: every trail, projectile, unit cue, death burst and impact is live.
enum { MAX_EFFECT_SEGMENTS=8192, EFFECT_TILE=16, MAX_EFFECT_REFERENCES=MAX_EFFECT_SEGMENTS*64 };
struct EffectSegment { float start[4],end[4],color[4]; };
// Caller owns preallocated output. No graphics objects, allocation or frame history.
bool effects_build(const Simulation*,const Camera*,const RenderConfig*,EffectSegment*,uint32_t*,uint32_t*,uint32_t);
#endif
