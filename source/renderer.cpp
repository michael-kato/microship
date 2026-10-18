#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <vulkan/vulkan.h>

#include "renderer.h"
#include "render_config.h"
#include "geometry.h"
#include "microfacet.h"
#include "window.h"
#include "render_scene.h"
#include "vector_math.h"
#include "math_constants.h"

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
static const int GPU_MEDIUM_BYTES = 80;
static const int GPU_RENDER_UNIFORM_BYTES = 528;
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
static const int RENDER_STORAGE_BUFFER_BINDINGS = 15;
static const int RENDER_DESCRIPTOR_SET_COUNT = 3;
static const int RENDER_STORAGE_IMAGE_BINDINGS = 3;
static const int RENDER_SAMPLER_BINDINGS = 2;
static const int POOL_STORAGE_BUFFER_DESCRIPTORS = RENDER_DESCRIPTOR_SET_COUNT*RENDER_STORAGE_BUFFER_BINDINGS;
static const int POOL_BACKGROUND_SAMPLER_DESCRIPTORS = RENDER_DESCRIPTOR_SET_COUNT*RENDER_SAMPLER_BINDINGS;
static const int DESCRIPTOR_POOL_TYPE_COUNT = 5;
static const int BACKDROP_PUSH_BYTES = 16;
static const int GPU_TRIANGLE_BYTES = 16;
static const int HUD_MAX_LINE_CHARACTERS = HUD_LINE_CAPACITY-1;
static const unsigned int STATS_TEXT_WIDTH_UNSIGNED = STATS_TEXT_WIDTH;
static const unsigned int STATS_TEXT_HEIGHT_UNSIGNED = STATS_TEXT_HEIGHT;
static const int GPU_TIMESTAMP_COUNT = 16;
static const int TIMESTAMP_WORD_BITS = 64;

enum { SCENE_BUFFER_VERTICES=0, SCENE_BUFFER_TRIANGLES=1, SCENE_BUFFER_UNIFORM=2, SCENE_BUFFER_MEDIA=3, SCENE_BUFFER_ENERGY=4, SCENE_BUFFER_RESERVED=5, SCENE_BUFFER_DEBUG=6 };

enum { GPU_TIME_FRAME_BEGIN=0, GPU_TIME_SURFACE_BEGIN=1, GPU_TIME_SURFACE_END=2, GPU_TIME_VOLUME_END=3, GPU_TIME_EFFECTS_BEGIN=4, GPU_TIME_BLOOM_END=5, GPU_TIME_OVERLAY_END=6, GPU_TIME_BACKDROP_END=7, GPU_TIME_UPLOAD_END=8, GPU_TIME_EFFECTS_END=9, GPU_TIME_GRID_END=10, GPU_TIME_MEDIUM_BLUR_END=11, GPU_TIME_FOG_END=12, GPU_TIME_FOCUS_END=13, GPU_TIME_COPY_END=14, GPU_TIME_BAKE_END=15 };

struct GpuBuffer
{
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkDeviceAddress address;
    void* mapped;
};

struct LightingInstance
{
    float transform[3][4];
    uint32_t first,count,cache_first,active;
};
static_assert(sizeof(LightingInstance)==64,"Lighting instance shader layout");

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
    uint32_t timestamp_bits;
    VkDeviceSize scratch_alignment;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkPresentModeKHR present_mode;
    std::vector<VkImage> swap_images;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkFence fence;
    VkQueryPool queries;
    GpuBuffer vertices;
    GpuBuffer triangles;
    GpuBuffer indices;
    GpuBuffer instances;
    GpuBuffer tlas_buffer;
    GpuBuffer tlas_scratch;
    GpuBuffer media,shadow_grid,sharp_layer,static_lighting,lighting_instances,surface_lighting,lighting_bounds,dynamic_lighting;
    std::vector<LightingInstance> lighting_input;
    uint32_t surface_cache_triangles;
    bool surface_lighting_valid;
    std::vector<VkAccelerationStructureInstanceKHR> baked_instances;
    std::vector<uint64_t> baked_mesh_revisions;
    std::vector<RenderMedium> baked_media;
    std::vector<RenderGridBounds> baked_bounds;
    std::vector<uint8_t> lighting_valid;
    RenderUniform baked_uniform;
    bool lighting_dependencies_valid;
    uint32_t lighting_bakes,lighting_reuses,lighting_surface_bakes;
    std::vector<uint32_t> surface_cache_meshes;
    uint32_t medium_count;
    GpuBuffer energy;
    GpuBuffer debug_pixels;
    GpuBuffer debug_text;
    GpuBuffer effects, effect_tiles, instance_styles,sight;
    std::vector<float> style_data;
    uint32_t effect_count, effect_tile_count;
    GpuBuffer uniform;
    RenderUniform uniform_data;
    uint32_t text_mask[(STATS_TEXT_WIDTH*STATS_TEXT_HEIGHT+HUD_TEXT_WIDTH*HUD_TEXT_HEIGHT)/TEXT_MASK_WORD_BITS];
    std::vector<VkAccelerationStructureInstanceKHR> instance_data;
    GpuBuffer readback;
    VkAccelerationStructureKHR tlas;
    std::vector<MeshAllocation> meshes;
    uint32_t mesh_count, instance_count;
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
    VkPipeline volume_pipeline,shadow_grid_pipeline;
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
    LARGE_INTEGER ticks,frequency;QueryPerformanceCounter(&ticks);QueryPerformanceFrequency(&frequency);
    return MILLISECONDS_PER_SECOND*(double)ticks.QuadPart/(double)frequency.QuadPart;
}

