#include "camera.h"
#include "render_config.h"
#include "vector_math.h"
#include "math_constants.h"

static const float ZOOM_EXPONENT_PER_WHEEL_STEP = 0.12f;

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
        * std::tan(config->vertical_fov_degrees * HALF_DEGREES_TO_RADIANS) / config->height;
    Vec3 translation = add(scale(right, -input->drag_x * units_per_pixel),
                           scale(up, input->drag_y * units_per_pixel));
    float zoom = distance * std::exp(-input->wheel * ZOOM_EXPONENT_PER_WHEEL_STEP);
    if (zoom < config->camera_min_distance) zoom = config->camera_min_distance;
    if (zoom > config->camera_max_distance) zoom = config->camera_max_distance;
    // Pan across the map plane. Preserve camera height and viewing direction.
    Vec3 horizontal={right.x,right.y,0};float planar_length=std::sqrt(dot(horizontal,horizontal));
    float magnitude=std::sqrt(input->pan_x*input->pan_x+input->pan_y*input->pan_y);
    if (planar_length>0 && magnitude>0 && input->pan_seconds>0)
    {
        horizontal=scale(horizontal,1/planar_length);Vec3 vertical={-horizontal.y,horizontal.x,0};
        // Zooming out past the authored view speeds panning in proportion to distance.
        Vec3 authored=subtract(config->camera_position,config->camera_target);
        float zoom_scale=std::fmax(1.0f,zoom/std::sqrt(dot(authored,authored)));
        float step=config->camera_keyboard_speed*zoom_scale*input->pan_seconds/std::fmax(1.0f,magnitude);
        translation=add(translation,scale(add(scale(horizontal,input->pan_x),scale(vertical,input->pan_y)),step));
    }
    camera->target = add(camera->target, translation);
    // Preserve the point under the cursor on the target plane while dollying.
    if (input->wheel && input->pointer_valid)
    {
        Vec3 anchor=add(scale(right,(input->pointer_x-config->width*.5f)*units_per_pixel),
                        scale(up,(config->height*.5f-input->pointer_y)*units_per_pixel));
        camera->target=add(camera->target,scale(anchor,1-zoom/distance));
    }
    camera->position = add(camera->target, scale(offset, zoom / distance));
}
