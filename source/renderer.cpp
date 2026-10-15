#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <vulkan/vulkan.h>

#include "renderer.h"
#include "render_config.h"
#include "geometry.h"
#include "microfacet.h"
#include "sampling.h"
#include "window.h"
#include "world.h"
#include "simulation.h"
#include "vector_math.h"

#include <atomic>
#include <cmath>
#include <float.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>
#include "debug_font.h"
#include "effects.h"

struct GpuBuffer
{
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkDeviceAddress address;
    void* mapped;
};

struct RenderUniform
{
    float camera_position[4];
    float camera_forward[4];
    float camera_right[4];
    float camera_up[4];
    float background[4];
    float light[4];
    uint32_t settings[4];
    uint32_t scene[4];
    float display[4];
    float sun_sphere[4];
    float sun_color[4];
    uint32_t quality[4];
    float scene_min[4];
    float scene_max[4];
    float ambient_top[4], ambient_bottom[4];
    float foreground_min[4], foreground_max[4];
    uint32_t background_quality[4];
    float backdrop_style[4], backdrop_animation[4];
    float studio_direction[3][4], studio_color[3][4], cell_material[4], backdrop_wave[4];
};

struct GpuMedium
{
    float center_radius[4];
    float rayleigh_mie[4];
    float absorption_phase[4];
    float shape[4]; // Scale height, base radius, density kind (0 planet, 1 uniform, 2 faded fog), fade fraction.
};

static_assert(sizeof(GpuVertex) == 48, "Vertex shader layout");
static_assert(sizeof(GpuTriangle) == 16, "Triangle shader layout");
static_assert(sizeof(RenderUniform) == 464, "Uniform shader layout");

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
    int32_t hover[4];
};
static_assert(sizeof(DebugParameters) == 112, "Debug push-constant layout");

struct DynamicMesh
{
    GpuBuffer storage;
    VkAccelerationStructureKHR acceleration;
    VkDeviceAddress address;
    MeshRange range;
    float low[3],high[3];
};

