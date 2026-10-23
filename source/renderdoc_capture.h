#ifndef NANOVORE_RENDERDOC_CAPTURE_H
#define NANOVORE_RENDERDOC_CAPTURE_H
#include <stdint.h>
bool renderdoc_capture_initialize();
bool renderdoc_capture_requested(uint64_t frame);
bool renderdoc_capture_begin();
bool renderdoc_capture_active();
bool renderdoc_capture_end();
void renderdoc_capture_shutdown();
#endif
