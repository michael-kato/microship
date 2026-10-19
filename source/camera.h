#ifndef NANOVA_CAMERA_H
#define NANOVA_CAMERA_H

#include "types.h"
#include <stdint.h>

struct RenderConfig;
struct PointerInput
{
    float drag_x, drag_y, wheel;
    float pan_x,pan_y,pan_seconds; // Held keyboard axes and elapsed input time.
    bool toggle_stats, toggle_buffers, click, command, pause, restart, focus, additive;
    int32_t click_x, click_y, command_x, command_y;
    int32_t pointer_x, pointer_y;
    bool pointer_valid;
    bool pointer_pressed,pointer_held,pointer_released,pointer_cancelled,pointer_moved;
    int32_t press_x,press_y;
    uint32_t toggle_render_features; // F5 through F10, one bit per feature.
    uint32_t group; // 1 fighters, 2 bombers, 3 all; zero keeps the selection.
    uint32_t screen_group; // 1 fighters, 2 bombers in the current view.
};
struct Camera { Vec3 position, target, up; };

void camera_initialize(Camera* camera, const RenderConfig* config);
void camera_apply_input(Camera* camera, const PointerInput* input, const RenderConfig* config);

#endif
