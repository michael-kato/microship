#ifndef NANOVA_RENDERER_H
#define NANOVA_RENDERER_H

#include <stdint.h>
#include "profiler.h"
#include "render_features.h"
#include "../assets/shaders/hud_layout.glsl"

struct RenderScene;
struct RenderConfig;
struct PointerInput;
struct Camera;
struct RendererState;

// One concrete Vulkan backend; the state owns resources allocated at startup.
struct Renderer
{
    RendererState* state;
    Profiler profiler;
    bool running;
    uint32_t scene_submissions;
    bool debug_stats;
    bool debug_views;
    uint32_t disabled_features,available_features;
    double previous_frame_ms;
    double gpu_frame_ms, work_frame_ms, acquire_ms, present_ms;
    bool overlay_enabled;
    char hud[HUD_TEXT_LINES][HUD_LINE_CAPACITY];
    int32_t selection_box[4];
    int32_t selection_marker[4];
    int32_t hover_marker[4];
    float hud_status[4],hud_accent[4],hud_feedback[4];
};

bool renderer_initialize(Renderer* renderer, const RenderScene* scene,
                         const RenderConfig* config, const char* shader_path, const char* builtin_shader_path,
                         bool headless, bool debug_views, bool no_activate);
bool renderer_poll(Renderer* renderer, PointerInput* input);
void renderer_toggle_features(Renderer* renderer, uint32_t mask);
bool renderer_render(Renderer* renderer, const RenderScene* scene, const Camera* camera, const char* capture_path);
bool renderer_shutdown(Renderer* renderer);

#endif
