#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#include <vulkan/vulkan.h>

#include "renderer.h"
#include "render_config.h"
#include "geometry.h"
#include "microfacet.h"
#include "window.h"
#include "render_scene.h"
#include "vector_math.h"
#include "math_constants.h"

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <cmath>
#include <float.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>
#include "debug_font.h"
#include "../assets/shaders/render_bindings.glsl"
#include "../assets/shaders/hud_layout.glsl"

static_assert(SIGHT_FIELD_SIDE==FOG_FIELD_SIZE,"Fog CPU/GPU grid layout");
static const int GPU_VERTEX_BYTES = 64;
static const int GPU_MEDIUM_BYTES = 96;
// Scene style floats, then surface weight, fog reveal, SSS weight and ray mask.
static const int INSTANCE_STYLE_GPU_FLOATS = INSTANCE_STYLE_FLOATS+4;
static const float MAX_PROJECTED_PIXELS = 1e6f;
static const int GPU_RENDER_UNIFORM_BYTES = 656;
static const int GPU_DEBUG_PUSH_BYTES = 128;
static const double MILLISECONDS_PER_SECOND = 1000.0;
static const double BINARY_UNIT_SCALE = 1024.0;
static const float LUMINANCE_RED_WEIGHT = .2126f;
static const float LUMINANCE_GREEN_WEIGHT = .7152f;
static const float LUMINANCE_BLUE_WEIGHT = .0722f;
static const double BACKDROP_TIME_WRAP_SECONDS = 10000.0;
static const int GPU_DEBUG_PIXEL_BYTES = 96;
static const int COMPANION_SHADER_PATH_CAPACITY = 1024;
static const double NANOSECONDS_PER_MILLISECOND = 1000000.0;
static const int RENDER_STORAGE_BUFFER_BINDINGS = 19;
static const int RENDER_DESCRIPTOR_SET_COUNT = 3;
static const int RENDER_STORAGE_IMAGE_BINDINGS = 3;
static const int RENDER_SAMPLER_BINDINGS = 4;
static const uint32_t OPTICAL_DEPTH_SUBSTEPS = 4;
static const int POOL_STORAGE_BUFFER_DESCRIPTORS = RENDER_DESCRIPTOR_SET_COUNT*RENDER_STORAGE_BUFFER_BINDINGS;
static const int POOL_SAMPLER_DESCRIPTORS = RENDER_DESCRIPTOR_SET_COUNT*RENDER_SAMPLER_BINDINGS;
static const int DESCRIPTOR_POOL_TYPE_COUNT = 5;
static const int BACKDROP_PUSH_BYTES = 16;
static const int GPU_TRIANGLE_BYTES = 16;
static const uint32_t THIN_MEMBRANE_TRIANGLE = 3; // GpuTriangle material tag.
static const int HUD_MAX_LINE_CHARACTERS = (HUD_TEXT_WIDTH-2*TEXT_MARGIN)/HUD_MASK_CHARACTER_SPACING;
static const unsigned int STATS_TEXT_WIDTH_UNSIGNED = STATS_TEXT_WIDTH;
static const unsigned int STATS_TEXT_HEIGHT_UNSIGNED = STATS_TEXT_HEIGHT;
static const int GPU_TIMESTAMP_COUNT = 17;
static const int TIMESTAMP_WORD_BITS = 64;

enum { SCENE_BUFFER_VERTICES=0, SCENE_BUFFER_TRIANGLES=1, SCENE_BUFFER_UNIFORM=2, SCENE_BUFFER_MEDIA=3, SCENE_BUFFER_ENERGY=4, SCENE_BUFFER_RESERVED=5, SCENE_BUFFER_DEBUG=6 };

enum { GPU_TIME_FRAME_BEGIN=0, GPU_TIME_SURFACE_BEGIN=1, GPU_TIME_SURFACE_END=2, GPU_TIME_VOLUME_END=3, GPU_TIME_EFFECTS_BEGIN=4, GPU_TIME_BLOOM_END=5, GPU_TIME_OVERLAY_END=6, GPU_TIME_BACKDROP_END=7, GPU_TIME_UPLOAD_END=8, GPU_TIME_EFFECTS_END=9, GPU_TIME_GRID_END=10, GPU_TIME_MEDIUM_BLUR_END=11, GPU_TIME_FOG_END=12, GPU_TIME_FOCUS_END=13, GPU_TIME_COPY_END=14, GPU_TIME_BAKE_END=15, GPU_TIME_CASCADE_END=16 };

struct GpuBuffer
{
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkDeviceAddress address;
    void* mapped;
};

struct MediumRayNode { float low[3];uint32_t escape;float high[3];uint32_t medium; };
static_assert(sizeof(MediumRayNode)==32,"Medium ray node GPU layout");

struct LightingInstance
{
    float transform[3][4];
    uint32_t first,count,cache_first,active;
};
static_assert(sizeof(LightingInstance)==64,"Lighting instance shader layout");
// Inputs of one slot that static light transport depends on.
struct LightingKey { float transform[3][4];uint32_t mesh,mask; };
struct WorldBounds { float low[3],high[3]; };

struct RenderUniform
{
    float camera_position[4];
    float camera_forward[4];
    float camera_right[4];
    float camera_up[4];
    float background[4];
    uint32_t settings[4];
    uint32_t scene[4];
    float display[4];
    uint32_t quality[4];
    float scene_min[4];
    float scene_max[4];
    float ambient_top[4], ambient_bottom[4];
    float foreground_min[4], foreground_max[4];
    uint32_t background_quality[4];
    float backdrop_style[4], backdrop_animation[4];
    float studio_direction[3][4], studio_color[3][4], thin_material[4], backdrop_wave[4];
    float fog[4],fog_tint[4],membrane_fade[4],focus_band[4],subsurface[4];
    uint32_t subsurface_quality[4];
    float fog_grid[4];
    float organelle_style[4],membrane_ripple[4],emission_fresnel[4],emission_color[4];
    uint32_t cascades[4];
    float cascade_interval[4];
    float wireframe_line[4],wireframe_fill[4];
};

static_assert(sizeof(GpuVertex) == GPU_VERTEX_BYTES, "Vertex shader layout");
static_assert(sizeof(GpuTriangle) == GPU_TRIANGLE_BYTES, "Triangle shader layout");
static_assert(sizeof(RenderMedium) == GPU_MEDIUM_BYTES, "Medium shader layout");
static_assert(sizeof(RenderUniform) == GPU_RENDER_UNIFORM_BYTES, "Uniform shader layout");

struct BloomParameters
{
    int32_t extent[2];
    int32_t radius;
    int32_t pass;
    float threshold;
    float strength;
    float exposure;
    float debug_views;
    uint32_t emissive_only;
};

struct DebugParameters
{
    uint32_t extent[2], views, stats;
    float settings[4];
    int32_t marker[4];
    float status[4],accent[4],feedback[4];
    int32_t hover[4],box[4];
};
static_assert(sizeof(DebugParameters) == GPU_DEBUG_PUSH_BYTES, "Debug push-constant layout");

struct ProfileBars { int32_t rectangles[PROFILE_BAR_COUNT][4];float colors[PROFILE_BAR_COUNT][4]; };
static_assert(PROFILE_NODE_COUNT==PROFILE_BAR_COUNT,"Profiler CPU/GPU bar layout");
static_assert(FONT_UNIFORM_BYTES+(STATS_TEXT_WIDTH*STATS_TEXT_HEIGHT+HUD_TEXT_WIDTH*HUD_TEXT_HEIGHT)/8+sizeof(ProfileBars)<=65536,"Debug uniform fits the Vulkan minimum range");

struct MeshAllocation
{
    GpuBuffer storage,scratch;
    VkAccelerationStructureKHR acceleration;
    VkDeviceAddress address;
    MeshRange range;
    uint32_t opaque_count; // Opaque triangles first, then thin membranes.
    uint32_t refits,unchanged; // Updates since the last full build; frames since the last change.
};

enum { FRAMES_IN_FLIGHT=2 };
// Values a frame's timing report needs after its GPU work completes.
struct FrameReport
{
    RenderUniform uniform;
    uint32_t disabled_features,lighting_bakes,lighting_reuses,lighting_surface_bakes,optical_depth_bakes,cascade_updates;
    bool debug_views,debug_stats,background_layer;
};
// Everything the CPU writes for one frame. GPU-only images and caches are
// shared: one queue executes frames in order and barriers order their reuse.
struct FrameResources
{
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkFence fence;
    VkSemaphore image_available;
    VkQueryPool queries;
    GpuBuffer uniform,instances,media,medium_ray_nodes,lighting_instances,lighting_bounds,sight,instance_styles,effects,effect_tiles,debug_text,vertex_upload;
    VkDescriptorSet descriptors,effect_descriptors,debug_descriptors,bloom_descriptors;
    bool pending; // Submitted; timestamps not yet reported.
    FrameReport report;
};

struct RendererState
{
    Window window;
    RenderConfig config;
    bool headless;
    bool debug_available, custom_shader, no_activate;
    bool validation_layer;
    std::atomic<bool> validation_failed;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    uint32_t timestamp_bits,subgroup_size;
    VkDeviceSize scratch_alignment;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkPresentModeKHR present_mode;
    std::vector<VkImage> swap_images;
    FrameResources frames[FRAMES_IN_FLIGHT];
    uint32_t frame; // Slot being recorded; the other slot may still execute.
    std::vector<VkSemaphore> render_finished; // One per swapchain image.
    // Non-owning handles of the slot being recorded.
    VkCommandBuffer command;
    VkQueryPool queries;
    GpuBuffer vertices;
    GpuBuffer triangles; // Each mesh orders its opaque triangles before its membranes.
    GpuBuffer indices;
    GpuBuffer mesh_ranges; // Per mesh: first triangle, opaque count.
    std::vector<VkDeviceSize> vertex_upload_offsets;
    GpuBuffer tlas_buffer;
    GpuBuffer tlas_scratch;
    GpuBuffer shadow_grid,sharp_layer,static_lighting,surface_lighting,dynamic_lighting;
    GpuBuffer radiance_cascades; // Cascade 0 SH projection, then every cascade; persists as bounce feedback.
    bool cascades_initialized;
    std::vector<uint32_t> cascade_age; // Per medium: frames since its cascades were traced.
    uint32_t cascade_updates;
    std::vector<LightingInstance> lighting_input; // Active instances in TLAS order.
    std::vector<uint32_t> slot_cache_first; // Per scene slot: first surface-cache triangle.
    uint32_t surface_cache_triangles;
    std::vector<uint8_t> surface_valid; // Per scene slot: baked thin-surface samples match current inputs.
    std::vector<WorldBounds> baked_slot_bounds; // Per scene slot, at its last bake.
    std::vector<uint32_t> active_slots; // Scene slot of each compacted active instance.
    std::vector<WorldBounds> lighting_changes; // Changed regions found this frame; preallocated.
    std::vector<LightingKey> baked_instances; // Per scene slot.
    std::vector<uint64_t> baked_mesh_revisions;
    std::vector<RenderMedium> baked_media;
    std::vector<RenderGridBounds> baked_bounds;
    std::vector<uint8_t> lighting_valid;
    std::vector<uint8_t> current_static; // The current grid holds exactly the valid static field.
    RenderUniform baked_uniform;
    bool lighting_dependencies_valid;
    uint32_t lighting_bakes,lighting_reuses,lighting_surface_bakes;
    std::vector<uint32_t> surface_cache_meshes,surface_slots;
    uint32_t medium_count;
    std::vector<uint32_t> medium_ray_order;
    std::vector<MediumRayNode> medium_ray_scratch;
    PFN_vkCmdCopyBufferToImage copy_buffer_to_image;
    PFN_vkCmdFillBuffer fill_buffer;
    PFN_vkCmdCopyBuffer copy_buffer;
    VkImage lighting_image;
    VkDeviceMemory lighting_memory;
    VkImageView lighting_view;
    VkExtent3D lighting_extent;
    std::vector<VkBufferImageCopy> lighting_copies,changed_copies; // Every block; blocks written this frame.
    std::vector<uint8_t> grid_written; // The current pass rewrote this medium's grid this frame.
    bool lighting_image_initialized;
    VkImage optical_depth_image;
    VkDeviceMemory optical_depth_memory;
    VkImageView optical_depth_view;
    GpuBuffer optical_depth_upload;
    std::vector<float> optical_depth_fades;
    bool optical_depth_initialized;
    uint32_t optical_depth_size,optical_depth_layers,optical_depth_bakes;
    GpuBuffer energy;
    GpuBuffer debug_pixels; // Full F2 diagnostic records; minimal unless buffer views can open.
    GpuBuffer guides; // Compact current-frame guides and spatial volume anchors.
    bool debug_records;

    std::vector<float> style_data;
    uint32_t effect_count, effect_tile_count;
    RenderUniform uniform_data;
    uint32_t text_mask[(STATS_TEXT_WIDTH*STATS_TEXT_HEIGHT+HUD_TEXT_WIDTH*HUD_TEXT_HEIGHT)/TEXT_MASK_WORD_BITS];
    std::vector<VkAccelerationStructureInstanceKHR> instance_data; // Active instances only.
    GpuBuffer readback;
    VkAccelerationStructureKHR tlas;
    std::vector<MeshAllocation> meshes;
    uint32_t mesh_count, instance_count, active_count; // Scene slots; slots with a nonzero mask.
    size_t effect_tile_size;
    VkImage hdr;
    VkDeviceMemory hdr_memory;
    VkImageView hdr_view;
    VkImage hdr_scratch;
    VkDeviceMemory hdr_scratch_memory;
    VkImageView hdr_scratch_view;
    VkImage backdrop_images[2];
    VkDeviceMemory backdrop_memory[2];
    VkImageView backdrop_views[2];
    VkSampler backdrop_sampler;
    VkPipeline backdrop_pipeline;
    bool backdrop_initialized;
    bool hdr_initialized;
    VkDescriptorSetLayout bloom_descriptor_layout;
    VkDescriptorPool bloom_descriptor_pool;
    VkDescriptorSet bloom_descriptors;
    VkPipelineLayout bloom_layout;
    VkPipeline bloom_pipeline,fog_pipeline,medium_blur_pipeline,focus_pipeline;
    VkPipeline guide_surface_pipeline;
    VkPipeline no_sss_pipeline,no_sss_guide_pipeline,no_sss_debug_pipeline;
    VkImage output;
    VkDeviceMemory output_memory;
    VkImageView output_view;
    VkImageLayout output_layout;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptors;
    VkDescriptorSet effect_descriptors;
    VkPipeline effects_pipeline;
    VkDescriptorSet debug_descriptors;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkPipeline volume_pipeline,shadow_grid_pipeline,volume_coarse_pipeline,volume_filter_pipeline,cascade_pipeline;
    VkPipeline debug_surface_pipeline, debug_volume_pipeline, debug_overlay_pipeline;
    VkPipelineLayout debug_overlay_layout;
    double trace_ms, volume_ms, bloom_ms;
    uint32_t triangle_count;
    std::vector<unsigned char> capture_row;
    std::vector<float> capture_hdr_row;
    PFN_vkCreateAccelerationStructureKHR create_as;
    PFN_vkDestroyAccelerationStructureKHR destroy_as;
    PFN_vkGetAccelerationStructureBuildSizesKHR get_as_sizes;
    PFN_vkCmdBuildAccelerationStructuresKHR build_as;
    PFN_vkGetAccelerationStructureDeviceAddressKHR get_as_address;
};

static double renderer_ms()
{
    return profiler_clock_ms();
}

static bool vk_ok(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS)
        return true;
    fprintf(stderr, "%s failed (Vulkan result %d).\n", operation, result);
    return false;
}

static FrameResources& frame_data(RendererState* state)
{
    return state->frames[state->frame];
}

static VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* message, void* user)
{
    fprintf(stderr, "Vulkan validation: %s\n", message->pMessage);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ((RendererState*)user)->validation_failed.store(true);
    return VK_FALSE;
}

static bool create_instance(RendererState* state)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, 0);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    bool validation = false;
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0)
            validation = true;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    state->validation_layer = validation;
    const char* extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME
#ifdef _WIN32
        ,VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
    };
    VkApplicationInfo application = {};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "nanovore";
    application.apiVersion = VK_API_VERSION_1_2;
    VkDebugUtilsMessengerCreateInfoEXT debug = {};
    debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = validation_message;
    debug.pUserData = state;
    VkInstanceCreateInfo create = {};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    create.enabledExtensionCount = state->headless ? 1 : 3;
    create.ppEnabledExtensionNames = extensions;
    create.enabledLayerCount = validation ? 1 : 0;
    create.ppEnabledLayerNames = &layer;
    // Proton's host layers are not listed by its Windows loader. Keep the
    // messenger active so host-layer errors can still reach this application.
    create.pNext = &debug;
    if (!vk_ok(vkCreateInstance(&create, 0, &state->instance), "Create Vulkan instance"))
        return false;
    PFN_vkCreateDebugUtilsMessengerEXT create_debug =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(state->instance, "vkCreateDebugUtilsMessengerEXT");
    if (!create_debug || !vk_ok(create_debug(state->instance, &debug, 0, &state->debug_messenger), "Create validation messenger"))
        return false;
    if (state->headless)
        return true;
#ifndef _WIN32
    fprintf(stderr,"Native diagnostic requires --headless.\n");return false;
#else
    printf("Window startup: open Win32 window.\n");
    if (!window_open(&state->window, state->config.width, state->config.height, !state->no_activate,state->config.borderless))
        return false;
    VkWin32SurfaceCreateInfoKHR surface = {};
    surface.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface.hinstance = state->window.instance;
    surface.hwnd = state->window.handle;
    printf("Window startup: create Vulkan surface.\n");
    return vk_ok(vkCreateWin32SurfaceKHR(state->instance, &surface, 0, &state->surface), "Create window surface");
#endif
}

static bool supports_device(RendererState* state, VkPhysicalDevice physical, uint32_t* family)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_2)
        return false;
    if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
        && properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        return false;
    VkPhysicalDeviceRayQueryFeaturesKHR ray = {};
    ray.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {};
    acceleration.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    acceleration.pNext = &ray;
    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &acceleration;
    VkPhysicalDeviceFeatures2 features = {};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    if (!ray.rayQuery || !acceleration.accelerationStructure || !features12.bufferDeviceAddress)
        return false;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, 0);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
    for (uint32_t i = 0; i < count; ++i)
    {
        VkBool32 present = VK_TRUE;
        if (state->surface)
            vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, state->surface, &present);
        uint32_t needed = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
        if ((queues[i].queueFlags & needed) == needed && present && queues[i].timestampValidBits)
        {
            *family = i;
            state->timestamp_bits = queues[i].timestampValidBits;
            return true;
        }
    }
    return false;
}

static bool report_validation(RendererState* state)
{
    // Query actual active tools rather than assuming an environment setting
    // proves that a layer loaded. The EXT alias works with our Vulkan 1.2 API.
    PFN_vkGetPhysicalDeviceToolPropertiesEXT get_tools =
        (PFN_vkGetPhysicalDeviceToolPropertiesEXT)vkGetInstanceProcAddr(state->instance, "vkGetPhysicalDeviceToolPropertiesEXT");
    if (get_tools)
    {
        uint32_t count = 0;
        if (!vk_ok(get_tools(state->physical, &count, 0), "Count active Vulkan tools")) return false;
        std::vector<VkPhysicalDeviceToolPropertiesEXT> tools(count);
        for (uint32_t i = 0; i < count; ++i)
            tools[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT;
        if (count && !vk_ok(get_tools(state->physical, &count, tools.data()), "Query active Vulkan tools")) return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (strcmp(tools[i].layer, "VK_LAYER_KHRONOS_validation") == 0
                && (tools[i].purposes & VK_TOOL_PURPOSE_VALIDATION_BIT_EXT))
            {
                state->validation_layer = true;
                printf("Validation tool: %s %s (%s).\n", tools[i].name, tools[i].version, tools[i].layer);
            }
        }
    }
    printf("Vulkan validation: %s.\n", state->validation_layer ? "enabled" : "not reported by loader/tooling");
    return true;
}

