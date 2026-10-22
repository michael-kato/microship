#ifndef NANOVA_GAME_H
#define NANOVA_GAME_H

#include <stdint.h>
#include "camera.h"

struct GameOptions
{
    const char* world_path;
    const char* player_faction; // Overrides match.player when set.
    const char* config_path;
    const char* shader_path;
    const char* builtin_shader_path;
    const char* capture_path;
    const char* audio_path;
    const char* settings_defaults_path;
    const char* settings_path; // Explicit file for preferences and menu diagnostics.
    bool pause_on_start; // Open the production pause menu before the first tick/frame.
    const char* audio_capture_path;
    uint32_t frames;
    uint32_t frame_ticks; // Fixed simulation work per frame in bounded diagnostics.
    uint32_t frame_delay_ms; // Bounded OS-input diagnostics only.
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
    uint32_t matches; // Balance runs: seeded matches played back to back, each until its outcome or --ticks.
    bool cpu_benchmark; // Measure initialization and each real simulation tick in --matches.
    PointerInput camera_input;
};

enum { GAME_RETURN_TO_MENU=2 };
int game_run(const GameOptions* options);

#endif
