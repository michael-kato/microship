#include "window.h"

#include <stdio.h>
#include <stdlib.h>

static const int DRAG_THRESHOLD_PIXELS = 5;
static const int KEY_WAS_DOWN_BIT = 30;
static const int KEY_PRESSED_MASK = 0x8000;

static LRESULT CALLBACK window_message(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    Window* window = (Window*)GetWindowLongPtrA(handle, GWLP_USERDATA);
    if (message == WM_NCCREATE)
    {
        window = (Window*)((CREATESTRUCTA*)lparam)->lpCreateParams;
        SetWindowLongPtrA(handle, GWLP_USERDATA, (LONG_PTR)window);
    }
    if (window && (message == WM_CLOSE || (message == WM_KEYDOWN && wparam == VK_ESCAPE)))
    {
        window->open = false;
        return 0;
    }
    if (window && message == WM_DESTROY)
    {
        window->open = false;
        window->handle = 0;
        return 0;
    }
    if (window && message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        BeginPaint(handle,&paint);
        EndPaint(handle,&paint);
        return 0;
    }
    if (window && message == WM_ERASEBKGND) return 1;
    if (window && message==WM_RBUTTONDOWN)
    {
        window->panning=true;window->pointer_x=(short)LOWORD(lparam);window->pointer_y=(short)HIWORD(lparam);
        SetCapture(handle);return 0;
    }
    if (window && message==WM_RBUTTONUP)
    { window->panning=false;if (!window->dragging) ReleaseCapture();return 0; }
    if (window && message == WM_LBUTTONDOWN)
    {
        window->dragging = true;window->input.pointer_pressed=true;
        window->pointer_x = (short)LOWORD(lparam);
        window->pointer_y = (short)HIWORD(lparam);
        window->press_x=window->pointer_x;window->press_y=window->pointer_y;window->moved=false;
        SetCapture(handle);
        return 0;
    }
    if (window && (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED))
    {
        if (message==WM_LBUTTONUP && window->dragging && !window->moved)
        { window->input.click=true;window->input.click_x=(short)LOWORD(lparam);window->input.click_y=(short)HIWORD(lparam); }
        if (message==WM_LBUTTONUP)
        {
            window->input.pointer_released=true;
            window->input.pointer_x=(short)LOWORD(lparam);window->input.pointer_y=(short)HIWORD(lparam);window->input.pointer_valid=true;
        }
        else if (window->dragging) window->input.pointer_cancelled=true;
        if (message==WM_CAPTURECHANGED) window->panning=false;
        window->dragging = false;
        if (message == WM_LBUTTONUP && !window->panning) ReleaseCapture();
        return 0;
    }
    if (window && message == WM_MOUSEMOVE)
    {
        int32_t x = (short)LOWORD(lparam), y = (short)HIWORD(lparam);
        window->input.pointer_x=x;window->input.pointer_y=y;window->input.pointer_valid=true;
        if (!window->dragging && !window->panning) return 0;
        if (abs(x-window->press_x)+abs(y-window->press_y)>DRAG_THRESHOLD_PIXELS) window->moved=true;
        if (window->panning)
        { window->input.drag_x += x - window->pointer_x;window->input.drag_y += y - window->pointer_y; }
        window->pointer_x = x;
        window->pointer_y = y;
        return 0;
    }
    if (window && message == WM_MOUSEWHEEL)
    {
        POINT point={(short)LOWORD(lparam),(short)HIWORD(lparam)};ScreenToClient(handle,&point);
        window->input.pointer_x=point.x;window->input.pointer_y=point.y;window->input.pointer_valid=true;
        window->input.wheel += (short)HIWORD(wparam) / (float)WHEEL_DELTA;
        return 0;
    }
    if (window && message==WM_MBUTTONUP)
    { window->input.command=true;window->input.command_x=(short)LOWORD(lparam);window->input.command_y=(short)HIWORD(lparam);return 0; }
    if (window && message == WM_KEYDOWN && !(lparam & (1L << KEY_WAS_DOWN_BIT)))
    {
        if (wparam == VK_F1) { window->input.toggle_stats = !window->input.toggle_stats; return 0; }
        if (wparam == VK_F2) { window->input.toggle_buffers = !window->input.toggle_buffers; return 0; }
        if (wparam>='1' && wparam<='3') { window->input.group=(uint32_t)(wparam-'0');return 0; }
        if (wparam=='F' || wparam=='S') { window->input.priority=wparam=='F' ? 1u : 2u;return 0; }
        if (wparam==VK_SPACE) { window->input.pause=true;return 0; }
        if (wparam=='R') { window->input.restart=true;return 0; }
        if (wparam==VK_HOME) { window->input.focus=true;return 0; }
    }
    return DefWindowProcA(handle, message, wparam, lparam);
}

bool window_open(Window* window, uint32_t width, uint32_t height, bool activate)
{
    // Physical pixel coordinates: Windows display scaling must not upscale the image.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
        && GetLastError() != ERROR_ACCESS_DENIED)
    {
        fprintf(stderr, "Cannot enable native-pixel DPI awareness (Windows error %lu).\n", GetLastError());
        return false;
    }
    window->instance = GetModuleHandleA(0);
    WNDCLASSA klass = {};
    klass.lpfnWndProc = window_message;
    klass.hInstance = window->instance;
    klass.hCursor = LoadCursorA(0, IDC_ARROW);
    klass.lpszClassName = "nanovaWindow";
    if (!RegisterClassA(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        fprintf(stderr, "Cannot register nanova window (Windows error %lu).\n", GetLastError());
        return false;
    }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD extended = activate ? 0 : WS_EX_NOACTIVATE;
    RECT extent = { 0, 0, (LONG)width, (LONG)height };
    if (!AdjustWindowRectExForDpi(&extent, style, FALSE, extended, GetDpiForSystem()))
        return false;
    window->handle = CreateWindowExA(extended, klass.lpszClassName, "Nanova", style,
        CW_USEDEFAULT, CW_USEDEFAULT, extent.right - extent.left, extent.bottom - extent.top,
        0, 0, window->instance, window);
    if (!window->handle)
    {
        fprintf(stderr, "Cannot create nanova window (Windows error %lu).\n", GetLastError());
        return false;
    }
    RECT client;
    if (!GetClientRect(window->handle, &client) || client.right != (LONG)width || client.bottom != (LONG)height)
    {
        fprintf(stderr, "Window client size differs from native render resolution.\n");
        return false;
    }
    window->open = true;
    ShowWindow(window->handle, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    return true;
}

bool window_poll(Window* window, PointerInput* input)
{
    MSG message;
    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
            window->open = false;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    *input = window->input;
    input->pointer_held=window->dragging;input->pointer_moved=window->moved;input->press_x=window->press_x;input->press_y=window->press_y;
    input->additive=(GetKeyState(VK_SHIFT)&KEY_PRESSED_MASK)!=0;
    POINT point;
    if (GetCursorPos(&point) && ScreenToClient(window->handle,&point))
    { input->pointer_x=point.x;input->pointer_y=point.y;input->pointer_valid=true; }
    window->input = PointerInput();
    return window->open;
}

void window_close(Window* window)
{
    if (window->handle)
        DestroyWindow(window->handle);
    if (window->instance)
        UnregisterClassA("nanovaWindow", window->instance);
    *window = Window();
}

