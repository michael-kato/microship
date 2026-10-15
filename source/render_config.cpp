#include "render_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>

enum { RENDER_LINE_SIZE = 512, RENDER_FIELD_COUNT = 25 };

static const char* const field_names[RENDER_FIELD_COUNT] = {
    "width", "height", "samples_per_pixel", "max_bounces", "seed",
    "sphere_segments", "sphere_rings", "exposure", "vertical_fov_degrees",
    "sun_radiance", "camera_position", "camera_target", "camera_up", "background",
    "camera_min_distance", "camera_max_distance", "volume_steps",
    "bloom_radius", "bloom_strength", "bloom_threshold",
    "volume_samples_per_pixel", "volume_local_bounds", "microfacet_energy_compensation", "spatial_sun_reuse", "spatial_denoise"
};

static char* read_word(char** cursor)
{
    char* position = *cursor;
    while (isspace((unsigned char)*position))
        ++position;
    char* word = position;
    while (*position && !isspace((unsigned char)*position))
        ++position;
    if (*position)
        *position++ = 0;
    *cursor = position;
    return *word ? word : 0;
}

static bool read_uint(char* word, uint32_t* value)
{
    if (!word || *word < '0' || *word > '9')
        return false;
    char* end = 0;
    errno = 0;
    unsigned long parsed = strtoul(word, &end, 10);
    if (*end || errno == ERANGE || parsed > UINT32_MAX)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool read_float(char* word, float* value)
{
    if (!word)
        return false;
    char* end = 0;
    errno = 0;
    float parsed = strtof(word, &end);
    if (*end || errno == ERANGE || !std::isfinite(parsed))
        return false;
    *value = parsed;
    return true;
}

static bool read_vector(char** cursor, Vec3* value)
{
    return read_float(read_word(cursor), &value->x)
        && read_float(read_word(cursor), &value->y)
        && read_float(read_word(cursor), &value->z);
}

static const char* read_record(char* line, RenderConfig* config, uint32_t* seen)
{
    char* comment = strchr(line, '#');
    if (comment)
        *comment = 0;
    char* cursor = line;
    char* name = read_word(&cursor);
    if (!name)
        return 0;
    uint32_t field = 0;
    while (field != RENDER_FIELD_COUNT && strcmp(name, field_names[field]) != 0)
        ++field;
    if (field == RENDER_FIELD_COUNT)
        return "unknown rendering setting";
    if (*seen & (1u << field))
        return "duplicate rendering setting";

    bool valid = false;
    switch (field)
    {
    case 0: valid = read_uint(read_word(&cursor), &config->width); break;
    case 1: valid = read_uint(read_word(&cursor), &config->height); break;
    case 2: valid = read_uint(read_word(&cursor), &config->samples_per_pixel); break;
    case 3: valid = read_uint(read_word(&cursor), &config->max_bounces); break;
    case 4: valid = read_uint(read_word(&cursor), &config->seed); break;
    case 5: valid = read_uint(read_word(&cursor), &config->sphere_segments); break;
    case 6: valid = read_uint(read_word(&cursor), &config->sphere_rings); break;
    case 7: valid = read_float(read_word(&cursor), &config->exposure); break;
    case 8: valid = read_float(read_word(&cursor), &config->vertical_fov_degrees); break;
    case 9: valid = read_float(read_word(&cursor), &config->sun_radiance); break;
    case 10: valid = read_vector(&cursor, &config->camera_position); break;
    case 11: valid = read_vector(&cursor, &config->camera_target); break;
    case 12: valid = read_vector(&cursor, &config->camera_up); break;
    case 13:
    {
        Vec3 color = {};
        valid = read_vector(&cursor, &color);
        config->background = Color{ color.x, color.y, color.z };
        break;
    }
    case 14: valid = read_float(read_word(&cursor), &config->camera_min_distance); break;
    case 15: valid = read_float(read_word(&cursor), &config->camera_max_distance); break;
    case 16: valid = read_uint(read_word(&cursor), &config->volume_steps); break;
    case 17: valid = read_uint(read_word(&cursor), &config->bloom_radius); break;
    case 18: valid = read_float(read_word(&cursor), &config->bloom_strength); break;
    case 19: valid = read_float(read_word(&cursor), &config->bloom_threshold); break;
    case 20: valid = read_uint(read_word(&cursor), &config->volume_samples_per_pixel); break;
    case 21: valid = read_uint(read_word(&cursor), &config->volume_local_bounds); break;
    case 22: valid = read_uint(read_word(&cursor), &config->microfacet_energy_compensation); break;
    case 23: valid = read_uint(read_word(&cursor), &config->spatial_sun_reuse); break;
    case 24: valid = read_uint(read_word(&cursor), &config->spatial_denoise); break;
    }
    if (!valid || read_word(&cursor))
        return "invalid setting fields: expected one number or three vector components";
    *seen |= 1u << field;
    return 0;
}

static const char* validate_config(const RenderConfig* config)
{
    if (config->spatial_denoise > 1) return "spatial_denoise must be 0 or 1";
    if (config->spatial_sun_reuse > 1) return "spatial_sun_reuse must be 0 or 1";
    if (config->volume_samples_per_pixel > 4096)
        return "volume_samples_per_pixel must be in [0, 4096]";
    if (config->volume_local_bounds > 1 || config->microfacet_energy_compensation > 1)
        return "volume_local_bounds and microfacet_energy_compensation must be 0 or 1";
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
    if (config->max_bounces == 0 || config->max_bounces > 32)
        return "max_bounces must be in [1, 32]";
    if (config->sphere_segments < 3 || config->sphere_segments > 512
        || config->sphere_rings < 2 || config->sphere_rings > 256)
        return "sphere_segments must be in [3, 512] and sphere_rings in [2, 256]";
    if (config->exposure <= 0.0f || config->exposure > 100.0f)
        return "exposure must be positive and no greater than 100";
    if (config->vertical_fov_degrees <= 1.0f || config->vertical_fov_degrees >= 179.0f)
        return "vertical_fov_degrees must be greater than 1 and less than 179";
    if (config->sun_radiance < 0.0f || config->sun_radiance > 1000000.0f)
        return "sun_radiance must be in [0,1000000]";
    if (config->background.r < 0.0f || config->background.r > 1.0f
        || config->background.g < 0.0f || config->background.g > 1.0f
        || config->background.b < 0.0f || config->background.b > 1.0f)
        return "background channels must be in [0, 1]";

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

static bool read_config(FILE* file, const char* path, RenderConfig* config)
{
    uint32_t seen = 0;
    uint32_t line_number = 0;
    char line[RENDER_LINE_SIZE];
    for (;;)
    {
        uint32_t length = 0;
        int character = fgetc(file);
        if (character == EOF)
            break;
        ++line_number;
        while (character != EOF && character != '\n')
        {
            if (character == 0 || length == RENDER_LINE_SIZE - 1)
            {
                fprintf(stderr, "%s:%u: NUL byte or line exceeds 511 characters\n", path, line_number);
                return false;
            }
            line[length++] = (char)character;
            character = fgetc(file);
        }
        line[length] = 0;
        const char* error = read_record(line, config, &seen);
        if (error)
        {
            fprintf(stderr, "%s:%u: %s\n", path, line_number, error);
            return false;
        }
    }
    if (ferror(file))
    {
        fprintf(stderr, "%s: rendering settings read failed\n", path);
        return false;
    }
    for (uint32_t i = 0; i < RENDER_FIELD_COUNT; ++i)
    {
        if (!(seen & (1u << i)))
        {
            fprintf(stderr, "%s: missing rendering setting: %s\n", path, field_names[i]);
            return false;
        }
    }
    const char* error = validate_config(config);
    if (error)
        fprintf(stderr, "%s: %s\n", path, error);
    return error == 0;
}

bool render_config_load(RenderConfig* config, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "%s: cannot open rendering settings: %s\n", path, strerror(errno));
        return false;
    }
    RenderConfig loaded = {};
    bool success = read_config(file, path, &loaded);
    if (fclose(file) != 0)
    {
        fprintf(stderr, "%s: cannot close rendering settings: %s\n", path, strerror(errno));
        return false;
    }
    if (success)
        *config = loaded;
    return success;
}
