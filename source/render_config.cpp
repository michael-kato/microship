#include "render_config.h"
#include "asset_json.h"
#include <stdio.h>
#include <cmath>

static const char* validate_config(const RenderConfig* config)
{
    if (config->volume_rays_per_pixel > 4096)
        return "volume_rays_per_pixel must be in [0, 4096]";
    if (config->microfacet_energy_compensation > 1)
        return "microfacet_energy_compensation must be 0 or 1";
    if (config->bloom_radius < 1 || config->bloom_radius > 64
        || config->bloom_strength < 0 || config->bloom_strength > 1
        || config->bloom_threshold < 0 || config->bloom_threshold > 1000000)
        return "bloom requires radius [1, 64], strength [0, 1], threshold [0, 1000000]";
    if (config->camera_min_distance < 0.1f || config->camera_max_distance > 1000000.0f
        || config->camera_max_distance <= config->camera_min_distance)
        return "camera distance limits must satisfy 0.1 <= minimum < maximum <= 1000000";
    if (config->volume_steps < 2 || config->volume_steps > 64)
        return "volume_steps must be in [2, 64]";
    if (config->width == 0 || config->height == 0 || config->width > 4096 || config->height > 4096)
        return "width and height must be in [1, 4096]";
    if (config->samples_per_pixel == 0 || config->samples_per_pixel > 4096)
        return "samples_per_pixel must be in [1, 4096]";
    if (config->max_transmission_layers == 0 || config->max_transmission_layers > 32)
        return "max_transmission_layers must be in [1, 32]";
    if (config->sphere_segments < 3 || config->sphere_segments > 512
        || config->sphere_rings < 2 || config->sphere_rings > 256)
        return "sphere_segments must be in [3, 512] and sphere_rings in [2, 256]";
    if (config->exposure <= 0.0f || config->exposure > 100.0f)
        return "exposure must be positive and no greater than 100";
    if (config->vertical_fov_degrees <= 1.0f || config->vertical_fov_degrees >= 179.0f)
        return "vertical_fov_degrees must be greater than 1 and less than 179";

    if (std::fabs(config->camera_position.x) > 1000000.0f
        || std::fabs(config->camera_position.y) > 1000000.0f
        || std::fabs(config->camera_position.z) > 1000000.0f
        || std::fabs(config->camera_target.x) > 1000000.0f
        || std::fabs(config->camera_target.y) > 1000000.0f
        || std::fabs(config->camera_target.z) > 1000000.0f
        || std::fabs(config->camera_up.x) > 1000000.0f
        || std::fabs(config->camera_up.y) > 1000000.0f
        || std::fabs(config->camera_up.z) > 1000000.0f)
        return "camera components must have magnitude no greater than 1000000";

    // Double precision prevents finite asset coordinates from overflowing this check.
    double x = (double)config->camera_target.x - config->camera_position.x;
    double y = (double)config->camera_target.y - config->camera_position.y;
    double z = (double)config->camera_target.z - config->camera_position.z;
    double direction_squared = x * x + y * y + z * z;
    double up_squared = (double)config->camera_up.x * config->camera_up.x
        + (double)config->camera_up.y * config->camera_up.y
        + (double)config->camera_up.z * config->camera_up.z;
    double cross_x = y * config->camera_up.z - z * config->camera_up.y;
    double cross_y = z * config->camera_up.x - x * config->camera_up.z;
    double cross_z = x * config->camera_up.y - y * config->camera_up.x;
    double cross_squared = cross_x * cross_x + cross_y * cross_y + cross_z * cross_z;
    if (direction_squared < 1e-12 || up_squared < 1e-12
        || cross_squared <= direction_squared * up_squared * 1e-12)
        return "camera position, target, and up must define a nondegenerate basis";
    return 0;
}

bool render_config_load(RenderConfig* config,const char* path)
{
    JsonDocument document;if (!json_load(&document,path)) return false;
    Json root=json_root(&document);json_object(root,"$schema|output|sampling|volume|geometry|camera|lighting|bloom");RenderConfig loaded={};
    {
        Json j=json_get(root,"output");json_object(j,"width|height|exposure");
        loaded.width=json_uint(json_get(j,"width"),1,4096);
        loaded.height=json_uint(json_get(j,"height"),1,4096);
        loaded.exposure=json_positive(json_get(j,"exposure"),100);
    }
    {
        Json j=json_get(root,"sampling");json_object(j,"samples_per_pixel|max_transmission_layers|seed");
        loaded.samples_per_pixel=json_uint(json_get(j,"samples_per_pixel"),1,4096);
        loaded.max_transmission_layers=json_uint(json_get(j,"max_transmission_layers"),1,32);
        loaded.seed=json_uint(json_get(j,"seed"),0,UINT32_MAX);
    }
    {
        Json j=json_get(root,"volume");json_object(j,"rays_per_pixel|integration_steps");
        loaded.volume_steps=json_uint(json_get(j,"integration_steps"),2,64);
        loaded.volume_rays_per_pixel=json_uint(json_get(j,"rays_per_pixel"),0,4096);
    }
    {
        Json j=json_get(root,"geometry");json_object(j,"sphere_segments|sphere_rings");
        loaded.sphere_segments=json_uint(json_get(j,"sphere_segments"),3,512);
        loaded.sphere_rings=json_uint(json_get(j,"sphere_rings"),2,256);
    }
    {
        Json j=json_get(root,"camera");json_object(j,"position|target|up|min_distance|max_distance|vertical_fov_degrees");
        loaded.camera_position=json_vector(json_get(j,"position"),-1000000,1000000);
        loaded.camera_target=json_vector(json_get(j,"target"),-1000000,1000000);
        loaded.camera_up=json_vector(json_get(j,"up"),-1000000,1000000);
        loaded.camera_min_distance=json_float(json_get(j,"min_distance"),0.1,1000000);
        loaded.camera_max_distance=json_float(json_get(j,"max_distance"),0.1,1000000);
        loaded.vertical_fov_degrees=json_float(json_get(j,"vertical_fov_degrees"),1,179);
    }
    {
        Json j=json_get(root,"lighting");json_object(j,"microfacet_energy_compensation");
        loaded.microfacet_energy_compensation=json_bool(json_get(j,"microfacet_energy_compensation"));
    }
    {
        Json j=json_get(root,"bloom");json_object(j,"radius|strength|threshold");
        loaded.bloom_radius=json_uint(json_get(j,"radius"),1,64);
        loaded.bloom_strength=json_float(json_get(j,"strength"),0,1);
        loaded.bloom_threshold=json_float(json_get(j,"threshold"),0,1000000);
    }
    if (!document.valid) return false;
    const char* error=validate_config(&loaded);if (error) return json_error(root,error);
    *config=loaded;return true;
}
