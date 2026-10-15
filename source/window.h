#ifndef MICROSHIP_WINDOW_H
#define MICROSHIP_WINDOW_H

#include <stdint.h>
#include "camera.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct Window
{
    HINSTANCE instance;
    HWND handle;
    bool open;
    bool dragging;
    int32_t pointer_x, pointer_y;
    int32_t press_x, press_y;
    bool moved;
    PointerInput input;
};

bool window_open(Window* window, uint32_t width, uint32_t height, bool activate);
bool window_poll(Window* window, PointerInput* input);
void window_close(Window* window);

#endif
