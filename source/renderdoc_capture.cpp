#include "renderdoc_capture.h"
#include <dlfcn.h>
#include <errno.h>
#include <renderdoc_app.h>
#include <stdio.h>
#include <stdlib.h>

static void* capture_module;
static RENDERDOC_API_1_6_0* capture_api;
static uint64_t capture_frame;
static bool capturing;

bool renderdoc_capture_initialize()
{
	const char* requested = getenv("NANOVORE_RENDERDOC_CAPTURE_FRAME");
	if (!requested)
		return true;
	char* end = 0;
	errno = 0;
	unsigned long frame = strtoul(requested, &end, 10);
	if (errno || !*requested || *end || frame == 0 || frame > UINT32_MAX ||
	    *requested == '-')
	{
		fprintf(stderr, "Invalid RenderDoc capture frame.\n");
		return false;
	}
	// Only use a module loaded by the standard RenderDoc launcher.
	capture_module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
	pRENDERDOC_GetAPI get_api =
	    capture_module
	        ? (pRENDERDOC_GetAPI)dlsym(capture_module, "RENDERDOC_GetAPI")
	        : 0;
	if (!get_api ||
	    !get_api(eRENDERDOC_API_Version_1_6_0, (void**)&capture_api))
	{
		fprintf(stderr,
		        "RenderDoc capture requires the native RenderDoc launcher.\n");
		return false;
	}
	capture_frame = frame;
	capture_api->MaskOverlayBits(0, 0);
	capture_api->SetCaptureKeys(0, 0);
	capture_api->SetFocusToggleKeys(0, 0);
	return true;
}

bool renderdoc_capture_requested(uint64_t frame)
{
	return capture_api && frame == capture_frame;
}

bool renderdoc_capture_begin()
{
	capture_api->StartFrameCapture(0, 0);
	capturing = capture_api->IsFrameCapturing() != 0;
	if (!capturing)
		fprintf(stderr,
		        "RenderDoc did not start the requested native frame.\n");
	return capturing;
}

bool renderdoc_capture_active() { return capturing; }

bool renderdoc_capture_end()
{
	bool saved = capture_api->EndFrameCapture(0, 0) != 0;
	capturing = false;
	printf("RenderDoc native frame %u saved: %u.\n", (unsigned)capture_frame,
	       saved ? 1u : 0u);
	return saved;
}

void renderdoc_capture_shutdown()
{
	if (capturing)
		capture_api->DiscardFrameCapture(0, 0);
	if (capture_module)
		dlclose(capture_module);
	capture_module = 0;
	capture_api = 0;
	capture_frame = 0;
	capturing = false;
}
