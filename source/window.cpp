#include "window.h"

#include <stdio.h>
#include <stdlib.h>

static const int DRAG_THRESHOLD_PIXELS = 5;
static const int KEY_WAS_DOWN_BIT = 30;
static const int KEY_PRESSED_MASK = 0x8000;
enum
{
	CAMERA_A = 1u << 0,
	CAMERA_D = 1u << 1,
	CAMERA_W = 1u << 2,
	CAMERA_S = 1u << 3,
	CAMERA_LEFT = 1u << 4,
	CAMERA_RIGHT = 1u << 5,
	CAMERA_UP = 1u << 6,
	CAMERA_DOWN = 1u << 7
};
static uint32_t camera_key(WPARAM key)
{
	switch (key)
	{
	case 'A':
		return CAMERA_A;
	case 'D':
		return CAMERA_D;
	case 'W':
		return CAMERA_W;
	case 'S':
		return CAMERA_S;
	case VK_LEFT:
		return CAMERA_LEFT;
	case VK_RIGHT:
		return CAMERA_RIGHT;
	case VK_UP:
		return CAMERA_UP;
	case VK_DOWN:
		return CAMERA_DOWN;
	default:
		return 0;
	}
}

static LRESULT CALLBACK window_message(HWND handle, UINT message, WPARAM wparam,
                                       LPARAM lparam)
{
	Window* window = (Window*)GetWindowLongPtrA(handle, GWLP_USERDATA);
	if (message == WM_NCCREATE)
	{
		window = (Window*)((CREATESTRUCTA*)lparam)->lpCreateParams;
		SetWindowLongPtrA(handle, GWLP_USERDATA, (LONG_PTR)window);
	}
	if (window && message == WM_CLOSE)
	{
		window->open = false;
		return 0;
	}
	if (window && window->suspended &&
	    ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
	     (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)))
		return 0;
	if (window && message == WM_DESTROY)
	{
		window->open = false;
		window->handle = 0;
		return 0;
	}
	if (window && message == WM_PAINT)
	{
		PAINTSTRUCT paint;
		BeginPaint(handle, &paint);
		EndPaint(handle, &paint);
		return 0;
	}
	if (window && message == WM_ERASEBKGND)
		return 1;
	if (window && message == WM_LBUTTONDOWN)
	{
		window->dragging = true;
		window->input.pointer_pressed = true;
		window->pointer_x = (short)LOWORD(lparam);
		window->pointer_y = (short)HIWORD(lparam);
		window->press_x = window->pointer_x;
		window->press_y = window->pointer_y;
		window->moved = false;
		SetCapture(handle);
		return 0;
	}
	if (window && message == WM_MBUTTONDOWN)
	{
		window->panning = true;
		window->pan_x = (short)LOWORD(lparam);
		window->pan_y = (short)HIWORD(lparam);
		SetCapture(handle);
		return 0;
	}
	if (window && message == WM_MBUTTONUP)
	{
		if (window->panning)
		{
			window->input.drag_x +=
			    (float)((short)LOWORD(lparam) - window->pan_x);
			window->input.drag_y +=
			    (float)((short)HIWORD(lparam) - window->pan_y);
		}
		window->panning = false;
		if (!window->dragging)
			ReleaseCapture();
		return 0;
	}
	if (window && (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED))
	{
		if (message == WM_LBUTTONUP && window->dragging && !window->moved)
		{
			window->input.click = true;
			window->input.click_x = (short)LOWORD(lparam);
			window->input.click_y = (short)HIWORD(lparam);
		}
		if (message == WM_LBUTTONUP)
		{
			window->input.pointer_released = true;
			window->input.pointer_x = (short)LOWORD(lparam);
			window->input.pointer_y = (short)HIWORD(lparam);
			window->input.pointer_valid = true;
		}
		else
		{
			if (window->dragging)
				window->input.pointer_cancelled = true;
			window->panning = false;
		}
		window->dragging = false;
		if (message == WM_LBUTTONUP && !window->panning)
			ReleaseCapture();
		return 0;
	}
	if (window && message == WM_MOUSEMOVE)
	{
		int32_t x = (short)LOWORD(lparam), y = (short)HIWORD(lparam);
		window->input.pointer_x = x;
		window->input.pointer_y = y;
		window->input.pointer_valid = true;
		if (window->panning)
		{
			window->input.drag_x += (float)(x - window->pan_x);
			window->input.drag_y += (float)(y - window->pan_y);
			window->pan_x = x;
			window->pan_y = y;
		}
		if (!window->dragging)
			return 0;
		if (abs(x - window->press_x) + abs(y - window->press_y) >
		    DRAG_THRESHOLD_PIXELS)
			window->moved = true;
		window->pointer_x = x;
		window->pointer_y = y;
		return 0;
	}
	if (window && message == WM_MOUSEWHEEL)
	{
		POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
		ScreenToClient(handle, &point);
		window->input.pointer_x = point.x;
		window->input.pointer_y = point.y;
		window->input.pointer_valid = true;
		window->input.wheel += (short)HIWORD(wparam) / (float)WHEEL_DELTA;
		return 0;
	}
	if (window && message == WM_RBUTTONUP)
	{
		window->input.command = true;
		window->input.command_x = (short)LOWORD(lparam);
		window->input.command_y = (short)HIWORD(lparam);
		return 0;
	}
	if (window &&
	    (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wparam)))
	{
		window->camera_keys = 0;
		window->render_keys = 0;
		window->panning = false;
		window->stats_held = false;
		window->buffers_held = false;
		if (GetCapture() == handle)
			ReleaseCapture();
	}
	// F10 is a system key on Win32. Consume only its system messages here.
	if (window && ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
	               wparam == VK_F10))
		message = message == WM_SYSKEYDOWN ? WM_KEYDOWN : WM_KEYUP;
	if (window && (message == WM_KEYDOWN || message == WM_KEYUP))
	{
#ifdef DEVELOPMENT
		if (wparam == VK_F3)
		{
			if (message == WM_KEYDOWN && !(lparam & (1L << KEY_WAS_DOWN_BIT)))
				window->input.toggle_wireframe = true;
			return 0;
		}
		if (wparam == VK_F1 || wparam == VK_F2)
		{
			bool* held =
			    wparam == VK_F1 ? &window->stats_held : &window->buffers_held;
			bool* toggle = wparam == VK_F1 ? &window->input.toggle_stats
			                               : &window->input.toggle_buffers;
			if (message == WM_KEYUP)
				*held = false;
			else
			{
				if (!*held && !(lparam & (1L << KEY_WAS_DOWN_BIT)))
					*toggle = !*toggle;
				*held = true;
			}
			return 0;
		}
		if (wparam >= VK_F5 && wparam <= VK_F10)
		{
			uint32_t bit = 1u << (wparam - VK_F5);
			if (message == WM_KEYUP)
				window->render_keys &= ~bit;
			else
			{
				if (!(window->render_keys & bit) &&
				    !(lparam & (1L << KEY_WAS_DOWN_BIT)))
					window->input.toggle_render_features ^= bit;
				window->render_keys |= bit;
			}
			return 0;
		}
#endif
		uint32_t key = camera_key(wparam);
		if (key)
		{
			if (message == WM_KEYDOWN)
			{
				if (!(lparam & (1L << KEY_WAS_DOWN_BIT)))
					window->camera_keys |= key;
			}
			else
				window->camera_keys &= ~key;
			return 0;
		}
	}
	if (window && message == WM_KEYDOWN && !(lparam & (1L << KEY_WAS_DOWN_BIT)))
	{
		if (wparam == VK_ESCAPE)
		{
			window->input.menu = true;
			return 0;
		}
		if (wparam >= '1' && wparam <= '3')
		{
			window->input.group = (uint32_t)(wparam - '0');
			return 0;
		}
		if (wparam == 'F' || wparam == 'B')
		{
			window->input.screen_group = wparam == 'F' ? 1u : 2u;
			return 0;
		}
		if (wparam == VK_SPACE)
		{
			window->input.pause = true;
			return 0;
		}
		if (wparam == 'R')
		{
			window->input.restart = true;
			return 0;
		}
		if (wparam == VK_HOME)
		{
			window->input.focus = true;
			return 0;
		}
	}
	return DefWindowProcA(handle, message, wparam, lparam);
}