enum { SPIKE_INSTANCE_BASE=2+MAX_UNITS+MAX_BODIES,
       MAX_RENDER_INSTANCES=SPIKE_INSTANCE_BASE+MAX_UNITS*MAX_SPIKES+MAX_PROJECTILES+2*(MAX_UNITS+MAX_BODIES) };
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
    GpuBuffer blas_buffer;
    GpuBuffer tlas_buffer;
    GpuBuffer sun_blas_buffer;
    GpuBuffer backdrop_blas_buffer;
    GpuBuffer tlas_scratch;
    GpuBuffer media;
    GpuBuffer energy;
    GpuBuffer sampling;
    GpuBuffer sun_sources;
    GpuBuffer debug_pixels;
    GpuBuffer debug_text;
    GpuBuffer effects, effect_tiles, instance_styles;
    EffectSegment effect_data[MAX_EFFECT_SEGMENTS];
    std::vector<uint32_t> effect_tile_data;
    float style_data[MAX_RENDER_INSTANCES][8];
    uint32_t effect_count, effect_tile_count;
    uint32_t attached_spikes,flying_spikes,embedded_spikes;
    GpuBuffer uniform;
    RenderUniform uniform_data;
    uint32_t text_mask[(512*92+960*160)/32];
    VkAccelerationStructureInstanceKHR instance_data[MAX_RENDER_INSTANCES];
    GpuBuffer readback;
    VkAccelerationStructureKHR blas;
    VkAccelerationStructureKHR tlas;
    VkAccelerationStructureKHR sun_blas;
    VkAccelerationStructureKHR backdrop_blas;
    Vec3 sun_origin;
    Vec3 current_sun;
    const Simulation* simulation;
    DynamicMesh dynamic_meshes[MAX_SHIPS+2];
    uint32_t mesh_count, ship_meshes[MAX_SHIPS], stalk_mesh, spike_mesh, instance_count;
    float static_min[3],static_max[3];
    float foreground_min[3],foreground_max[3];
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
    VkPipeline bloom_pipeline;
    VkDescriptorSetLayout denoise_descriptor_layout;
    VkDescriptorPool denoise_descriptor_pool;
    VkDescriptorSet denoise_descriptors[2];
    VkPipelineLayout denoise_layout;
    VkPipeline denoise_pipeline;
    VkPipeline guide_surface_pipeline, guide_volume_pipeline;
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
    VkPipeline volume_pipeline;
    VkPipeline debug_surface_pipeline, debug_volume_pipeline, debug_overlay_pipeline;
    VkPipelineLayout debug_overlay_layout;
    double trace_ms, volume_ms, bloom_ms, denoise_ms;
    VkPipeline sun_source_pipeline;
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
    return 1000.0*(double)ticks.QuadPart/(double)frequency.QuadPart;
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
    application.pApplicationName = "Floria";
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
                state->memory_properties.memoryHeaps[i].size / (1024.0 * 1024.0 * 1024.0));
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
    query.queryCount = 10;
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
    GpuBuffer* storage, VkAccelerationStructureKHR* result)
{
    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = type;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
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
    GpuBuffer* scratch = type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? &state->tlas_scratch : &local_scratch;
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

static bool upload_scene(RendererState* state, const World* world)
{
    Geometry geometry;
    if (!geometry_build(&geometry, world, &state->config))
        return false;
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
    if (!build_acceleration(state, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        &mesh, geometry.static_triangle_count, &state->blas_buffer, &state->blas))
        return false;
    mesh.geometry.triangles.indexData.deviceAddress = state->indices.address + geometry.light_first_triangle * 3 * sizeof(uint32_t);
    if (geometry.light_triangle_count && !build_acceleration(state, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        &mesh, geometry.light_triangle_count, &state->sun_blas_buffer, &state->sun_blas)) return false;
    if (world->backdrop_soft)
    {
        mesh.geometry.triangles.indexData.deviceAddress=state->indices.address+geometry.backdrop.first*3*sizeof(uint32_t);
        if (!build_acceleration(state,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,&mesh,geometry.backdrop.count,
            &state->backdrop_blas_buffer,&state->backdrop_blas)) return false;
    }
    if (world->gameplay.enabled)
    {
        for (uint32_t i=0;i<world->ship_count+1+(world->spikes.enabled ? 1u : 0u);++i)
        {
            MeshRange range=i>world->ship_count ? geometry.spike : i==world->ship_count ? geometry.stalk : geometry.ships[i];
            uint32_t index=0;
            while (index<state->mesh_count && state->dynamic_meshes[index].range.first!=range.first) ++index;
            if (index==state->mesh_count)
            {
                DynamicMesh* m=&state->dynamic_meshes[state->mesh_count++];m->range=range;
                for (uint32_t axis=0;axis<3;++axis) { m->low[axis]=FLT_MAX;m->high[axis]=-FLT_MAX; }
                for (uint32_t t=range.first;t<range.first+range.count;++t)
                    for (uint32_t v=0;v<3;++v)
                        for (uint32_t axis=0;axis<3;++axis)
                        {
                            float coordinate=geometry.vertices[geometry.triangles[t].index[v]].position[axis];
                            m->low[axis]=std::fmin(m->low[axis],coordinate);m->high[axis]=std::fmax(m->high[axis],coordinate);
                        }
                mesh.geometry.triangles.indexData.deviceAddress=state->indices.address+range.first*3*sizeof(uint32_t);
                if (!build_acceleration(state,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,&mesh,range.count,
                    &m->storage,&m->acceleration)) return false;
                VkAccelerationStructureDeviceAddressInfoKHR address={};
                address.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
                address.accelerationStructure=m->acceleration;
                m->address=state->get_as_address(state->device,&address);
            }
            if (i>world->ship_count) state->spike_mesh=index;else if (i==world->ship_count) state->stalk_mesh=index;else state->ship_meshes[i]=index;
        }
    }
    VkAccelerationStructureDeviceAddressInfoKHR address = {};
    address.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    address.accelerationStructure = state->blas;
    VkAccelerationStructureInstanceKHR* instance=state->instance_data;
    state->instance_count=world->gameplay.enabled ? 2+MAX_UNITS+world->body_count : 2;
    if (world->spikes.enabled) state->instance_count=SPIKE_INSTANCE_BASE+MAX_UNITS*world->spikes.count+MAX_PROJECTILES+2*(MAX_UNITS+MAX_BODIES);
    for (uint32_t i = 0; i < 2; ++i)
    {
        instance[i].transform.matrix[0][0] = instance[i].transform.matrix[1][1] = instance[i].transform.matrix[2][2] = 1.0f;
        // Gameplay rays use mask 1. Slot 1 is the legacy sun mesh (mask 2),
        // or an isolated decorative backdrop (mask 4) in ambient-only worlds.
        instance[i].mask = i == 0 ? 0x1 : geometry.light_triangle_count ? 0x2 : 0;
        instance[i].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        address.accelerationStructure = i == 0 || !geometry.light_triangle_count ? state->blas : state->sun_blas;
        instance[i].accelerationStructureReference = state->get_as_address(state->device, &address);
        instance[i].instanceCustomIndex = i == 0 ? 0 : geometry.light_first_triangle;
    }
    if (world->backdrop_soft)
    {
        address.accelerationStructure=state->backdrop_blas;
        instance[1].accelerationStructureReference=state->get_as_address(state->device,&address);
        instance[1].instanceCustomIndex=geometry.backdrop.first;
        instance[1].mask=4;
    }
    for (uint32_t i=2;i<state->instance_count;++i)
    {
        instance[i]=instance[0];instance[i].mask=0;
        address.accelerationStructure=state->dynamic_meshes[0].acceleration;
        instance[i].accelerationStructureReference=state->get_as_address(state->device,&address);
    }
    if (!create_buffer(state, &state->instances, state->instance_count*sizeof(instance[0]), input, host))
        return false;
    memcpy(state->instances.mapped, instance, state->instances.size);

    state->sun_origin = state->current_sun = world->sun.position;
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
    RenderUniform uniform = {};
    Vec3 forward = normalize(subtract(state->config.camera_target, state->config.camera_position));
    Vec3 right = normalize(cross(forward, state->config.camera_up));
    Vec3 up = cross(right, forward);
    store_vector(uniform.camera_position, state->config.camera_position);
    store_vector(uniform.camera_forward, forward);
    store_vector(uniform.camera_right, right);
    store_vector(uniform.camera_up, up);
    Color environment=world->ambient_enabled ? world->ambient : state->config.background;
    uniform.background[0] = environment.r;
    uniform.background[1] = environment.g;
    uniform.background[2] = environment.b;
    uniform.light[3] = world->sun.radius>0 ? state->config.sun_radiance : 0;
    uniform.ambient_top[0]=world->ambient_top.r;uniform.ambient_top[1]=world->ambient_top.g;uniform.ambient_top[2]=world->ambient_top.b;
    uniform.ambient_bottom[0]=world->ambient_bottom.r;uniform.ambient_bottom[1]=world->ambient_bottom.g;uniform.ambient_bottom[2]=world->ambient_bottom.b;
    uniform.settings[0] = state->config.width;
    uniform.settings[1] = state->config.height;
    uniform.settings[2] = state->config.samples_per_pixel;
    uniform.settings[3] = state->config.max_bounces;
    uniform.scene[0] = geometry.light_first_triangle;
    uniform.scene[1] = geometry.light_triangle_count;
    uniform.scene[2] = state->config.seed;
    uniform.display[0] = state->config.exposure;
    uniform.display[1] = std::tan(state->config.vertical_fov_degrees*0.00872664626f);
    uniform.display[2] = (float)state->config.width/state->config.height;
    uniform.display[3] = (float)state->config.volume_steps;
    store_vector(uniform.sun_sphere, world->sun.position);
    uniform.sun_sphere[3] = world->sun.radius;
    uniform.sun_color[0] = world->sun.color.r;
    uniform.sun_color[1] = world->sun.color.g;
    uniform.sun_color[2] = world->sun.color.b;
    uniform.quality[0] = state->config.volume_samples_per_pixel;
    uniform.quality[1] = state->config.volume_local_bounds;
    uniform.quality[2] = state->config.microfacet_energy_compensation;
    uniform.quality[3] = state->config.spatial_sun_reuse;
    uniform.background_quality[0]=world->backdrop_soft ? 0 : world->backdrop_samples;
    uniform.background_quality[1]=world->backdrop_soft ? 0 : geometry.backdrop.count;
    uniform.backdrop_style[0]=world->backdrop_soft ? world->backdrop_blur : 0;
    uniform.backdrop_style[1]=world->backdrop_glow;uniform.backdrop_style[2]=world->backdrop_ambient;
    uniform.cell_material[0]=world->cell_transmission;uniform.cell_material[1]=world->cell_absorption;
    uniform.cell_material[2]=world->studio_mask ? 1 : 0;uniform.cell_material[3]=world->cell_clarity;
    float total_weight=0;
    for (uint32_t i=0;i<3;++i)
    {
        const StudioLight* light=&world->studio[i];store_vector(uniform.studio_direction[i],light->direction);
        uniform.studio_color[i][0]=light->color.r*light->irradiance;
        uniform.studio_color[i][1]=light->color.g*light->irradiance;
        uniform.studio_color[i][2]=light->color.b*light->irradiance;
        float weight=light->irradiance*(.2126f*light->color.r+.7152f*light->color.g+.0722f*light->color.b);
        uniform.studio_direction[i][3]=weight;total_weight+=weight;
    }
    if (total_weight>0) for (uint32_t i=0;i<3;++i) uniform.studio_direction[i][3]/=total_weight;
    uniform.backdrop_wave[0]=world->backdrop_wave.x;uniform.backdrop_wave[1]=world->backdrop_wave.y;uniform.backdrop_wave[2]=world->backdrop_wave.z;
    const float* first = geometry.vertices[geometry.triangles[0].index[0]].position;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        uniform.scene_min[axis] = uniform.scene_max[axis] = first[axis];
        uniform.foreground_min[axis]=FLT_MAX;uniform.foreground_max[axis]=-FLT_MAX;
    }
    for (uint32_t i = 0; i < geometry.static_triangle_count; ++i)
        for (uint32_t j = 0; j < 3; ++j)
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                float coordinate = geometry.vertices[geometry.triangles[i].index[j]].position[axis];
                uniform.scene_min[axis] = std::fmin(uniform.scene_min[axis],coordinate);
                uniform.scene_max[axis] = std::fmax(uniform.scene_max[axis],coordinate);
                if (i<geometry.backdrop.first || i>=geometry.backdrop.first+geometry.backdrop.count)
                {
                    uniform.foreground_min[axis]=std::fmin(uniform.foreground_min[axis],coordinate);
                    uniform.foreground_max[axis]=std::fmax(uniform.foreground_max[axis],coordinate);
                }
            }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        float margin = std::fmax(0.0001f,0.00001f*std::fmax(std::fabs(uniform.scene_min[axis]),std::fabs(uniform.scene_max[axis])));
        uniform.scene_min[axis] -= margin;
        uniform.scene_max[axis] += margin;
        if (!std::isfinite(uniform.scene_min[axis]) || !std::isfinite(uniform.scene_max[axis]))
        {
            fprintf(stderr,"Scene geometry has unrepresentable bounds.\n");
            return false;
        }
    }
    if (!create_buffer(state, &state->energy, MICROFACET_TABLE_SIZE*MICROFACET_TABLE_SIZE*sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host)) return false;
    memcpy(state->static_min,uniform.scene_min,sizeof(state->static_min));
    memcpy(state->static_max,uniform.scene_max,sizeof(state->static_max));
    memcpy(state->foreground_min,uniform.foreground_min,sizeof(state->foreground_min));
    memcpy(state->foreground_max,uniform.foreground_max,sizeof(state->foreground_max));
    microfacet_energy_table((float*)state->energy.mapped);
    uint32_t sequence_count = state->config.samples_per_pixel > state->config.volume_samples_per_pixel
        ? state->config.samples_per_pixel : state->config.volume_samples_per_pixel;
    VkDeviceSize coefficients_size = sizeof(sz_columns)+sizeof(sz_diagonals);
    VkDeviceSize sampling_size = coefficients_size+(VkDeviceSize)sequence_count*256*sizeof(uint32_t);
    if (!create_buffer(state,&state->sampling,sampling_size,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)) return false;
    memcpy(state->sampling.mapped,sz_columns,sizeof(sz_columns));
    memcpy((unsigned char*)state->sampling.mapped+sizeof(sz_columns),sz_diagonals,sizeof(sz_diagonals));
    make_sz_samples((uint32_t*)((unsigned char*)state->sampling.mapped+coefficients_size),sequence_count);
    // Five vec4 records per padded cell pixel. Allocate full scratch only when used.
    VkDeviceSize source_size = state->config.spatial_sun_reuse
        ? (VkDeviceSize)((state->config.width+7)/8)*((state->config.height+7)/8)*64*80 : 80;
    if (source_size > state->properties.limits.maxStorageBufferRange)
    {
        fprintf(stderr,"SPMIS sun source buffer exceeds the device storage-buffer limit.\n");
        return false;
    }
    if (!create_buffer(state,&state->sun_sources,source_size,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    GpuMedium media[MAX_BODIES + 2] = {};
    uint32_t medium_count = 0;
    if (world->global_atmosphere_radius > 0.0f)
    {
        GpuMedium* m = &media[medium_count++];
        store_vector(m->center_radius, world->global_atmosphere_center);
        m->center_radius[3] = world->global_atmosphere_radius;
        m->rayleigh_mie[0] = world->global_atmosphere_scattering.r;
        m->rayleigh_mie[1] = world->global_atmosphere_scattering.g;
        m->rayleigh_mie[2] = world->global_atmosphere_scattering.b;
        m->absorption_phase[3] = world->global_atmosphere_anisotropy;
        m->shape[2] = world->global_atmosphere_edge_fade > 0.0f ? 2.0f : 1.0f;
        m->shape[3] = world->global_atmosphere_edge_fade;
    }
    if (world->haze_radius > 0.0f)
    {
        GpuMedium* m = &media[medium_count++];
        store_vector(m->center_radius, world->haze_center);
        m->center_radius[3] = world->haze_radius;
        m->rayleigh_mie[0] = world->haze_scattering.r;
        m->rayleigh_mie[1] = world->haze_scattering.g;
        m->rayleigh_mie[2] = world->haze_scattering.b;
        m->absorption_phase[3] = world->haze_anisotropy;
        m->shape[2] = world->haze_edge_fade > 0.0f ? 2.0f : 1.0f;
        m->shape[3] = world->haze_edge_fade;
    }
    for (uint32_t i = 0; i < world->body_count; ++i)
    {
        const Cell* planet = &world->bodies[i];
        if (planet->atmosphere_height <= 0.0f) continue;
        GpuMedium* m = &media[medium_count++];
        store_vector(m->center_radius, planet->position);
        m->center_radius[3] = planet->radius * (1.0f + planet->atmosphere_height);
        m->rayleigh_mie[0] = planet->rayleigh.r;
        m->rayleigh_mie[1] = planet->rayleigh.g;
        m->rayleigh_mie[2] = planet->rayleigh.b;
        m->rayleigh_mie[3] = planet->mie;
        m->absorption_phase[0] = m->absorption_phase[1] = m->absorption_phase[2] = planet->absorption;
        m->absorption_phase[3] = planet->anisotropy;
        m->shape[0] = planet->radius * planet->atmosphere_scale_height;
        m->shape[1] = planet->radius;
    }
    uint32_t allocated_media = medium_count ? medium_count : 1;
    if (!create_buffer(state, &state->media, allocated_media * sizeof(GpuMedium), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host)) return false;
    memcpy(state->media.mapped, media, allocated_media * sizeof(GpuMedium));
    uniform.scene[3] = medium_count;
    state->uniform_data=uniform;
    memcpy(state->uniform.mapped, &uniform, sizeof(uniform));
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
        ? (VkDeviceSize)state->config.width*state->config.height*80 : 80;
    if (debug_bytes > state->properties.limits.maxStorageBufferRange)
    {
        fprintf(stderr,"Debug buffers exceed the device's storage-buffer range.\n");
        return false;
    }
    if (!create_buffer(state,&state->debug_pixels,debug_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return false;
    if (!create_buffer(state,&state->debug_text,512+(512*92+960*160)/8,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
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
    state->effect_tile_count=((state->config.width+15)/16)*((state->config.height+15)/16);
    state->effect_tile_data.resize(2*state->effect_tile_count+1+MAX_EFFECT_REFERENCES);
    VkMemoryPropertyFlags host=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!create_buffer(state,&state->effects,sizeof(state->effect_data),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
        || !create_buffer(state,&state->effect_tiles,state->effect_tile_data.size()*sizeof(uint32_t),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)
        || !create_buffer(state,&state->instance_styles,sizeof(state->style_data),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,host)) return false;
    memcpy(state->instance_styles.mapped,state->style_data,sizeof(state->style_data));
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
    bool success = vk_ok(vkCreateComputePipelines(state->device, VK_NULL_HANDLE, 1, &pipeline, 0, output_pipeline), "Create compute pipeline");

    vkDestroyShaderModule(state->device, shader, 0);
    return success;
}

static bool create_pipeline(RendererState* state, const char* shader_path)
{
    if (state->properties.limits.maxPerStageDescriptorStorageBuffers < 10 ||
        state->properties.limits.maxDescriptorSetStorageBuffers < 10)
    {
        fprintf(stderr,"Renderer requires ten compute storage-buffer descriptors.\n");
        return false;
    }
    VkDescriptorSetLayoutBinding bindings[17] = {};
    const VkDescriptorType types[] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
    for (uint32_t i = 0; i < 17; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = types[i];
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout = {};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 17;
    layout.pBindings = bindings;
    if (!vk_ok(vkCreateDescriptorSetLayout(state->device, &layout, 0, &state->descriptor_layout), "Create descriptor layout"))
        return false;
    VkDescriptorPoolSize pool_sizes[] = { { types[0], 3 }, { types[1], 9 }, { types[2], 30 }, { types[4], 3 }, {types[12],6} };
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 3;
    pool.poolSizeCount = 5;
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
        { state->sampling.handle, 0, state->sampling.size },
        { state->sun_sources.handle, 0, state->sun_sources.size },
        { state->debug_pixels.handle, 0, state->debug_pixels.size }
    };
    VkWriteDescriptorSet writes[17] = {};
    for (uint32_t i = 0; i < 17; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = state->descriptors;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = types[i];
    }
    writes[0].pNext = &acceleration;
    writes[1].pImageInfo = &image;
    writes[2].pBufferInfo = &buffers[0];
    writes[3].pBufferInfo = &buffers[1];
    writes[4].pBufferInfo = &buffers[2];
    writes[5].pBufferInfo = &buffers[3];
    writes[6].pBufferInfo = &buffers[4];
    writes[7].pBufferInfo = &buffers[5];
    writes[8].pBufferInfo = &buffers[6];
    writes[9].pBufferInfo = &buffers[7];
    VkDescriptorImageInfo background[2]={};
    for (uint32_t i=0;i<2;++i)
    {
        background[i].sampler=state->backdrop_sampler;
        background[i].imageView=state->backdrop_views[i] ? state->backdrop_views[i] : state->hdr_view;
        background[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
        writes[10+i].pImageInfo=&background[i];writes[12+i].pImageInfo=&background[i];
    }
    VkDescriptorBufferInfo effect_buffers[]={{state->effects.handle,0,state->effects.size},
        {state->effect_tiles.handle,0,state->effect_tiles.size},{state->instance_styles.handle,0,state->instance_styles.size}};
    for (uint32_t i=0;i<3;++i) writes[14+i].pBufferInfo=&effect_buffers[i];
    vkUpdateDescriptorSets(state->device, 17, writes, 0, 0);
    if (!vk_ok(vkAllocateDescriptorSets(state->device,&allocate,&state->effect_descriptors),"Allocate effects descriptors")) return false;
    image.imageView=state->config.spatial_denoise ? state->hdr_scratch_view : state->hdr_view;
    for (uint32_t i=0;i<17;++i) writes[i].dstSet=state->effect_descriptors;
    vkUpdateDescriptorSets(state->device,17,writes,0,0);
    // Same layout, with display RGBA8 instead of the tracer's linear-HDR image.
    image.imageView = state->output_view;
    VkDescriptorBufferInfo text_buffer = {state->debug_text.handle,0,state->debug_text.size};
    writes[4].pBufferInfo = &text_buffer;
    for (uint32_t i = 0; i < 17; ++i) writes[i].dstSet = state->debug_descriptors;
    vkUpdateDescriptorSets(state->device, 17, writes, 0, 0);

    VkPipelineLayoutCreateInfo pipeline_layout = {};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &state->descriptor_layout;
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    pipeline_layout.pushConstantRangeCount=1;pipeline_layout.pPushConstantRanges=&push;
    return vk_ok(vkCreatePipelineLayout(state->device, &pipeline_layout, 0, &state->pipeline_layout), "Create pipeline layout")
        && load_compute_pipeline(state, shader_path, state->pipeline_layout, &state->pipeline);
}

static bool load_companion(RendererState* state, const char* path_trace_path,
    const char* filename, VkPipelineLayout layout, VkPipeline* pipeline)
{
    const char* slash = strrchr(path_trace_path,'/');
    const char* backslash = strrchr(path_trace_path,'\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    size_t directory_length = slash ? (size_t)(slash-path_trace_path+1) : 0;
    char path[1024];
    if (directory_length+strlen(filename)+1 > sizeof(path)) return false;
    memcpy(path,path_trace_path,directory_length);
    strcpy(path+directory_length,filename);
    return load_compute_pipeline(state,path,layout,pipeline);
}

static bool create_bloom(RendererState* state, const char* path_trace_path)
{
    VkDescriptorSetLayoutBinding bindings[4] = {};
    for (uint32_t i = 0; i < 4; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout = {};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 4;
    layout.pBindings = bindings;
    if (!vk_ok(vkCreateDescriptorSetLayout(state->device, &layout, 0, &state->bloom_descriptor_layout), "Create bloom descriptor layout")) return false;
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    if (!vk_ok(vkCreateDescriptorPool(state->device, &pool, 0, &state->bloom_descriptor_pool), "Create bloom descriptor pool")) return false;
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = state->bloom_descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &state->bloom_descriptor_layout;
    if (!vk_ok(vkAllocateDescriptorSets(state->device, &allocate, &state->bloom_descriptors), "Allocate bloom descriptors")) return false;
    VkDescriptorImageInfo images[3] = {};
    VkImageView views[] = {state->config.spatial_denoise ? state->hdr_scratch_view : state->hdr_view,
        state->config.spatial_denoise ? state->hdr_view : state->hdr_scratch_view,state->output_view};
    VkWriteDescriptorSet writes[4] = {};
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
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = state->bloom_descriptors;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].pBufferInfo = &debug_buffer;
    vkUpdateDescriptorSets(state->device, 4, writes, 0, 0);
    VkPushConstantRange constants = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BloomParameters)};
    VkPipelineLayoutCreateInfo pipeline_layout = {};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &state->bloom_descriptor_layout;
    pipeline_layout.pushConstantRangeCount = 1;
    pipeline_layout.pPushConstantRanges = &constants;
    if (!vk_ok(vkCreatePipelineLayout(state->device, &pipeline_layout, 0, &state->bloom_layout), "Create bloom pipeline layout")) return false;
    return load_companion(state,path_trace_path,"bloom.comp.spv",state->bloom_layout,&state->bloom_pipeline);
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
    return load_companion(state,builtin_shader_path,state->config.spatial_sun_reuse ? "debug_spatial.comp.spv" : "debug_surface.comp.spv",
        state->pipeline_layout,&state->debug_surface_pipeline)
        && load_companion(state,builtin_shader_path,"debug_volume.comp.spv",state->pipeline_layout,&state->debug_volume_pipeline);
}

static bool create_denoise(RendererState* state, const char* builtin_shader_path)
{
    if (!state->config.spatial_denoise) return true;
    VkDescriptorSetLayoutBinding bindings[3] = {};
    for (uint32_t i=0; i<3; ++i)
    {
        bindings[i].binding=i;
        bindings[i].descriptorType=i==2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount=1;
        bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout = {};
    layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount=3; layout.pBindings=bindings;
    if (!vk_ok(vkCreateDescriptorSetLayout(state->device,&layout,0,&state->denoise_descriptor_layout),"Create denoise descriptor layout")) return false;
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}};
    VkDescriptorPoolCreateInfo pool = {};
    pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets=2; pool.poolSizeCount=2; pool.pPoolSizes=sizes;
    if (!vk_ok(vkCreateDescriptorPool(state->device,&pool,0,&state->denoise_descriptor_pool),"Create denoise descriptor pool")) return false;
    VkDescriptorSetLayout layouts[]={state->denoise_descriptor_layout,state->denoise_descriptor_layout};
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool=state->denoise_descriptor_pool;
    allocate.descriptorSetCount=2; allocate.pSetLayouts=layouts;
    if (!vk_ok(vkAllocateDescriptorSets(state->device,&allocate,state->denoise_descriptors),"Allocate denoise descriptors")) return false;
    VkImageView views[]={state->hdr_view,state->hdr_scratch_view};
    VkDescriptorBufferInfo guides={state->debug_pixels.handle,0,state->debug_pixels.size};
    for (uint32_t set=0; set<2; ++set)
    {
        VkDescriptorImageInfo images[2] = {};
        VkWriteDescriptorSet writes[3] = {};
        for (uint32_t i=0; i<3; ++i)
        {
            writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet=state->denoise_descriptors[set];
            writes[i].dstBinding=i; writes[i].descriptorCount=1;
            writes[i].descriptorType=bindings[i].descriptorType;
            if (i<2)
            {
                images[i].imageView=views[(set+i)%2];
                images[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
                writes[i].pImageInfo=&images[i];
            }
            else writes[i].pBufferInfo=&guides;
        }
        vkUpdateDescriptorSets(state->device,3,writes,0,0);
    }
    VkPushConstantRange constants={VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo pipeline_layout = {};
    pipeline_layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount=1; pipeline_layout.pSetLayouts=&state->denoise_descriptor_layout;
    pipeline_layout.pushConstantRangeCount=1; pipeline_layout.pPushConstantRanges=&constants;
    return vk_ok(vkCreatePipelineLayout(state->device,&pipeline_layout,0,&state->denoise_layout),"Create denoise pipeline layout")
        && load_companion(state,builtin_shader_path,"denoise.comp.spv",state->denoise_layout,&state->denoise_pipeline)
        && load_companion(state,builtin_shader_path,state->config.spatial_sun_reuse ? "debug_guide_spatial.comp.spv" : "debug_guide_surface.comp.spv",
            state->pipeline_layout,&state->guide_surface_pipeline)
        && load_companion(state,builtin_shader_path,"debug_guide_volume.comp.spv",state->pipeline_layout,&state->guide_volume_pipeline);
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
    parameters.settings[3] = renderer->gameplay_hud ? 1.0f : 0.0f;
    memcpy(parameters.marker,renderer->planet_marker,sizeof(parameters.marker));
    memcpy(parameters.status,renderer->hud_status,sizeof(parameters.status));
    memcpy(parameters.accent,renderer->hud_accent,sizeof(parameters.accent));
    memcpy(parameters.feedback,renderer->hud_feedback,sizeof(parameters.feedback));
    memcpy(parameters.hover,renderer->hover_marker,sizeof(parameters.hover));
    char lines[3][33] = {};
    if (renderer->gpu_frame_ms>0)
        snprintf(lines[0],sizeof(lines[0]),"GPU %.2f MS WORK %.2f MS",renderer->gpu_frame_ms,renderer->work_frame_ms);
    else snprintf(lines[0],sizeof(lines[0]),"GPU -- MS WORK -- MS");
    snprintf(lines[1],sizeof(lines[1]),"FPS %.1f %s",renderer->previous_frame_ms>0 ? 1000.0/renderer->previous_frame_ms : 0,
        state->headless ? "HEADLESS" : state->present_mode==VK_PRESENT_MODE_IMMEDIATE_KHR ? "VSYNC OFF" : "VSYNC ON");
    snprintf(lines[2],sizeof(lines[2]),"ACQUIRE %.2f PRESENT %.2f MS",renderer->acquire_ms,renderer->present_ms);
    uint32_t* mask = state->text_mask;
    memset(mask,0,(512*92+960*160)/8);
    for (uint32_t line = 0; line < 3; ++line)
        for (uint32_t column = 0; column < 32; ++column)
        {
            unsigned char character = (unsigned char)lines[line][column];
            uint32_t glyph = character < 128 ? debug_glyphs[character] : 0;
            for (uint32_t y = 0; y < 7; ++y)
                for (uint32_t x = 0; x < 4; ++x)
                    if (glyph & (1u << (y*4+x)))
                        for (uint32_t row = 0; row < 3; ++row)
                            for (uint32_t col = 0; col < 3; ++col)
                            {
                                uint32_t bit = (8+line*25+y*3+row)*512+8+column*15+x*3+col;
                                mask[bit/32] |= 1u << (bit%32);
                            }
        }
    if (renderer->gameplay_hud)
        for (uint32_t line=0;line<8;++line)
            for (uint32_t column=0;column<95 && renderer->hud[line][column];++column)
            {
                unsigned char character=(unsigned char)renderer->hud[line][column];
                uint32_t glyph=character<128 ? debug_glyphs[character] : 0;
                for (uint32_t y=0;y<7;++y)
                    for (uint32_t x=0;x<4;++x)
                        if (glyph & (1u << (y*4+x)))
                            for (uint32_t row=0;row<2;++row)
                                for (uint32_t col=0;col<2;++col)
                                {
                                    uint32_t bit=512*92+(8+line*18+y*2+row)*960+8+column*10+x*2+col;
                                    mask[bit/32] |= 1u << (bit%32);
                                }
            }
    memcpy((uint32_t*)state->debug_text.mapped+128,mask,sizeof(state->text_mask));
    vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->debug_overlay_pipeline);
    vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->debug_overlay_layout,
        0,1,&state->debug_descriptors,0,0);
    vkCmdPushConstants(state->command,state->debug_overlay_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
    uint32_t width = (renderer->debug_views || renderer->gameplay_hud) ? state->config.width : std::min(state->config.width,512u);
    uint32_t height = (renderer->debug_views || renderer->gameplay_hud) ? state->config.height : std::min(state->config.height,92u);
    vkCmdDispatch(state->command,(width+7)/8,(height+7)/8,1);
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
        VkImage beauty = state->config.spatial_denoise ? state->hdr_scratch : state->hdr;
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

bool renderer_initialize(Renderer* renderer, const World* world, const RenderConfig* config,
                         const char* shader_path, const char* builtin_shader_path, bool headless, bool debug_views, bool no_activate, const Simulation* simulation)
{
    *renderer = Renderer();
    renderer->state = new (std::nothrow) RendererState();
    if (!renderer->state)
        return false;
    RendererState* state = renderer->state;
    state->validation_failed.store(false);
    state->config = *config;
    state->headless = headless;
    state->simulation = simulation;
    state->no_activate = no_activate;
    state->debug_available = !headless || debug_views || config->spatial_denoise || world->gameplay.enabled;
    state->custom_shader = strcmp(shader_path,builtin_shader_path) != 0;
    if (state->custom_shader && (config->spatial_denoise || world->gameplay.enabled))
    {
        fprintf(stderr,"Shader overrides require spatial_denoise 0 and a static world without gameplay rules.\n");
        renderer_shutdown(renderer);
        return false;
    }
    bool success = create_instance(state) && create_device(state) && create_commands(state)
        && create_swapchain(state) && upload_scene(state, world) && create_output(state) && create_pipeline(state, shader_path)
        && load_companion(state,shader_path,"volume.comp.spv",state->pipeline_layout,&state->volume_pipeline)
        && create_bloom(state, shader_path) && create_debug(state,builtin_shader_path)
        && create_denoise(state,builtin_shader_path)
        && load_companion(state,builtin_shader_path,"effects.comp.spv",state->pipeline_layout,&state->effects_pipeline);
    if (success && world->backdrop_soft)
        success=load_companion(state,builtin_shader_path,"backdrop.comp.spv",state->pipeline_layout,&state->backdrop_pipeline);
    if (success && config->spatial_sun_reuse)
    {
        vkDestroyPipeline(state->device,state->pipeline,0);
        state->pipeline = VK_NULL_HANDLE;
        success = load_companion(state,shader_path,"spatial.comp.spv",state->pipeline_layout,&state->pipeline)
            && load_companion(state,shader_path,"sun_sources.comp.spv",state->pipeline_layout,&state->sun_source_pipeline);
    }
    if (!success || state->validation_failed.load())
    {
        renderer_shutdown(renderer);
        return false;
    }
    renderer->submitted_body_count = world->body_count;
    renderer->running = true;
    printf("Renderer: %ux%u native, %u spp, %u surface bounces, %u triangles, no temporal history.\n",
        config->width, config->height, config->samples_per_pixel, config->max_bounces, state->triangle_count);
    printf("Sampler: SZ-driven NILE O2; spatial sun reuse %u; source scratch %llu bytes. Shader overrides may differ.\n",
        config->spatial_sun_reuse,(unsigned long long)state->sun_sources.size);
    printf("Reconstruction: spatial denoise %u; current-frame guides %llu bytes; no history or upscaling.\n",
        config->spatial_denoise,(unsigned long long)state->debug_pixels.size);
    printf("Configured sampling: %u volume spp; local bounds %u; metal energy compensation %u. Selected shader controls estimators.\n",
        config->volume_samples_per_pixel, config->volume_local_bounds, config->microfacet_energy_compensation);
    return true;
}

bool renderer_poll(Renderer* renderer, PointerInput* input)
{
    *input = PointerInput();
    renderer->running = renderer->state->headless || window_poll(&renderer->state->window, input);
    return renderer->running;
}

static void set_dynamic_instance(RendererState* state, uint32_t slot, uint32_t mesh,
    Vec3 position, Vec3 x, Vec3 y, Vec3 z, RenderUniform* uniform)
{
    VkAccelerationStructureInstanceKHR* instance=state->instance_data+slot;
    *instance=VkAccelerationStructureInstanceKHR();
    const float columns[3][3]={{x.x,x.y,x.z},{y.x,y.y,y.z},{z.x,z.y,z.z}};
    const float center[3]={position.x,position.y,position.z};
    for (uint32_t row=0;row<3;++row)
    {
        for (uint32_t column=0;column<3;++column) instance->transform.matrix[row][column]=columns[column][row];
        instance->transform.matrix[row][3]=center[row];
        float midpoint=center[row],radius=0;
        for (uint32_t column=0;column<3;++column)
        {
            const DynamicMesh* model=&state->dynamic_meshes[mesh];
            midpoint+=columns[column][row]*.5f*(model->low[column]+model->high[column]);
            radius+=std::fabs(columns[column][row])*.5f*(model->high[column]-model->low[column]);
        }
        radius+=std::fmax(.0001f,.00001f*(std::fabs(midpoint)+radius));
        uniform->scene_min[row]=std::fmin(uniform->scene_min[row],midpoint-radius);
        uniform->scene_max[row]=std::fmax(uniform->scene_max[row],midpoint+radius);
        uniform->foreground_min[row]=std::fmin(uniform->foreground_min[row],midpoint-radius);
        uniform->foreground_max[row]=std::fmax(uniform->foreground_max[row],midpoint+radius);
    }
    instance->mask=1;instance->flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance->instanceCustomIndex=state->dynamic_meshes[mesh].range.first;
    instance->accelerationStructureReference=state->dynamic_meshes[mesh].address;
}

static float unit_visual_scale(const Simulation* s,const Unit* unit)
{
    float size=s->world.ships[unit->model].scale;
    if (!s->world.combat.flight_enabled) return size;
    float t=std::fmin(1.0f,unit->age/.25f);t=t*t*(3-2*t);size*=.3f+.7f*t;
    if (unit->kind==UNIT_SEEDSHIP && unit->landing>0) size*=1-.8f*std::fmin(1.0f,unit->landing/s->world.gameplay.landing_seconds);
    return size;
}
static float node_visual_growth(const Simulation* s,uint32_t cell)
{
    float t=std::fmax(.02f,s->colonies[cell].growth);
    if (s->world.combat.flight_enabled) t=std::fmax(.02f,t*t*(3-2*t))*(1+.018f*std::sin((float)s->time_seconds*2.4f+cell));
    return t;
}
static Vec3 posed_direction(Vec3 local,Vec3 x,Vec3 y,Vec3 z)
{ return add(add(scale(x,local.x),scale(y,local.y)),scale(z,local.z)); }
static void spike_instance(RendererState* state,RenderUniform* uniform,uint32_t slot,Vec3 root,Vec3 direction,float length,float width,Color color)
{
    Vec3 axis=std::fabs(direction.y)<.95f ? Vec3{0,1,0} : Vec3{1,0,0};
    Vec3 x=normalize(cross(axis,direction)),y=cross(direction,x);
    set_dynamic_instance(state,slot,state->spike_mesh,root,scale(x,width),scale(y,width),scale(direction,length),uniform);
    float* style=state->style_data[slot];style[0]=color.r;style[1]=color.g;style[2]=color.b;style[3]=1;
}
static void update_spike_instances(RendererState* state,RenderUniform* uniform)
{
    const Simulation* s=state->simulation;const SpikeRules* r=&s->world.spikes;if (!r->enabled) return;
    state->attached_spikes=state->flying_spikes=state->embedded_spikes=0;
    uint32_t flying=SPIKE_INSTANCE_BASE+MAX_UNITS*r->count,embedded=flying+MAX_PROJECTILES;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* u=&s->units[i];if (u->health<=0) continue;
        float size=unit_visual_scale(s,u);Vec3 x,y,z;simulation_unit_axes(u,&x,&y,&z);
        Color color=u->flash>0 ? s->world.combat.flash : u->wounds ? r->injury[u->wounds-1] : s->world.combat.faction[u->faction];
        if (u->kind==UNIT_FIGHTER && u->faction==FACTION_PLAYER)
            for (uint32_t j=0;j<r->count;++j)
            {
                float growth=u->spikes[j];if (growth<=.001f) continue;
                Vec3 dir=posed_direction(simulation_spike_direction(j,r->count),x,y,z);
                spike_instance(state,uniform,SPIKE_INSTANCE_BASE+i*r->count+j,add(u->position,scale(dir,r->root*size)),dir,
                    size*r->length*growth,size*.5f*growth,color);++state->attached_spikes;
            }
        for (uint32_t j=0;j<u->wounds && j<MAX_EMBEDDED;++j)
        {
            Vec3 outward=posed_direction(u->embedded[j].direction,x,y,z);float length=std::fmax(r->scale,size*.45f);
            Vec3 root=add(add(u->position,scale(posed_direction(u->embedded[j].anchor,x,y,z),size)),scale(outward,length*.7f));
            spike_instance(state,uniform,embedded+i*2+j,root,scale(outward,-1),length,length*.5f,s->world.combat.faction[u->embedded[j].faction]);++state->embedded_spikes;
        }
    }
    for (uint32_t i=0;i<MAX_PROJECTILES;++i)
    {
        const Projectile* p=&s->projectiles[i];if (p->damage<=0) continue;
        float blend=std::fmin(1.0f,p->age/.06f),length=p->launch_scale+(r->scale-p->launch_scale)*blend;
        spike_instance(state,uniform,flying+i,p->position,p->direction,length,length*.5f,s->world.combat.faction[p->faction]);++state->flying_spikes;
    }
    for (uint32_t i=0;i<s->world.body_count;++i)
    {
        const Colony* c=&s->colonies[i];if (c->health<=0) continue;
        Vec3 tip=add(c->surface,scale(s->world.bodies[i].colony_normal,s->world.gameplay.stalk_height*node_visual_growth(s,i)));
        for (uint32_t j=0;j<c->wounds && j<MAX_EMBEDDED;++j)
        {
            Vec3 outward=c->embedded[j].direction;float length=r->scale*2;
            spike_instance(state,uniform,embedded+MAX_UNITS*2+i*2+j,add(add(tip,c->embedded[j].anchor),scale(outward,length*.7f)),scale(outward,-1),length,length*.5f,
                s->world.combat.faction[c->embedded[j].faction]);++state->embedded_spikes;
        }
        if (c->wounds)
        {
            Color tint=r->injury[c->wounds-1];float* style=state->style_data[2+MAX_UNITS+i];
            style[0]=tint.r;style[1]=tint.g;style[2]=tint.b;style[3]=.95f;
        }
    }
}

static void update_gameplay_instances(RendererState* state, RenderUniform* uniform)
{
    const Simulation* s=state->simulation;
    VkAccelerationStructureInstanceKHR* instances=state->instance_data;
    memcpy(uniform->scene_min,state->static_min,sizeof(state->static_min));
    memcpy(uniform->scene_max,state->static_max,sizeof(state->static_max));
    memcpy(uniform->foreground_min,state->foreground_min,sizeof(state->foreground_min));
    memcpy(uniform->foreground_max,state->foreground_max,sizeof(state->foreground_max));
    memset(state->style_data,0,sizeof(state->style_data));
    for (uint32_t slot=2;slot<state->instance_count;++slot) instances[slot].mask=0;
    for (uint32_t i=0;i<MAX_UNITS;++i)
    {
        const Unit* unit=&s->units[i];if (unit->health<=0) continue;
        const Ship* ship=&s->world.ships[unit->model];float yaw=ship->yaw_degrees*.01745329252f;
        if (unit->target>=0)
        {
            Vec3 delta=subtract(s->world.bodies[unit->target].position,unit->position);
            yaw=std::atan2(delta.x,delta.z);
        }
        if (s->world.combat.enabled)
        {
            Vec3 velocity=subtract(unit->position,s->trails[i].points[0]);
            if (dot(velocity,velocity)>.000001f) yaw=std::atan2(velocity.x,velocity.z);
            const CombatVisuals* fx=&s->world.combat;float max_health=unit->kind==UNIT_FIGHTER ? s->world.gameplay.fighter_health : s->world.gameplay.seed_health;
            bool flash=unit->flash>0;float flash_strength=flash ? std::sqrt(unit->flash/fx->flash_seconds) : 0;Color tint=flash ? fx->flash : fx->injured;
            if (!flash && s->world.spikes.enabled && unit->wounds) tint=s->world.spikes.injury[unit->wounds-1];
            float* style=state->style_data[2+i];style[0]=tint.r;style[1]=tint.g;style[2]=tint.b;
            style[3]=flash ? .9f*flash_strength : s->world.spikes.enabled ? (unit->wounds ? .95f : 0) : unit->health<max_health*fx->low_health ? .85f : 0;
            style[4]=flash ? .6f*flash_strength : 0;
        }
        if (s->world.combat.flight_enabled)
        {
            float size=unit_visual_scale(s,unit);Vec3 x,y,z;simulation_unit_axes(unit,&x,&y,&z);
            set_dynamic_instance(state,2+i,state->ship_meshes[unit->model],unit->position,scale(x,size),scale(y,size),scale(z,size),uniform);
            continue;
        }
        float c=std::cos(yaw)*ship->scale,v=std::sin(yaw)*ship->scale;
        set_dynamic_instance(state,2+i,state->ship_meshes[unit->model],unit->position,
            Vec3{c,0,-v},Vec3{0,ship->scale,0},Vec3{v,0,c},uniform);
    }
    for (uint32_t p=0;p<s->world.body_count;++p)
    {
        const Colony* colony=&s->colonies[p];if (colony->health<=0) continue;
        Vec3 normal=s->world.bodies[p].colony_normal;
        Vec3 axis=std::fabs(normal.z)<.9f ? Vec3{0,0,1} : Vec3{1,0,0};
        Vec3 tangent=normalize(cross(normal,axis)),bitangent=cross(tangent,normal);
        float growth=node_visual_growth(s,p);
        float width=s->world.gameplay.stalk_width*growth;
        set_dynamic_instance(state,2+MAX_UNITS+p,state->stalk_mesh,colony->surface,
            scale(tangent,width),scale(normal,s->world.gameplay.stalk_height*growth),scale(bitangent,width),uniform);
    }
    update_spike_instances(state,uniform);
}

bool renderer_render(Renderer* renderer, const World* world, const Camera* camera, const char* capture_path)
{
    RendererState* state = renderer->state;
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
    if (!begin_commands(state))
        return false;
    vkCmdResetQueryPool(state->command,state->queries,0,10);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,state->queries,0);
    RenderUniform* uniform = &state->uniform_data;
    Vec3 forward = normalize(subtract(camera->target, camera->position));
    Vec3 right = normalize(cross(forward, camera->up));
    store_vector(uniform->camera_position, camera->position);
    store_vector(uniform->camera_forward, forward);
    store_vector(uniform->camera_right, right);
    store_vector(uniform->camera_up, cross(right, forward));
    Vec3 translation = subtract(world->sun.position, state->sun_origin);
    store_vector(uniform->light, translation);
    store_vector(uniform->sun_sphere, world->sun.position);
    uniform->backdrop_wave[3]=(float)std::fmod(state->simulation->time_seconds,10000.0);
    if (world->backdrop_soft)
    {
        double phase=state->simulation->time_seconds*6.283185307179586/world->backdrop_period;
        uniform->backdrop_animation[0]=(float)std::sin(phase);
        uniform->backdrop_animation[1]=(float)std::cos(phase);
        uniform->backdrop_animation[2]=(float)std::fmod(phase,6.283185307179586);
        float pulse=world->backdrop_pulse;
        const float center[3]={world->backdrop.position.x,world->backdrop.position.y,world->backdrop.position.z};
        const float stretch[3]={1+pulse*(float)std::sin(phase),1+pulse*(float)std::sin(phase+.7),1};
        for (uint32_t axis=0;axis<3;++axis)
        {
            state->instance_data[1].transform.matrix[axis][axis]=stretch[axis];
            state->instance_data[1].transform.matrix[axis][3]=center[axis]*(1-stretch[axis]);
        }
    }
    if (world->gameplay.enabled || world->backdrop_soft || memcmp(&state->current_sun, &world->sun.position, sizeof(Vec3)) != 0)
    {
        VkAccelerationStructureInstanceKHR* instances = state->instance_data;
        if (!world->backdrop_soft)
        {
            instances[1].transform.matrix[0][3] = translation.x;
            instances[1].transform.matrix[1][3] = translation.y;
            instances[1].transform.matrix[2][3] = translation.z;
        }
        if (world->gameplay.enabled) update_gameplay_instances(state,uniform);
        memcpy(state->instances.mapped,instances,state->instances.size);
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
        before.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &before, 0, 0, 0, 0);
        state->build_as(state->command, 1, &build, &ranges);
        VkMemoryBarrier after = {};
        after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(state->command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &after, 0, 0, 0, 0);
        state->current_sun = world->sun.position;
    }
    memcpy(state->uniform.mapped,uniform,sizeof(*uniform));
    memcpy(state->instance_styles.mapped,state->style_data,sizeof(state->style_data));
    if (!effects_build(state->simulation,camera,&state->config,state->effect_data,&state->effect_count,
        state->effect_tile_data.data(),state->effect_tile_count)) return false;
    memcpy(state->effects.mapped,state->effect_data,state->effect_count*sizeof(EffectSegment));
    memcpy(state->effect_tiles.mapped,state->effect_tile_data.data(),state->effect_tiles.size);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,8);
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
    if (world->backdrop_soft)
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
            vkCmdPushConstants(state->command,state->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,parameters);
            vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);
            for (uint32_t i=0;i<2;++i)
                image_barrier(state->command,state->backdrop_images[i],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,7);
    if (renderer->debug_views || state->config.spatial_denoise || world->combat.enabled) shader_buffer_barrier(state->command);
    if (uniform->quality[3])
    {
        VkMemoryBarrier source_barrier = {};
        source_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        source_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        source_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&source_barrier,0,0,0,0);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->sun_source_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline_layout,
            0,1,&state->descriptors,0,0);
        vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);
        source_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        source_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(state->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&source_barrier,0,0,0,0);
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,1);
    vkCmdBindPipeline(state->command, VK_PIPELINE_BIND_POINT_COMPUTE,
        (renderer->debug_views || (world->combat.enabled && !state->config.spatial_denoise)) ? state->debug_surface_pipeline :
        state->config.spatial_denoise ? state->guide_surface_pipeline : state->pipeline);
    vkCmdBindDescriptorSets(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->pipeline_layout,
        0, 1, &state->descriptors, 0, 0);
    vkCmdDispatch(state->command, (state->config.width+7)/8, (state->config.height+7)/8, 1);
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,2);
    if (renderer->debug_views || state->config.spatial_denoise || world->combat.enabled) shader_buffer_barrier(state->command);
    if (uniform->quality[0] && uniform->scene[3])
    {
        image_barrier(state->command,state->hdr,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,
            renderer->debug_views ? state->debug_volume_pipeline :
            state->config.spatial_denoise ? state->guide_volume_pipeline : state->volume_pipeline);
        vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);
    }
    vkCmdWriteTimestamp(state->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state->queries, 3);
    if (renderer->debug_views || state->config.spatial_denoise || world->combat.enabled) shader_buffer_barrier(state->command);
    image_barrier(state->command, state->hdr, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (state->config.spatial_denoise)
    {
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->denoise_pipeline);
        for (uint32_t pass=0; pass<3; ++pass)
        {
            int32_t parameters[]={(int32_t)state->config.width,(int32_t)state->config.height,(int32_t)(1u<<pass),0};
            vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->denoise_layout,
                0,1,&state->denoise_descriptors[pass%2],0,0);
            vkCmdPushConstants(state->command,state->denoise_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),parameters);
            vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);
            // Both ping-pong images change read/write roles at each phase boundary.
            for (uint32_t i=0; i<2; ++i)
                image_barrier(state->command,hdr_images[i],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,4);
    if (world->combat.enabled && state->effect_count)
    {
        VkImage beauty=state->config.spatial_denoise ? state->hdr_scratch : state->hdr;
        image_barrier(state->command,beauty,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->effects_pipeline);
        vkCmdBindDescriptorSets(state->command,VK_PIPELINE_BIND_POINT_COMPUTE,state->pipeline_layout,0,1,&state->effect_descriptors,0,0);
        vkCmdDispatch(state->command,(state->config.width+7)/8,(state->config.height+7)/8,1);
        image_barrier(state->command,beauty,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,9);
    BloomParameters bloom = {};
    bloom.extent[0] = (int32_t)state->config.width;
    bloom.extent[1] = (int32_t)state->config.height;
    bloom.radius = (int32_t)state->config.bloom_radius;
    bloom.threshold = state->config.bloom_threshold;
    bloom.strength = state->config.bloom_strength;
    bloom.exposure = state->config.exposure;
    bloom.debug_views = renderer->debug_views ? 1.0f : 0.0f;
    vkCmdBindPipeline(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->bloom_pipeline);
    vkCmdBindDescriptorSets(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->bloom_layout, 0, 1, &state->bloom_descriptors, 0, 0);
    vkCmdPushConstants(state->command, state->bloom_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bloom), &bloom);
    vkCmdDispatch(state->command, (state->config.width+7)/8, (state->config.height+7)/8, 1);
    image_barrier(state->command, state->config.spatial_denoise ? state->hdr : state->hdr_scratch, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    bloom.pass = 1;
    vkCmdPushConstants(state->command, state->bloom_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bloom), &bloom);
    vkCmdDispatch(state->command, (state->config.width+7)/8, (state->config.height+7)/8, 1);
    vkCmdWriteTimestamp(state->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state->queries, 5);
    if (renderer->debug_stats || renderer->debug_views || renderer->gameplay_hud)
    {
        shader_buffer_barrier(state->command);
        image_barrier(state->command,state->output,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        draw_debug(state,renderer);
    }
    vkCmdWriteTimestamp(state->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state->queries,6);
    if (capture_path || !state->headless)
        copy_output(state,image_index,capture_path);
    if (!submit_commands(state, !state->headless))
        return false;
    uint64_t timestamps[10];
    if (!vk_ok(vkGetQueryPoolResults(state->device, state->queries, 0, 10, sizeof(timestamps),
        timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "Read GPU timestamps"))
        return false;
    uint64_t difference = timestamps[3] - timestamps[8];
    if (state->timestamp_bits < 64)
        difference &= (UINT64_C(1) << state->timestamp_bits) - 1;
    double milliseconds = difference * state->properties.limits.timestampPeriod / 1000000.0;
    printf("GPU trace: %.3f ms; %u spp; %u triangles\n", milliseconds,
        state->config.samples_per_pixel, state->triangle_count);
    uint64_t source_ticks = timestamps[1]-timestamps[7], surface_ticks = timestamps[2]-timestamps[1], volume_ticks = timestamps[3]-timestamps[2];
    if (state->timestamp_bits < 64)
    {
        source_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
        surface_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
        volume_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    }
    uint64_t background_ticks=timestamps[7]-timestamps[8];
    if (state->timestamp_bits<64) background_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    printf("GPU backdrop: %.3f ms; separate current-frame layer %u.\n",
        background_ticks*state->properties.limits.timestampPeriod/1000000.0,world->backdrop_soft ? 1u : 0u);
    printf("GPU sun sources: %.3f ms.\n",source_ticks*state->properties.limits.timestampPeriod/1000000.0);
    printf("GPU surface: %.3f ms; GPU volumes: %.3f ms.\n",
        surface_ticks*state->properties.limits.timestampPeriod/1000000.0,
        volume_ticks*state->properties.limits.timestampPeriod/1000000.0);
    uint64_t denoise_ticks = timestamps[4]-timestamps[3];
    if (state->timestamp_bits < 64) denoise_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    double denoise_ms = denoise_ticks*state->properties.limits.timestampPeriod/1000000.0;
    printf("GPU spatial denoise: %.3f ms; enabled %u.\n",denoise_ms,state->config.spatial_denoise);
    uint64_t bloom_ticks = timestamps[5] - timestamps[9];
    if (state->timestamp_bits < 64)
        bloom_ticks &= (UINT64_C(1) << state->timestamp_bits) - 1;
    double bloom_ms = bloom_ticks * state->properties.limits.timestampPeriod / 1000000.0;
    uint64_t debug_ticks = timestamps[6]-timestamps[5];
    if (state->timestamp_bits < 64) debug_ticks &= (UINT64_C(1)<<state->timestamp_bits)-1;
    double debug_ms = debug_ticks*state->properties.limits.timestampPeriod/1000000.0;
    uint64_t gpu_ticks=timestamps[6]-timestamps[0],as_ticks=timestamps[8]-timestamps[0],fx_ticks=timestamps[9]-timestamps[4];
    if (state->timestamp_bits<64) { uint64_t mask=(UINT64_C(1)<<state->timestamp_bits)-1;gpu_ticks&=mask;as_ticks&=mask;fx_ticks&=mask; }
    double scale=state->properties.limits.timestampPeriod/1000000.0;
    renderer->gpu_frame_ms=gpu_ticks*scale;
    if (state->simulation->world.spikes.enabled)
        printf("Spike instances: attached %u; flying %u; embedded %u.\n",state->attached_spikes,state->flying_spikes,state->embedded_spikes);
    printf("GPU acceleration update: %.3f ms; GPU effects: %.3f ms.\n",as_ticks*scale,fx_ticks*scale);
    printf("GPU bloom/display: %.3f ms; total render: %.3f ms\n",bloom_ms,renderer->gpu_frame_ms);
    printf("GPU debug overlay: %.3f ms; buffer views %u; FPS stats %u.\n",debug_ms,
        renderer->debug_views ? 1u : 0u,renderer->debug_stats ? 1u : 0u);
    state->denoise_ms = denoise_ms;
    state->trace_ms = milliseconds;
    state->volume_ms = volume_ticks*state->properties.limits.timestampPeriod/1000000.0;
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
    renderer->acquire_ms=acquire_ms;renderer->present_ms=present_ms;
    printf("Presentation waits: acquire %.3f ms; present %.3f ms; GPU work %.3f ms.\n",renderer->acquire_ms,renderer->present_ms,renderer->gpu_frame_ms);
    if (capture_path && !capture_image(state, capture_path))
        return false;
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
        if (state->denoise_pipeline) vkDestroyPipeline(state->device,state->denoise_pipeline,0);
        if (state->guide_surface_pipeline) vkDestroyPipeline(state->device,state->guide_surface_pipeline,0);
        if (state->guide_volume_pipeline) vkDestroyPipeline(state->device,state->guide_volume_pipeline,0);
        if (state->denoise_layout) vkDestroyPipelineLayout(state->device,state->denoise_layout,0);
        if (state->denoise_descriptor_pool) vkDestroyDescriptorPool(state->device,state->denoise_descriptor_pool,0);
        if (state->denoise_descriptor_layout) vkDestroyDescriptorSetLayout(state->device,state->denoise_descriptor_layout,0);
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
        if (state->volume_pipeline) vkDestroyPipeline(state->device,state->volume_pipeline,0);
        if (state->debug_surface_pipeline) vkDestroyPipeline(state->device,state->debug_surface_pipeline,0);
        if (state->debug_volume_pipeline) vkDestroyPipeline(state->device,state->debug_volume_pipeline,0);
        if (state->debug_overlay_pipeline) vkDestroyPipeline(state->device,state->debug_overlay_pipeline,0);
        if (state->debug_overlay_layout) vkDestroyPipelineLayout(state->device,state->debug_overlay_layout,0);
        if (state->sun_source_pipeline) vkDestroyPipeline(state->device,state->sun_source_pipeline,0);
        if (state->pipeline_layout) vkDestroyPipelineLayout(state->device, state->pipeline_layout, 0);
        if (state->descriptor_pool) vkDestroyDescriptorPool(state->device, state->descriptor_pool, 0);
        if (state->descriptor_layout) vkDestroyDescriptorSetLayout(state->device, state->descriptor_layout, 0);
        for (uint32_t i=0;i<state->mesh_count;++i)
        {
            DynamicMesh* mesh=&state->dynamic_meshes[i];
            if (mesh->acceleration) state->destroy_as(state->device,mesh->acceleration,0);
            destroy_buffer(state,&mesh->storage);
        }
        if (state->tlas) state->destroy_as(state->device, state->tlas, 0);
        if (state->blas) state->destroy_as(state->device, state->blas, 0);
        if (state->backdrop_blas) state->destroy_as(state->device,state->backdrop_blas,0);
        if (state->sun_blas) state->destroy_as(state->device, state->sun_blas, 0);
        GpuBuffer* buffers[] = { &state->vertices, &state->triangles, &state->indices, &state->instances,
            &state->blas_buffer, &state->sun_blas_buffer, &state->backdrop_blas_buffer, &state->tlas_buffer, &state->tlas_scratch,
            &state->uniform, &state->readback, &state->media, &state->energy, &state->sampling, &state->sun_sources,
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