static bool vk_ok(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS)
        return true;
    fprintf(stderr, "%s failed (Vulkan result %d).\n", operation, result);
    return false;
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
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };
    VkApplicationInfo application = {};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "nanova";
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
    printf("Window startup: open Win32 window.\n");
    if (!window_open(&state->window, state->config.width, state->config.height, !state->no_activate))
        return false;
    VkWin32SurfaceCreateInfoKHR surface = {};
    surface.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface.hinstance = state->window.instance;
    surface.hwnd = state->window.handle;
    printf("Window startup: create Vulkan surface.\n");
    return vk_ok(vkCreateWin32SurfaceKHR(state->instance, &surface, 0, &state->surface), "Create window surface");
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
    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.pNext = &acceleration;
    const char* extensions[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo create = {};
    create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create.pNext = &features12;
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue;
    create.enabledExtensionCount = state->headless ? 3 : 4;
    create.ppEnabledExtensionNames = extensions;
    if (!vk_ok(vkCreateDevice(state->physical, &create, 0, &state->device), "Create Vulkan device"))
        return false;
    vkGetDeviceQueue(state->device, state->queue_family, 0, &state->queue);
    state->create_as = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(state->device, "vkCreateAccelerationStructureKHR");
    state->destroy_as = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(state->device, "vkDestroyAccelerationStructureKHR");
    state->get_as_sizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(state->device, "vkGetAccelerationStructureBuildSizesKHR");
    state->build_as = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(state->device, "vkCmdBuildAccelerationStructuresKHR");
    state->get_as_address = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(state->device, "vkGetAccelerationStructureDeviceAddressKHR");
    return state->create_as && state->destroy_as && state->get_as_sizes && state->build_as && state->get_as_address;
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
    if (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
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
    VkCommandPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = state->queue_family;
    if (!vk_ok(vkCreateCommandPool(state->device, &pool, 0, &state->command_pool), "Create command pool"))
        return false;
    VkCommandBufferAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = state->command_pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    if (!vk_ok(vkAllocateCommandBuffers(state->device, &allocate, &state->command), "Allocate command buffer"))
        return false;
    VkFenceCreateInfo fence = {};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkQueryPoolCreateInfo query = {};
    query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query.queryCount = GPU_TIMESTAMP_COUNT;
    return vk_ok(vkCreateFence(state->device, &fence, 0, &state->fence), "Create frame fence")
        && vk_ok(vkCreateQueryPool(state->device, &query, 0, &state->queries), "Create timestamp queries");
}

static bool begin_commands(RendererState* state)
{
    if (!vk_ok(vkWaitForFences(state->device, 1, &state->fence, VK_TRUE, UINT64_MAX), "Wait for GPU")
        || !vk_ok(vkResetCommandPool(state->device, state->command_pool, 0), "Reset commands"))
        return false;
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vk_ok(vkBeginCommandBuffer(state->command, &begin), "Begin commands");
}

static bool submit_commands(RendererState* state, bool presentation)
{
    if (!vk_ok(vkEndCommandBuffer(state->command), "End commands")
        || !vk_ok(vkResetFences(state->device, 1, &state->fence), "Reset frame fence"))
        return false;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &state->command;
    submit.waitSemaphoreCount = presentation ? 1 : 0;
    submit.pWaitSemaphores = &state->image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.signalSemaphoreCount = presentation ? 1 : 0;
    submit.pSignalSemaphores = &state->render_finished;
    return vk_ok(vkQueueSubmit(state->queue, 1, &submit, state->fence), "Submit commands")
        && vk_ok(vkWaitForFences(state->device, 1, &state->fence, VK_TRUE, UINT64_MAX), "Wait for frame");
}

static bool build_acceleration(RendererState* state, VkAccelerationStructureTypeKHR type,
    VkAccelerationStructureGeometryKHR* geometry, uint32_t primitive_count,
    GpuBuffer* storage, VkAccelerationStructureKHR* result,GpuBuffer* update_scratch=0)
{
    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = type;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR || update_scratch)
        build.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    state->get_as_sizes(state->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &primitive_count, &sizes);
    if (!create_buffer(state, storage, sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        return false;
    VkAccelerationStructureCreateInfoKHR create = {};
    create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create.type = type;
    create.buffer = storage->handle;
    create.size = sizes.accelerationStructureSize;
    if (!vk_ok(state->create_as(state->device, &create, 0, result), "Create acceleration structure"))
        return false;
    GpuBuffer local_scratch = {};
    GpuBuffer* scratch = type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? &state->tlas_scratch
        : update_scratch ? update_scratch : &local_scratch;
    VkDeviceSize scratch_size = sizes.buildScratchSize > sizes.updateScratchSize ? sizes.buildScratchSize : sizes.updateScratchSize;
    if (!create_buffer(state, scratch, scratch_size + state->scratch_alignment,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
    {
        destroy_buffer(state, &local_scratch);
        return false;
    }
    build.dstAccelerationStructure = *result;
    build.scratchData.deviceAddress = (scratch->address + state->scratch_alignment - 1)
        & ~(state->scratch_alignment - 1);
    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount = primitive_count;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    bool success = begin_commands(state);
    if (success)
    {
        VkMemoryBarrier before = {};
        before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        before.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_HOST_WRITE_BIT;
        before.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
            | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &before, 0, 0, 0, 0);
        state->build_as(state->command, 1, &build, &ranges);
        VkMemoryBarrier after = {};
        after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &after, 0, 0, 0, 0);
        success = submit_commands(state, false);
    }
    destroy_buffer(state, &local_scratch);
    return success;
}

static void store_vector(float* destination, Vec3 value)
{
    destination[0] = value.x; destination[1] = value.y; destination[2] = value.z;
}

static void scene_uniform(RenderUniform* uniform,const RenderScene* scene)
{
    uniform->background[0]=scene->ambient.r;uniform->background[1]=scene->ambient.g;uniform->background[2]=scene->ambient.b;
    uniform->ambient_top[0]=scene->ambient_top.r;uniform->ambient_top[1]=scene->ambient_top.g;uniform->ambient_top[2]=scene->ambient_top.b;
    uniform->ambient_bottom[0]=scene->ambient_bottom.r;uniform->ambient_bottom[1]=scene->ambient_bottom.g;uniform->ambient_bottom[2]=scene->ambient_bottom.b;
    uniform->background_quality[0]=scene->background_samples;uniform->background_quality[1]=scene->background_triangles;
    uniform->backdrop_style[0]=scene->background_blur;uniform->backdrop_style[1]=scene->background_glow;uniform->backdrop_style[2]=scene->background_ambient;
    uniform->membrane_fade[0]=scene->thin_edge_strength;uniform->membrane_fade[1]=scene->thin_edge_power;uniform->membrane_fade[2]=scene->thin_blur_radius;
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

// Only scene inputs used by static light transport invalidate the bake.
static void update_lighting_dependencies(RendererState* state,const RenderScene* scene)
{
    if (!state->uniform_data.scene[3]) return;
    bool changed=!state->lighting_dependencies_valid || state->config.force_lighting_rebuild;
    const RenderUniform& u=state->uniform_data;RenderUniform& old=state->baked_uniform;
    changed=changed || memcmp(u.studio_direction,old.studio_direction,sizeof(u.studio_direction))
        || memcmp(u.thin_material,old.thin_material,sizeof(u.thin_material))
        || memcmp(u.membrane_fade,old.membrane_fade,2*sizeof(float));
    for (uint32_t i=0;i<state->instance_count;++i)
    {
        VkAccelerationStructureInstanceKHR current={};
        if (scene->instances[i].static_lighting && (state->instance_data[i].mask&1u)) current=state->instance_data[i];
        changed=changed || memcmp(&current,&state->baked_instances[i],sizeof(current));
        state->baked_instances[i]=current;
    }
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        changed=changed || state->baked_mesh_revisions[i]!=scene->geometry.meshes[i].revision;
        state->baked_mesh_revisions[i]=scene->geometry.meshes[i].revision;
    }
    for (uint32_t i=0;i<state->medium_count;++i)
    {
        // Extinction uses center/radius, density fade, scattering and absorption.
        changed=changed || memcmp(&state->baked_media[i],&scene->media[i],3*4*sizeof(float))
            || memcmp(&state->baked_bounds[i],&scene->lighting_bounds[i],sizeof(RenderGridBounds));
        state->baked_media[i]=scene->media[i];state->baked_bounds[i]=scene->lighting_bounds[i];
    }
    if (changed)
    {
        for (uint32_t i=0;i<state->medium_count;++i) state->lighting_valid[i]=0;
        state->surface_lighting_valid=false;
    }
    old=u;state->lighting_dependencies_valid=true;
}

// Frustum culling changes work only; newly visible grids are baked before use.
static void update_grid_activity(RendererState* state)
{
    RenderMedium* media=(RenderMedium*)state->media.mapped;const RenderUniform& u=state->uniform_data;
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
        m.lighting[2]=state->config.shadow_grid_resolution && m.lighting[0]>0 && m.lighting[1]>0 && r>0 && visible ? 1.0f : 0.0f;
    }
}

static void scene_instances(RendererState* state,const RenderScene* scene)
{
    for (uint32_t i=0;i<state->instance_count;++i)
    {
        const RenderInstance* source=&scene->instances[i];VkAccelerationStructureInstanceKHR* out=&state->instance_data[i];
        memcpy(out->transform.matrix,source->transform,sizeof(source->transform));
        out->mask=source->mask;
        out->flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        const MeshAllocation* mesh=&state->meshes[source->mesh];
        out->instanceCustomIndex=mesh->range.first;out->accelerationStructureReference=mesh->address;
        memcpy(&state->style_data[i*INSTANCE_STYLE_FLOATS],source->style,INSTANCE_STYLE_FLOATS*sizeof(float));
        LightingInstance& light=state->lighting_input[i];
        memcpy(light.transform,source->transform,sizeof(light.transform));
        light.first=mesh->range.first;light.count=mesh->range.count;
        light.active=source->static_lighting && (source->mask&1u) ? 1u : 0u;
    }
}

static bool upload_scene(RendererState* state,const RenderScene* scene)
{
    const Geometry& geometry=scene->geometry;
    state->triangle_count = geometry.triangles.size();
    VkBufferUsageFlags input = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!create_buffer(state, &state->vertices, geometry.vertices.size()*sizeof(GpuVertex),
            input | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host)
        || !create_buffer(state, &state->triangles, geometry.triangles.size()*sizeof(GpuTriangle),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host)
        || !create_buffer(state, &state->indices, geometry.triangles.size()*3*sizeof(uint32_t), input, host))
        return false;
    memcpy(state->vertices.mapped, geometry.vertices.data(), state->vertices.size);
    memcpy(state->triangles.mapped, geometry.triangles.data(), state->triangles.size);
    uint32_t* indices = (uint32_t*)state->indices.mapped;
    for (uint32_t i = 0; i < state->triangle_count; ++i)
        for (uint32_t j = 0; j < 3; ++j)
            indices[i*3+j] = geometry.triangles[i].index[j];
    VkAccelerationStructureGeometryKHR mesh = {};
    mesh.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    mesh.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    mesh.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    mesh.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    mesh.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    mesh.geometry.triangles.vertexData.deviceAddress = state->vertices.address;
    mesh.geometry.triangles.vertexStride = sizeof(GpuVertex);
    mesh.geometry.triangles.maxVertex = geometry.vertices.size() - 1;
    mesh.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    mesh.geometry.triangles.indexData.deviceAddress = state->indices.address;
    state->mesh_count=(uint32_t)geometry.meshes.size();state->instance_count=(uint32_t)scene->instances.size();
    try
    {
        state->meshes.resize(state->mesh_count);state->instance_data.resize(state->instance_count);state->style_data.resize(state->instance_count*INSTANCE_STYLE_FLOATS);
        state->baked_instances.resize(state->instance_count);state->baked_mesh_revisions.resize(state->mesh_count);
        state->baked_media.resize(scene->media.size());state->baked_bounds.resize(scene->media.size());state->lighting_input.resize(state->instance_count);
        state->surface_cache_meshes.resize(state->instance_count);
        state->lighting_valid.resize(scene->media.size());
    }
    catch (const std::bad_alloc&) { fprintf(stderr,"Cannot allocate render upload storage.\\n");return false; }
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        MeshAllocation* allocation=&state->meshes[i];allocation->range=geometry.meshes[i];
        mesh.geometry.triangles.indexData.deviceAddress=state->indices.address+allocation->range.first*3*sizeof(uint32_t);
        if (!build_acceleration(state,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,&mesh,allocation->range.count,
            &allocation->storage,&allocation->acceleration,allocation->range.deformable ? &allocation->scratch : 0)) return false;
        VkAccelerationStructureDeviceAddressInfoKHR address={};address.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        address.accelerationStructure=allocation->acceleration;allocation->address=state->get_as_address(state->device,&address);
    }
    for (uint32_t i=0;i<state->instance_count;++i) if (scene->instances[i].static_lighting)
    {
        state->surface_cache_meshes[i]=scene->instances[i].mesh+1;
        state->lighting_input[i].cache_first=state->surface_cache_triangles;
        state->surface_cache_triangles+=state->meshes[scene->instances[i].mesh].range.count;
    }
    scene_instances(state,scene);
    if (!create_buffer(state,&state->instances,state->instance_count*sizeof(state->instance_data[0]),input,host)) return false;
    memcpy(state->instances.mapped,state->instance_data.data(),state->instances.size);
    VkAccelerationStructureGeometryKHR instances = {};
    instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    instances.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instances.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instances.geometry.instances.data.deviceAddress = state->instances.address;
    if (!build_acceleration(state, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        &instances, state->instance_count, &state->tlas_buffer, &state->tlas))
        return false;
    if (!create_buffer(state, &state->uniform, sizeof(RenderUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host))
        return false;
    RenderUniform uniform={};
    uniform.settings[0]=state->config.width;uniform.settings[1]=state->config.height;
    uniform.settings[2]=state->config.samples_per_pixel;uniform.settings[3]=state->config.max_transmission_layers;uniform.scene[0]=state->config.seed;
    uniform.display[0]=state->config.exposure;uniform.display[1]=std::tan(state->config.vertical_fov_degrees*HALF_DEGREES_TO_RADIANS);
    uniform.display[2]=(float)state->config.width/state->config.height;uniform.display[3]=(float)state->config.volume_steps;
    uniform.quality[0]=state->config.volume_rays_per_pixel;
    uniform.quality[1]=state->config.shadow_grid_resolution;
    uniform.scene[3]=state->config.static_lighting_cache && state->config.shadow_grid_resolution && !state->custom_shader ? 1 : 0;
    uniform.quality[2]=state->config.microfacet_energy_compensation;
    uniform.focus_band[0]=state->config.focus_center;uniform.focus_band[1]=state->config.focus_half_width;
    uniform.focus_band[2]=state->config.focus_falloff;uniform.focus_band[3]=state->config.focus_radius;
    uniform.subsurface_quality[0]=state->config.subsurface_samples;
    uniform.subsurface[0]=state->config.subsurface_strength;uniform.subsurface[1]=state->config.subsurface_wrap;
    uniform.subsurface[2]=state->config.subsurface_transmission;uniform.subsurface[3]=state->config.subsurface_thickness;
    scene_uniform(&uniform,scene);
    if (!create_buffer(state, &state->energy, MICROFACET_TABLE_SIZE*MICROFACET_TABLE_SIZE*sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host)) return false;
    microfacet_energy_table((float*)state->energy.mapped);
    if (!create_buffer(state,&state->sight,sizeof(scene->sight)+sizeof(scene->fog_field.reveal),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)) return false;
    state->medium_count=(uint32_t)scene->media.size();
    if (!create_buffer(state,&state->media,(state->medium_count ? state->medium_count : 1)*sizeof(RenderMedium),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)) return false;
    if (state->medium_count) memcpy(state->media.mapped,scene->media.data(),state->medium_count*sizeof(RenderMedium));
    if (scene->fog_blur>0) memcpy(state->sight.mapped,scene->fog_field.reveal,sizeof(scene->fog_field.reveal));
    memcpy((char*)state->sight.mapped+sizeof(scene->fog_field.reveal),scene->sight,sizeof(scene->sight));
    state->effect_tile_count=scene->tile_count;state->effect_tile_size=scene->effect_tiles.size();
    state->uniform_data=uniform;memcpy(state->uniform.mapped,&uniform,sizeof(uniform));
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

static bool create_output(RendererState* state)
{
    // Five vec4 records per native pixel, only for interactive/buffer-view runs.
    VkDeviceSize debug_bytes = state->debug_available
        ? (VkDeviceSize)state->config.width*state->config.height*GPU_DEBUG_PIXEL_BYTES : GPU_DEBUG_PIXEL_BYTES;
    if (debug_bytes > state->properties.limits.maxStorageBufferRange)
    {
        fprintf(stderr,"Debug buffers exceed the device's storage-buffer range.\n");
        return false;
    }
    if (!create_buffer(state,&state->debug_pixels,debug_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    if (!create_buffer(state,&state->debug_text,FONT_UNIFORM_BYTES+(STATS_TEXT_WIDTH*STATS_TEXT_HEIGHT+HUD_TEXT_WIDTH*HUD_TEXT_HEIGHT)/TEXT_MASK_BYTE_BITS+sizeof(ProfileBars),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) return false;
    memcpy(state->debug_text.mapped,debug_glyphs,sizeof(debug_glyphs));
    if (!create_render_image(state, VK_FORMAT_R32G32B32A32_SFLOAT, &state->hdr, &state->hdr_memory, &state->hdr_view)
        || !create_render_image(state, VK_FORMAT_R32G32B32A32_SFLOAT, &state->hdr_scratch, &state->hdr_scratch_memory, &state->hdr_scratch_view)
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
    if (!create_buffer(state,&state->effects,MAX_EFFECT_SEGMENTS*sizeof(EffectSegment),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
        || !create_buffer(state,&state->effect_tiles,state->effect_tile_size*sizeof(uint32_t),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
        || !create_buffer(state,&state->instance_styles,state->style_data.size()*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)) return false;
    memcpy(state->instance_styles.mapped,state->style_data.data(),state->style_data.size()*sizeof(float));
    VkDeviceSize n=state->config.shadow_grid_resolution;
    VkDeviceSize grid_bytes=n ? n*n*n*3*(state->medium_count ? state->medium_count : 1)*4*sizeof(float) : 4*sizeof(float);
    if (grid_bytes>state->properties.limits.maxStorageBufferRange)
    { fprintf(stderr,"Shadow grid exceeds the storage-buffer range.\n");return false; }
    if (!create_buffer(state,&state->shadow_grid,grid_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    VkDeviceSize static_bytes=state->uniform_data.scene[3] ? grid_bytes : 4*sizeof(float);
    VkDeviceSize surface_bytes=state->uniform_data.scene[3] ? (VkDeviceSize)state->surface_cache_triangles*SURFACE_LIGHTING_RECORDS_PER_TRIANGLE*4*sizeof(float) : 4*sizeof(float);
    if (!surface_bytes) surface_bytes=4*sizeof(float);
    if (surface_bytes>state->properties.limits.maxStorageBufferRange)
    { fprintf(stderr,"Surface lighting exceeds the storage-buffer range.\n");return false; }
    if (!create_buffer(state,&state->static_lighting,static_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state,&state->dynamic_lighting,static_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state,&state->lighting_bounds,(state->medium_count ? state->medium_count : 1)*sizeof(RenderGridBounds),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
        || !create_buffer(state,&state->surface_lighting,surface_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !create_buffer(state,&state->lighting_instances,state->lighting_input.size()*sizeof(LightingInstance),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)) return false;
    VkDeviceSize sharp_bytes=state->uniform_data.quality[3] ? (VkDeviceSize)state->config.width*state->config.height*4*sizeof(float) : 4*sizeof(float);
    if (sharp_bytes>state->properties.limits.maxStorageBufferRange)
    { fprintf(stderr,"Medium sharp layer exceeds the storage-buffer range.\n");return false; }
    if (!create_buffer(state,&state->sharp_layer,sharp_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    state->capture_row.resize(state->config.width*3);
    state->capture_hdr_row.resize(state->config.width*3);
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
    return vk_ok(vkCreateSemaphore(state->device, &semaphore, 0, &state->image_available), "Create acquire semaphore")
        && vk_ok(vkCreateSemaphore(state->device, &semaphore, 0, &state->render_finished), "Create present semaphore");
}

static bool load_compute_pipeline(RendererState* state, const char* shader_path,
    VkPipelineLayout pipeline_layout, VkPipeline* output_pipeline)
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
    uint32_t constants[]={state->config.volume_steps,state->uniform_data.scene[3]};
    VkSpecializationMapEntry entries[]={
        {VOLUME_STEPS_SPECIALIZATION_ID,0,sizeof(uint32_t)},
        {LIGHTING_CACHE_SPECIALIZATION_ID,sizeof(uint32_t),sizeof(uint32_t)}};
    VkSpecializationInfo specialization={2,entries,sizeof(constants),constants};
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
        fprintf(stderr,"Renderer requires fifteen compute storage-buffer descriptors.\n");
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
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
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
    VkDescriptorPoolSize pool_sizes[] = { { types[RENDER_BINDING_ACCELERATION], RENDER_DESCRIPTOR_SET_COUNT }, { types[RENDER_BINDING_BEAUTY], RENDER_DESCRIPTOR_SET_COUNT*RENDER_STORAGE_IMAGE_BINDINGS }, { types[RENDER_BINDING_VERTICES], POOL_STORAGE_BUFFER_DESCRIPTORS }, { types[RENDER_BINDING_UNIFORM], RENDER_DESCRIPTOR_SET_COUNT }, {types[RENDER_BINDING_SOURCE_A],POOL_BACKGROUND_SAMPLER_DESCRIPTORS} };
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = RENDER_DESCRIPTOR_SET_COUNT;
    pool.poolSizeCount = DESCRIPTOR_POOL_TYPE_COUNT;
    pool.pPoolSizes = pool_sizes;
    if (!vk_ok(vkCreateDescriptorPool(state->device, &pool, 0, &state->descriptor_pool), "Create descriptor pool"))
        return false;
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = state->descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &state->descriptor_layout;
    if (!vk_ok(vkAllocateDescriptorSets(state->device, &allocate, &state->descriptors), "Allocate descriptors"))
        return false;
    if (!vk_ok(vkAllocateDescriptorSets(state->device, &allocate, &state->debug_descriptors), "Allocate debug descriptors"))
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
        { state->uniform.handle, 0, state->uniform.size },
        { state->media.handle, 0, state->media.size },
        { state->energy.handle, 0, state->energy.size },
        { state->shadow_grid.handle, 0, state->shadow_grid.size }, // Current-frame visibility only.
        { state->debug_pixels.handle, 0, state->debug_pixels.size }
    };
    VkWriteDescriptorSet writes[RENDER_DESCRIPTOR_COUNT] = {};
    for (uint32_t i = 0; i < RENDER_DESCRIPTOR_COUNT; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = state->descriptors;
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
    VkDescriptorBufferInfo effect_buffers[]={{state->effects.handle,0,state->effects.size},
        {state->effect_tiles.handle,0,state->effect_tiles.size},{state->instance_styles.handle,0,state->instance_styles.size}};
    for (uint32_t i=0;i<3;++i) writes[RENDER_BINDING_EFFECTS+i].pBufferInfo=&effect_buffers[i];
    VkDescriptorBufferInfo sharp_buffer={state->sharp_layer.handle,0,state->sharp_layer.size};
    writes[RENDER_BINDING_SHARP_LAYER].pBufferInfo=&sharp_buffer;
    VkDescriptorBufferInfo lighting_buffer={state->static_lighting.handle,0,state->static_lighting.size};
    VkDescriptorBufferInfo instance_buffer={state->lighting_instances.handle,0,state->lighting_instances.size};
    VkDescriptorBufferInfo surface_buffer={state->surface_lighting.handle,0,state->surface_lighting.size};
    writes[RENDER_BINDING_STATIC_LIGHTING].pBufferInfo=&lighting_buffer;
    writes[RENDER_BINDING_LIGHTING_INSTANCES].pBufferInfo=&instance_buffer;
    writes[RENDER_BINDING_SURFACE_LIGHTING].pBufferInfo=&surface_buffer;
    VkDescriptorBufferInfo bounds_buffer={state->lighting_bounds.handle,0,state->lighting_bounds.size};
    VkDescriptorBufferInfo dynamic_buffer={state->dynamic_lighting.handle,0,state->dynamic_lighting.size};
    writes[RENDER_BINDING_LIGHTING_BOUNDS].pBufferInfo=&bounds_buffer;
    writes[RENDER_BINDING_DYNAMIC_LIGHTING].pBufferInfo=&dynamic_buffer;
    vkUpdateDescriptorSets(state->device, RENDER_DESCRIPTOR_COUNT, writes, 0, 0);
    if (!vk_ok(vkAllocateDescriptorSets(state->device,&allocate,&state->effect_descriptors),"Allocate effects descriptors")) return false;
    image.imageView=state->hdr_view;
    for (uint32_t i=0;i<RENDER_DESCRIPTOR_COUNT;++i) writes[i].dstSet=state->effect_descriptors;
    vkUpdateDescriptorSets(state->device,RENDER_DESCRIPTOR_COUNT,writes,0,0);
    // Same layout, with display RGBA8 instead of the tracer's linear-HDR image.
    image.imageView = state->output_view;
    VkDescriptorBufferInfo text_buffer = {state->debug_text.handle,0,state->debug_text.size};
    writes[RENDER_BINDING_UNIFORM].pBufferInfo = &text_buffer;
    for (uint32_t i = 0; i < RENDER_DESCRIPTOR_COUNT; ++i) writes[i].dstSet = state->debug_descriptors;
    vkUpdateDescriptorSets(state->device, RENDER_DESCRIPTOR_COUNT, writes, 0, 0);

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
    const char* filename, VkPipelineLayout layout, VkPipeline* pipeline)
{
    const char* slash = strrchr(ray_trace_path,'/');
    const char* backslash = strrchr(ray_trace_path,'\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    size_t directory_length = slash ? (size_t)(slash-ray_trace_path+1) : 0;
    char path[COMPANION_SHADER_PATH_CAPACITY];
    if (directory_length+strlen(filename)+1 > sizeof(path)) return false;
    memcpy(path,ray_trace_path,directory_length);
    strcpy(path+directory_length,filename);
    return load_compute_pipeline(state,path,layout,pipeline);
}

static bool create_bloom(RendererState* state, const char* ray_trace_path)
{
    VkDescriptorSetLayoutBinding bindings[BLOOM_DESCRIPTOR_COUNT] = {};
    for (uint32_t i = 0; i < BLOOM_DESCRIPTOR_COUNT; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = i==BLOOM_BINDING_UNIFORM ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER :
            (i==BLOOM_BINDING_DEBUG || i==BLOOM_BINDING_SIGHT || i==BLOOM_BINDING_SHARP_LAYER) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout = {};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = BLOOM_DESCRIPTOR_COUNT;
    layout.pBindings = bindings;
    if (!vk_ok(vkCreateDescriptorSetLayout(state->device, &layout, 0, &state->bloom_descriptor_layout), "Create bloom descriptor layout")) return false;
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 3;
    pool.pPoolSizes = sizes;
    if (!vk_ok(vkCreateDescriptorPool(state->device, &pool, 0, &state->bloom_descriptor_pool), "Create bloom descriptor pool")) return false;
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = state->bloom_descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &state->bloom_descriptor_layout;
    if (!vk_ok(vkAllocateDescriptorSets(state->device, &allocate, &state->bloom_descriptors), "Allocate bloom descriptors")) return false;
    VkDescriptorImageInfo images[3] = {};
    VkImageView views[] = {state->hdr_view,
        state->hdr_scratch_view,state->output_view};
    VkWriteDescriptorSet writes[BLOOM_DESCRIPTOR_COUNT] = {};
    for (uint32_t i = 0; i < 3; ++i)
    {
        images[i].imageView = views[i];
        images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = state->bloom_descriptors;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &images[i];
    }
    VkDescriptorBufferInfo debug_buffer = {state->debug_pixels.handle,0,state->debug_pixels.size};
    writes[BLOOM_BINDING_DEBUG].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[BLOOM_BINDING_DEBUG].dstSet = state->bloom_descriptors;
    writes[BLOOM_BINDING_DEBUG].dstBinding = BLOOM_BINDING_DEBUG;
    writes[BLOOM_BINDING_DEBUG].descriptorCount = 1;
    writes[BLOOM_BINDING_DEBUG].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[BLOOM_BINDING_DEBUG].pBufferInfo = &debug_buffer;
    VkDescriptorBufferInfo extra_buffers[]={{state->uniform.handle,0,state->uniform.size},{state->sight.handle,0,state->sight.size},{state->sharp_layer.handle,0,state->sharp_layer.size}};
    for (uint32_t i=0;i<3;++i)
    {
        uint32_t binding=BLOOM_BINDING_UNIFORM+i;
        writes[binding].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[binding].dstSet=state->bloom_descriptors;
        writes[binding].dstBinding=binding;writes[binding].descriptorCount=1;
        writes[binding].descriptorType=i ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[binding].pBufferInfo=&extra_buffers[i];
    }
    vkUpdateDescriptorSets(state->device, BLOOM_DESCRIPTOR_COUNT, writes, 0, 0);
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
    return load_companion(state,builtin_shader_path,"debug_surface.comp.spv",
        state->pipeline_layout,&state->debug_surface_pipeline)
        && load_companion(state,builtin_shader_path,"debug_volume.comp.spv",state->pipeline_layout,&state->debug_volume_pipeline)
        && load_companion(state,builtin_shader_path,"debug_guide_surface.comp.spv",state->pipeline_layout,&state->guide_surface_pipeline);
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
    uint32_t n=state->config.shadow_grid_resolution;
    const RenderMedium* media=(const RenderMedium*)state->media.mapped;
    if (n)
    {
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->shadow_grid_pipeline);
        if (state->uniform_data.scene[3])
        {
            if (!state->surface_lighting_valid)
            {
                for (uint32_t i=0;i<state->instance_count;++i) if (state->lighting_input[i].active)
                {
                    uint32_t parameters[4]={0,LIGHTING_PASS_SURFACE,i,state->lighting_input[i].count};
                    vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
                    vkCmdDispatch(state->command,(state->lighting_input[i].count+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1,1);
                    ++state->lighting_surface_bakes;
                }
                state->surface_lighting_valid=true;
            }
            for (uint32_t i=0;i<state->medium_count;++i) if (media[i].lighting[2]>0)
            {
                if (state->lighting_valid[i]) { ++state->lighting_reuses;continue; }
                uint32_t parameters[4]={i,LIGHTING_PASS_STATIC,0,0};
                vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
                vkCmdDispatch(state->command,(n+7)/8,(n*n+7)/8,1);
                state->lighting_valid[i]=1;++state->lighting_bakes;
            }
            shader_buffer_barrier(state->command);
        }
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_BAKE_END);
    if (n)
    {
        for (uint32_t i=0;i<state->medium_count;++i) if (media[i].lighting[2]>0)
        {
            if (state->uniform_data.scene[3])
            {
                uint32_t parameters[4]={i,LIGHTING_PASS_DYNAMIC,0,0};
                vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
                vkCmdDispatch(state->command,(n+7)/8,(n*n+7)/8,1);
            }
            uint32_t parameters[4]={i,LIGHTING_PASS_CURRENT,0,0};
            vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,BACKDROP_PUSH_BYTES,parameters);
            vkCmdDispatch(state->command,(n+7)/8,(n*n+7)/8,1);
        }
        shader_buffer_barrier(state->command);
    }
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
static void profile_overlay(RendererState* state,const Renderer* renderer,ProfileBars* bars)
{
    const Profiler* p=&renderer->profiler;char line[160];
    snprintf(line,sizeof(line),"F1 FRAME PROFILER / ROLLING MEAN %u OF %u FRAMES",p->count,PROFILE_HISTORY);
    profile_text(state,line,8,8,STATS_TEXT_WIDTH-8);
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
    profile_text(state,"GPU / EXECUTION TIME / CHILDREN BELOW PARENTS",8,156,STATS_TEXT_WIDTH-8);
    static const float palette[8][3]={{.24f,.47f,.68f},{.38f,.56f,.77f},{.33f,.62f,.52f},{.66f,.48f,.29f},{.56f,.42f,.66f},{.55f,.35f,.32f},{.31f,.57f,.61f},{.53f,.53f,.34f}};
    int hover=-1;
    for (uint32_t i=0;i<PROFILE_NODE_COUNT;++i)
    {
        const ProfileDefinition& definition=profile_definitions[i];const ProfileSample& sample=p->mean[i];
        if (sample.duration_ms<=0) continue;
        int32_t* r=bars->rectangles[i];
        r[0]=8+(int)(sample.start_ms/range*(STATS_TEXT_WIDTH-16));
        r[1]=(i<PROFILE_GPU_FRAME ? 84 : 176)+definition.depth*22;
        r[2]=std::min(STATS_TEXT_WIDTH-8,std::max(r[0]+1,8+(int)((sample.start_ms+sample.duration_ms)/range*(STATS_TEXT_WIDTH-16))));r[3]=r[1]+20;
        memcpy(bars->colors[i],palette[i%8],3*sizeof(float));bars->colors[i][3]=1;
        snprintf(line,sizeof(line),"%s %.2F",definition.name,sample.duration_ms);
        profile_text(state,line,r[0]+4,r[1]+3,r[2]-3);
        if (p->pointer_x>=r[0] && p->pointer_x<r[2] && p->pointer_y>=r[1] && p->pointer_y<r[3]) hover=(int)i;
    }
    if (hover>=0)
        snprintf(line,sizeof(line),"%s / LAST %.3F MS / MEAN %.3F MS",profile_definitions[hover].name,p->latest[hover].duration_ms,p->mean[hover].duration_ms);
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
        snprintf(line,sizeof(line),"TOP GPU: %s %.2F / %s %.2F / %s %.2F MS",profile_definitions[top[0]].name,p->mean[top[0]].duration_ms,
            profile_definitions[top[1]].name,p->mean[top[1]].duration_ms,profile_definitions[top[2]].name,p->mean[top[2]].duration_ms);
    }
    profile_text(state,line,8,250,STATS_TEXT_WIDTH-8);
    profile_text(state,"SURFACE INCLUDES LIGHTING + SSS / GPU WAIT IS CPU BLOCKING / TIMINGS INCLUDE BARRIERS",8,274,STATS_TEXT_WIDTH-8);
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
    parameters.settings[3] = renderer->overlay_enabled ? 1.0f : 0.0f;
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
                            for (uint32_t row=0;row<HUD_GLYPH_SCALE;++row)
                                for (uint32_t col=0;col<HUD_GLYPH_SCALE;++col)
                                {
                                    uint32_t bit=STATS_TEXT_WIDTH*STATS_TEXT_HEIGHT+(TEXT_MARGIN+line*HUD_LINE_SPACING+y*HUD_GLYPH_SCALE+row)*HUD_TEXT_WIDTH+TEXT_MARGIN+column*HUD_CHARACTER_SPACING+x*HUD_GLYPH_SCALE+col;
                                    mask[bit/TEXT_MASK_WORD_BITS] |= 1u << (bit%TEXT_MASK_WORD_BITS);
                                }
            }
    memcpy((uint32_t*)state->debug_text.mapped+FONT_GLYPH_COUNT,mask,sizeof(state->text_mask));
    memcpy((char*)state->debug_text.mapped+FONT_UNIFORM_BYTES+sizeof(state->text_mask),&bars,sizeof(bars));
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
    image_barrier(state->command, destination, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
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
            const float* source = (const float*)state->readback.mapped;
            uint32_t row = state->config.height-1-y; // PFM rows are bottom to top.
            for (uint32_t x = 0; x < state->config.width; ++x)
                memcpy(&state->capture_hdr_row[x*3],&source[((size_t)row*state->config.width+x)*4],3*sizeof(float));
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
    state->debug_available = !headless || debug_views || scene->guides_required || config->focus_radius>0;
    state->custom_shader = strcmp(shader_path,builtin_shader_path) != 0;
    if (state->custom_shader && scene->guides_required)
    {
        fprintf(stderr,"Shader overrides require no depth-guided overlays.\n");
        renderer_shutdown(renderer);
        return false;
    }
    bool success = create_instance(state) && create_device(state) && create_commands(state)
        && create_swapchain(state) && upload_scene(state, scene) && create_output(state) && create_pipeline(state, shader_path)
        && load_companion(state,shader_path,"volume.comp.spv",state->pipeline_layout,&state->volume_pipeline)
        && load_companion(state,shader_path,"shadow_grid.comp.spv",state->pipeline_layout,&state->shadow_grid_pipeline)
        && create_bloom(state, shader_path) && create_debug(state,builtin_shader_path)
        && load_companion(state,builtin_shader_path,"effects.comp.spv",state->pipeline_layout,&state->effects_pipeline);
    if (success && (scene->background_blur>0))
        success=load_companion(state,builtin_shader_path,"backdrop.comp.spv",state->pipeline_layout,&state->backdrop_pipeline);

    if (!success || state->validation_failed.load())
    {
        renderer_shutdown(renderer);
        return false;
    }
    renderer->running = true;
    printf("Renderer: direct ray tracing; %ux%u native; %u film samples; %u transmission layers; %u triangles.\n",
        config->width,config->height,config->samples_per_pixel,config->max_transmission_layers,state->triangle_count);
    printf("Sampling: fixed mirrored film points; no stochastic surface transport, denoising, or temporal history.\n");
    printf("Subsurface: %.2f strength; %u existing film samples; one strongest back light; current geometry.\n",config->subsurface_strength,config->subsurface_samples);
    printf("Volumes: %u film rays; %u midpoint integration steps; analytic ambient fill; ray-traced studio shadows.\n",
        config->volume_rays_per_pixel,config->volume_steps);
    printf("Current-frame guides: %llu bytes; no history or upscaling.\n",(unsigned long long)state->debug_pixels.size);
    return true;
}

bool renderer_poll(Renderer* renderer, PointerInput* input)
{
    *input = PointerInput();
    renderer->running = renderer->state->headless || window_poll(&renderer->state->window, input);
    return renderer->running;
}

// The CPU scene changes vertex positions and normals, with fixed mesh topology.
// Each mutable BLAS owns persistent scratch. Completed frame fences protect uploads.
static bool update_meshes(RendererState* state,const Geometry& scene)
{
    if (scene.vertices.size()*sizeof(GpuVertex)!=state->vertices.size
        || scene.triangles.size()!=state->triangle_count)
    { fprintf(stderr,"Render mesh storage changed after initialization.\n");return false; }
    bool changed=false;
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        MeshAllocation& mesh=state->meshes[i];const MeshRange& source=scene.meshes[i];
        if (source.first!=mesh.range.first || source.count!=mesh.range.count || source.deformable!=mesh.range.deformable
            || source.first_vertex!=mesh.range.first_vertex || source.vertex_count!=mesh.range.vertex_count)
        { fprintf(stderr,"Render mesh topology changed after initialization.\n");return false; }
        if (source.revision==mesh.range.revision) continue;
        if (!source.deformable) { fprintf(stderr,"Static render mesh changed after initialization.\n");return false; }
        memcpy((GpuVertex*)state->vertices.mapped+source.first_vertex,
            scene.vertices.data()+source.first_vertex,source.vertex_count*sizeof(GpuVertex));
        changed=true;
    }
    if (!changed) return true;
    VkMemoryBarrier before={};before.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
    before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0,1,&before,0,0,0,0);
    uint32_t updates=0;
    for (uint32_t i=0;i<state->mesh_count;++i)
    {
        MeshAllocation& mesh=state->meshes[i];const MeshRange& source=scene.meshes[i];
        if (source.revision==mesh.range.revision) continue;
        VkAccelerationStructureGeometryKHR geometry={};geometry.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress=state->vertices.address;
        geometry.geometry.triangles.vertexStride=sizeof(GpuVertex);geometry.geometry.triangles.maxVertex=(uint32_t)scene.vertices.size()-1;
        geometry.geometry.triangles.indexType=VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress=state->indices.address+source.first*3*sizeof(uint32_t);
        VkAccelerationStructureBuildGeometryInfoKHR build={};build.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;build.srcAccelerationStructure=build.dstAccelerationStructure=mesh.acceleration;
        build.geometryCount=1;build.pGeometries=&geometry;
        build.scratchData.deviceAddress=(mesh.scratch.address+state->scratch_alignment-1)&~(state->scratch_alignment-1);
        VkAccelerationStructureBuildRangeInfoKHR range={};range.primitiveCount=source.count;
        const VkAccelerationStructureBuildRangeInfoKHR* ranges=&range;state->build_as(state->command,1,&build,&ranges);
        mesh.range=source;++updates;
    }
    // The following TLAS build barrier makes these BLAS writes visible.
    printf("Deformed mesh updates: %u BLAS refits.\n",updates);
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
    if (!state->headless)
    {
        if (!renderer->scene_submissions) printf("Window frame: acquire.\n");
        double acquire_start=renderer_ms();
        VkResult result = vkAcquireNextImageKHR(state->device, state->swapchain, UINT64_MAX,
            state->image_available, VK_NULL_HANDLE, &image_index);
        acquire_ms=renderer_ms()-acquire_start;
        if (result != VK_SUBOPTIMAL_KHR && !vk_ok(result, "Acquire window image"))
            return false;
    }
    double phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_ACQUIRE,phase_start,phase_end);phase_start=phase_end;
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
    if (state->custom_shader) uniform->quality[3]=0;
    if (scene->instances.size()!=state->instance_count || scene->media.size()!=state->medium_count
        || scene->geometry.meshes.size()!=state->mesh_count || scene->effect_tiles.size()!=state->effect_tile_size
        || scene->lighting_bounds.size()!=state->medium_count)
    { fprintf(stderr,"Render scene storage changed after initialization.\n");return false; }
    if (uniform->scene[3]) for (uint32_t i=0;i<state->instance_count;++i)
        if (state->surface_cache_meshes[i]!=(scene->instances[i].static_lighting ? scene->instances[i].mesh+1 : 0))
        { fprintf(stderr,"Static lighting instance layout changed after initialization.\n");return false; }
    if (!update_meshes(state,scene->geometry)) return false;
    scene_instances(state,scene);
    if (state->medium_count) memcpy(state->media.mapped,scene->media.data(),state->medium_count*sizeof(RenderMedium));
    memcpy(state->lighting_instances.mapped,state->lighting_input.data(),state->lighting_input.size()*sizeof(LightingInstance));
    if (state->medium_count) memcpy(state->lighting_bounds.mapped,scene->lighting_bounds.data(),state->medium_count*sizeof(RenderGridBounds));
    update_lighting_dependencies(state,scene);
    update_grid_activity(state);
    if (scene->fog_blur>0) memcpy(state->sight.mapped,scene->fog_field.reveal,sizeof(scene->fog_field.reveal));
    memcpy((char*)state->sight.mapped+sizeof(scene->fog_field.reveal),scene->sight,sizeof(scene->sight));
    {
        memcpy(state->instances.mapped,state->instance_data.data(),state->instances.size);
        VkAccelerationStructureGeometryKHR geometry = {};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.data.deviceAddress = state->instances.address;
        VkAccelerationStructureBuildGeometryInfoKHR build = {};
        build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        build.srcAccelerationStructure = build.dstAccelerationStructure = state->tlas;
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        build.scratchData.deviceAddress = (state->tlas_scratch.address + state->scratch_alignment - 1) & ~(state->scratch_alignment - 1);
        VkAccelerationStructureBuildRangeInfoKHR range = {};
        range.primitiveCount = state->instance_count;
        const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
        VkMemoryBarrier before = {};
        before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        before.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        before.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &before, 0, 0, 0, 0);
        state->build_as(state->command, 1, &build, &ranges);
        VkMemoryBarrier after = {};
        after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &after, 0, 0, 0, 0);
    }
    memcpy(state->uniform.mapped,uniform,sizeof(*uniform));
    memcpy(state->instance_styles.mapped,state->style_data.data(),state->style_data.size()*sizeof(float));
    state->effect_count=scene->effect_count;
    memcpy(state->effects.mapped,scene->effects.data(),state->effect_count*sizeof(EffectSegment));
    memcpy(state->effect_tiles.mapped,scene->effect_tiles.data(),state->effect_tiles.size);
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
    if (renderer->debug_views || scene->guides_required || uniform->quality[3] || state->config.focus_radius>0) shader_buffer_barrier(state->command);

    shader_buffer_barrier(state->command);
    vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline_layout,0,1,&state->descriptors,0,0);
    record_lighting_fields(state);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_GRID_END);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_SURFACE_BEGIN);
    vkCmdBindPipeline(state->command, VK_PIPELINE_BIND_POINT_COMPUTE,
        renderer->debug_views ? state->debug_surface_pipeline : (scene->guides_required || state->config.focus_radius>0) ? state->guide_surface_pipeline : state->pipeline);
    vkCmdBindDescriptorSets(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->pipeline_layout,
        0, 1, &state->descriptors, 0, 0);
    vkCmdDispatch(state->command, (state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, (state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, 1);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_SURFACE_END);
    if (renderer->debug_views || scene->guides_required || uniform->quality[3] || state->config.focus_radius>0) shader_buffer_barrier(state->command);
    if (uniform->quality[0] && uniform->scene[1])
    {
        image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,
            renderer->debug_views ? state->debug_volume_pipeline : state->volume_pipeline);
        vkCmdDispatch(state->command,(state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,(state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
    }
    vkCmdWriteTimestamp(state->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state->queries, GPU_TIME_VOLUME_END);
    if (renderer->debug_views || scene->guides_required || uniform->quality[3] || state->config.focus_radius>0) shader_buffer_barrier(state->command);
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
    bloom.strength = state->config.bloom_strength;
    bloom.exposure = state->config.exposure;
    bloom.debug_views = renderer->debug_views ? 1.0f : 0.0f;
    if (scene->fog_blur>0 && !renderer->debug_views)
    {
        image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->fog_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->bloom_layout,0,1,&state->bloom_descriptors,0,0);
        for (int pass=0;pass<2;++pass)
        {
            bloom.pass=pass;
            vkCmdPushConstants(state->command,state->bloom_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(bloom),&bloom);
            vkCmdDispatch(state->command,(state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,(state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE,1);
            image_barrier(state->command,pass ? state->hdr : state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        image_barrier(state->command,state->hdr_scratch,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        bloom.pass=0;
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,GPU_TIME_FOG_END);
    if (state->config.focus_radius>0 && !renderer->debug_views)
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
    if (bloom.strength>0.0f)
    {
        vkCmdPushConstants(state->command, state->bloom_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bloom), &bloom);
        vkCmdDispatch(state->command, (state->config.width+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, (state->config.height+COMPUTE_GROUP_SIZE-1)/COMPUTE_GROUP_SIZE, 1);
        image_barrier(state->command, state->hdr_scratch, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    bloom.pass = 1;
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
    if (!submit_commands(state, !state->headless))
        return false;
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_SUBMIT_WAIT,phase_start,phase_end);phase_start=phase_end;
    uint64_t timestamps[GPU_TIMESTAMP_COUNT];
    if (!vk_ok(vkGetQueryPoolResults(state->device, state->queries, 0, GPU_TIMESTAMP_COUNT, sizeof(timestamps),
        timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "Read GPU timestamps"))
        return false;
    uint64_t difference = timestamps[GPU_TIME_VOLUME_END] - timestamps[GPU_TIME_UPLOAD_END];
    if (state->timestamp_bits < TIMESTAMP_WORD_BITS)
        difference &= (UINT64_C(1) << state->timestamp_bits) - 1;
    double milliseconds = difference * state->properties.limits.timestampPeriod / NANOSECONDS_PER_MILLISECOND;
    printf("GPU trace: %.3f ms; %u spp; %u triangles\n", milliseconds,
        state->config.samples_per_pixel, state->triangle_count);
    uint64_t surface_ticks = timestamps[GPU_TIME_SURFACE_END]-timestamps[GPU_TIME_SURFACE_BEGIN], volume_ticks = timestamps[GPU_TIME_VOLUME_END]-timestamps[GPU_TIME_SURFACE_END];
    if (state->timestamp_bits < TIMESTAMP_WORD_BITS)
    {
        surface_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
        volume_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    }
    uint64_t medium_blur_ticks=timestamps[GPU_TIME_MEDIUM_BLUR_END]-timestamps[GPU_TIME_EFFECTS_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) medium_blur_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;
    printf("GPU medium blur: %.3f ms; native resolution; transmitted light and effects.\n",medium_blur_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND);
    uint64_t grid_ticks=timestamps[GPU_TIME_GRID_END]-timestamps[GPU_TIME_BACKDROP_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) grid_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;
    printf("GPU shadow grid: %.3f ms; resolution %u; current dynamic blockers.\n",grid_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,state->config.shadow_grid_resolution);
    uint64_t bake_ticks=timestamps[GPU_TIME_BAKE_END]-timestamps[GPU_TIME_BACKDROP_END];
    uint64_t dynamic_ticks=timestamps[GPU_TIME_GRID_END]-timestamps[GPU_TIME_BAKE_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS)
    { bake_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1;dynamic_ticks&=(UINT64_C(1)<<state->timestamp_bits)-1; }
    printf("GPU static lighting bake: %.3f ms; GPU dynamic lighting: %.3f ms.\n",
        bake_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,
        dynamic_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND);
    if (uniform->scene[3]) printf("Static lighting: %u baked, %u reused; %llu bytes; %u surface bakes; current dynamic shadows.\n",
        state->lighting_bakes,state->lighting_reuses,(unsigned long long)(state->static_lighting.size+state->surface_lighting.size+state->lighting_instances.size+state->dynamic_lighting.size+state->lighting_bounds.size),state->lighting_surface_bakes);
    uint64_t background_ticks=timestamps[GPU_TIME_BACKDROP_END]-timestamps[GPU_TIME_UPLOAD_END];
    if (state->timestamp_bits<TIMESTAMP_WORD_BITS) background_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    printf("GPU backdrop: %.3f ms; separate current-frame layer %u.\n",
        background_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND,(scene->background_blur>0) ? 1u : 0u);
    printf("GPU surface: %.3f ms; GPU volumes: %.3f ms.\n",
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
    printf("GPU acceleration update: %.3f ms; GPU effects: %.3f ms.\n",as_ticks*scale,fx_ticks*scale);
    printf("GPU fog/bloom/display: %.3f ms; total render: %.3f ms\n",bloom_ms,renderer->gpu_frame_ms);
    printf("GPU debug overlay: %.3f ms; buffer views %u; FPS stats %u.\n",debug_ms,
        renderer->debug_views ? 1u : 0u,renderer->debug_stats ? 1u : 0u);
    static const struct { ProfileNode node;int begin,end; } spans[]={
        {PROFILE_GPU_FRAME,GPU_TIME_FRAME_BEGIN,GPU_TIME_COPY_END},
        {PROFILE_GPU_SCENE,GPU_TIME_FRAME_BEGIN,GPU_TIME_VOLUME_END},
        {PROFILE_ACCELERATION,GPU_TIME_FRAME_BEGIN,GPU_TIME_UPLOAD_END},
        {PROFILE_BACKDROP,GPU_TIME_UPLOAD_END,GPU_TIME_BACKDROP_END},
        {PROFILE_SHADOW_GRID,GPU_TIME_BACKDROP_END,GPU_TIME_GRID_END},
        {PROFILE_SURFACE,GPU_TIME_GRID_END,GPU_TIME_SURFACE_END},
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
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_READ_TIMES,phase_start,phase_end);phase_start=phase_end;
    state->trace_ms = milliseconds;
    state->volume_ms = volume_ticks*state->properties.limits.timestampPeriod/NANOSECONDS_PER_MILLISECOND;
    state->bloom_ms = bloom_ms;
    if (!state->headless)
    {
        if (!renderer->scene_submissions) printf("Window frame: present.\n");
        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &state->render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &state->swapchain;
        present.pImageIndices = &image_index;
        double present_start=renderer_ms();
        VkResult result = vkQueuePresentKHR(state->queue, &present);
        if (result != VK_SUBOPTIMAL_KHR && !vk_ok(result, "Present frame"))
            return false;
        // One frame in flight; guarantee presentation consumed its binary semaphore.
        if (!vk_ok(vkQueueWaitIdle(state->queue), "Wait for presentation"))
            return false;
        present_ms=renderer_ms()-present_start;
    }
    phase_end=profiler_clock_ms();profiler_cpu(profile,PROFILE_PRESENT,phase_start,phase_end);phase_start=phase_end;
    renderer->acquire_ms=acquire_ms;renderer->present_ms=present_ms;
    printf("Presentation waits: acquire %.3f ms; present %.3f ms; GPU work %.3f ms.\n",renderer->acquire_ms,renderer->present_ms,renderer->gpu_frame_ms);
    if (capture_path && !capture_image(state, capture_path))
        return false;
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
    if (state->device)
    {
        vkDeviceWaitIdle(state->device);
        if (state->effects_pipeline) vkDestroyPipeline(state->device,state->effects_pipeline,0);
        if (state->backdrop_pipeline) vkDestroyPipeline(state->device,state->backdrop_pipeline,0);
        if (state->backdrop_sampler) vkDestroySampler(state->device,state->backdrop_sampler,0);
        for (uint32_t i=0;i<2;++i)
        {
            if (state->backdrop_views[i]) vkDestroyImageView(state->device,state->backdrop_views[i],0);
            if (state->backdrop_images[i]) vkDestroyImage(state->device,state->backdrop_images[i],0);
            if (state->backdrop_memory[i]) vkFreeMemory(state->device,state->backdrop_memory[i],0);
        }
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
        GpuBuffer* buffers[] = { &state->vertices, &state->triangles, &state->indices, &state->instances,
            &state->tlas_buffer, &state->tlas_scratch,
            &state->uniform, &state->readback, &state->media, &state->shadow_grid, &state->static_lighting, &state->lighting_instances, &state->surface_lighting, &state->lighting_bounds, &state->dynamic_lighting, &state->sharp_layer, &state->energy, &state->sight,
            &state->debug_pixels, &state->debug_text, &state->effects, &state->effect_tiles, &state->instance_styles };
        for (uint32_t i = 0; i < sizeof(buffers)/sizeof(buffers[0]); ++i) destroy_buffer(state, buffers[i]);
        if (state->output_view) vkDestroyImageView(state->device, state->output_view, 0);
        if (state->output) vkDestroyImage(state->device, state->output, 0);
        if (state->output_memory) vkFreeMemory(state->device, state->output_memory, 0);
        if (state->swapchain) vkDestroySwapchainKHR(state->device, state->swapchain, 0);
        if (state->image_available) vkDestroySemaphore(state->device, state->image_available, 0);
        if (state->render_finished) vkDestroySemaphore(state->device, state->render_finished, 0);
        if (state->queries) vkDestroyQueryPool(state->device, state->queries, 0);
        if (state->fence) vkDestroyFence(state->device, state->fence, 0);
        if (state->command_pool) vkDestroyCommandPool(state->device, state->command_pool, 0);
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
    bool valid = !state->validation_failed.load();
    delete state;
    renderer->state = 0;
    renderer->running = false;
    return valid;
}