bool window_open(Window* window, uint32_t width, uint32_t height, bool activate,
                 bool borderless)
{
	// Physical pixel coordinates: Windows display scaling must not upscale the
	// image.
	if (!SetProcessDpiAwarenessContext(
	        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
	    GetLastError() != ERROR_ACCESS_DENIED)
	{
		fprintf(
		    stderr,
		    "Cannot enable native-pixel DPI awareness (Windows error %lu).\n",
		    GetLastError());
		return false;
	}
	window->instance = GetModuleHandleA(0);
	WNDCLASSA klass = {};
	klass.lpfnWndProc = window_message;
	klass.hInstance = window->instance;
	klass.hCursor = LoadCursorA(0, IDC_ARROW);
	klass.lpszClassName = "nanovoreWindow";
	if (!RegisterClassA(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		fprintf(stderr,
		        "Cannot register nanovore window (Windows error %lu).\n",
		        GetLastError());
		return false;
	}
	DWORD style =
	    borderless ? WS_POPUP
		           : WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	DWORD extended = activate ? 0 : WS_EX_NOACTIVATE;
	RECT extent = {0, 0, (LONG)width, (LONG)height};
	if (!AdjustWindowRectExForDpi(&extent, style, FALSE, extended,
	                              GetDpiForSystem()))
		return false;
	window->handle = CreateWindowExA(
	    extended, klass.lpszClassName, "Nanovore", style,
	    borderless ? 0 : CW_USEDEFAULT, borderless ? 0 : CW_USEDEFAULT,
	    extent.right - extent.left, extent.bottom - extent.top, 0, 0,
	    window->instance, window);
	if (!window->handle)
	{
		fprintf(stderr, "Cannot create nanovore window (Windows error %lu).\n",
		        GetLastError());
		return false;
	}
	RECT client;
	if (!GetClientRect(window->handle, &client) ||
	    client.right != (LONG)width || client.bottom != (LONG)height)
	{
		fprintf(stderr,
		        "Window client size differs from native render resolution.\n");
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
	input->pointer_held = window->dragging;
	input->pointer_moved = window->moved;
	input->press_x = window->press_x;
	input->press_y = window->press_y;
	input->additive = (GetKeyState(VK_SHIFT) & KEY_PRESSED_MASK) != 0;
	bool left = (window->camera_keys & (CAMERA_A | CAMERA_LEFT)) != 0,
	     right = (window->camera_keys & (CAMERA_D | CAMERA_RIGHT)) != 0;
	bool up = (window->camera_keys & (CAMERA_W | CAMERA_UP)) != 0,
	     down = (window->camera_keys & (CAMERA_S | CAMERA_DOWN)) != 0;
	input->pan_x = (float)right - (float)left;
	input->pan_y = (float)up - (float)down;
	POINT point;
	if (GetCursorPos(&point) && ScreenToClient(window->handle, &point))
	{
		input->pointer_x = point.x;
		input->pointer_y = point.y;
		input->pointer_valid = true;
	}
	window->input = PointerInput();
	return window->open;
}

void window_suspend_input(Window* window, bool suspended)
{
	window->suspended = suspended;
	if (GetCapture() == window->handle)
		ReleaseCapture();
	window->dragging = false;
	window->panning = false;
	window->moved = false;
	window->camera_keys = 0;
	window->render_keys = 0;
	window->stats_held = false;
	window->buffers_held = false;
	window->input = PointerInput();
}

void window_close(Window* window)
{
	if (window->handle)
		DestroyWindow(window->handle);
	if (window->instance)
		UnregisterClassA("nanovoreWindow", window->instance);
	*window = Window();
}
