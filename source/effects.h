#ifndef NANOVORE_EFFECTS_H
#define NANOVORE_EFFECTS_H
#include "render_scene.h"
struct Simulation;
struct Camera;
struct RenderConfig;
// Caller owns preallocated output. No graphics objects, allocation or frame
// history.
bool effects_build(const Simulation*, const Camera*, const RenderConfig*,
                   EffectSegment*, uint32_t*, uint32_t*, uint32_t);
#endif
