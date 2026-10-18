// Diagnostic Vulkan layer. This is a host capture tool, not a game target.
#include <stdbool.h>
#include <vulkan/vk_layer.h>
#include <renderdoc_app.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// renderer.cpp resets this complete timestamp pool at each game frame start.
static const uint32_t frame_query_count=16;
static const uint32_t capture_frame=2;
static RENDERDOC_API_1_6_0* capture_api;
static void* capture_library;
static uint32_t frames;
static bool capturing;
static PFN_vkCmdResetQueryPool reset_queries_next;
static PFN_vkWaitForFences wait_fences_next;

__attribute__((constructor)) static void capture_init(void)
{
    const char* library=getenv("NANOVA_RENDERDOC_LIBRARY");
    const char* output=getenv("NANOVA_RENDERDOC_CAPTURE");
    if (!library || !output) return;
    capture_library=dlopen(library,RTLD_NOW|RTLD_GLOBAL);
    if (!capture_library) { fprintf(stderr,"RenderDoc load failed: %s\n",dlerror());return; }
    pRENDERDOC_GetAPI get_api=(pRENDERDOC_GetAPI)dlsym(capture_library,"RENDERDOC_GetAPI");
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_6_0,(void**)&capture_api)) return;
    capture_api->SetCaptureFilePathTemplate(output);
    capture_api->MaskOverlayBits(0,0);
}

static void VKAPI_CALL reset_queries(VkCommandBuffer command,VkQueryPool pool,uint32_t first,uint32_t count)
{
    if (first==0 && count==frame_query_count && ++frames==capture_frame && capture_api)
    {
        capture_api->StartFrameCapture(0,0);
        capturing=capture_api->IsFrameCapturing()!=0;
        fprintf(stderr,"RenderDoc started second headless frame: %u.\n",capturing ? 1u : 0u);
    }
    reset_queries_next(command,pool,first,count);
}

static VkResult VKAPI_CALL wait_fences(VkDevice device,uint32_t count,const VkFence* fences,VkBool32 all,uint64_t timeout)
{
    VkResult result=wait_fences_next(device,count,fences,all,timeout);
    if (capturing && result==VK_SUCCESS)
    {
        uint32_t ok=capture_api->EndFrameCapture(0,0);capturing=false;
        fprintf(stderr,"RenderDoc ended second headless frame: %u.\n",ok);
    }
    return result;
}

static PFN_vkVoidFunction hook_function(const char* name,PFN_vkVoidFunction function)
{
    if (!function) return 0;
    if (!strcmp(name,"vkCmdResetQueryPool"))
    { reset_queries_next=(PFN_vkCmdResetQueryPool)function;return (PFN_vkVoidFunction)reset_queries; }
    if (!strcmp(name,"vkWaitForFences"))
    { wait_fences_next=(PFN_vkWaitForFences)function;return (PFN_vkVoidFunction)wait_fences; }
    return function;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL VK_LAYER_RENDERDOC_CaptureGetInstanceProcAddr(VkInstance instance,const char* name)
{
    PFN_vkGetInstanceProcAddr next=(PFN_vkGetInstanceProcAddr)dlsym(capture_library,"VK_LAYER_RENDERDOC_CaptureGetInstanceProcAddr");
    return hook_function(name,next ? next(instance,name) : 0);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL VK_LAYER_RENDERDOC_CaptureGetDeviceProcAddr(VkDevice device,const char* name)
{
    PFN_vkGetDeviceProcAddr next=(PFN_vkGetDeviceProcAddr)dlsym(capture_library,"VK_LAYER_RENDERDOC_CaptureGetDeviceProcAddr");
    return hook_function(name,next ? next(device,name) : 0);
}

VKAPI_ATTR VkResult VKAPI_CALL VK_LAYER_RENDERDOC_CaptureNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info)
{
    PFN_vkNegotiateLoaderLayerInterfaceVersion next=(PFN_vkNegotiateLoaderLayerInterfaceVersion)
        dlsym(capture_library,"VK_LAYER_RENDERDOC_CaptureNegotiateLoaderLayerInterfaceVersion");
    VkResult result=next ? next(info) : VK_ERROR_INITIALIZATION_FAILED;
    if (result==VK_SUCCESS)
    {
        info->pfnGetInstanceProcAddr=VK_LAYER_RENDERDOC_CaptureGetInstanceProcAddr;
        info->pfnGetDeviceProcAddr=VK_LAYER_RENDERDOC_CaptureGetDeviceProcAddr;
    }
    return result;
}
