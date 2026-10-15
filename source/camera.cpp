#include "camera.h"
#include "render_config.h"
#include "vector_math.h"

void camera_initialize(Camera* camera, const RenderConfig* config)
{
    camera->position = config->camera_position;
    camera->target = config->camera_target;
    camera->up = config->camera_up;
}

void camera_apply_input(Camera* camera, const PointerInput* input, const RenderConfig* config)
{
    Vec3 offset = subtract(camera->position, camera->target);
    float distance = std::sqrt(dot(offset, offset));
    Vec3 forward = scale(offset, -1.0f / distance);
    Vec3 right = normalize(cross(forward, camera->up));
    Vec3 up = cross(right, forward);
    float units_per_pixel = 2.0f * distance
        * std::tan(config->vertical_fov_degrees * 0.00872664626f) / config->height;
    Vec3 translation = add(scale(right, -input->drag_x * units_per_pixel),
                           scale(up, input->drag_y * units_per_pixel));
    camera->target = add(camera->target, translation);
    float zoom = distance * std::exp(-input->wheel * 0.12f);
    if (zoom < config->camera_min_distance) zoom = config->camera_min_distance;
    if (zoom > config->camera_max_distance) zoom = config->camera_max_distance;
    // Preserve the point under the cursor on the target plane while dollying.
    if (input->wheel && input->pointer_valid)
    {
        Vec3 anchor=add(scale(right,(input->pointer_x-config->width*.5f)*units_per_pixel),
                        scale(up,(config->height*.5f-input->pointer_y)*units_per_pixel));
        camera->target=add(camera->target,scale(anchor,1-zoom/distance));
    }
    camera->position = add(camera->target, scale(offset, zoom / distance));
}
