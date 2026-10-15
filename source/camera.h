#ifndef MICROSHIP_CAMERA_H
#define MICROSHIP_CAMERA_H

#include "types.h"
#include <stdint.h>

struct RenderConfig;
struct PointerInput
{
    float drag_x, drag_y, wheel;
    bool toggle_stats, toggle_buffers, click, command, pause, restart, focus, additive;
    int32_t click_x, click_y, command_x, command_y;
    int32_t pointer_x, pointer_y;
    bool pointer_valid;
    uint32_t group; // 1 fighters, 2 seedships, 3 all; zero leaves selection unchanged.
    uint32_t priority; // 1 fighter, 2 seedship.
};
struct Camera { Vec3 position, target, up; };

void camera_initialize(Camera* camera, const RenderConfig* config);
void camera_apply_input(Camera* camera, const PointerInput* input, const RenderConfig* config);

#endif
