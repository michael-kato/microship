#ifndef NANOVA_GAME_H
#define NANOVA_GAME_H

#include <stdint.h>
#include "camera.h"

struct GameOptions
{
    const char* world_path;
    const char* config_path;
    const char* shader_path;
    const char* builtin_shader_path;
    const char* capture_path;
    const char* audio_path;
    const char* audio_capture_path;
    uint32_t frames;
    uint32_t samples;
    uint32_t seed;
    uint32_t world_seed;
    bool world_seed_set;
    bool seed_set;
    bool vsync;
    bool smoke;
    bool headless;
    bool stats_set, show_stats, debug_views, no_activate;
    double time_seconds;
    double step_seconds;
    bool time_set;
    const char* replay_path;
    const char* state_path;
    const char* scene_state_path;
    uint32_t ticks;
    PointerInput camera_input;
};

int game_run(const GameOptions* options);

#endif
