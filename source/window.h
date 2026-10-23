#ifndef NANOVORE_WINDOW_H
#define NANOVORE_WINDOW_H

#include "camera.h"
#include <stdint.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct Window
{
#ifdef _WIN32
	HINSTANCE instance;
	HWND handle;
#endif
	bool open, suspended;
	bool dragging, panning;
	bool stats_held, buffers_held;
	int32_t pan_x, pan_y;
	uint32_t camera_keys, render_keys;
	int32_t pointer_x, pointer_y;
	int32_t press_x, press_y;
	bool moved;
	PointerInput input;
};

bool window_open(Window* window, uint32_t width, uint32_t height, bool activate,
                 bool borderless = false);
bool window_poll(Window* window, PointerInput* input);
void window_suspend_input(Window*, bool suspended);
void window_close(Window* window);

#endif