static bool create_device(RendererState* state)
{
    uint32_t count = 0;
    if (!vk_ok(vkEnumeratePhysicalDevices(state->instance, &count, 0), "Enumerate GPUs"))
        return false;
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(state->instance, &count, devices.data());
    for (uint32_t i = 0; i < count; ++i)
    {
        if (supports_device(state, devices[i], &state->queue_family))
        {
            state->physical = devices[i];
            break;
        }
    }
    if (!state->physical)
    {
        fprintf(stderr, "No hardware GPU supports the required Vulkan ray-query features.\n");
        return false;
    }
    if (!report_validation(state)) return false;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_properties = {};
    acceleration_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceDriverProperties driver = {};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    acceleration_properties.pNext = &driver;
    VkPhysicalDeviceSubgroupSizeControlPropertiesEXT subgroup_properties={};
    subgroup_properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT;
    driver.pNext=&subgroup_properties;
    VkPhysicalDeviceProperties2 properties = {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &acceleration_properties;
    vkGetPhysicalDeviceProperties2(state->physical, &properties);
    state->properties = properties.properties;
    state->scratch_alignment = acceleration_properties.minAccelerationStructureScratchOffsetAlignment;
    vkGetPhysicalDeviceMemoryProperties(state->physical, &state->memory_properties);
    printf("GPU: %s\n", state->properties.deviceName);
    printf("Driver: %s; %s\n", driver.driverName, driver.driverInfo);
    printf("Device API: %u.%u.%u; acceleration structures, ray queries, buffer addresses enabled.\n",
        VK_VERSION_MAJOR(state->properties.apiVersion), VK_VERSION_MINOR(state->properties.apiVersion),
        VK_VERSION_PATCH(state->properties.apiVersion));
    for (uint32_t i = 0; i < state->memory_properties.memoryHeapCount; ++i)
        if (state->memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            printf("Device-local heap: %.2f GiB (capacity, not available budget).\n",
                state->memory_properties.memoryHeaps[i].size / (BINARY_UNIT_SCALE * BINARY_UNIT_SCALE * BINARY_UNIT_SCALE));
    state->subgroup_size=state->custom_shader ? 0u : state->config.subgroup_size;
    VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroup={};
    subgroup.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
    if (state->subgroup_size)
    {
        PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions=(PFN_vkEnumerateDeviceExtensionProperties)
            vkGetInstanceProcAddr(state->instance,"vkEnumerateDeviceExtensionProperties");
        if (!enumerate_extensions) { fprintf(stderr,"GPU extension query is unavailable.\n");return false; }
        uint32_t extension_count=0;
        if (!vk_ok(enumerate_extensions(state->physical,0,&extension_count,0),"Count GPU extensions")) return false;
        std::vector<VkExtensionProperties> available(extension_count);
        if (!vk_ok(enumerate_extensions(state->physical,0,&extension_count,available.data()),"Read GPU extensions")) return false;
        bool supported=false;
        for (uint32_t i=0;i<extension_count;++i)
            if (!strcmp(available[i].extensionName,VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)) supported=true;
        VkPhysicalDeviceFeatures2 supported_features={};supported_features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported_features.pNext=&subgroup;vkGetPhysicalDeviceFeatures2(state->physical,&supported_features);
        if (!supported || !subgroup.subgroupSizeControl
            || !(subgroup_properties.requiredSubgroupSizeStages&VK_SHADER_STAGE_COMPUTE_BIT)
            || state->subgroup_size<subgroup_properties.minSubgroupSize || state->subgroup_size>subgroup_properties.maxSubgroupSize
            || COMPUTE_GROUP_SIZE*COMPUTE_GROUP_SIZE>state->subgroup_size*subgroup_properties.maxComputeWorkgroupSubgroups)
        { fprintf(stderr,"Requested compute subgroup size %u is unsupported.\n",state->subgroup_size);return false; }
        subgroup.computeFullSubgroups=VK_FALSE;
    }
    printf("Compute subgroups: %u (zero uses driver choice).\n",state->subgroup_size);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = state->queue_family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceRayQueryFeaturesKHR ray = {};
    ray.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    ray.rayQuery = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {};
    acceleration.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    acceleration.accelerationStructure = VK_TRUE;
    acceleration.pNext = &ray;
    ray.pNext=state->subgroup_size ? &subgroup : 0;
    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.pNext = &acceleration;
    const char* extensions[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo create = {};
    create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create.pNext = &features12;
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue;
    // Keep swapchain last; omit optional subgroup control when it is not requested.
    uint32_t extension_count=3;
    if (state->subgroup_size) ++extension_count;
    if (!state->headless) extensions[extension_count++]=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    create.enabledExtensionCount = extension_count;
    create.ppEnabledExtensionNames = extensions;
    if (!vk_ok(vkCreateDevice(state->physical, &create, 0, &state->device), "Create Vulkan device"))
        return false;
    vkGetDeviceQueue(state->device, state->queue_family, 0, &state->queue);
    state->create_as = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(state->device, "vkCreateAccelerationStructureKHR");
    state->destroy_as = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(state->device, "vkDestroyAccelerationStructureKHR");
    state->get_as_sizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(state->device, "vkGetAccelerationStructureBuildSizesKHR");
    state->build_as = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(state->device, "vkCmdBuildAccelerationStructuresKHR");
    state->get_as_address = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(state->device, "vkGetAccelerationStructureDeviceAddressKHR");
    state->copy_buffer = (PFN_vkCmdCopyBuffer)vkGetDeviceProcAddr(state->device, "vkCmdCopyBuffer");
    return state->copy_buffer && state->create_as && state->destroy_as && state->get_as_sizes && state->build_as && state->get_as_address;
}

static uint32_t memory_type(RendererState* state, uint32_t bits, VkMemoryPropertyFlags required,
                            VkMemoryPropertyFlags preferred = 0)
{
    if (preferred)
        for (uint32_t i = 0; i < state->memory_properties.memoryTypeCount; ++i)
            if ((bits & (1u << i))
                && (state->memory_properties.memoryTypes[i].propertyFlags & (required | preferred)) == (required | preferred))
                return i;
    for (uint32_t i = 0; i < state->memory_properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (state->memory_properties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    return UINT32_MAX;
}

static bool create_buffer(RendererState* state, GpuBuffer* buffer, VkDeviceSize size,
                          VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    buffer->size = size;
    VkBufferCreateInfo create = {};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = size;
    create.usage = usage;
    if (!vk_ok(vkCreateBuffer(state->device, &create, 0, &buffer->handle), "Create buffer"))
        return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(state->device, buffer->handle, &requirements);
    VkMemoryAllocateFlagsInfo flags = {};
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags = usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
    VkMemoryAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = flags.flags ? &flags : 0;
    allocation.allocationSize = requirements.size;
    VkMemoryPropertyFlags preferred = 0;
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        && (usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)))
        preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    allocation.memoryTypeIndex = memory_type(state, requirements.memoryTypeBits, properties, preferred);
    if (allocation.memoryTypeIndex == UINT32_MAX)
    {
        fprintf(stderr, "Required GPU memory type is unavailable.\n");
        return false;
    }
    if (!vk_ok(vkAllocateMemory(state->device, &allocation, 0, &buffer->memory), "Allocate buffer memory")
        || !vk_ok(vkBindBufferMemory(state->device, buffer->handle, buffer->memory, 0), "Bind buffer memory"))
        return false;
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        if (!vk_ok(vkMapMemory(state->device, buffer->memory, 0, size, 0, &buffer->mapped), "Map buffer memory"))
            return false;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        VkBufferDeviceAddressInfo address = {};
        address.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        address.buffer = buffer->handle;
        buffer->address = vkGetBufferDeviceAddress(state->device, &address);
    }
    return true;
}

static void destroy_buffer(RendererState* state, GpuBuffer* buffer)
{
    if (buffer->mapped)
        vkUnmapMemory(state->device, buffer->memory);
    if (buffer->handle)
        vkDestroyBuffer(state->device, buffer->handle, 0);
    if (buffer->memory)
        vkFreeMemory(state->device, buffer->memory, 0);
    *buffer = GpuBuffer();
}

static bool create_commands(RendererState* state)
{
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
    {
        FrameResources& frame=state->frames[f];
        VkCommandPoolCreateInfo pool = {};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex = state->queue_family;
        if (!vk_ok(vkCreateCommandPool(state->device, &pool, 0, &frame.command_pool), "Create command pool"))
            return false;
        VkCommandBufferAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = frame.command_pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        if (!vk_ok(vkAllocateCommandBuffers(state->device, &allocate, &frame.command), "Allocate command buffer"))
            return false;
        VkFenceCreateInfo fence = {};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkQueryPoolCreateInfo query = {};
        query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query.queryCount = GPU_TIMESTAMP_COUNT;
        if (!vk_ok(vkCreateFence(state->device, &fence, 0, &frame.fence), "Create frame fence")
            || !vk_ok(vkCreateQueryPool(state->device, &query, 0, &frame.queries), "Create timestamp queries"))
            return false;
    }
    return true;
}

// Records into the current slot after its previous submission completes.
static bool begin_commands(RendererState* state)
{
    FrameResources& frame=frame_data(state);
    if (!vk_ok(vkWaitForFences(state->device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "Wait for GPU")
        || !vk_ok(vkResetCommandPool(state->device, frame.command_pool, 0), "Reset commands"))
        return false;
    state->command=frame.command;state->queries=frame.queries;
    state->descriptors=frame.descriptors;state->effect_descriptors=frame.effect_descriptors;
    state->debug_descriptors=frame.debug_descriptors;state->bloom_descriptors=frame.bloom_descriptors;
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vk_ok(vkBeginCommandBuffer(state->command, &begin), "Begin commands");
}

// Presentation submissions wait for the slot's acquire and signal the image's
// present semaphore. Only startup work and captures wait for completion here.
static bool submit_commands(RendererState* state, bool presentation, uint32_t image_index=0, bool wait=true)
{
    FrameResources& frame=frame_data(state);
    if (!vk_ok(vkEndCommandBuffer(state->command), "End commands")
        || !vk_ok(vkResetFences(state->device, 1, &frame.fence), "Reset frame fence"))
        return false;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &state->command;
    submit.waitSemaphoreCount = presentation ? 1 : 0;
    submit.pWaitSemaphores = &frame.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.signalSemaphoreCount = presentation ? 1 : 0;
    submit.pSignalSemaphores = presentation ? &state->render_finished[image_index] : 0;
    if (!vk_ok(vkQueueSubmit(state->queue, 1, &submit, frame.fence), "Submit commands")) return false;
    return !wait || vk_ok(vkWaitForFences(state->device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "Wait for frame");
}

static VkDeviceAddress aligned_scratch(const RendererState* state,VkDeviceAddress address)
{
    return (address+state->scratch_alignment-1)&~(state->scratch_alignment-1);
}

static VkAccelerationStructureGeometryKHR instance_geometry(VkDeviceAddress instances)
{
    VkAccelerationStructureGeometryKHR geometry = {};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instances;
    return geometry;
}

// Full build of the slot's compacted active instances. A fresh build keeps the
// tree matched to current positions; refitting kept the startup topology.
static void record_tlas_build(RendererState* state,VkDeviceAddress instances)
{
    VkAccelerationStructureGeometryKHR geometry=instance_geometry(instances);
    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.dstAccelerationStructure = state->tlas;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    build.scratchData.deviceAddress = aligned_scratch(state,state->tlas_scratch.address);
    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount = state->active_count;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    // Host instance writes, the previous build, and the previous frame's traces come first.
    VkMemoryBarrier before = {};
    before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    before.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
        | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &before, 0, 0, 0, 0);
    state->build_as(state->command, 1, &build, &ranges);
    VkMemoryBarrier after = {};
    after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &after, 0, 0, 0, 0);
}

// Storage and scratch cover every scene slot; each frame builds only active ones.
static bool create_tlas(RendererState* state)
{
    VkAccelerationStructureGeometryKHR geometry=instance_geometry(state->frames[0].instances.address);
    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    uint32_t capacity=state->instance_count;
    state->get_as_sizes(state->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &capacity, &sizes);
    if (!create_buffer(state, &state->tlas_buffer, sizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state, &state->tlas_scratch, sizes.buildScratchSize + state->scratch_alignment,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        return false;
    VkAccelerationStructureCreateInfoKHR create = {};
    create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    create.buffer = state->tlas_buffer.handle;
    create.size = sizes.accelerationStructureSize;
    if (!vk_ok(state->create_as(state->device, &create, 0, &state->tlas), "Create acceleration structure")
        || !begin_commands(state))
        return false;
    record_tlas_build(state,state->frames[0].instances.address);
    return submit_commands(state, false);
}

static void store_vector(float* destination, Vec3 value)
{
    destination[0] = value.x; destination[1] = value.y; destination[2] = value.z;
}

static void scene_uniform(RenderUniform* uniform,const RenderScene* scene)
{
    memcpy(uniform->organelle_style,scene->organelle_style,sizeof(uniform->organelle_style));
    memcpy(uniform->emission_fresnel,scene->emission_fresnel,sizeof(uniform->emission_fresnel));
    memcpy(uniform->emission_color,scene->emission_color,sizeof(uniform->emission_color));
    memcpy(uniform->membrane_ripple,scene->membrane_ripple,sizeof(uniform->membrane_ripple));
    uniform->background[0]=scene->ambient.r;uniform->background[1]=scene->ambient.g;uniform->background[2]=scene->ambient.b;
    uniform->ambient_top[0]=scene->ambient_top.r;uniform->ambient_top[1]=scene->ambient_top.g;uniform->ambient_top[2]=scene->ambient_top.b;
    uniform->ambient_bottom[0]=scene->ambient_bottom.r;uniform->ambient_bottom[1]=scene->ambient_bottom.g;uniform->ambient_bottom[2]=scene->ambient_bottom.b;
    uniform->background_quality[0]=scene->background_samples;uniform->background_quality[1]=scene->background_triangles;
    uniform->backdrop_style[0]=scene->background_blur;uniform->backdrop_style[1]=scene->background_glow;uniform->backdrop_style[2]=scene->background_ambient;
    uniform->membrane_fade[0]=scene->thin_edge_strength;uniform->membrane_fade[1]=scene->thin_edge_power;uniform->membrane_fade[2]=scene->thin_blur_radius;uniform->membrane_fade[3]=1;
    uniform->thin_material[0]=scene->thin_transmission;uniform->thin_material[1]=scene->thin_absorption;
    uniform->thin_material[2]=scene->lights_enabled ? 1 : 0;uniform->thin_material[3]=scene->thin_clarity;
    float total=0;
    for (uint32_t i=0;i<3;++i)
    {
        const RenderLight* light=&scene->lights[i];store_vector(uniform->studio_direction[i],light->direction);
        uniform->studio_color[i][0]=light->color.r*light->irradiance;
        uniform->studio_color[i][1]=light->color.g*light->irradiance;
        uniform->studio_color[i][2]=light->color.b*light->irradiance;
        float weight=light->irradiance*(LUMINANCE_RED_WEIGHT*light->color.r+LUMINANCE_GREEN_WEIGHT*light->color.g+LUMINANCE_BLUE_WEIGHT*light->color.b);
        uniform->studio_direction[i][3]=weight;total+=weight;
    }
    if (total>0) for (uint32_t i=0;i<3;++i) uniform->studio_direction[i][3]/=total;
    store_vector(uniform->backdrop_wave,scene->background_wave);
    uniform->backdrop_wave[3]=(float)std::fmod(scene->time_seconds,BACKDROP_TIME_WRAP_SECONDS);
    memcpy(uniform->backdrop_animation,scene->background_animation,sizeof(uniform->backdrop_animation));
    memcpy(uniform->scene_min,scene->bounds_min,3*sizeof(float));memcpy(uniform->scene_max,scene->bounds_max,3*sizeof(float));
    memcpy(uniform->foreground_min,scene->foreground_min,3*sizeof(float));memcpy(uniform->foreground_max,scene->foreground_max,3*sizeof(float));
    uniform->quality[3]=scene->thin_blur_radius>0 ? 1 : 0;
    for (uint32_t i=0;i<scene->media.size();++i) if ((scene->media[i].blur[0]>0 || scene->media[i].blur[3]>0) && scene->media[i].blur[2]>0) uniform->quality[3]=1;
    uniform->scene[1]=(uint32_t)scene->media.size();uniform->scene[2]=scene->sight_count;
    uniform->fog_grid[0]=scene->fog_field.low_x;uniform->fog_grid[1]=scene->fog_field.low_y;
    uniform->fog_grid[2]=scene->fog_field.spacing;uniform->fog_grid[3]=SIGHT_FIELD_SIDE;
    uniform->fog_tint[0]=scene->fog_tint.r;uniform->fog_tint[1]=scene->fog_tint.g;uniform->fog_tint[2]=scene->fog_tint.b;
    uniform->fog[0]=scene->fog_blur;uniform->fog[1]=scene->fog_feather;uniform->fog[2]=scene->fog_plane_z;
}

static WorldBounds empty_bounds()
{
    WorldBounds b;
    for (uint32_t a=0;a<3;++a) { b.low[a]=FLT_MAX;b.high[a]=-FLT_MAX; }
    return b;
}
static void include_bounds(WorldBounds* target,const WorldBounds& source)
{
    for (uint32_t a=0;a<3;++a) { target->low[a]=std::min(target->low[a],source.low[a]);target->high[a]=std::max(target->high[a],source.high[a]); }
}
static WorldBounds slot_bounds(const RenderInstance& instance,const MeshRange& mesh)
{
    WorldBounds b;
    for (uint32_t r=0;r<3;++r)
    {
        float center=instance.transform[r][3],extent=0;
        for (uint32_t c=0;c<3;++c)
        {
            center+=instance.transform[r][c]*.5f*(mesh.low[c]+mesh.high[c]);
            extent+=std::fabs(instance.transform[r][c])*.5f*(mesh.high[c]-mesh.low[c]);
        }
        b.low[r]=center-extent;b.high[r]=center+extent;
    }
    return b;
}
static WorldBounds medium_bounds(const RenderMedium& medium)
{
    WorldBounds b;
    for (uint32_t a=0;a<3;++a) { b.low[a]=medium.center_radius[a]-medium.center_radius[3];b.high[a]=medium.center_radius[a]+medium.center_radius[3]; }
    return b;
}

// Static transport at a point depends on what its rays toward the lights cross.
// A change reaches a target if the change, swept away from any active light,
// meets the target: a ray from the change's center along -L against the target
// grown by the change's half extent. The margin covers ray-origin offsets.
static bool change_reaches(const WorldBounds& change,const WorldBounds& target,const RenderUniform& u)
{
    if (change.low[0]>change.high[0] || target.low[0]>target.high[0]) return false;
    const float margin=.01f;
    for (uint32_t light=0;light<3;++light)
    {
        if (u.studio_direction[light][3]<=0) continue;
        float begin=0,end=FLT_MAX;bool hit=true;
        for (uint32_t a=0;a<3 && hit;++a)
        {
            float half=.5f*(change.high[a]-change.low[a])+margin,origin=.5f*(change.low[a]+change.high[a]);
            float low=target.low[a]-half,high=target.high[a]+half,direction=-u.studio_direction[light][a];
            if (std::fabs(direction)<1e-12f) { hit=origin>=low && origin<=high;continue; }
            float t0=(low-origin)/direction,t1=(high-origin)/direction;
            begin=std::max(begin,std::min(t0,t1));end=std::min(end,std::max(t0,t1));hit=begin<=end;
        }
        if (hit) return true;
    }
    return false;
}

// Static light transport depends on light directions, thin-surface transport,
// stationary blockers and media. A change invalidates only the grids and thin
// surfaces whose light paths it can reach; light or material changes reset all.
static void update_lighting_dependencies(RendererState* state,const RenderScene* scene)
{
    if (!state->uniform_data.scene[3]) return;
    const RenderUniform& u=state->uniform_data;RenderUniform& old=state->baked_uniform;
    bool global=!state->lighting_dependencies_valid || state->config.force_lighting_rebuild
        || memcmp(u.studio_direction,old.studio_direction,sizeof(u.studio_direction))
        || memcmp(u.thin_material,old.thin_material,sizeof(u.thin_material))
        || memcmp(u.membrane_fade,old.membrane_fade,2*sizeof(float));
    std::vector<WorldBounds>& changes=state->lighting_changes;changes.clear();
    std::vector<WorldBounds>& slot_bake=state->baked_slot_bounds;
    for (uint32_t slot=0;slot<state->surface_slots.size();++slot)
    {
        uint32_t i=state->surface_slots[slot];
        LightingKey current={};const RenderInstance& source=scene->instances[i];
        bool stationary=source.static_lighting && (source.mask&SURFACE_RAY_MASK);
        if (stationary)
        { memcpy(current.transform,source.transform,sizeof(current.transform));current.mesh=source.mesh;current.mask=source.mask; }
        const MeshRange& mesh=scene->geometry.meshes[source.mesh];
        bool deformed=stationary && state->baked_mesh_revisions[source.mesh]!=mesh.revision;
        if (!memcmp(&current,&state->baked_instances[i],sizeof(current)) && !deformed) continue;
        // Both the old and the new blocker can change light elsewhere.
        WorldBounds now=stationary ? slot_bounds(source,mesh) : empty_bounds(),both=slot_bake[i];include_bounds(&both,now);
        changes.push_back(both);
        slot_bake[i]=now;state->baked_instances[i]=current;state->surface_valid[i]=0;
    }
    for (uint32_t i=0;i<state->mesh_count;++i) state->baked_mesh_revisions[i]=scene->geometry.meshes[i].revision;
    for (uint32_t i=0;i<state->medium_count;++i)
    {
        // Extinction uses center/radius, density fade, scattering and absorption.
        bool medium=memcmp(&state->baked_media[i],&scene->media[i],3*4*sizeof(float))!=0;
        if (medium) { WorldBounds both=medium_bounds(state->baked_media[i]);include_bounds(&both,medium_bounds(scene->media[i]));changes.push_back(both); }
        if (medium || memcmp(&state->baked_bounds[i],&scene->lighting_bounds[i],sizeof(RenderGridBounds))) state->lighting_valid[i]=0;
        state->baked_media[i]=scene->media[i];state->baked_bounds[i]=scene->lighting_bounds[i];
    }
    for (uint32_t c=0;c<changes.size() || (global && !c);++c)
    {
        for (uint32_t i=0;i<state->medium_count;++i)
            if (global || change_reaches(changes[c],medium_bounds(scene->media[i]),u)) state->lighting_valid[i]=0;
        for (uint32_t slot=0;slot<state->surface_slots.size();++slot)
        {
            uint32_t i=state->surface_slots[slot];
            if (global || change_reaches(changes[c],slot_bake[i],u)) state->surface_valid[i]=0;
        }
    }
    old=u;state->lighting_dependencies_valid=true;
}

// Frustum culling changes work only; newly visible grids are baked before use.
static void update_grid_activity(RendererState* state)
{
    RenderMedium* media=(RenderMedium*)frame_data(state).media.mapped;const RenderUniform& u=state->uniform_data;
    float vertical=u.display[1],horizontal=vertical*u.display[2];
    for (uint32_t i=0;i<state->medium_count;++i)
    {
        RenderMedium& m=media[i];float delta[3];
        for (uint32_t j=0;j<3;++j) delta[j]=m.center_radius[j]-u.camera_position[j];
        float x=0,y=0,z=0;
        for (uint32_t j=0;j<3;++j) { x+=delta[j]*u.camera_right[j];y+=delta[j]*u.camera_up[j];z+=delta[j]*u.camera_forward[j]; }
        float r=m.center_radius[3];
        bool visible=z+r>0 && std::fabs(x)<=z*horizontal+r*std::sqrt(1+horizontal*horizontal)
            && std::fabs(y)<=z*vertical+r*std::sqrt(1+vertical*vertical);
        m.lighting[2]=state->config.shadow_grid_resolution && ((m.lighting[0]>0 && m.lighting[1]>0)
            || (state->config.volume_lighting_grid && m.lighting[0]<=0 && !state->custom_shader)) && r>0 && visible ? 1.0f : 0.0f;
    }
}

// Screen-space level of detail. Smooth ramps of the projected radius keep shading
// continuous while zooming; the fog reveal is the current fog pass visibility.
static float projected_radius(const RenderUniform& u,const float center[3],float radius)
{
    float z=0;for (uint32_t j=0;j<3;++j) z+=(center[j]-u.camera_position[j])*u.camera_forward[j];
    if (z<=radius) return MAX_PROJECTED_PIXELS;
    return std::fmin(MAX_PROJECTED_PIXELS,radius/(z*u.display[1])*.5f*u.settings[1]);
}
static float fog_visibility(const RenderScene* scene,float x,float y)
{
    const SightField& f=scene->fog_field;float visibility=0;
    float cx=(x-f.low_x)/f.spacing,cy=(y-f.low_y)/f.spacing;
    if (cx>=0 && cy>=0 && cx<=SIGHT_FIELD_SIDE-1 && cy<=SIGHT_FIELD_SIDE-1)
    {
        int32_t lx=std::min((int32_t)cx,(int32_t)SIGHT_FIELD_SIDE-2),ly=std::min((int32_t)cy,(int32_t)SIGHT_FIELD_SIDE-2);
        float fx=cx-lx,fy=cy-ly;const float* r=f.reveal+ly*SIGHT_FIELD_SIDE+lx;
        visibility=(r[0]*(1-fx)+r[1]*fx)*(1-fy)+(r[SIGHT_FIELD_SIDE]*(1-fx)+r[SIGHT_FIELD_SIDE+1]*fx)*fy;
    }
    // Coarse fields also keep current sight analytically, as the fog pass does.
    float feather=scene->fog_feather;
    if (f.spacing>feather*.25f) for (uint32_t i=0;i<scene->sight_count;++i)
    {
        const float* s=scene->sight[i].center_radius;
        float dx=x-s[0],dy=y-s[1],dz=scene->fog_plane_z-s[2];
        float t=std::fmin(1.0f,std::fmax(0.0f,(std::sqrt(dx*dx+dy*dy+dz*dz)-s[3]+feather)/(2*feather)));
        visibility=std::fmax(visibility,1-t*t*(3-2*t));
    }
    return visibility;
}
// The most revealed of the center and four points at the bounding radius.
static float fog_reveal(const RenderScene* scene,const float center[3],float radius)
{
    if (scene->fog_blur<=0) return 1;
    float x=center[0],y=center[1],v=fog_visibility(scene,x,y);
    v=std::fmax(v,std::fmax(fog_visibility(scene,x+radius,y),fog_visibility(scene,x-radius,y)));
    v=std::fmax(v,std::fmax(fog_visibility(scene,x,y+radius),fog_visibility(scene,x,y-radius)));
    return std::fmin(1.0f,v);
}
// Instances: separate surface and SSS size ramps, fog reveal and ray mask. Cells:
// projected radius, fog reveal, then moving-shadow and cascade weights.
static void update_level_of_detail(RendererState* state,const RenderScene* scene)
{
    const RenderUniform& u=state->uniform_data;const RenderConfig& c=state->config;
    for (uint32_t j=0;j<state->active_count;++j)
    {
        const RenderInstance& source=scene->instances[state->active_slots[j]];
        const MeshRange& mesh=scene->geometry.meshes[source.mesh];
        float local[3],center[3],extent=0,scale=0;
        for (uint32_t k=0;k<3;++k) { local[k]=.5f*(mesh.low[k]+mesh.high[k]);extent+=(mesh.high[k]-mesh.low[k])*(mesh.high[k]-mesh.low[k]); }
        for (uint32_t row=0;row<3;++row)
            center[row]=source.transform[row][0]*local[0]+source.transform[row][1]*local[1]+source.transform[row][2]*local[2]+source.transform[row][3];
        for (uint32_t column=0;column<3;++column)
            scale=std::fmax(scale,std::sqrt(source.transform[0][column]*source.transform[0][column]
                +source.transform[1][column]*source.transform[1][column]+source.transform[2][column]*source.transform[2][column]));
        float radius=.5f*std::sqrt(extent)*scale,reveal=fog_reveal(scene,center,radius);
        float size=radius>0 ? detail_ramp(projected_radius(u,center,radius),c.detail_surface_minimum,c.detail_surface_full) : 1;
        float* detail=&state->style_data[j*INSTANCE_STYLE_GPU_FLOATS+INSTANCE_STYLE_FLOATS];
        detail[0]=size*reveal;detail[1]=reveal;
        detail[2]=detail_ramp(projected_radius(u,center,radius),c.detail_subsurface_minimum,c.detail_subsurface_full)*reveal;
        detail[3]=source.static_lighting ? STATIC_BLOCKER_RAY_MASK : DYNAMIC_BLOCKER_RAY_MASK;
    }
    RenderMedium* media=(RenderMedium*)frame_data(state).media.mapped;
    for (uint32_t i=0;i<state->medium_count;++i)
    {
        RenderMedium& m=media[i];
        m.detail[0]=projected_radius(u,m.center_radius,m.center_radius[3]);
        m.detail[1]=fog_reveal(scene,m.center_radius,m.center_radius[3]);
        m.detail[2]=m.detail[3]=detail_ramp(m.detail[0],c.detail_cell_minimum,c.detail_cell_full)*m.detail[1];
    }
}

// A balanced, stackless tree over current medium spheres. Build at the frame
// boundary into this slot's buffer; attenuation rays keep exact sphere tests.
static uint32_t build_medium_ray_node(const RenderMedium* media,uint32_t* order,
    uint32_t begin,uint32_t end,MediumRayNode* nodes,uint32_t cursor)
{
    uint32_t index=cursor++;MediumRayNode& node=nodes[index];
    for (uint32_t a=0;a<3;++a) { node.low[a]=FLT_MAX;node.high[a]=-FLT_MAX; }
    for (uint32_t j=begin;j<end;++j)
    {
        const RenderMedium& medium=media[order[j]];const float* sphere=medium.center_radius;
        float radius=std::fmax(sphere[3],medium.lighting[3]);
        for (uint32_t a=0;a<3;++a)
        { node.low[a]=std::fmin(node.low[a],sphere[a]-radius);node.high[a]=std::fmax(node.high[a],sphere[a]+radius); }
    }
    node.medium=UINT32_MAX;
    if (end-begin==1) node.medium=order[begin];
    else
    {
        uint32_t axis=0;
        for (uint32_t a=1;a<3;++a) if (node.high[a]-node.low[a]>node.high[axis]-node.low[axis]) axis=a;
        std::sort(order+begin,order+end,[media,axis](uint32_t a,uint32_t b) {
            float x=media[a].center_radius[axis],y=media[b].center_radius[axis];return x==y ? a<b : x<y;
        });
        uint32_t middle=begin+(end-begin)/2;
        cursor=build_medium_ray_node(media,order,begin,middle,nodes,cursor);
        cursor=build_medium_ray_node(media,order,middle,end,nodes,cursor);
    }
    node.escape=cursor;return cursor;
}

static void update_medium_ray_nodes(RendererState* state,const RenderScene* scene)
{
    if (!state->medium_count) return;
    FrameResources& frame=frame_data(state);
    for (uint32_t i=0;i<state->medium_count;++i) state->medium_ray_order[i]=i;
    build_medium_ray_node(scene->media.data(),state->medium_ray_order.data(),0,
        state->medium_count,state->medium_ray_scratch.data(),0);
    memcpy(frame.medium_ray_nodes.mapped,state->medium_ray_scratch.data(),state->medium_ray_scratch.size()*sizeof(MediumRayNode));
}

// Masked-out slots stay out of the TLAS. Instance IDs index the compacted
// style and lighting arrays, which use the same order.
static void scene_instances(RendererState* state,const RenderScene* scene)
{
    uint32_t active=0;
    for (uint32_t r=0;r<scene->ranges.size();++r) for (uint32_t i=scene->ranges[r].first;i<scene->ranges[r].first+scene->ranges[r].count;++i)
    {
        const RenderInstance* source=&scene->instances[i];if (!source->mask) continue;
        uint32_t j=active++;VkAccelerationStructureInstanceKHR* out=&state->instance_data[j];state->active_slots[j]=i;
        memcpy(out->transform.matrix,source->transform,sizeof(source->transform));
        bool stationary=source->static_lighting && (source->mask&SURFACE_RAY_MASK);
        out->mask=source->mask | (source->mask&SURFACE_RAY_MASK ? (stationary ? STATIC_BLOCKER_RAY_MASK : DYNAMIC_BLOCKER_RAY_MASK) : 0u);
        out->flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        const MeshAllocation* mesh=&state->meshes[source->mesh];
        out->instanceCustomIndex=source->mesh;out->accelerationStructureReference=mesh->address;
        memcpy(&state->style_data[j*INSTANCE_STYLE_GPU_FLOATS],source->style,INSTANCE_STYLE_FLOATS*sizeof(float));
        LightingInstance& light=state->lighting_input[j];
        memcpy(light.transform,source->transform,sizeof(light.transform));
        light.first=mesh->range.first;light.count=mesh->range.count;light.cache_first=state->slot_cache_first[i];
        light.active=stationary ? 1u : 0u;
    }
    state->active_count=active;
}

// Geometry 0 is opaque and committed by traversal. Geometry 1 holds thin
// membranes, which shadow rays inspect once each to multiply transmission.
// Each BLAS reads only its mesh's vertices; the index buffer holds mesh-local indices.
// An empty geometry points at the mesh's first triangle so the address stays inside the buffer.
static void mesh_geometries(RendererState* state,const MeshAllocation& mesh,
    VkAccelerationStructureGeometryKHR* geometries,VkAccelerationStructureBuildRangeInfoKHR* ranges)
{
    uint32_t first[2]={mesh.range.first,mesh.opaque_count<mesh.range.count ? mesh.range.first+mesh.opaque_count : mesh.range.first};
    uint32_t counts[2]={mesh.opaque_count,mesh.range.count-mesh.opaque_count};
    for (uint32_t g=0;g<2;++g)
    {
        VkAccelerationStructureGeometryKHR& geometry=geometries[g];geometry=VkAccelerationStructureGeometryKHR();
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = g==MEMBRANE_GEOMETRY ? VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR : VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress = state->vertices.address+(VkDeviceSize)mesh.range.first_vertex*sizeof(GpuVertex);
        geometry.geometry.triangles.vertexStride = sizeof(GpuVertex);
        geometry.geometry.triangles.maxVertex = mesh.range.vertex_count - 1;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress = state->indices.address+(VkDeviceSize)first[g]*3*sizeof(uint32_t);
        ranges[g]=VkAccelerationStructureBuildRangeInfoKHR();ranges[g].primitiveCount=counts[g];
    }
}

// Device-local geometry is uploaded once through staging. All BLAS builds share
// one submission; static meshes use temporary scratch, deformable meshes keep theirs.
static bool upload_geometry(RendererState* state,const Geometry& geometry)
{
    VkBufferUsageFlags input = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkDeviceSize vertex_bytes=geometry.vertices.size()*sizeof(GpuVertex),triangle_bytes=geometry.triangles.size()*sizeof(GpuTriangle);
    VkDeviceSize index_bytes=geometry.triangles.size()*3*sizeof(uint32_t),range_bytes=(VkDeviceSize)state->mesh_count*2*sizeof(uint32_t);
    GpuBuffer staging={},scratch={};
    // Stable partition inside each mesh: opaque triangles, then thin membranes.
    std::vector<GpuTriangle> ordered(geometry.triangles);
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        const MeshRange& range=geometry.meshes[i];uint32_t next=range.first;
        for (uint32_t pass=0;pass<2;++pass)
            for (uint32_t t=range.first;t<range.first+range.count;++t)
                if ((geometry.triangles[t].index[3]==THIN_MEMBRANE_TRIANGLE)==(pass==1)) ordered[next++]=geometry.triangles[t];
        uint32_t membranes=0;
        for (uint32_t t=range.first;t<range.first+range.count;++t) membranes+=geometry.triangles[t].index[3]==THIN_MEMBRANE_TRIANGLE;
        state->meshes[i].opaque_count=range.count-membranes;
    }
    if (!create_buffer(state, &state->vertices, vertex_bytes, input | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state, &state->triangles, triangle_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state, &state->indices, index_bytes, input, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state, &state->mesh_ranges, range_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state, &staging, vertex_bytes+triangle_bytes+index_bytes+range_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    { destroy_buffer(state,&staging);return false; }
    char* bytes=(char*)staging.mapped;
    memcpy(bytes, geometry.vertices.data(), vertex_bytes);
    memcpy(bytes+vertex_bytes, ordered.data(), triangle_bytes);
    uint32_t* indices = (uint32_t*)(bytes+vertex_bytes+triangle_bytes);
    for (uint32_t m=0;m<state->mesh_count;++m)
    {
        const MeshRange& range=geometry.meshes[m];
        for (uint32_t i=range.first;i<range.first+range.count;++i)
            for (uint32_t j=0;j<3;++j)
            {
                assert(ordered[i].index[j]-range.first_vertex<range.vertex_count);
                indices[i*3+j]=ordered[i].index[j]-range.first_vertex;
            }
    }
    uint32_t* mesh_ranges=indices+3*state->triangle_count;
    for (uint32_t i=0;i<state->mesh_count;++i) { mesh_ranges[2*i]=geometry.meshes[i].first;mesh_ranges[2*i+1]=state->meshes[i].opaque_count; }
    VkDeviceSize deformable_bytes=0;
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds(state->mesh_count);
    std::vector<VkAccelerationStructureGeometryKHR> meshes(2*state->mesh_count);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(2*state->mesh_count);
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> range_pointers(state->mesh_count);
    std::vector<VkDeviceSize> scratch_offsets(state->mesh_count);
    VkDeviceSize scratch_bytes=0;
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        MeshAllocation* allocation=&state->meshes[i];allocation->range=geometry.meshes[i];
        mesh_geometries(state,*allocation,&meshes[2*i],&ranges[2*i]);
        state->vertex_upload_offsets[i]=deformable_bytes;
        if (allocation->range.deformable) deformable_bytes+=(VkDeviceSize)allocation->range.vertex_count*sizeof(GpuVertex);
        VkAccelerationStructureBuildGeometryInfoKHR& build=builds[i];build=VkAccelerationStructureBuildGeometryInfoKHR();
        build.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
            | (allocation->range.deformable ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
        build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=2;build.pGeometries=&meshes[2*i];
        VkAccelerationStructureBuildSizesInfoKHR sizes={};sizes.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        uint32_t counts[2]={ranges[2*i].primitiveCount,ranges[2*i+1].primitiveCount};
        state->get_as_sizes(state->device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,counts,&sizes);
        if (!create_buffer(state,&allocation->storage,sizes.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        { destroy_buffer(state,&staging);return false; }
        VkAccelerationStructureCreateInfoKHR create={};create.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        create.type=build.type;create.buffer=allocation->storage.handle;create.size=sizes.accelerationStructureSize;
        if (!vk_ok(state->create_as(state->device,&create,0,&allocation->acceleration),"Create acceleration structure"))
        { destroy_buffer(state,&staging);return false; }
        build.dstAccelerationStructure=allocation->acceleration;
        if (allocation->range.deformable)
        {
            VkDeviceSize own=std::max(sizes.buildScratchSize,sizes.updateScratchSize)+state->scratch_alignment;
            if (!create_buffer(state,&allocation->scratch,own,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            { destroy_buffer(state,&staging);return false; }
            scratch_offsets[i]=UINT64_MAX;
        }
        else { scratch_offsets[i]=scratch_bytes;scratch_bytes+=sizes.buildScratchSize+state->scratch_alignment; }
        range_pointers[i]=&ranges[2*i];
    }
    if (!create_buffer(state,&scratch,std::max<VkDeviceSize>(scratch_bytes,4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        )
    { destroy_buffer(state,&staging);destroy_buffer(state,&scratch);return false; }
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
        if (!create_buffer(state,&state->frames[f].vertex_upload,std::max<VkDeviceSize>(deformable_bytes,sizeof(GpuVertex)),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        { destroy_buffer(state,&staging);destroy_buffer(state,&scratch);return false; }
    for (uint32_t i=0;i<state->mesh_count;++i)
        builds[i].scratchData.deviceAddress=aligned_scratch(state,scratch_offsets[i]==UINT64_MAX
            ? state->meshes[i].scratch.address : scratch.address+scratch_offsets[i]);
    bool success=begin_commands(state);
    if (success)
    {
        VkBufferCopy copies[4]={{0,0,vertex_bytes},{vertex_bytes,0,triangle_bytes},{vertex_bytes+triangle_bytes,0,index_bytes},
            {vertex_bytes+triangle_bytes+index_bytes,0,range_bytes}};
        state->copy_buffer(state->command,staging.handle,state->vertices.handle,1,&copies[0]);
        state->copy_buffer(state->command,staging.handle,state->triangles.handle,1,&copies[1]);
        state->copy_buffer(state->command,staging.handle,state->indices.handle,1,&copies[2]);
        state->copy_buffer(state->command,staging.handle,state->mesh_ranges.handle,1,&copies[3]);
        VkMemoryBarrier uploaded={};uploaded.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        uploaded.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;uploaded.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&uploaded,0,0,0,0);
        state->build_as(state->command,state->mesh_count,builds.data(),range_pointers.data());
        VkMemoryBarrier built={};built.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        built.srcAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;built.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&built,0,0,0,0);
        success=submit_commands(state,false);
    }
    destroy_buffer(state,&staging);destroy_buffer(state,&scratch);
    if (!success) return false;
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        VkAccelerationStructureDeviceAddressInfoKHR address={};address.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        address.accelerationStructure=state->meshes[i].acceleration;state->meshes[i].address=state->get_as_address(state->device,&address);
    }
    return true;
}

static bool upload_scene(RendererState* state,const RenderScene* scene)
{
    const Geometry& geometry=scene->geometry;
    state->triangle_count = geometry.triangles.size();
    VkBufferUsageFlags input = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    state->mesh_count=(uint32_t)geometry.meshes.size();state->instance_count=(uint32_t)scene->instances.size();
    try
    {
        state->meshes.resize(state->mesh_count);state->instance_data.resize(state->instance_count);state->style_data.resize(state->instance_count*INSTANCE_STYLE_GPU_FLOATS);
        state->baked_instances.resize(state->instance_count);state->baked_mesh_revisions.resize(state->mesh_count);
        state->baked_media.resize(scene->media.size());state->baked_bounds.resize(scene->media.size());state->lighting_input.resize(state->instance_count);
        state->slot_cache_first.resize(state->instance_count);state->active_slots.resize(state->instance_count);
        state->surface_valid.resize(state->instance_count);state->baked_slot_bounds.assign(state->instance_count,empty_bounds());
        state->lighting_changes.reserve(state->instance_count+scene->media.size());
        state->surface_cache_meshes.resize(state->instance_count);state->surface_slots.reserve(state->instance_count);
        state->lighting_valid.resize(scene->media.size());state->current_static.assign(scene->media.size(),0);state->vertex_upload_offsets.resize(state->mesh_count);
    }
    catch (const std::bad_alloc&) { fprintf(stderr,"Cannot allocate render upload storage.\n");return false; }
    if (!upload_geometry(state,geometry)) return false;
    for (uint32_t i=0;i<state->instance_count;++i) if (scene->instances[i].static_lighting)
    {
        state->surface_slots.push_back(i);state->surface_cache_meshes[i]=scene->instances[i].mesh+1;
        state->slot_cache_first[i]=state->surface_cache_triangles;
        state->surface_cache_triangles+=state->meshes[scene->instances[i].mesh].range.count;
    }
    scene_instances(state,scene);
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
    {
        GpuBuffer& instances=state->frames[f].instances;
        if (!create_buffer(state,&instances,state->instance_count*sizeof(state->instance_data[0]),input,host)) return false;
        memcpy(instances.mapped,state->instance_data.data(),state->active_count*sizeof(state->instance_data[0]));
    }
    if (!create_tlas(state)) return false;
    RenderUniform uniform={};
    uniform.settings[0]=state->config.width;uniform.settings[1]=state->config.height;
    uniform.settings[2]=state->config.samples_per_pixel;uniform.settings[3]=state->config.max_transmission_layers;uniform.scene[0]=state->config.seed;
    uniform.display[0]=state->config.exposure;uniform.display[1]=std::tan(state->config.vertical_fov_degrees*HALF_DEGREES_TO_RADIANS);
    uniform.display[2]=(float)state->config.width/state->config.height;uniform.display[3]=(float)state->config.volume_steps;
    uniform.quality[0]=state->config.volume_rays_per_pixel;
    uniform.background_quality[3]=state->config.bloom_strength>0 && !state->custom_shader ? 1u : 0u;
    uniform.quality[1]=state->config.shadow_grid_resolution;
    uniform.scene[3]=state->config.static_lighting_cache && state->config.shadow_grid_resolution && !state->custom_shader ? 1 : 0;
    uniform.quality[2]=state->config.microfacet_energy_compensation;
    if (!state->custom_shader && state->config.cascade_probes)
    {
        uniform.cascades[0]=state->config.cascade_probes;uniform.cascades[1]=state->config.cascade_directions;
        uniform.cascades[2]=state->config.cascade_count;uniform.cascades[3]=state->config.cascade_feedback ? 1u : 0u;
        uniform.cascade_interval[0]=state->config.cascade_interval;uniform.cascade_interval[1]=state->config.cascade_growth;
    }
    uniform.focus_band[0]=state->config.focus_center;uniform.focus_band[1]=state->config.focus_half_width;
    uniform.focus_band[2]=state->config.focus_falloff;uniform.focus_band[3]=state->config.focus_radius;
    uniform.subsurface_quality[0]=state->config.subsurface_samples;
    uniform.subsurface[0]=state->config.subsurface_strength;uniform.subsurface[1]=state->config.subsurface_wrap;
    uniform.subsurface[2]=state->config.subsurface_transmission;uniform.subsurface[3]=state->config.subsurface_thickness;
    scene_uniform(&uniform,scene);
    if (!create_buffer(state, &state->energy, 2*MICROFACET_TABLE_SIZE*MICROFACET_TABLE_SIZE*sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host)) return false;
    microfacet_energy_table((float*)state->energy.mapped);
    state->medium_count=(uint32_t)scene->media.size();
    state->medium_ray_order.resize(state->medium_count);
    state->medium_ray_scratch.resize(state->medium_count ? 2*state->medium_count-1 : 0);
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
    {
        FrameResources& frame=state->frames[f];
        if (!create_buffer(state, &frame.uniform, sizeof(RenderUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host)
            || !create_buffer(state,&frame.sight,sizeof(scene->sight)+sizeof(scene->fog_field.reveal),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
            || !create_buffer(state,&frame.media,(state->medium_count ? state->medium_count : 1)*sizeof(RenderMedium),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
            || !create_buffer(state,&frame.medium_ray_nodes,(state->medium_count ? 2*state->medium_count-1 : 1)*sizeof(MediumRayNode),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host))
            return false;
        if (state->medium_count) memcpy(frame.media.mapped,scene->media.data(),state->medium_count*sizeof(RenderMedium));
        if (scene->fog_blur>0) memcpy(frame.sight.mapped,scene->fog_field.reveal,sizeof(scene->fog_field.reveal));
        memcpy((char*)frame.sight.mapped+sizeof(scene->fog_field.reveal),scene->sight,sizeof(scene->sight));
        memcpy(frame.uniform.mapped,&uniform,sizeof(uniform));
    }
    state->effect_tile_count=scene->tile_count;state->effect_tile_size=scene->effect_tiles.size();
    state->uniform_data=uniform;
    return true;
}

static bool create_render_image(RendererState* state, VkFormat format, VkImage* handle,
    VkDeviceMemory* memory, VkImageView* image_view)
{
    VkImageCreateInfo image = {};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = VkExtent3D{ state->config.width, state->config.height, 1 };
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!vk_ok(vkCreateImage(state->device, &image, 0, handle), "Create render image"))
        return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(state->device, *handle, &requirements);
    VkMemoryAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(state, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX)
        return false;
    if (!vk_ok(vkAllocateMemory(state->device, &allocation, 0, memory), "Allocate render image")
        || !vk_ok(vkBindImageMemory(state->device, *handle, *memory, 0), "Bind render image"))
        return false;
    VkImageViewCreateInfo view = {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = *handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    if (!vk_ok(vkCreateImageView(state->device, &view, 0, image_view), "Create render view"))
        return false;
    return true;
}

static bool create_optical_depth(RendererState* state)
{
    state->copy_buffer_to_image=(PFN_vkCmdCopyBufferToImage)vkGetDeviceProcAddr(state->device,"vkCmdCopyBufferToImage");
    if (!state->copy_buffer_to_image) return false;
    state->fill_buffer=(PFN_vkCmdFillBuffer)vkGetDeviceProcAddr(state->device,"vkCmdFillBuffer");
    if (!state->fill_buffer) return false;
    bool enabled=state->config.optical_depth_lut && !state->custom_shader;
    state->optical_depth_size=enabled ? OPTICAL_DEPTH_TABLE_SIZE : 1;
    state->optical_depth_layers=enabled && state->medium_count ? state->medium_count : 1;
    VkFormatProperties format;vkGetPhysicalDeviceFormatProperties(state->physical,VK_FORMAT_R32_SFLOAT,&format);
    if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    { fprintf(stderr,"Optical depth requires linearly filterable R32F.\n");return false; }
    VkImageCreateInfo image={};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R32_SFLOAT;
    image.extent=VkExtent3D{state->optical_depth_size,state->optical_depth_size,1};
    image.mipLevels=1;image.arrayLayers=state->optical_depth_layers;image.samples=VK_SAMPLE_COUNT_1_BIT;
    image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!vk_ok(vkCreateImage(state->device,&image,0,&state->optical_depth_image),"Create optical depth image")) return false;
    VkMemoryRequirements requirements;vkGetImageMemoryRequirements(state->device,state->optical_depth_image,&requirements);
    VkMemoryAllocateInfo allocation={};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memory_type(state,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation.memoryTypeIndex==UINT32_MAX
        || !vk_ok(vkAllocateMemory(state->device,&allocation,0,&state->optical_depth_memory),"Allocate optical depth image")
        || !vk_ok(vkBindImageMemory(state->device,state->optical_depth_image,state->optical_depth_memory,0),"Bind optical depth image")) return false;
    VkImageViewCreateInfo view={};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image=state->optical_depth_image;view.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;view.format=image.format;
    view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;view.subresourceRange.levelCount=1;
    view.subresourceRange.layerCount=state->optical_depth_layers;
    if (!vk_ok(vkCreateImageView(state->device,&view,0,&state->optical_depth_view),"Create optical depth view")) return false;
    state->optical_depth_fades.resize(state->optical_depth_layers,-1.0f);
    VkDeviceSize bytes=(VkDeviceSize)state->optical_depth_size*state->optical_depth_size*state->optical_depth_layers*sizeof(float);
    return create_buffer(state,&state->optical_depth_upload,bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

// Each column is one unit-sphere half-chord. The cumulative integral has an
// exact zero endpoint and no Earth-atmosphere assumptions. Only density fade
// changes the table; radius, position and extinction remain current shader input.
static void bake_optical_depth(float* output,uint32_t size,float fade)
{
    if (size==1) { output[0]=0;return; }
    double step=1.0/(size-1),substep=step/OPTICAL_DEPTH_SUBSTEPS;
    for (uint32_t x=0;x<size;++x)
    {
        double chord=x*step,integral=0;output[x]=0;
        for (uint32_t y=1;y<size;++y)
        {
            for (uint32_t sub=0;sub<OPTICAL_DEPTH_SUBSTEPS;++sub)
            {
                double t=(y-1)*step+(sub+.5)*substep;
                double radius=std::sqrt(1.0-chord*chord*(1.0-t*t));
                double density=fade>0 ? (1.0-radius)/fade : 1.0;
                if (density>1) density=1;
                integral+=chord*substep*density*density*(3.0-2.0*density);
            }
            output[y*size+x]=(float)integral;
        }
    }
}

static void record_optical_depth(RendererState* state)
{
    state->optical_depth_bakes=0;
    uint32_t size=state->optical_depth_size;
    const RenderMedium* media=(const RenderMedium*)frame_data(state).media.mapped;
    for (uint32_t i=0;i<state->optical_depth_layers;++i)
    {
        float fade=size>1 && i<state->medium_count ? media[i].scattering[3] : 0;
        if (state->optical_depth_fades[i]==fade && !state->config.force_lighting_rebuild) continue;
        // The upload buffer is shared by both slots; rebakes are rare, so wait for the other.
        if (!state->optical_depth_bakes)
            for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
                vkWaitForFences(state->device,1,&state->frames[f].fence,VK_TRUE,UINT64_MAX);
        float* output=(float*)state->optical_depth_upload.mapped+(size_t)i*size*size;
        bake_optical_depth(output,size,fade);state->optical_depth_fades[i]=fade;++state->optical_depth_bakes;
    }
    if (!state->optical_depth_bakes && state->optical_depth_initialized) return;
    VkImageMemoryBarrier barrier={};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout=state->optical_depth_initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask=state->optical_depth_initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=state->optical_depth_image;
    barrier.subresourceRange=VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,state->optical_depth_layers};
    VkMemoryBarrier host={};host.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&host,0,0,1,&barrier);
    VkBufferImageCopy copy={};copy.imageSubresource=VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT,0,0,state->optical_depth_layers};
    copy.imageExtent=VkExtent3D{size,size,1};
    state->copy_buffer_to_image(state->command,state->optical_depth_upload.handle,state->optical_depth_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,0,0,0,1,&barrier);
    state->optical_depth_initialized=true;
}

static bool create_lighting_texture(RendererState* state)
{
    uint32_t n=state->config.texture_lighting_grid && !state->custom_shader ? state->config.shadow_grid_resolution : 0;
    uint32_t count=state->medium_count ? state->medium_count : 1;
    uint32_t rows=count>32 ? 4 : 1;
    state->lighting_extent=n ? VkExtent3D{3*n,rows*n,((count+rows-1)/rows)*n} : VkExtent3D{1,1,1};
    uint32_t limit=state->properties.limits.maxImageDimension3D;
    if (state->lighting_extent.width>limit || state->lighting_extent.height>limit || state->lighting_extent.depth>limit)
    { fprintf(stderr,"Lighting texture exceeds the 3D image limit.\n");return false; }
    VkFormatProperties format;vkGetPhysicalDeviceFormatProperties(state->physical,VK_FORMAT_R32G32B32A32_SFLOAT,&format);
    if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    { fprintf(stderr,"Lighting texture requires linearly filterable RGBA32F.\n");return false; }
    VkImageCreateInfo image={};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType=VK_IMAGE_TYPE_3D;image.format=VK_FORMAT_R32G32B32A32_SFLOAT;image.extent=state->lighting_extent;
    image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!vk_ok(vkCreateImage(state->device,&image,0,&state->lighting_image),"Create lighting texture")) return false;
    VkMemoryRequirements requirements;vkGetImageMemoryRequirements(state->device,state->lighting_image,&requirements);
    VkMemoryAllocateInfo allocation={};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memory_type(state,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation.memoryTypeIndex==UINT32_MAX
        || !vk_ok(vkAllocateMemory(state->device,&allocation,0,&state->lighting_memory),"Allocate lighting texture")
        || !vk_ok(vkBindImageMemory(state->device,state->lighting_image,state->lighting_memory,0),"Bind lighting texture")) return false;
    VkImageViewCreateInfo view={};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image=state->lighting_image;view.viewType=VK_IMAGE_VIEW_TYPE_3D;view.format=image.format;
    view.subresourceRange=VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if (!vk_ok(vkCreateImageView(state->device,&view,0,&state->lighting_view),"Create lighting texture view")) return false;
    state->lighting_copies.resize(count*3);state->changed_copies.reserve(count*3);state->grid_written.assign(count,0);
    for (uint32_t medium=0;medium<count;++medium) for (uint32_t light=0;light<3;++light)
    {
        VkBufferImageCopy& copy=state->lighting_copies[medium*3+light];
        copy.bufferOffset=(VkDeviceSize)(medium*3+light)*n*n*n*4*sizeof(float);
        copy.imageSubresource=VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageOffset=VkOffset3D{(int32_t)(light*n),(int32_t)((medium%rows)*n),(int32_t)((medium/rows)*n)};
        copy.imageExtent=VkExtent3D{n,n,n};
    }
    return true;
}

static bool create_output(RendererState* state)
{
    // Six vec4 records per native pixel, only when F2 buffer views can open.
    VkDeviceSize debug_bytes = state->debug_records
        ? (VkDeviceSize)state->config.width*state->config.height*GPU_DEBUG_PIXEL_BYTES : GPU_DEBUG_PIXEL_BYTES;
    if (debug_bytes > state->properties.limits.maxStorageBufferRange)
    {
        fprintf(stderr,"Debug buffers exceed the device's storage-buffer range.\n");
        return false;
    }
    uint32_t stride=std::max(1u,state->config.volume_spatial_stride);
    VkDeviceSize anchors=(VkDeviceSize)((state->config.width+stride-1)/stride)*((state->config.height+stride-1)/stride);
    VkDeviceSize guide_bytes=((VkDeviceSize)state->config.width*state->config.height+2*anchors)*4*sizeof(float);
    if (!create_buffer(state,&state->debug_pixels,debug_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state,&state->guides,guide_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
    {
        GpuBuffer& text=state->frames[f].debug_text;
        if (!create_buffer(state,&text,FONT_UNIFORM_BYTES+(STATS_TEXT_WIDTH*STATS_TEXT_HEIGHT+HUD_TEXT_WIDTH*HUD_TEXT_HEIGHT)/TEXT_MASK_BYTE_BITS+sizeof(ProfileBars),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) return false;
        memcpy(text.mapped,debug_glyphs,sizeof(debug_glyphs));
    }
    // Half-float HDR halves post-pass bandwidth; each frame writes it once, so no accumulation precision is lost.
    if (!create_render_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, &state->hdr, &state->hdr_memory, &state->hdr_view)
        || !create_render_image(state, VK_FORMAT_R16G16B16A16_SFLOAT, &state->hdr_scratch, &state->hdr_scratch_memory, &state->hdr_scratch_view)
        || !create_render_image(state, VK_FORMAT_R8G8B8A8_UNORM, &state->output, &state->output_memory, &state->output_view))
        return false;
    VkSamplerCreateInfo sampler={};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!vk_ok(vkCreateSampler(state->device,&sampler,0,&state->backdrop_sampler),"Create backdrop sampler")) return false;
    if (state->uniform_data.backdrop_style[0]>0)
    {
        VkFormatProperties properties;vkGetPhysicalDeviceFormatProperties(state->physical,VK_FORMAT_R16G16B16A16_SFLOAT,&properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        { fprintf(stderr,"Backdrop requires linearly filterable RGBA16F.\n");return false; }
        for (uint32_t i=0;i<2;++i)
            if (!create_render_image(state,VK_FORMAT_R16G16B16A16_SFLOAT,&state->backdrop_images[i],
                &state->backdrop_memory[i],&state->backdrop_views[i])) return false;
    }
    VkMemoryPropertyFlags host=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
    {
        FrameResources& frame=state->frames[f];
        if (!create_buffer(state,&frame.effects,MAX_EFFECT_SEGMENTS*sizeof(EffectSegment),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
            || !create_buffer(state,&frame.effect_tiles,state->effect_tile_size*sizeof(uint32_t),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
            || !create_buffer(state,&frame.instance_styles,state->style_data.size()*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
            || !create_buffer(state,&frame.lighting_bounds,(state->medium_count ? state->medium_count : 1)*sizeof(RenderGridBounds),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
            || !create_buffer(state,&frame.lighting_instances,state->lighting_input.size()*sizeof(LightingInstance),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)) return false;
    }
    VkDeviceSize n=state->config.shadow_grid_resolution;
    VkDeviceSize grid_bytes=n ? n*n*n*3*(state->medium_count ? state->medium_count : 1)*4*sizeof(float) : 4*sizeof(float);
    if (grid_bytes>state->properties.limits.maxStorageBufferRange)
    { fprintf(stderr,"Shadow grid exceeds the storage-buffer range.\n");return false; }
    if (!create_buffer(state,&state->shadow_grid,grid_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    VkDeviceSize static_bytes=state->uniform_data.scene[3] ? grid_bytes : 4*sizeof(float);
    VkDeviceSize surface_bytes=state->uniform_data.scene[3] ? (VkDeviceSize)state->surface_cache_triangles*SURFACE_LIGHTING_RECORDS_PER_TRIANGLE*4*sizeof(float) : 4*sizeof(float);
    if (!surface_bytes) surface_bytes=4*sizeof(float);
    if (surface_bytes>state->properties.limits.maxStorageBufferRange)
    { fprintf(stderr,"Surface lighting exceeds the storage-buffer range.\n");return false; }
    if (!create_buffer(state,&state->static_lighting,static_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state,&state->dynamic_lighting,static_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state,&state->surface_lighting,surface_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    // Every cascade holds half the texels of the one below it, per medium.
    VkDeviceSize probes=state->uniform_data.cascades[0],side=state->uniform_data.cascades[1];
    // Each dispatch selects a block of 32 media; the final block can be partial.
    if (probes && state->medium_count>256)
    { fprintf(stderr,"Radiance cascades support at most 256 media.\n");return false; }
    VkDeviceSize cascade_texels=probes*probes*probes*CASCADE_SH_COEFFICIENTS;
    for (uint32_t c=0;c<state->uniform_data.cascades[2];++c) cascade_texels+=(probes>>c)*(probes>>c)*(probes>>c)*(side<<c)*(side<<c);
    VkDeviceSize cascade_bytes=probes ? cascade_texels*(state->medium_count ? state->medium_count : 1)*4*sizeof(float) : 4*sizeof(float);
    if (cascade_bytes>state->properties.limits.maxStorageBufferRange)
    { fprintf(stderr,"Radiance cascades exceed the storage-buffer range.\n");return false; }
    if (!create_buffer(state,&state->radiance_cascades,cascade_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    VkDeviceSize sharp_bytes=(state->uniform_data.quality[3] || state->uniform_data.background_quality[3]) ? (VkDeviceSize)state->config.width*state->config.height*4*sizeof(float) : 4*sizeof(float);
    if (sharp_bytes>state->properties.limits.maxStorageBufferRange)
    { fprintf(stderr,"Medium sharp layer exceeds the storage-buffer range.\n");return false; }
    if (!create_buffer(state,&state->sharp_layer,sharp_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    state->capture_row.resize(state->config.width*3);
    state->capture_hdr_row.resize(state->config.width*3);
    if (!create_optical_depth(state) || !create_lighting_texture(state)) return false;
    return create_buffer(state, &state->readback, (VkDeviceSize)state->config.width*state->config.height*4*sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

static bool create_swapchain(RendererState* state)
{
    if (state->headless)
        return true;
    printf("Window startup: create swapchain.\n");
    VkSurfaceCapabilitiesKHR capabilities;
    if (!vk_ok(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state->physical, state->surface, &capabilities), "Query surface"))
        return false;
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    {
        fprintf(stderr, "Window surface does not support transfer presentation.\n");
        return false;
    }
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(state->physical, state->surface, &count, 0);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(state->physical, state->surface, &count, formats.data());
    VkSurfaceFormatKHR selected = {};
    for (uint32_t i = 0; i < count; ++i)
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
            && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            selected = formats[i];
    if (selected.format == VK_FORMAT_UNDEFINED)
    {
        fprintf(stderr, "Window requires a supported UNORM presentation format.\n");
        return false;
    }
    VkExtent2D extent = { state->config.width, state->config.height };
    if (capabilities.currentExtent.width != UINT32_MAX)
        extent = capabilities.currentExtent;
    if (extent.width != state->config.width || extent.height != state->config.height)
    {
        fprintf(stderr, "Window extent differs from native render resolution.\n");
        return false;
    }
    VkSwapchainCreateInfoKHR create = {};
    create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create.surface = state->surface;
    create.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && create.minImageCount > capabilities.maxImageCount)
        create.minImageCount = capabilities.maxImageCount;
    create.imageFormat = selected.format;
    create.imageColorSpace = selected.colorSpace;
    create.imageExtent = extent;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create.preTransform = capabilities.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha & create.compositeAlpha))
        create.compositeAlpha = (VkCompositeAlphaFlagBitsKHR)(capabilities.supportedCompositeAlpha & -capabilities.supportedCompositeAlpha);
    uint32_t mode_count=0;vkGetPhysicalDeviceSurfacePresentModesKHR(state->physical,state->surface,&mode_count,0);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(state->physical,state->surface,&mode_count,modes.data());
    state->present_mode=VK_PRESENT_MODE_FIFO_KHR;
    if (!state->config.vsync)
        for (uint32_t i=0;i<mode_count;++i) if (modes[i]==VK_PRESENT_MODE_IMMEDIATE_KHR) state->present_mode=modes[i];
    create.presentMode = state->present_mode;
    printf("Presentation: %s; GPU work timestamps exclude acquire/present waits.\n",
        state->present_mode==VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE (vsync off)" : "FIFO (vsync on or required fallback)");
    create.clipped = VK_TRUE;
    if (!vk_ok(vkCreateSwapchainKHR(state->device, &create, 0, &state->swapchain), "Create swapchain"))
        return false;
    vkGetSwapchainImagesKHR(state->device, state->swapchain, &count, 0);
    state->swap_images.resize(count);
    vkGetSwapchainImagesKHR(state->device, state->swapchain, &count, state->swap_images.data());
    VkSemaphoreCreateInfo semaphore = {};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
        if (!vk_ok(vkCreateSemaphore(state->device, &semaphore, 0, &state->frames[f].image_available), "Create acquire semaphore"))
            return false;
    // A present semaphore is reused only after its image is acquired again.
    state->render_finished.resize(count);
    for (uint32_t i=0;i<count;++i)
        if (!vk_ok(vkCreateSemaphore(state->device, &semaphore, 0, &state->render_finished[i]), "Create present semaphore"))
            return false;
    return true;
}

static bool load_compute_pipeline(RendererState* state, const char* shader_path,
    VkPipelineLayout pipeline_layout, VkPipeline* output_pipeline, bool no_subsurface=false)
{
    FILE* file = fopen(shader_path, "rb");
    if (!file)
    {
        fprintf(stderr, "Cannot open compiled shader '%s'.\n", shader_path);
        return false;
    }
    fseek(file, 0, SEEK_END);
    long byte_count = ftell(file);
    rewind(file);
    if (byte_count <= 0 || byte_count % 4)
    {
        fclose(file);
        fprintf(stderr, "Invalid SPIR-V shader size.\n");
        return false;
    }
    std::vector<uint32_t> code(byte_count/4);
    bool read = fread(code.data(), 1, byte_count, file) == (size_t)byte_count;
    fclose(file);
    if (!read)
        return false;
    VkShaderModuleCreateInfo module = {};
    module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module.codeSize = byte_count;
    module.pCode = code.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    if (!vk_ok(vkCreateShaderModule(state->device, &module, 0, &shader), "Create shader module"))
        return false;
    VkComputePipelineCreateInfo pipeline = {};
    pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline.layout = pipeline_layout;
    pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = shader;
    pipeline.stage.pName = "main";
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup={};
    subgroup.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT;
    subgroup.requiredSubgroupSize=state->subgroup_size;
    pipeline.stage.pNext=state->subgroup_size ? &subgroup : 0;
    uint32_t constants[]={state->config.volume_steps,state->uniform_data.scene[3],state->config.optical_depth_lut ? 1u : 0u,state->config.volume_lighting_grid ? 1u : 0u,state->config.texture_lighting_grid && state->config.shadow_grid_resolution ? 1u : 0u,state->config.organelle_lighting_cache && state->uniform_data.scene[3] ? 1u : 0u,state->config.subsurface_strength>0.0f && !no_subsurface ? 1u : 0u,state->config.samples_per_pixel==1 && state->config.volume_rays_per_pixel==1 && !state->config.volume_spatial_filter ? 1u : 0u,state->config.volume_gauss ? 1u : 0u,state->config.spatial_antialiasing ? 1u : 0u,state->config.volume_spatial_stride};
    VkSpecializationMapEntry entries[]={
        {VOLUME_STEPS_SPECIALIZATION_ID,0,sizeof(uint32_t)},
        {LIGHTING_CACHE_SPECIALIZATION_ID,sizeof(uint32_t),sizeof(uint32_t)},
        {OPTICAL_DEPTH_SPECIALIZATION_ID,2*sizeof(uint32_t),sizeof(uint32_t)},
        {VOLUME_LIGHTING_GRID_SPECIALIZATION_ID,3*sizeof(uint32_t),sizeof(uint32_t)},
        {LIGHTING_TEXTURE_SPECIALIZATION_ID,4*sizeof(uint32_t),sizeof(uint32_t)},
        {ORGANELLE_CACHE_SPECIALIZATION_ID,5*sizeof(uint32_t),sizeof(uint32_t)},
        {SUBSURFACE_ENABLED_SPECIALIZATION_ID,6*sizeof(uint32_t),sizeof(uint32_t)},
        {FUSED_VOLUME_SPECIALIZATION_ID,7*sizeof(uint32_t),sizeof(uint32_t)},
        {VOLUME_GAUSS_SPECIALIZATION_ID,8*sizeof(uint32_t),sizeof(uint32_t)},
        {SPATIAL_AA_SPECIALIZATION_ID,9*sizeof(uint32_t),sizeof(uint32_t)},
        {VOLUME_GRID_STRIDE_SPECIALIZATION_ID,10*sizeof(uint32_t),sizeof(uint32_t)}};
    VkSpecializationInfo specialization={11,entries,sizeof(constants),constants};
    // Built-in pipelines use the startup count. External shaders keep their own contract.
    pipeline.stage.pSpecializationInfo=state->custom_shader ? 0 : &specialization;
    bool success = vk_ok(vkCreateComputePipelines(state->device, VK_NULL_HANDLE, 1, &pipeline, 0, output_pipeline), "Create compute pipeline");

    vkDestroyShaderModule(state->device, shader, 0);
    return success;
}

static bool create_pipeline(RendererState* state, const char* shader_path)
{
    if (state->properties.limits.maxPerStageDescriptorStorageBuffers < RENDER_STORAGE_BUFFER_BINDINGS ||
        state->properties.limits.maxDescriptorSetStorageBuffers < RENDER_STORAGE_BUFFER_BINDINGS)
    {
        fprintf(stderr,"Renderer requires nineteen compute storage-buffer descriptors.\n");
        return false;
    }
    VkDescriptorSetLayoutBinding bindings[RENDER_DESCRIPTOR_COUNT] = {};
    const VkDescriptorType types[] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
    for (uint32_t i = 0; i < RENDER_DESCRIPTOR_COUNT; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = types[i];
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout = {};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = RENDER_DESCRIPTOR_COUNT;
    layout.pBindings = bindings;
    if (!vk_ok(vkCreateDescriptorSetLayout(state->device, &layout, 0, &state->descriptor_layout), "Create descriptor layout"))
        return false;
    const uint32_t sets=RENDER_DESCRIPTOR_SET_COUNT*FRAMES_IN_FLIGHT;
    VkDescriptorPoolSize pool_sizes[] = { { types[RENDER_BINDING_ACCELERATION], sets }, { types[RENDER_BINDING_BEAUTY], sets*RENDER_STORAGE_IMAGE_BINDINGS }, { types[RENDER_BINDING_VERTICES], POOL_STORAGE_BUFFER_DESCRIPTORS*FRAMES_IN_FLIGHT }, { types[RENDER_BINDING_UNIFORM], sets }, {types[RENDER_BINDING_SOURCE_A],POOL_SAMPLER_DESCRIPTORS*FRAMES_IN_FLIGHT} };
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = sets;
    pool.poolSizeCount = DESCRIPTOR_POOL_TYPE_COUNT;
    pool.pPoolSizes = pool_sizes;
    if (!vk_ok(vkCreateDescriptorPool(state->device, &pool, 0, &state->descriptor_pool), "Create descriptor pool"))
        return false;
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = state->descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &state->descriptor_layout;
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
    {
        FrameResources& frame=state->frames[f];
        if (!vk_ok(vkAllocateDescriptorSets(state->device, &allocate, &frame.descriptors), "Allocate descriptors")
            || !vk_ok(vkAllocateDescriptorSets(state->device, &allocate, &frame.debug_descriptors), "Allocate debug descriptors")
            || !vk_ok(vkAllocateDescriptorSets(state->device,&allocate,&frame.effect_descriptors),"Allocate effects descriptors"))
            return false;
        VkWriteDescriptorSetAccelerationStructureKHR acceleration = {};
        acceleration.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        acceleration.accelerationStructureCount = 1;
        acceleration.pAccelerationStructures = &state->tlas;
        VkDescriptorImageInfo image = {};
        image.imageView = state->hdr_view;
        image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo buffers[] = {
            { state->vertices.handle, 0, state->vertices.size },
            { state->triangles.handle, 0, state->triangles.size },
            { frame.uniform.handle, 0, frame.uniform.size },
            { frame.media.handle, 0, frame.media.size },
            { state->energy.handle, 0, state->energy.size },
            { state->shadow_grid.handle, 0, state->shadow_grid.size }, // Current-frame visibility only.
            { state->debug_pixels.handle, 0, state->debug_pixels.size }
        };
        VkWriteDescriptorSet writes[RENDER_DESCRIPTOR_COUNT] = {};
        for (uint32_t i = 0; i < RENDER_DESCRIPTOR_COUNT; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = frame.descriptors;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = types[i];
        }
        writes[RENDER_BINDING_ACCELERATION].pNext = &acceleration;
        writes[RENDER_BINDING_BEAUTY].pImageInfo = &image;
        writes[RENDER_BINDING_VERTICES].pBufferInfo = &buffers[SCENE_BUFFER_VERTICES];
        writes[RENDER_BINDING_TRIANGLES].pBufferInfo = &buffers[SCENE_BUFFER_TRIANGLES];
        writes[RENDER_BINDING_UNIFORM].pBufferInfo = &buffers[SCENE_BUFFER_UNIFORM];
        writes[RENDER_BINDING_MEDIA].pBufferInfo = &buffers[SCENE_BUFFER_MEDIA];
        VkDescriptorBufferInfo medium_ray_buffer={frame.medium_ray_nodes.handle,0,frame.medium_ray_nodes.size};
        writes[RENDER_BINDING_MEDIUM_RAYS].pBufferInfo=&medium_ray_buffer;
        writes[RENDER_BINDING_ENERGY].pBufferInfo = &buffers[SCENE_BUFFER_ENERGY];
        writes[RENDER_BINDING_RESERVED].pBufferInfo = &buffers[SCENE_BUFFER_RESERVED];
        writes[RENDER_BINDING_DEBUG].pBufferInfo = &buffers[SCENE_BUFFER_DEBUG];
        VkDescriptorImageInfo background[2]={};
        for (uint32_t i=0;i<2;++i)
        {
            background[i].sampler=state->backdrop_sampler;
            background[i].imageView=state->backdrop_views[i] ? state->backdrop_views[i] : state->hdr_view;
            background[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            writes[RENDER_BINDING_BACKGROUND_A+i].pImageInfo=&background[i];writes[RENDER_BINDING_SOURCE_A+i].pImageInfo=&background[i];
        }
        VkDescriptorBufferInfo effect_buffers[]={{frame.effects.handle,0,frame.effects.size},
            {frame.effect_tiles.handle,0,frame.effect_tiles.size},{frame.instance_styles.handle,0,frame.instance_styles.size}};
        for (uint32_t i=0;i<3;++i) writes[RENDER_BINDING_EFFECTS+i].pBufferInfo=&effect_buffers[i];
        VkDescriptorBufferInfo sharp_buffer={state->sharp_layer.handle,0,state->sharp_layer.size};
        writes[RENDER_BINDING_SHARP_LAYER].pBufferInfo=&sharp_buffer;
        VkDescriptorBufferInfo lighting_buffer={state->static_lighting.handle,0,state->static_lighting.size};
        VkDescriptorBufferInfo instance_buffer={frame.lighting_instances.handle,0,frame.lighting_instances.size};
        VkDescriptorBufferInfo surface_buffer={state->surface_lighting.handle,0,state->surface_lighting.size};
        writes[RENDER_BINDING_STATIC_LIGHTING].pBufferInfo=&lighting_buffer;
        writes[RENDER_BINDING_LIGHTING_INSTANCES].pBufferInfo=&instance_buffer;
        writes[RENDER_BINDING_SURFACE_LIGHTING].pBufferInfo=&surface_buffer;
        VkDescriptorBufferInfo bounds_buffer={frame.lighting_bounds.handle,0,frame.lighting_bounds.size};
        VkDescriptorBufferInfo dynamic_buffer={state->dynamic_lighting.handle,0,state->dynamic_lighting.size};
        writes[RENDER_BINDING_LIGHTING_BOUNDS].pBufferInfo=&bounds_buffer;
        writes[RENDER_BINDING_DYNAMIC_LIGHTING].pBufferInfo=&dynamic_buffer;
        VkDescriptorImageInfo optical={state->backdrop_sampler,state->optical_depth_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[RENDER_BINDING_OPTICAL_DEPTH].pImageInfo=&optical;
        VkDescriptorImageInfo lighting={state->backdrop_sampler,state->lighting_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[RENDER_BINDING_LIGHTING_TEXTURE].pImageInfo=&lighting;
        VkDescriptorBufferInfo range_buffer={state->mesh_ranges.handle,0,state->mesh_ranges.size};
        writes[RENDER_BINDING_MESH_RANGES].pBufferInfo=&range_buffer;
        VkDescriptorBufferInfo guide_buffer={state->guides.handle,0,state->guides.size};
        writes[RENDER_BINDING_GUIDES].pBufferInfo=&guide_buffer;
        VkDescriptorBufferInfo cascade_buffer={state->radiance_cascades.handle,0,state->radiance_cascades.size};
        writes[RENDER_BINDING_CASCADES].pBufferInfo=&cascade_buffer;
        vkUpdateDescriptorSets(state->device, RENDER_DESCRIPTOR_COUNT, writes, 0, 0);
        for (uint32_t i=0;i<RENDER_DESCRIPTOR_COUNT;++i) writes[i].dstSet=frame.effect_descriptors;
        vkUpdateDescriptorSets(state->device,RENDER_DESCRIPTOR_COUNT,writes,0,0);
        // Same layout, with display RGBA8 instead of the tracer's linear-HDR image.
        image.imageView = state->output_view;
        VkDescriptorBufferInfo text_buffer = {frame.debug_text.handle,0,frame.debug_text.size};
        writes[RENDER_BINDING_UNIFORM].pBufferInfo = &text_buffer;
        for (uint32_t i = 0; i < RENDER_DESCRIPTOR_COUNT; ++i) writes[i].dstSet = frame.debug_descriptors;
        vkUpdateDescriptorSets(state->device, RENDER_DESCRIPTOR_COUNT, writes, 0, 0);
    }

    VkPipelineLayoutCreateInfo pipeline_layout = {};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &state->descriptor_layout;
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES};
    pipeline_layout.pushConstantRangeCount=1;pipeline_layout.pPushConstantRanges=&push;
    return vk_ok(vkCreatePipelineLayout(state->device, &pipeline_layout, 0, &state->pipeline_layout), "Create pipeline layout")
        && load_compute_pipeline(state, shader_path, state->pipeline_layout, &state->pipeline);
}

static bool load_companion(RendererState* state, const char* ray_trace_path,
    const char* filename, VkPipelineLayout layout, VkPipeline* pipeline, bool no_subsurface=false)
{
    const char* slash = strrchr(ray_trace_path,'/');
    const char* backslash = strrchr(ray_trace_path,'\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    size_t directory_length = slash ? (size_t)(slash-ray_trace_path+1) : 0;
    char path[COMPANION_SHADER_PATH_CAPACITY];
    if (directory_length+strlen(filename)+1 > sizeof(path)) return false;
    memcpy(path,ray_trace_path,directory_length);
    strcpy(path+directory_length,filename);
    return load_compute_pipeline(state,path,layout,pipeline,no_subsurface);
}

static bool create_bloom(RendererState* state, const char* ray_trace_path)
{
    VkDescriptorSetLayoutBinding bindings[BLOOM_DESCRIPTOR_COUNT] = {};
    for (uint32_t i = 0; i < BLOOM_DESCRIPTOR_COUNT; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = i==BLOOM_BINDING_UNIFORM ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER :
            (i==BLOOM_BINDING_DEBUG || i==BLOOM_BINDING_SIGHT || i==BLOOM_BINDING_SHARP_LAYER || i==BLOOM_BINDING_GUIDES) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout = {};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = BLOOM_DESCRIPTOR_COUNT;
    layout.pBindings = bindings;
    if (!vk_ok(vkCreateDescriptorSetLayout(state->device, &layout, 0, &state->bloom_descriptor_layout), "Create bloom descriptor layout")) return false;
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,3*FRAMES_IN_FLIGHT},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4*FRAMES_IN_FLIGHT},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,FRAMES_IN_FLIGHT}};
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = FRAMES_IN_FLIGHT;
    pool.poolSizeCount = 3;
    pool.pPoolSizes = sizes;
    if (!vk_ok(vkCreateDescriptorPool(state->device, &pool, 0, &state->bloom_descriptor_pool), "Create bloom descriptor pool")) return false;
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = state->bloom_descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &state->bloom_descriptor_layout;
    for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
    {
        FrameResources& frame=state->frames[f];
        if (!vk_ok(vkAllocateDescriptorSets(state->device, &allocate, &frame.bloom_descriptors), "Allocate bloom descriptors")) return false;
        VkDescriptorImageInfo images[3] = {};
        VkImageView views[] = {state->hdr_view,
            state->hdr_scratch_view,state->output_view};
        VkWriteDescriptorSet writes[BLOOM_DESCRIPTOR_COUNT] = {};
        for (uint32_t i = 0; i < 3; ++i)
        {
            images[i].imageView = views[i];
            images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = frame.bloom_descriptors;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo = &images[i];
        }
        VkDescriptorBufferInfo debug_buffer = {state->debug_pixels.handle,0,state->debug_pixels.size};
        writes[BLOOM_BINDING_DEBUG].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[BLOOM_BINDING_DEBUG].dstSet = frame.bloom_descriptors;
        writes[BLOOM_BINDING_DEBUG].dstBinding = BLOOM_BINDING_DEBUG;
        writes[BLOOM_BINDING_DEBUG].descriptorCount = 1;
        writes[BLOOM_BINDING_DEBUG].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[BLOOM_BINDING_DEBUG].pBufferInfo = &debug_buffer;
        VkDescriptorBufferInfo extra_buffers[]={{frame.uniform.handle,0,frame.uniform.size},{frame.sight.handle,0,frame.sight.size},{state->sharp_layer.handle,0,state->sharp_layer.size},
            {state->guides.handle,0,state->guides.size}};
        for (uint32_t i=0;i<4;++i)
        {
            uint32_t binding=BLOOM_BINDING_UNIFORM+i;
            writes[binding].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[binding].dstSet=frame.bloom_descriptors;
            writes[binding].dstBinding=binding;writes[binding].descriptorCount=1;
            writes[binding].descriptorType=i ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[binding].pBufferInfo=&extra_buffers[i];
        }
        vkUpdateDescriptorSets(state->device, BLOOM_DESCRIPTOR_COUNT, writes, 0, 0);
    }
    VkPushConstantRange constants = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BloomParameters)};
    VkPipelineLayoutCreateInfo pipeline_layout = {};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &state->bloom_descriptor_layout;
    pipeline_layout.pushConstantRangeCount = 1;
    pipeline_layout.pPushConstantRanges = &constants;
    if (!vk_ok(vkCreatePipelineLayout(state->device, &pipeline_layout, 0, &state->bloom_layout), "Create bloom pipeline layout")) return false;
    return load_companion(state,ray_trace_path,"bloom.comp.spv",state->bloom_layout,&state->bloom_pipeline)
        && load_companion(state,ray_trace_path,"fog.comp.spv",state->bloom_layout,&state->fog_pipeline)
        && load_companion(state,ray_trace_path,"medium_blur.comp.spv",state->bloom_layout,&state->medium_blur_pipeline)
        && load_companion(state,ray_trace_path,"focus.comp.spv",state->bloom_layout,&state->focus_pipeline);
}

static bool create_debug(RendererState* state, const char* builtin_shader_path)
{
    VkPushConstantRange constants = {VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(DebugParameters)};
    VkPipelineLayoutCreateInfo layout = {};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &state->descriptor_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    if (!vk_ok(vkCreatePipelineLayout(state->device,&layout,0,&state->debug_overlay_layout),"Create debug layout")
        || !load_companion(state,builtin_shader_path,"debug.comp.spv",state->debug_overlay_layout,&state->debug_overlay_pipeline))
        return false;
    if (!state->debug_available) return true;
#ifdef DEVELOPMENT
    if (!load_companion(state,builtin_shader_path,"debug_surface.comp.spv",state->pipeline_layout,&state->debug_surface_pipeline)
        || !load_companion(state,builtin_shader_path,"debug_volume.comp.spv",state->pipeline_layout,&state->debug_volume_pipeline)) return false;
#endif
    return load_companion(state,builtin_shader_path,"debug_guide_surface.comp.spv",state->pipeline_layout,&state->guide_surface_pipeline);
}

static bool create_surface_toggles(RendererState* state,const char* shader_path)
{
    if (state->headless || state->custom_shader || state->config.subsurface_strength<=0) return true;
    return load_compute_pipeline(state,shader_path,state->pipeline_layout,&state->no_sss_pipeline,true)
#ifdef DEVELOPMENT
        && load_companion(state,shader_path,"debug_surface.comp.spv",state->pipeline_layout,&state->no_sss_debug_pipeline,true)
#endif
        && load_companion(state,shader_path,"debug_guide_surface.comp.spv",state->pipeline_layout,&state->no_sss_guide_pipeline,true);
}

static void shader_buffer_barrier(VkCommandBuffer command)
{
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,1,&barrier,0,0,0,0);
}

static void record_lighting_fields(RendererState* state)
{
    state->lighting_bakes=state->lighting_reuses=state->lighting_surface_bakes=0;
    std::fill(state->grid_written.begin(),state->grid_written.end(),0);
    uint32_t n=state->config.shadow_grid_resolution;
    const RenderMedium* media=(const RenderMedium*)frame_data(state).media.mapped;
    if (n)
    {
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->shadow_grid_pipeline);
        if (state->uniform_data.scene[3])
        {
            {
                for (uint32_t i=0;i<state->active_count;++i) if (state->lighting_input[i].active && !state->surface_valid[state->active_slots[i]])
                {
                    uint32_t parameters[4]={0,LIGHTING_PASS_SURFACE,i,state->lighting_input[i].count};
                    vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
                    vkCmdDispatch(state->command,(state->lighting_input[i].count+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1,1);
                    ++state->lighting_surface_bakes;state->surface_valid[state->active_slots[i]]=1;
                }
            }
            for (uint32_t i=0;i<state->medium_count;++i) if (media[i].lighting[2]>0)
            {
                if (state->lighting_valid[i]) { ++state->lighting_reuses;continue; }
                uint32_t parameters[4]={i,LIGHTING_PASS_STATIC,0,0};
                vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
                vkCmdDispatch(state->command,(n+7)/8,(n*n+7)/8,1);
                state->lighting_valid[i]=1;state->current_static[i]=0;++state->lighting_bakes;
            }
            shader_buffer_barrier(state->command);
        }
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_BAKE_END);
    if (n)
    {
        for (uint32_t i=0;i<state->medium_count;++i) if (media[i].lighting[2]>0)
        {
            bool moving=media[i].lighting[0]>0 && media[i].detail[2]>0;
            if (state->uniform_data.scene[3] && moving)
            {
                uint32_t parameters[4]={i,LIGHTING_PASS_DYNAMIC,0,0};
                vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
                vkCmdDispatch(state->command,(n+7)/8,(n*n+7)/8,1);
            }
            // Without moving shadows the current grid is a copy of the static field.
            bool static_only=state->uniform_data.scene[3] && !moving;
            if (static_only && state->current_static[i]) continue;
            state->current_static[i]=static_only;state->grid_written[i]=1;
            uint32_t parameters[4]={i,LIGHTING_PASS_CURRENT,0,0};
            vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
            vkCmdDispatch(state->command,(n+7)/8,(n*n+7)/8,1);
        }
        shader_buffer_barrier(state->command);
    }
}

// Trace and merge cascades from the farthest down, then project cascade 0 to SH.
// With bounce feedback, surfaces hit by cascade rays read the previous projection.
static void record_radiance_cascades(RendererState* state)
{
    const RenderUniform& u=state->uniform_data;state->cascade_updates=0;
    if (!u.cascades[0] || !state->medium_count) return;
    if (!state->cascades_initialized)
    {
        state->fill_buffer(state->command,state->radiance_cascades.handle,0,VK_WHOLE_SIZE,0);
        VkMemoryBarrier filled={};filled.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        filled.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;filled.dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&filled,0,0,0,0);
        state->cascades_initialized=true;
    }
    // Active cells refresh in turns, stalest first; newly visible cells have the oldest data.
    const RenderMedium* media=(const RenderMedium*)frame_data(state).media.mapped;
    if (state->cascade_age.size()!=state->medium_count)
        state->cascade_age.assign(state->medium_count,UINT32_MAX);
    uint32_t selected[8]={},count=0,limit=state->config.cascade_cells_per_frame;
    // Cells without cascade weight drop their projection; regaining weight makes them stalest.
    for (uint32_t i=0;i<state->medium_count;++i)
        if (media[i].detail[3]<=0) state->cascade_age[i]=UINT32_MAX;
        else if (state->cascade_age[i]<UINT32_MAX) ++state->cascade_age[i];
    for (;;)
    {
        uint32_t best=UINT32_MAX;
        for (uint32_t i=0;i<state->medium_count;++i)
        {
            const RenderMedium& m=media[i];
            if (m.lighting[2]<=0 || m.lighting[0]<=0 || m.center_radius[3]<=0 || m.detail[3]<=0 || state->cascade_age[i]==0) continue;
            if (best==UINT32_MAX || state->cascade_age[i]>state->cascade_age[best]) best=i;
        }
        if (best==UINT32_MAX || (limit && count==limit)) break;
        selected[best/32]|=1u<<(best%32);++count;state->cascade_age[best]=0;
    }
    // A projection older than this weight's eligibility is not shown.
    for (uint32_t i=0;i<state->medium_count;++i)
        if (state->cascade_age[i]==UINT32_MAX) ((RenderMedium*)media)[i].detail[3]=0;
    state->cascade_updates=count;
    if (!count) return;
    // Earlier submissions read the projection; this frame overwrites it.
    shader_buffer_barrier(state->command);
    vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->cascade_pipeline);
    // One dispatch traces every interval; merges run from the second-to-last cascade down;
    // the final pass projects cascade 0.
    const uint32_t group=COMPUTE_GROUP_SIZE*COMPUTE_GROUP_SIZE,count_c=u.cascades[2];
    uint32_t n0=u.cascades[0],block0=n0*n0*n0*u.cascades[1]*u.cascades[1],traced=0;
    for (uint32_t c=0;c<count_c;++c) traced+=block0>>c;
    for (uint32_t step=0;step<count_c+1;++step)
    {
        uint32_t pass=step==0 ? CASCADE_PASS_TRACE : step==count_c ? CASCADE_PASS_PROJECT : CASCADE_PASS_MERGE;
        uint32_t cascade=pass==CASCADE_PASS_MERGE ? count_c-1-step : 0;
        uint32_t items=pass==CASCADE_PASS_TRACE ? traced : pass==CASCADE_PASS_PROJECT ? n0*n0*n0 : block0>>cascade;
        for (uint32_t first=0;first<state->medium_count;first+=32)
        {
            if (!selected[first/32]) continue;
            uint32_t parameters[4]={cascade,pass,selected[first/32],first};
            vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
            vkCmdDispatch(state->command,(items+group-1)/group,1,std::min(32u,state->medium_count-first));
        }
        shader_buffer_barrier(state->command);
    }
}

// Copy current lighting fields to a spatially filtered texture. This is a
// representation change, not a previous-frame lighting or image dependency.
static void record_lighting_texture(RendererState* state)
{
    if (!state->config.texture_lighting_grid || !state->config.shadow_grid_resolution || state->custom_shader) return;
    // Blocks not rewritten this frame already hold their current field.
    state->changed_copies.clear();
    for (uint32_t i=0;i<state->lighting_copies.size();++i)
        if (!state->lighting_image_initialized || state->grid_written[i/3]) state->changed_copies.push_back(state->lighting_copies[i]);
    if (state->changed_copies.empty()) return;
    VkImageMemoryBarrier barrier={};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout=state->lighting_image_initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask=state->lighting_image_initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=state->lighting_image;barrier.subresourceRange=VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkMemoryBarrier fields={};fields.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fields.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;fields.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&fields,0,0,1,&barrier);
    // Inactive blocks may contain unspecified data. They are never sampled.
    state->copy_buffer_to_image(state->command,state->shadow_grid.handle,state->lighting_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        (uint32_t)state->changed_copies.size(),state->changed_copies.data());
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,0,0,0,1,&barrier);
    state->lighting_image_initialized=true;
}

// A fixed bit mask holds debug text. It is separate from all scene images.
static void profile_text(RendererState* state,const char* text,int x,int y,int right)
{
    const int glyph_scale=2,spacing=10;
    for (uint32_t c=0;text[c];++c)
    {
        int left=x+(int)c*spacing;if (left+FONT_GLYPH_WIDTH*glyph_scale>right) break;
        unsigned char character=(unsigned char)text[c];uint32_t glyph=character<FONT_GLYPH_COUNT ? debug_glyphs[character] : 0;
        for (int row=0;row<FONT_GLYPH_HEIGHT*glyph_scale;++row)
            for (int column=0;column<FONT_GLYPH_WIDTH*glyph_scale;++column)
                if (glyph&(1u<<((row/glyph_scale)*FONT_GLYPH_WIDTH+column/glyph_scale)))
                {
                    uint32_t bit=(y+row)*STATS_TEXT_WIDTH+left+column;
                    state->text_mask[bit/TEXT_MASK_WORD_BITS]|=1u<<(bit%TEXT_MASK_WORD_BITS);
                }
    }
}
static const char* feature_status(const Renderer* renderer,uint32_t feature)
{
    if (!(renderer->available_features&feature)) return "N/A";
    return renderer->disabled_features&feature ? "OFF" : "ON";
}

void renderer_toggle_features(Renderer* renderer,uint32_t mask)
{
    mask&=renderer->available_features&RENDER_FEATURE_MASK;
    if (!renderer->state->no_sss_pipeline) mask&=~RENDER_SSS;
    if (!mask) return;
    renderer->disabled_features^=mask;
    profiler_clear_history(&renderer->profiler);
    renderer->debug_stats=true;
    printf("Render features: disabled %u; changed %u; profiler mean reset.\n",renderer->disabled_features,mask);
}

static uint32_t available_features(const RendererState* state,const RenderScene* scene)
{
    uint32_t mask=0;
    if (!state->custom_shader && state->config.subsurface_strength>0) mask|=RENDER_SSS;
    if (!state->custom_shader && state->config.volume_rays_per_pixel && !scene->media.empty()) mask|=RENDER_VOLUME;
    if (!state->custom_shader && state->uniform_data.quality[3]) mask|=RENDER_CELL_BLUR;
    if (state->config.focus_radius>0) mask|=RENDER_TILT_SHIFT;
    if (scene->fog_blur>0) mask|=RENDER_FOG_BLUR;
    if (scene->self_glow || state->config.bloom_strength>0) mask|=RENDER_GLOW;
    return mask;
}

static const char* profile_name(const RendererState* state,uint32_t node)
{
    bool combined=!state->custom_shader && state->config.samples_per_pixel==1 && state->config.volume_rays_per_pixel==1 && !state->config.volume_spatial_filter;
    if (combined && node==PROFILE_SURFACE) return "SURFACE + SSS + VOLUME";
    if (combined && node==PROFILE_VOLUME) return "VOLUME PASS UNUSED";
    return profile_definitions[node].name;
}

static void profile_overlay(RendererState* state,const Renderer* renderer,ProfileBars* bars)
{
    const Profiler* p=&renderer->profiler;char line[160];
    snprintf(line,sizeof(line),"F1 FRAME PROFILER / ROLLING MEAN %u OF %u FRAMES",p->count,PROFILE_HISTORY);
    profile_text(state,line,8,8,STATS_TEXT_WIDTH-8);
    snprintf(line,sizeof(line),"F5 SSS %s / F6 VOLUME %s / F7 CELL BLUR %s",
        feature_status(renderer,RENDER_SSS),feature_status(renderer,RENDER_VOLUME),feature_status(renderer,RENDER_CELL_BLUR));
    profile_text(state,line,8,268,STATS_TEXT_WIDTH-8);
    snprintf(line,sizeof(line),"F8 TILT SHIFT %s / F9 FOG BLUR %s / F10 GLOW %s",
        feature_status(renderer,RENDER_TILT_SHIFT),feature_status(renderer,RENDER_FOG_BLUR),feature_status(renderer,RENDER_GLOW));
    profile_text(state,line,8,286,STATS_TEXT_WIDTH-8);
    if (!p->count) { profile_text(state,"WAITING FOR THE FIRST COMPLETED FRAME",8,30,STATS_TEXT_WIDTH-8);return; }
    snprintf(line,sizeof(line),"MEAN CPU %.2F / GPU %.2F MS   LAST CPU %.2F / GPU %.2F MS   %.1F FPS",
        p->mean[PROFILE_CPU_FRAME].duration_ms,p->mean[PROFILE_GPU_FRAME].duration_ms,
        p->latest[PROFILE_CPU_FRAME].duration_ms,p->latest[PROFILE_GPU_FRAME].duration_ms,
        renderer->previous_frame_ms>0 ? MILLISECONDS_PER_SECOND/renderer->previous_frame_ms : 0);
    profile_text(state,line,8,26,STATS_TEXT_WIDTH-8);
    double range=std::ceil(std::fmax(p->mean[PROFILE_CPU_FRAME].duration_ms,p->mean[PROFILE_GPU_FRAME].duration_ms)/5.0)*5.0;
    range=std::fmax(5.0,range);
    snprintf(line,sizeof(line),"SEPARATE CPU/GPU CLOCKS / SAME SCALE 0 TO %.1F MS / BARS SHOW MEAN / HOVER FOR LAST",range);
    profile_text(state,line,8,44,STATS_TEXT_WIDTH-8);
    profile_text(state,"CPU / WALL TIME INCLUDING WAIT / CHILDREN BELOW PARENTS",8,64,STATS_TEXT_WIDTH-8);
    profile_text(state,"GPU / EXECUTION TIME / CHILDREN BELOW PARENTS",8,164,STATS_TEXT_WIDTH-8);
    static const float palette[8][3]={{.24f,.47f,.68f},{.38f,.56f,.77f},{.33f,.62f,.52f},{.66f,.48f,.29f},{.56f,.42f,.66f},{.55f,.35f,.32f},{.31f,.57f,.61f},{.53f,.53f,.34f}};
    int hover=-1;
    for (uint32_t i=0;i<PROFILE_NODE_COUNT;++i)
    {
        const ProfileDefinition& definition=profile_definitions[i];const ProfileSample& sample=p->mean[i];
        if (sample.duration_ms<=0) continue;
        int32_t* r=bars->rectangles[i];
        r[0]=8+(int)(sample.start_ms/range*(STATS_TEXT_WIDTH-16));
        r[1]=(i<PROFILE_GPU_FRAME ? 84 : 184)+definition.depth*20;
        r[2]=std::min(STATS_TEXT_WIDTH-8,std::max(r[0]+1,8+(int)((sample.start_ms+sample.duration_ms)/range*(STATS_TEXT_WIDTH-16))));r[3]=r[1]+18;
        memcpy(bars->colors[i],palette[i%8],3*sizeof(float));bars->colors[i][3]=1;
        snprintf(line,sizeof(line),"%s %.2F",profile_name(state,i),sample.duration_ms);
        profile_text(state,line,r[0]+4,r[1]+3,r[2]-3);
        if (p->pointer_x>=r[0] && p->pointer_x<r[2] && p->pointer_y>=r[1] && p->pointer_y<r[3]) hover=(int)i;
    }
    if (hover>=0)
        snprintf(line,sizeof(line),"%s / LAST %.3F MS / MEAN %.3F MS",profile_name(state,(uint32_t)hover),p->latest[hover].duration_ms,p->mean[hover].duration_ms);
    else
    {
        int top[3]={PROFILE_SURFACE,PROFILE_VOLUME,PROFILE_CELL_BLUR};
        for (int rank=0;rank<3;++rank)
        {
            top[rank]=-1;
            for (int i=PROFILE_ACCELERATION;i<PROFILE_NODE_COUNT;++i)
            {
                if (profile_definitions[i].depth!=2) continue;
                bool used=false;for (int j=0;j<rank;++j) used=used || top[j]==i;
                if (!used && (top[rank]<0 || p->mean[i].duration_ms>p->mean[top[rank]].duration_ms)) top[rank]=i;
            }
        }
        snprintf(line,sizeof(line),"TOP GPU: %s %.2F / %s %.2F / %s %.2F MS",profile_name(state,(uint32_t)top[0]),p->mean[top[0]].duration_ms,
            profile_name(state,(uint32_t)top[1]),p->mean[top[1]].duration_ms,profile_name(state,(uint32_t)top[2]),p->mean[top[2]].duration_ms);
    }
    profile_text(state,line,8,246,STATS_TEXT_WIDTH-8);
}

static void draw_debug(RendererState* state, const Renderer* renderer)
{
    DebugParameters parameters = {};
    parameters.extent[0] = state->config.width;
    parameters.extent[1] = state->config.height;
    parameters.views = renderer->debug_views ? 1u : 0u;
    parameters.stats = renderer->debug_stats ? 1u : 0u;
    parameters.settings[0] = state->config.exposure;
    parameters.settings[1] = (float)state->config.samples_per_pixel;
    parameters.settings[2] = state->config.camera_max_distance;
    parameters.settings[3] = renderer->overlay_enabled ? (float)(1u+2u*renderer->notice_mask) : 0.0f;
    memcpy(parameters.marker,renderer->selection_marker,sizeof(parameters.marker));
    memcpy(parameters.status,renderer->hud_status,sizeof(parameters.status));
    memcpy(parameters.accent,renderer->hud_accent,sizeof(parameters.accent));
    memcpy(parameters.feedback,renderer->hud_feedback,sizeof(parameters.feedback));
    memcpy(parameters.hover,renderer->hover_marker,sizeof(parameters.hover));
    memcpy(parameters.box,renderer->selection_box,sizeof(parameters.box));
    uint32_t* mask=state->text_mask;memset(mask,0,sizeof(state->text_mask));
    ProfileBars bars={};if (renderer->debug_stats) profile_overlay(state,renderer,&bars);
    if (renderer->overlay_enabled)
        for (uint32_t line=0;line<HUD_TEXT_LINES;++line)
            for (uint32_t column=0;column<HUD_MAX_LINE_CHARACTERS && renderer->hud[line][column];++column)
            {
                unsigned char character=(unsigned char)renderer->hud[line][column];
                uint32_t glyph=character<FONT_GLYPH_COUNT ? debug_glyphs[character] : 0;
                for (uint32_t y=0;y<FONT_GLYPH_HEIGHT;++y)
                    for (uint32_t x=0;x<FONT_GLYPH_WIDTH;++x)
                        if (glyph & (1u << (y*FONT_GLYPH_WIDTH+x)))
                            {
                                uint32_t bit=STATS_TEXT_WIDTH*STATS_TEXT_HEIGHT+(TEXT_MARGIN+line*HUD_MASK_LINE_SPACING+y)*HUD_TEXT_WIDTH+TEXT_MARGIN+column*HUD_MASK_CHARACTER_SPACING+x;
                                mask[bit/TEXT_MASK_WORD_BITS] |= 1u << (bit%TEXT_MASK_WORD_BITS);
                            }
            }
    GpuBuffer& text=frame_data(state).debug_text;
    memcpy((uint32_t*)text.mapped+FONT_GLYPH_COUNT,mask,sizeof(state->text_mask));
    memcpy((char*)text.mapped+FONT_UNIFORM_BYTES+sizeof(state->text_mask),&bars,sizeof(bars));
    vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->debug_overlay_pipeline);
    vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->debug_overlay_layout,
        0,1,&state->debug_descriptors,0,0);
    vkCmdPushConstants(state->command,state->debug_overlay_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
    uint32_t width = (renderer->debug_views || renderer->overlay_enabled) ? state->config.width : std::min(state->config.width,STATS_TEXT_WIDTH_UNSIGNED);
    uint32_t height = (renderer->debug_views || renderer->overlay_enabled) ? state->config.height : std::min(state->config.height,STATS_TEXT_HEIGHT_UNSIGNED);
    vkCmdDispatch(state->command,(width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,(height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
}

static void image_barrier(VkCommandBuffer command, VkImage image, VkImageLayout old_layout,
    VkImageLayout new_layout, VkAccessFlags source, VkAccessFlags destination,
    VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = source;
    barrier.dstAccessMask = destination;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, source_stage, destination_stage, 0, 0, 0, 0, 0, 1, &barrier);
}

static bool hdr_capture(const char* path)
{
    size_t length = path ? strlen(path) : 0;
    return length >= 4 && strcmp(path+length-4,".pfm") == 0;
}

static void copy_output(RendererState* state, uint32_t image_index, const char* capture_path)
{
    image_barrier(state->command, state->output, VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    state->output_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (capture_path)
    {
        bool linear = hdr_capture(capture_path);
        VkImage beauty = state->hdr;
        if (linear)
            image_barrier(state->command,beauty,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = VkExtent3D{ state->config.width, state->config.height, 1 };
        vkCmdCopyImageToBuffer(state->command,linear ? beauty : state->output,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            state->readback.handle, 1, &copy);
        if (linear)
            image_barrier(state->command,beauty,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        VkMemoryBarrier host = {};
        host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, 0, 0, 0);
    }
    if (state->headless)
        return;
    VkImage destination = state->swap_images[image_index];
    // The acquire semaphore waits at TRANSFER; the transition must chain to that wait.
    image_barrier(state->command, destination, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageBlit blit = {};
    blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
    blit.srcOffsets[1] = blit.dstOffsets[1] = VkOffset3D{ (int32_t)state->config.width, (int32_t)state->config.height, 1 };
    // Equal source/destination extents: format conversion only, no upscaling.
    vkCmdBlitImage(state->command, state->output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    image_barrier(state->command, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

static float half_to_float(uint16_t value)
{
    uint32_t sign=(uint32_t)(value>>15)<<31,exponent=(value>>10)&31u,mantissa=value&1023u,bits;
    if (!exponent)
    {
        if (!mantissa) bits=sign;
        else
        {
            exponent=113;while (!(mantissa&1024u)) { mantissa<<=1;--exponent; }
            bits=sign|(exponent<<23)|((mantissa&1023u)<<13);
        }
    }
    else if (exponent==31) bits=sign|0x7f800000u|(mantissa<<13);
    else bits=sign|((exponent+112)<<23)|(mantissa<<13);
    float result;memcpy(&result,&bits,sizeof(result));return result;
}

static bool capture_image(RendererState* state, const char* path)
{
    FILE* file = fopen(path, "wb");
    if (!file)
    {
        fprintf(stderr, "Cannot write capture '%s'.\n", path);
        return false;
    }
    bool linear = hdr_capture(path);
    bool success = fprintf(file,linear ? "PF\n%u %u\n-1.0\n" : "P6\n%u %u\n255\n",
        state->config.width,state->config.height) > 0;
    const unsigned char* pixels = (const unsigned char*)state->readback.mapped;
    for (uint32_t y = 0; y < state->config.height && success; ++y)
    {
        if (linear)
        {
            const uint16_t* source = (const uint16_t*)state->readback.mapped; // RGBA16F texels.
            uint32_t row = state->config.height-1-y; // PFM rows are bottom to top.
            for (uint32_t x = 0; x < state->config.width; ++x)
                for (uint32_t c = 0; c < 3; ++c)
                    state->capture_hdr_row[x*3+c] = half_to_float(source[((size_t)row*state->config.width+x)*4+c]);
            success = fwrite(state->capture_hdr_row.data(),sizeof(float),state->capture_hdr_row.size(),file)
                == state->capture_hdr_row.size();
        }
        else
        {
            for (uint32_t x = 0; x < state->config.width; ++x)
                memcpy(&state->capture_row[x*3],&pixels[((size_t)y*state->config.width+x)*4],3);
            success = fwrite(state->capture_row.data(),1,state->capture_row.size(),file) == state->capture_row.size();
        }
    }
    if (fclose(file) != 0)
        success = false;
    if (!success)
        fprintf(stderr, "Capture write failed for '%s'.\n", path);
    else
        printf("Capture: %s (%ux%u native)\n", path, state->config.width, state->config.height);
    return success;
}

bool renderer_initialize(Renderer* renderer, const RenderScene* scene, const RenderConfig* config,
                         const char* shader_path, const char* builtin_shader_path, bool headless, bool debug_views, bool no_activate)
{
    if (!render_scene_validate(scene)) return false;
    *renderer = Renderer();
    renderer->state = new (std::nothrow) RendererState();
    if (!renderer->state)
        return false;
    RendererState* state = renderer->state;
    state->validation_failed.store(false);
    state->config = *config;
    state->headless = headless;
    state->no_activate = no_activate;
    state->debug_available = !headless || debug_views || scene->guides_required || config->focus_radius>0 || config->volume_spatial_filter;
    state->debug_records = !headless || debug_views;
    state->custom_shader = strcmp(shader_path,builtin_shader_path) != 0;
    if (state->custom_shader && (scene->guides_required || config->volume_spatial_filter))
    {
        fprintf(stderr,"Shader overrides require no depth-guided overlays.\n");
        renderer_shutdown(renderer);
        return false;
    }
    bool success = create_instance(state) && create_device(state) && create_commands(state)
        && create_swapchain(state) && upload_scene(state, scene) && create_output(state) && create_pipeline(state, shader_path)
        && load_companion(state,shader_path,"volume.comp.spv",state->pipeline_layout,&state->volume_pipeline)
        && load_companion(state,shader_path,"shadow_grid.comp.spv",state->pipeline_layout,&state->shadow_grid_pipeline)
        && load_companion(state,shader_path,"radiance_cascade.comp.spv",state->pipeline_layout,&state->cascade_pipeline)
        && create_bloom(state, shader_path) && create_debug(state,builtin_shader_path)
        && create_surface_toggles(state,shader_path)
        && load_companion(state,builtin_shader_path,"effects.comp.spv",state->pipeline_layout,&state->effects_pipeline);
    if (success && config->volume_spatial_filter)
        success=load_companion(state,shader_path,"volume_coarse.comp.spv",state->pipeline_layout,&state->volume_coarse_pipeline)
            && load_companion(state,shader_path,"volume_filter.comp.spv",state->pipeline_layout,&state->volume_filter_pipeline);
    if (success && (scene->background_blur>0))
        success=load_companion(state,builtin_shader_path,"backdrop.comp.spv",state->pipeline_layout,&state->backdrop_pipeline);

    if (!success || state->validation_failed.load())
    {
        renderer_shutdown(renderer);
        return false;
    }
    renderer->running = true;
    renderer->available_features=available_features(state,scene);
    printf("Renderer: direct ray tracing; %ux%u native; %u film samples; %u transmission layers; %u triangles.\n",
        config->width,config->height,config->samples_per_pixel,config->max_transmission_layers,state->triangle_count);
    printf("Sampling: fixed mirrored film points; no stochastic surface transport, denoising, or temporal history.\n");
    printf("Spatial antialiasing: %s; current native pixels; HUD drawn afterward.\n",config->spatial_antialiasing ? "ON" : "OFF");
    if (!state->custom_shader && config->samples_per_pixel==1 && config->volume_rays_per_pixel==1 && !config->volume_spatial_filter)
        printf("Camera traversal: combined surface/SSS/volume; GPU surface includes volume work.\n");
    printf("Subsurface: %.2f strength; %u existing film samples; one strongest back light; current geometry.\n",config->subsurface_strength,config->subsurface_samples);
    printf("Volumes: %u film rays; %u %s integration steps; analytic ambient fill; ray-traced studio shadows.\n",
        config->volume_rays_per_pixel,config->volume_steps,config->volume_gauss ? "Gauss-Legendre" : "midpoint");
    printf("Volume spatial filter: %s; stride %u native pixels; current-frame grid, depth rejection and native silhouette fallback.\n",config->volume_spatial_filter ? "ON" : "OFF",config->volume_spatial_stride);
    printf("Optical depth: %s; %ux%u x %u R32F layers; %llu bytes plus upload storage.\n",
        config->optical_depth_lut && !state->custom_shader ? "lookup" : "midpoint",state->optical_depth_size,state->optical_depth_size,state->optical_depth_layers,
        (unsigned long long)state->optical_depth_upload.size);
    printf("Lighting field: %s; texture %ux%ux%u RGBA32F; %llu texel bytes; static thickness %s.\n",
        config->volume_lighting_grid && !state->custom_shader ? "atmosphere grid" : "per-point atmosphere",
        state->lighting_extent.width,state->lighting_extent.height,state->lighting_extent.depth,
        (unsigned long long)state->lighting_extent.width*state->lighting_extent.height*state->lighting_extent.depth*4*sizeof(float),
        config->organelle_lighting_cache && state->uniform_data.scene[3] ? "baked with live fallback" : "live");
    printf("Current-frame guides: %llu bytes; F2 records %llu bytes; no frame history; native surfaces and output.\n",
        (unsigned long long)state->guides.size,(unsigned long long)state->debug_pixels.size);
    return true;
}

Window* renderer_window(Renderer* renderer)
{ return renderer->state && !renderer->state->headless ? &renderer->state->window : 0; }

bool renderer_poll(Renderer* renderer, PointerInput* input)
{
    *input = PointerInput();
    renderer->running = renderer->state->headless || window_poll(&renderer->state->window, input);
    return renderer->running;
}

// The CPU scene changes vertex positions and normals, with fixed mesh topology.
// Changed ranges go through host staging into device-local vertices. A changing
// BLAS refits; refits keep the tree built for the earlier shape, so it is fully
// rebuilt about once a second during a morph and once the shape has settled.
// Rendering outpaces 60 Hz simulation, so settling needs several unchanged frames.
static const uint32_t BLAS_REFITS_PER_BUILD = 60;
static const uint32_t BLAS_SETTLED_FRAMES = 30;
static bool update_meshes(RendererState* state,const Geometry& scene,bool log)
{
    if (scene.vertices.size()*sizeof(GpuVertex)!=state->vertices.size
        || scene.triangles.size()!=state->triangle_count)
    { fprintf(stderr,"Render mesh storage changed after initialization.\n");return false; }
    uint32_t uploads=0,work=0;
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        MeshAllocation& mesh=state->meshes[i];const MeshRange& source=scene.meshes[i];
        if (source.first!=mesh.range.first || source.count!=mesh.range.count || source.deformable!=mesh.range.deformable
            || source.first_vertex!=mesh.range.first_vertex || source.vertex_count!=mesh.range.vertex_count)
        { fprintf(stderr,"Render mesh topology changed after initialization.\n");return false; }
        bool changed=source.revision!=mesh.range.revision;
        mesh.unchanged=changed ? 0 : mesh.unchanged+1;
        if (!changed && !(mesh.refits && mesh.unchanged>=BLAS_SETTLED_FRAMES)) continue;
        if (!source.deformable) { fprintf(stderr,"Static render mesh changed after initialization.\n");return false; }
        if (!work++)
        {
            // Earlier submissions may still read these vertices and this BLAS.
            vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,0,0,0,0,0,0,0);
        }
        if (!changed) continue;
        VkDeviceSize bytes=(VkDeviceSize)source.vertex_count*sizeof(GpuVertex);
        GpuBuffer& upload=frame_data(state).vertex_upload;
        memcpy((char*)upload.mapped+state->vertex_upload_offsets[i],scene.vertices.data()+source.first_vertex,bytes);
        VkBufferCopy copy={state->vertex_upload_offsets[i],(VkDeviceSize)source.first_vertex*sizeof(GpuVertex),bytes};
        state->copy_buffer(state->command,upload.handle,state->vertices.handle,1,&copy);
        ++uploads;
    }
    if (!work) return true;
    VkMemoryBarrier uploaded={};uploaded.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    uploaded.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    uploaded.dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    if (uploads) vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&uploaded,0,0,0,0);
    uint32_t refits=0,builds=0;
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        MeshAllocation& mesh=state->meshes[i];const MeshRange& source=scene.meshes[i];
        bool changed=source.revision!=mesh.range.revision;
        if (!changed && !(mesh.refits && mesh.unchanged>=BLAS_SETTLED_FRAMES)) continue;
        bool rebuild=!changed || mesh.refits>=BLAS_REFITS_PER_BUILD;
        VkAccelerationStructureGeometryKHR geometries[2];VkAccelerationStructureBuildRangeInfoKHR ranges[2];
        mesh_geometries(state,mesh,geometries,ranges);
        VkAccelerationStructureBuildGeometryInfoKHR build={};build.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build.mode=rebuild ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        build.srcAccelerationStructure=rebuild ? VK_NULL_HANDLE : mesh.acceleration;build.dstAccelerationStructure=mesh.acceleration;
        build.geometryCount=2;build.pGeometries=geometries;
        build.scratchData.deviceAddress=aligned_scratch(state,mesh.scratch.address);
        const VkAccelerationStructureBuildRangeInfoKHR* pointer=ranges;state->build_as(state->command,1,&build,&pointer);
        // After a rebuild during a change, one more rebuild follows once the shape settles.
        mesh.refits=rebuild ? (changed ? 1u : 0u) : mesh.refits+1;
        mesh.range=source;rebuild ? ++builds : ++refits;
    }
    // The following TLAS build barrier makes these BLAS writes visible.
    if (log) printf("Deformed mesh updates: %u BLAS refits; %u rebuilds.\n",refits,builds);
    return true;
}

// Bilinear fog is zero if every field vertex under the projected viewport is one.
// Include a two-cell guard. Unbounded projections and field edges retain the filter.
static bool fog_covers_view(const RenderUniform& u,const SightField& field)
{
    if (field.spacing<=0) return true;
    double low[2]={1e30,1e30},high[2]={-1e30,-1e30};
    for (int y=-1;y<=1;y+=2) for (int x=-1;x<=1;x+=2)
    {
        double direction[3];
        for (int a=0;a<3;++a) direction[a]=u.camera_forward[a]
            +x*u.display[1]*u.display[2]*u.camera_right[a]+y*u.display[1]*u.camera_up[a];
        if (fabs(direction[2])<.000001) return true;
        double distance=(u.fog[2]-u.camera_position[2])/direction[2];
        if (distance<=0) return true;
        for (int a=0;a<2;++a)
        {
            double p=u.camera_position[a]+distance*direction[a];
            low[a]=std::min(low[a],p);high[a]=std::max(high[a],p);
        }
    }
    double origin[2]={field.low_x,field.low_y};int begin[2],end[2];
    for (int a=0;a<2;++a)
    {
        double l=floor((low[a]-origin[a])/field.spacing)-2;
        double h=ceil((high[a]-origin[a])/field.spacing)+2;
        if (l<0 || h>=SIGHT_FIELD_SIDE) return true;
        begin[a]=(int)l;end[a]=(int)h;
    }
    for (int y=begin[1];y<=end[1];++y) for (int x=begin[0];x<=end[0];++x)
        if (field.reveal[y*SIGHT_FIELD_SIDE+x]!=1.0f) return true;
    return false;
}

// Reads a completed slot's timestamps. Lines and profiler spans describe that
// earlier frame, so bounded runs still print one report per rendered frame.
static bool report_frame(RendererState* state,Renderer* renderer,FrameResources& frame,double slot_start=0)
{
    Profiler* profile=&renderer->profiler;
    uint64_t timestamps[GPU_TIMESTAMP_COUNT];
    if (!vk_ok(vkGetQueryPoolResults(state->device, frame.queries, 0, GPU_TIMESTAMP_COUNT, sizeof(timestamps),
        timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "Read GPU timestamps"))
        return false;
    double query_end=profiler_clock_ms();
    if (slot_start>0) profiler_cpu(profile,PROFILE_QUERY_READ,slot_start,query_end);
    frame.pending=false;
    const FrameReport& report=frame.report;const RenderUniform* uniform=&report.uniform;
    uint64_t difference = timestamps[GPU_TIME_VOLUME_END] - timestamps[GPU_TIME_UPLOAD_END];
    if (state->timestamp_bits < TIMESTAMP_WORD_BITS)
        difference &= (UINT64_C(1) << state->timestamp_bits) - 1;
    double milliseconds = difference * state->properties.limits.timestampPeriod / NANOSECONDS_PER_MILLISECOND;
    if (renderer->frame_log) printf("GPU trace: %.3f ms; %u spp; %u triangles\n", milliseconds,
        state->config.samples_per_pixel, state->triangle_count);
    uint64_t surface_ticks = timestamps[GPU_TIME_SURFACE_END]-timestamps[GPU_TIME_SURFACE_BEGIN], volume_ticks = timestamps[GPU_TIME_VOLUME_END]-timestamps[GPU_TIME_SURFACE_END];
    if (state->timestamp_bits < TIMESTAMP_WORD_BITS)
    {
        surface_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
        volume_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    }
    uint64_t medium_blur_ticks=timestamps[GPU_TIME_MEDIUM_BLUR_END]-timestamps[GPU_TIME_EFFECTS_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) medium_blur_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;
    if (renderer->frame_log) printf("GPU medium blur: %.3f ms; native resolution; transmitted light and effects.\n",medium_blur_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND);
    uint64_t grid_ticks=timestamps[GPU_TIME_GRID_END]-timestamps[GPU_TIME_BACKDROP_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) grid_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;
    if (renderer->frame_log) printf("GPU shadow grid: %.3f ms; resolution %u; current dynamic blockers.\n",grid_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,state->config.shadow_grid_resolution);
    uint64_t cascade_ticks=timestamps[GPU_TIME_CASCADE_END]-timestamps[GPU_TIME_GRID_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) cascade_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;
    if (renderer->frame_log && uniform->cascades[0]) printf("GPU radiance cascades: %.3f ms; %u cells traced; %u probes, %u directions, %u cascades, bounce feedback %s; %llu bytes.\n",
        cascade_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,report.cascade_updates,uniform->cascades[0],uniform->cascades[1]*uniform->cascades[1],
        uniform->cascades[2],uniform->cascades[3] ? "ON" : "OFF",(unsigned long long)state->radiance_cascades.size);
    uint64_t bake_ticks=timestamps[GPU_TIME_BAKE_END]-timestamps[GPU_TIME_BACKDROP_END];
    uint64_t dynamic_ticks=timestamps[GPU_TIME_GRID_END]-timestamps[GPU_TIME_BAKE_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS)
    { bake_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;dynamic_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1; }
    if (renderer->frame_log && state->config.optical_depth_lut) printf("Optical depth tables: %u rebuilt.\n",report.optical_depth_bakes);
    if (renderer->frame_log) printf("GPU static lighting bake: %.3f ms; GPU dynamic lighting: %.3f ms.\n",
        bake_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,
        dynamic_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND);
    if (renderer->frame_log && uniform->scene[3]) printf("Static lighting: %u baked, %u reused; %llu bytes; %u surface bakes; current dynamic shadows.\n",
        report.lighting_bakes,report.lighting_reuses,(unsigned long long)(state->static_lighting.size+state->surface_lighting.size+frame.lighting_instances.size+state->dynamic_lighting.size+frame.lighting_bounds.size),report.lighting_surface_bakes);
    uint64_t background_ticks=timestamps[GPU_TIME_BACKDROP_END]-timestamps[GPU_TIME_UPLOAD_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) background_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    if (renderer->frame_log) printf("GPU backdrop: %.3f ms; separate current-frame layer %u.\n",
        background_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,report.background_layer ? 1u : 0u);
    if (renderer->frame_log) printf("GPU surface: %.3f ms; GPU volumes: %.3f ms.\n",
        surface_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,
        volume_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND);
    uint64_t bloom_ticks = timestamps[GPU_TIME_BLOOM_END] - timestamps[GPU_TIME_MEDIUM_BLUR_END];
    if (state->timestamp_bits < TIMESTAMP_WORD_BITS)
        bloom_ticks &= (UINT64_C(1) << state->timestamp_bits) - 1;
    double bloom_ms = bloom_ticks * state->properties.limits.timestampPeriod / NANOSECONDS_PER_MILLISECOND;
    uint64_t debug_ticks = timestamps[GPU_TIME_OVERLAY_END]-timestamps[GPU_TIME_BLOOM_END];
    if (state->timestamp_bits < TIMESTAMP_WORD_BITS) debug_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    double debug_ms = debug_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND;
    uint64_t gpu_ticks=timestamps[GPU_TIME_OVERLAY_END]-timestamps[GPU_TIME_FRAME_BEGIN],as_ticks=timestamps[GPU_TIME_UPLOAD_END]-timestamps[GPU_TIME_FRAME_BEGIN],fx_ticks=timestamps[GPU_TIME_EFFECTS_END]-timestamps[GPU_TIME_EFFECTS_BEGIN];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) { uint64_t mask=(UINT64_C(1)<<state->timestamp_bits)-1;gpu_ticks&=mask;as_ticks&=mask;fx_ticks&=mask; }
    double scale=state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND;
    renderer->gpu_frame_ms=gpu_ticks*scale;
    if (renderer->frame_log) printf("GPU acceleration update: %.3f ms; GPU effects: %.3f ms.\n",as_ticks*scale,fx_ticks*scale);
    if (renderer->frame_log) printf("GPU fog/bloom/display: %.3f ms; total render: %.3f ms\n",bloom_ms,renderer->gpu_frame_ms);
    uint64_t halo_ticks=(timestamps[GPU_TIME_BLOOM_END]-timestamps[GPU_TIME_FOCUS_END]);
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) halo_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;
    if (renderer->frame_log) printf("GPU halo/display: %.3f ms.\n",halo_ticks*scale);
    if (renderer->frame_log) printf("GPU debug overlay: %.3f ms; buffer views %u; FPS stats %u.\n",debug_ms,
        report.debug_views ? 1u : 0u,report.debug_stats ? 1u : 0u);
    if (renderer->frame_log) printf("Surface self-glow: %s; halo strength %.3f.\n",uniform->membrane_fade[3]>0 ? "ON" : "OFF",report.disabled_features&RENDER_GLOW ? 0.0f : state->config.bloom_strength);
    if (renderer->frame_log) printf("Render settings: disabled %u; SSS %s; volume rays %u; cell blur %u; tilt radius %.1f; fog radius %.1f.\n",
        report.disabled_features,feature_status(renderer,RENDER_SSS),uniform->quality[0],uniform->quality[3],uniform->focus_band[3],uniform->fog[0]);
    static const struct { ProfileNode node;int begin,end; } spans[]={
        {PROFILE_GPU_FRAME,GPU_TIME_FRAME_BEGIN,GPU_TIME_COPY_END},
        {PROFILE_GPU_SCENE,GPU_TIME_FRAME_BEGIN,GPU_TIME_VOLUME_END},
        {PROFILE_ACCELERATION,GPU_TIME_FRAME_BEGIN,GPU_TIME_UPLOAD_END},
        {PROFILE_BACKDROP,GPU_TIME_UPLOAD_END,GPU_TIME_BACKDROP_END},
        {PROFILE_SHADOW_GRID,GPU_TIME_BACKDROP_END,GPU_TIME_CASCADE_END},
        {PROFILE_SURFACE,GPU_TIME_CASCADE_END,GPU_TIME_SURFACE_END},
        {PROFILE_VOLUME,GPU_TIME_SURFACE_END,GPU_TIME_VOLUME_END},
        {PROFILE_GPU_POST,GPU_TIME_VOLUME_END,GPU_TIME_BLOOM_END},
        {PROFILE_EFFECTS,GPU_TIME_VOLUME_END,GPU_TIME_EFFECTS_END},
        {PROFILE_CELL_BLUR,GPU_TIME_EFFECTS_END,GPU_TIME_MEDIUM_BLUR_END},
        {PROFILE_FOG,GPU_TIME_MEDIUM_BLUR_END,GPU_TIME_FOG_END},
        {PROFILE_FOCUS,GPU_TIME_FOG_END,GPU_TIME_FOCUS_END},
        {PROFILE_BLOOM,GPU_TIME_FOCUS_END,GPU_TIME_BLOOM_END},
        {PROFILE_GPU_OUTPUT,GPU_TIME_BLOOM_END,GPU_TIME_COPY_END},
        {PROFILE_OVERLAY,GPU_TIME_BLOOM_END,GPU_TIME_OVERLAY_END},
        {PROFILE_COPY,GPU_TIME_OVERLAY_END,GPU_TIME_COPY_END}
    };
    uint64_t mask=state->timestamp_bits<TIMESTAMP_WORD_BITS ? (UINT64_C(1)<<state->timestamp_bits)-1 : UINT64_MAX;
    for (uint32_t i=0;i<sizeof(spans)/sizeof(spans[0]);++i)
        profiler_span(profile,spans[i].node,((timestamps[spans[i].begin]-timestamps[GPU_TIME_FRAME_BEGIN])&mask)*scale,
            ((timestamps[spans[i].end]-timestamps[spans[i].begin])&mask)*scale);
    state->trace_ms = milliseconds;
    state->volume_ms = volume_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND;
    state->bloom_ms = bloom_ms;
    return true;
}

// Reports every submitted slot in submission order after the GPU finishes them.
static bool report_pending(RendererState* state,Renderer* renderer)
{
    if (!vk_ok(vkQueueWaitIdle(state->queue),"Wait for queued frames")) return false;
    for (uint32_t i=1;i<=FRAMES_IN_FLIGHT;++i)
    {
        FrameResources& frame=state->frames[(state->frame+i)%FRAMES_IN_FLIGHT];
        if (frame.pending && !report_frame(state,renderer,frame)) return false;
    }
    if (renderer->frame_log) fflush(stdout);
    return true;
}

bool renderer_render(Renderer* renderer, const RenderScene* scene, const Camera* camera, const char* capture_path)
{
    RendererState* state = renderer->state;
    Profiler* profile=&renderer->profiler;double render_start=profiler_clock_ms(),phase_start=render_start;
    if (renderer->debug_views && (!state->debug_available || state->custom_shader))
    {
        fprintf(stderr,"Buffer views require the bundled integrator and an interactive or --debug-views launch.\n");
        return false;
    }
    double acquire_ms=0,present_ms=0;
    uint32_t image_index = 0;
    FrameResources& frame=frame_data(state);
    // Only this slot's previous submission must finish; the other slot keeps the GPU busy.
    if (!vk_ok(vkWaitForFences(state->device,1,&frame.fence,VK_TRUE,UINT64_MAX),"Wait for frame slot")) return false;
    double fence_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_SLOT_FENCE,phase_start,fence_end);
    profiler_cpu(profile,PROFILE_QUERY_READ,fence_end,fence_end);
    if (frame.pending && !report_frame(state,renderer,frame,fence_end)) return false;
    double phase_end=profiler_clock_ms();
    profiler_cpu(profile,PROFILE_QUERY_REPORT,fence_end+profile->current[PROFILE_QUERY_READ].duration_ms,phase_end);
    profiler_cpu(profile,PROFILE_READ_TIMES,phase_start,phase_end);phase_start=phase_end;
    if (!state->headless)
    {
        if (!renderer->scene_submissions) printf("Window frame: acquire.\n");
        double acquire_start=renderer_ms();
        VkResult result = vkAcquireNextImageKHR(state->device, state->swapchain, UINT64_MAX,
            frame.image_available, VK_NULL_HANDLE, &image_index);
        acquire_ms=renderer_ms()-acquire_start;
        if (result != VK_SUBOPTIMAL_KHR && !vk_ok(result, "Acquire window image"))
            return false;
    }
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_ACQUIRE,phase_start,phase_end);phase_start=phase_end;
    if (!begin_commands(state))
        return false;
    vkCmdResetQueryPool(state->command,state->queries,0,GPU_TIMESTAMP_COUNT);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,state->queries,GPU_TIME_FRAME_BEGIN);
    RenderUniform* uniform = &state->uniform_data;
    Vec3 forward = normalize(subtract(camera->target, camera->position));
    Vec3 right = normalize(cross(forward, camera->up));
    store_vector(uniform->camera_position, camera->position);
    store_vector(uniform->camera_forward, forward);
    store_vector(uniform->camera_right, right);
    store_vector(uniform->camera_up, cross(right, forward));
    scene_uniform(uniform,scene);
    renderer->available_features=available_features(state,scene);
    // Restore authored values before applying this frame's diagnostic switches.
    uniform->quality[0]=renderer->disabled_features&RENDER_VOLUME ? 0u : state->config.volume_rays_per_pixel;
    uniform->subsurface[0]=renderer->disabled_features&RENDER_SSS ? 0.0f : state->config.subsurface_strength;
    uniform->focus_band[3]=renderer->disabled_features&RENDER_TILT_SHIFT ? 0.0f : state->config.focus_radius;
    if (renderer->disabled_features&RENDER_FOG_BLUR) uniform->fog[0]=0;
    uniform->membrane_fade[3]=renderer->disabled_features&RENDER_GLOW ? 0.0f : 1.0f;
    if (state->custom_shader || (renderer->disabled_features&RENDER_CELL_BLUR))
    { uniform->quality[3]=0;uniform->membrane_fade[2]=0; }
#ifdef DEVELOPMENT
    uniform->wireframe_line[3]=0;
    if (renderer->wireframe && !state->custom_shader && state->config.wireframe_pixels>0)
    {
        const RenderConfig& c=state->config;
        uniform->wireframe_line[0]=c.wireframe_line.r;uniform->wireframe_line[1]=c.wireframe_line.g;uniform->wireframe_line[2]=c.wireframe_line.b;
        uniform->wireframe_line[3]=c.wireframe_pixels;
        uniform->wireframe_fill[0]=c.wireframe_fill.r;uniform->wireframe_fill[1]=c.wireframe_fill.g;uniform->wireframe_fill[2]=c.wireframe_fill.b;
        // Edges only: no medium, blur, focus or glow over the lines.
        uniform->quality[0]=0;uniform->subsurface[0]=0;uniform->focus_band[3]=0;uniform->fog[0]=0;
        uniform->membrane_fade[3]=0;uniform->quality[3]=0;uniform->membrane_fade[2]=0;
    }
#endif
    if (scene->instances.size()!=state->instance_count || scene->media.size()!=state->medium_count
        || scene->geometry.meshes.size()!=state->mesh_count || scene->effect_tiles.size()!=state->effect_tile_size
        || scene->lighting_bounds.size()!=state->medium_count)
    { fprintf(stderr,"Render scene storage changed after initialization.\n");return false; }
    if (uniform->scene[3])
    {
        // Slots outside the ranges are empty, so every static slot must lie inside them.
        uint32_t covered=0;
        for (uint32_t r=0;r<scene->ranges.size();++r) for (uint32_t i=scene->ranges[r].first;i<scene->ranges[r].first+scene->ranges[r].count;++i)
        {
            if (state->surface_cache_meshes[i]!=(scene->instances[i].static_lighting ? scene->instances[i].mesh+1 : 0))
            { fprintf(stderr,"Static lighting instance layout changed after initialization.\n");return false; }
            covered+=state->surface_cache_meshes[i]!=0;
        }
        if (covered!=state->surface_slots.size()) { fprintf(stderr,"Static lighting instance layout changed after initialization.\n");return false; }
    }
    if (!update_meshes(state,scene->geometry,renderer->frame_log)) return false;
    scene_instances(state,scene);
    if (state->medium_count) memcpy(frame.media.mapped,scene->media.data(),state->medium_count*sizeof(RenderMedium));
    memcpy(frame.lighting_instances.mapped,state->lighting_input.data(),state->active_count*sizeof(LightingInstance));
    if (state->medium_count) memcpy(frame.lighting_bounds.mapped,scene->lighting_bounds.data(),state->medium_count*sizeof(RenderGridBounds));
    record_optical_depth(state);
    update_lighting_dependencies(state,scene);
    update_grid_activity(state);
    update_level_of_detail(state,scene);
    update_medium_ray_nodes(state,scene);
    if (scene->fog_blur>0) memcpy(frame.sight.mapped,scene->fog_field.reveal,sizeof(scene->fog_field.reveal));
    memcpy((char*)frame.sight.mapped+sizeof(scene->fog_field.reveal),scene->sight,sizeof(scene->sight));
    memcpy(frame.instances.mapped,state->instance_data.data(),state->active_count*sizeof(state->instance_data[0]));
    record_tlas_build(state,frame.instances.address);
    memcpy(frame.uniform.mapped,uniform,sizeof(*uniform));
    memcpy(frame.instance_styles.mapped,state->style_data.data(),state->active_count*INSTANCE_STYLE_GPU_FLOATS*sizeof(float));
    state->effect_count=scene->effect_count;
    memcpy(frame.effects.mapped,scene->effects.data(),state->effect_count*sizeof(EffectSegment));
    // Tile offsets, CPU cursors, then only the references this frame uses.
    if (state->effect_count)
        memcpy(frame.effect_tiles.mapped,scene->effect_tiles.data(),
            (2*(size_t)scene->tile_count+1+scene->effect_tiles[scene->tile_count])*sizeof(uint32_t));
    VkMemoryBarrier host_upload={};host_upload.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host_upload.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host_upload.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,1,&host_upload,0,0,0,0);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_UPLOAD_END);
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_UPLOAD,phase_start,phase_end);phase_start=phase_end;
    VkAccessFlags previous = state->output_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_WRITE_BIT;
    image_barrier(state->command, state->output, state->output_layout, VK_IMAGE_LAYOUT_GENERAL,
        state->output_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : previous, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    state->output_layout = VK_IMAGE_LAYOUT_GENERAL;
    VkImage hdr_images[] = {state->hdr, state->hdr_scratch};
    for (uint32_t i = 0; i < 2; ++i)
        image_barrier(state->command, hdr_images[i],
            state->hdr_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            state->hdr_initialized ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0,
            VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    state->hdr_initialized = true;
    if ((scene->background_blur>0))
    {
        for (uint32_t i=0;i<2;++i)
            image_barrier(state->command,state->backdrop_images[i],
                state->backdrop_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,
                state->backdrop_initialized ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        state->backdrop_initialized=true;
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->backdrop_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline_layout,0,1,&state->descriptors,0,0);
        for (int32_t pass=0;pass<3;++pass)
        {
            int32_t parameters[4]={pass,0,0,0};
            vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
            vkCmdDispatch(state->command,(state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,(state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
            for (uint32_t i=0;i<2;++i)
                image_barrier(state->command,state->backdrop_images[i],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_BACKDROP_END);
    shader_buffer_barrier(state->command);
    vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline_layout,0,1,&state->descriptors,0,0);
    record_lighting_fields(state);
    record_lighting_texture(state);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_GRID_END);
    record_radiance_cascades(state);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_CASCADE_END);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_SURFACE_BEGIN);
    bool no_sss=(renderer->disabled_features&RENDER_SSS)!=0;
    VkPipeline surface=renderer->debug_views ? (no_sss ? state->no_sss_debug_pipeline : state->debug_surface_pipeline)
        : (scene->guides_required || state->config.volume_spatial_filter || uniform->focus_band[3]>0) ? (no_sss ? state->no_sss_guide_pipeline : state->guide_surface_pipeline)
        : (no_sss ? state->no_sss_pipeline : state->pipeline);
    vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,surface);
    vkCmdBindDescriptorSets(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->pipeline_layout,
        0, 1, &state->descriptors, 0, 0);
    vkCmdDispatch(state->command, (state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, (state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, 1);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_SURFACE_END);
    if (renderer->debug_views || scene->guides_required || state->config.volume_spatial_filter || uniform->quality[3] || state->config.focus_radius>0) shader_buffer_barrier(state->command);
    if (uniform->quality[0] && uniform->scene[1]
        && (state->custom_shader || state->config.samples_per_pixel!=1 || state->config.volume_rays_per_pixel!=1 || state->config.volume_spatial_filter))
    {
        image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (state->config.volume_spatial_filter && !renderer->debug_views)
        {
            vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->volume_coarse_pipeline);
            vkCmdDispatch(state->command,((state->config.width+state->config.volume_spatial_stride-1)/state->config.volume_spatial_stride+7)/8,((state->config.height+state->config.volume_spatial_stride-1)/state->config.volume_spatial_stride+7)/8,1);
            shader_buffer_barrier(state->command);
            vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->volume_filter_pipeline);
            vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);

        }
        else
        {
            vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,
                renderer->debug_views ? state->debug_volume_pipeline : state->volume_pipeline);
            vkCmdDispatch(state->command,(state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,(state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
        }
    }
    vkCmdWriteTimestamp(state->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state->queries, GPU_TIME_VOLUME_END);
    if (renderer->debug_views || scene->guides_required || state->config.volume_spatial_filter || uniform->quality[3] || state->config.focus_radius>0) shader_buffer_barrier(state->command);
    image_barrier(state->command, state->hdr, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_EFFECTS_BEGIN);
    if (scene->guides_required && state->effect_count)
    {
        VkImage beauty=state->hdr;
        image_barrier(state->command,beauty,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->effects_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline_layout,0,1,&state->effect_descriptors,0,0);
        vkCmdDispatch(state->command,(state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,(state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
        image_barrier(state->command,beauty,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_EFFECTS_END);
    if (uniform->quality[3])
    {
        shader_buffer_barrier(state->command);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->medium_blur_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->bloom_layout,0,1,&state->bloom_descriptors,0,0);
        for (int pass=0;pass<2;++pass)
        {
            image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            BloomParameters parameters={};parameters.extent[0]=state->config.width;parameters.extent[1]=state->config.height;parameters.pass=pass;
            vkCmdPushConstants(state->command,state->bloom_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
            vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);
        }
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline_layout,0,1,&state->descriptors,0,0);
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_MEDIUM_BLUR_END);
    BloomParameters bloom = {};
    bloom.extent[0] = (int32_t)state->config.width;
    bloom.extent[1] = (int32_t)state->config.height;
    bloom.radius = (int32_t)state->config.bloom_radius;
    bloom.threshold = state->config.bloom_threshold;
    bloom.strength = renderer->disabled_features&RENDER_GLOW ? 0.0f : state->config.bloom_strength;
    bloom.exposure = state->config.exposure;
    bloom.debug_views = renderer->debug_views ? 1.0f : 0.0f;
    bloom.emissive_only=state->uniform_data.background_quality[3];
    if (uniform->fog[0]>0 && !renderer->debug_views && fog_covers_view(*uniform,scene->fog_field))
    {
        image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->fog_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->bloom_layout,0,1,&state->bloom_descriptors,0,0);
        uint32_t width=(state->config.width+FOG_DOWNSAMPLE-1)/FOG_DOWNSAMPLE,height=(state->config.height+FOG_DOWNSAMPLE-1)/FOG_DOWNSAMPLE;
        for (int pass=0;pass<=FOG_COMPOSITE_PASS;++pass)
        {
            bloom.pass=pass;
            vkCmdPushConstants(state->command,state->bloom_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(bloom),&bloom);
            bool composite=pass==FOG_COMPOSITE_PASS;
            vkCmdDispatch(state->command,((composite ? state->config.width : width)+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,
                ((composite ? state->config.height : height)+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
            image_barrier(state->command,composite ? state->hdr : state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        bloom.pass=0;
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_FOG_END);
    if (uniform->focus_band[3]>0 && !renderer->debug_views)
    {
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->focus_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->bloom_layout,0,1,&state->bloom_descriptors,0,0);
        for (int pass=0;pass<2;++pass)
        {
            image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            bloom.pass=pass;vkCmdPushConstants(state->command,state->bloom_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(bloom),&bloom);
            vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);
        }
        image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_FOCUS_END);
    vkCmdBindPipeline(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->bloom_pipeline);
    vkCmdBindDescriptorSets(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->bloom_layout, 0, 1, &state->bloom_descriptors, 0, 0);
    if (bloom.strength>0.0f && bloom.emissive_only) shader_buffer_barrier(state->command);
    if (bloom.strength>0.0f && state->config.height>=2)
    {
        const uint32_t width=(state->config.width+BLOOM_DOWNSAMPLE-1)/BLOOM_DOWNSAMPLE;
        const uint32_t height=(state->config.height+BLOOM_DOWNSAMPLE-1)/BLOOM_DOWNSAMPLE;
        for (bloom.pass=0;bloom.pass<BLOOM_COMPOSITE_PASS;++bloom.pass)
        {
            image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            vkCmdPushConstants(state->command,state->bloom_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(bloom),&bloom);
            vkCmdDispatch(state->command,(width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,(height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
        }
        image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    bloom.pass = BLOOM_COMPOSITE_PASS;
    vkCmdPushConstants(state->command, state->bloom_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bloom), &bloom);
    vkCmdDispatch(state->command, (state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, (state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, 1);
    vkCmdWriteTimestamp(state->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state->queries, GPU_TIME_BLOOM_END);
    if (renderer->debug_stats || renderer->debug_views || renderer->overlay_enabled)
    {
        shader_buffer_barrier(state->command);
        image_barrier(state->command,state->output,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        draw_debug(state,renderer);
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_OVERLAY_END);
    if (capture_path || !state->headless)
        copy_output(state,image_index,capture_path);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_COPY_END);
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_RECORD,phase_start,phase_end);phase_start=phase_end;
    FrameReport& report=frame.report;report.uniform=*uniform;report.disabled_features=renderer->disabled_features;
    report.lighting_bakes=state->lighting_bakes;report.lighting_reuses=state->lighting_reuses;
    report.lighting_surface_bakes=state->lighting_surface_bakes;report.optical_depth_bakes=state->optical_depth_bakes;
    report.cascade_updates=state->cascade_updates;
    report.debug_views=renderer->debug_views;report.debug_stats=renderer->debug_stats;report.background_layer=scene->background_blur>0;
    if (!submit_commands(state, !state->headless, image_index, false))
        return false;
    frame.pending=true;
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_SUBMIT_WAIT,phase_start,phase_end);phase_start=phase_end;
    if (!state->headless)
    {
        if (!renderer->scene_submissions) printf("Window frame: present.\n");
        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &state->render_finished[image_index];
        present.swapchainCount = 1;
        present.pSwapchains = &state->swapchain;
        present.pImageIndices = &image_index;
        double present_start=renderer_ms();
        VkResult result = vkQueuePresentKHR(state->queue, &present);
        if (result != VK_SUBOPTIMAL_KHR && !vk_ok(result, "Present frame"))
            return false;
        present_ms=renderer_ms()-present_start;
    }
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_PRESENT,phase_start,phase_end);phase_start=phase_end;
    renderer->acquire_ms=acquire_ms;renderer->present_ms=present_ms;
    if (renderer->frame_log) printf("Presentation waits: acquire %.3f ms; present %.3f ms; GPU work %.3f ms.\n",renderer->acquire_ms,renderer->present_ms,renderer->gpu_frame_ms);
    // A capture reads this frame's readback, so it waits for every queued frame.
    if (capture_path && (!report_pending(state,renderer) || !capture_image(state, capture_path)))
        return false;
    state->frame=(state->frame+1)%FRAMES_IN_FLIGHT;
    if (renderer->frame_log) fflush(stdout);
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_CAPTURE,phase_start,phase_end);
    profiler_cpu(profile,PROFILE_RENDER,render_start,phase_end);
    ++renderer->scene_submissions;
    return !state->validation_failed.load();
}

bool renderer_shutdown(Renderer* renderer)
{
    RendererState* state = renderer->state;
    if (!state)
        return true;
    bool reported=true;
    if (state->device)
    {
        vkDeviceWaitIdle(state->device);
        reported=report_pending(state,renderer);
        if (state->lighting_view) vkDestroyImageView(state->device,state->lighting_view,0);
        if (state->lighting_image) vkDestroyImage(state->device,state->lighting_image,0);
        if (state->lighting_memory) vkFreeMemory(state->device,state->lighting_memory,0);
        if (state->optical_depth_view) vkDestroyImageView(state->device,state->optical_depth_view,0);
        if (state->optical_depth_image) vkDestroyImage(state->device,state->optical_depth_image,0);
        if (state->optical_depth_memory) vkFreeMemory(state->device,state->optical_depth_memory,0);
        if (state->effects_pipeline) vkDestroyPipeline(state->device,state->effects_pipeline,0);
        if (state->backdrop_pipeline) vkDestroyPipeline(state->device,state->backdrop_pipeline,0);
        if (state->backdrop_sampler) vkDestroySampler(state->device,state->backdrop_sampler,0);
        for (uint32_t i=0;i<2;++i)
        {
            if (state->backdrop_views[i]) vkDestroyImageView(state->device,state->backdrop_views[i],0);
            if (state->backdrop_images[i]) vkDestroyImage(state->device,state->backdrop_images[i],0);
            if (state->backdrop_memory[i]) vkFreeMemory(state->device,state->backdrop_memory[i],0);
        }
        if (state->no_sss_pipeline) vkDestroyPipeline(state->device,state->no_sss_pipeline,0);
        if (state->no_sss_guide_pipeline) vkDestroyPipeline(state->device,state->no_sss_guide_pipeline,0);
        if (state->no_sss_debug_pipeline) vkDestroyPipeline(state->device,state->no_sss_debug_pipeline,0);
        if (state->guide_surface_pipeline) vkDestroyPipeline(state->device,state->guide_surface_pipeline,0);
        if (state->focus_pipeline) vkDestroyPipeline(state->device,state->focus_pipeline,0);
        if (state->medium_blur_pipeline) vkDestroyPipeline(state->device,state->medium_blur_pipeline,0);
        if (state->fog_pipeline) vkDestroyPipeline(state->device,state->fog_pipeline,0);
        if (state->bloom_pipeline) vkDestroyPipeline(state->device, state->bloom_pipeline, 0);
        if (state->bloom_layout) vkDestroyPipelineLayout(state->device, state->bloom_layout, 0);
        if (state->bloom_descriptor_pool) vkDestroyDescriptorPool(state->device, state->bloom_descriptor_pool, 0);
        if (state->bloom_descriptor_layout) vkDestroyDescriptorSetLayout(state->device, state->bloom_descriptor_layout, 0);
        VkImage hdr_images[] = {state->hdr, state->hdr_scratch};
        VkImageView hdr_views[] = {state->hdr_view, state->hdr_scratch_view};
        VkDeviceMemory hdr_memory[] = {state->hdr_memory, state->hdr_scratch_memory};
        for (uint32_t i = 0; i < 2; ++i)
        {
            if (hdr_views[i]) vkDestroyImageView(state->device, hdr_views[i], 0);
            if (hdr_images[i]) vkDestroyImage(state->device, hdr_images[i], 0);
            if (hdr_memory[i]) vkFreeMemory(state->device, hdr_memory[i], 0);
        }
        if (state->pipeline) vkDestroyPipeline(state->device, state->pipeline, 0);
        if (state->shadow_grid_pipeline) vkDestroyPipeline(state->device,state->shadow_grid_pipeline,0);
        if (state->cascade_pipeline) vkDestroyPipeline(state->device,state->cascade_pipeline,0);
        if (state->volume_coarse_pipeline) vkDestroyPipeline(state->device,state->volume_coarse_pipeline,0);
        if (state->volume_filter_pipeline) vkDestroyPipeline(state->device,state->volume_filter_pipeline,0);
        if (state->volume_pipeline) vkDestroyPipeline(state->device,state->volume_pipeline,0);
        if (state->debug_surface_pipeline) vkDestroyPipeline(state->device,state->debug_surface_pipeline,0);
        if (state->debug_volume_pipeline) vkDestroyPipeline(state->device,state->debug_volume_pipeline,0);
        if (state->debug_overlay_pipeline) vkDestroyPipeline(state->device,state->debug_overlay_pipeline,0);
        if (state->debug_overlay_layout) vkDestroyPipelineLayout(state->device,state->debug_overlay_layout,0);
        if (state->pipeline_layout) vkDestroyPipelineLayout(state->device, state->pipeline_layout, 0);
        if (state->descriptor_pool) vkDestroyDescriptorPool(state->device, state->descriptor_pool, 0);
        if (state->descriptor_layout) vkDestroyDescriptorSetLayout(state->device, state->descriptor_layout, 0);
        for (uint32_t i=0;i<state->mesh_count;++i)
        {
            MeshAllocation* mesh=&state->meshes[i];
            if (mesh->acceleration) state->destroy_as(state->device,mesh->acceleration,0);
            destroy_buffer(state,&mesh->storage);
            destroy_buffer(state,&mesh->scratch);
        }
        if (state->tlas) state->destroy_as(state->device, state->tlas, 0);
        GpuBuffer* buffers[] = { &state->vertices, &state->triangles, &state->indices, &state->mesh_ranges,
            &state->tlas_buffer, &state->tlas_scratch,
            &state->readback, &state->optical_depth_upload, &state->shadow_grid, &state->static_lighting, &state->surface_lighting, &state->dynamic_lighting, &state->sharp_layer, &state->energy,
            &state->debug_pixels, &state->guides, &state->radiance_cascades };
        for (uint32_t i = 0; i < sizeof(buffers)/sizeof(buffers[0]); ++i) destroy_buffer(state, buffers[i]);
        for (uint32_t f=0;f<FRAMES_IN_FLIGHT;++f)
        {
            FrameResources& frame=state->frames[f];
            GpuBuffer* owned[]={&frame.uniform,&frame.instances,&frame.media,&frame.medium_ray_nodes,&frame.lighting_instances,&frame.lighting_bounds,&frame.sight,
                &frame.instance_styles,&frame.effects,&frame.effect_tiles,&frame.debug_text,&frame.vertex_upload};
            for (uint32_t i=0;i<sizeof(owned)/sizeof(owned[0]);++i) destroy_buffer(state,owned[i]);
            if (frame.image_available) vkDestroySemaphore(state->device,frame.image_available,0);
            if (frame.queries) vkDestroyQueryPool(state->device,frame.queries,0);
            if (frame.fence) vkDestroyFence(state->device,frame.fence,0);
            if (frame.command_pool) vkDestroyCommandPool(state->device,frame.command_pool,0);
        }
        if (state->output_view) vkDestroyImageView(state->device, state->output_view, 0);
        if (state->output) vkDestroyImage(state->device, state->output, 0);
        if (state->output_memory) vkFreeMemory(state->device, state->output_memory, 0);
        if (state->swapchain) vkDestroySwapchainKHR(state->device, state->swapchain, 0);
        for (uint32_t i=0;i<state->render_finished.size();++i) vkDestroySemaphore(state->device,state->render_finished[i],0);
        vkDestroyDevice(state->device, 0);
    }
    if (state->surface) vkDestroySurfaceKHR(state->instance, state->surface, 0);
    if (state->debug_messenger)
    {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(state->instance, "vkDestroyDebugUtilsMessengerEXT");
        destroy_debug(state->instance, state->debug_messenger, 0);
    }
    if (state->instance) vkDestroyInstance(state->instance, 0);
    window_close(&state->window);
    bool valid = reported && !state->validation_failed.load();
    delete state;
    renderer->state = 0;
    renderer->running = false;
    return valid;
}
