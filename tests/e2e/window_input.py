#!/usr/bin/env python3
"""Drive a real nanova window with OS mouse/keyboard events, then exit it."""

import json
import ctypes as c
import ctypes.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


class XWindowAttributes(c.Structure):
    _fields_ = [('x',c.c_int),('y',c.c_int),('width',c.c_int),('height',c.c_int),
                ('border_width',c.c_int),('depth',c.c_int),('visual',c.c_void_p),('root',c.c_ulong),
                ('klass',c.c_int),('bit_gravity',c.c_int),('win_gravity',c.c_int),('backing_store',c.c_int),
                ('backing_planes',c.c_ulong),('backing_pixel',c.c_ulong),('save_under',c.c_int),
                ('colormap',c.c_ulong),('map_installed',c.c_int),('map_state',c.c_int),
                ('all_event_masks',c.c_long),('your_event_mask',c.c_long),('do_not_propagate_mask',c.c_long),
                ('override_redirect',c.c_int),('screen',c.c_void_p)]


class Desktop:
    def __init__(self):
        if sys.platform == 'win32':
            from ctypes import wintypes
            self.api = c.WinDLL('user32', use_last_error=True)
            self.api.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, c.c_int]
            self.api.EnumWindows.argtypes = [c.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM), wintypes.LPARAM]
            self.api.GetClientRect.argtypes = [wintypes.HWND, c.POINTER(wintypes.RECT)]
            self.api.ClientToScreen.argtypes = [wintypes.HWND, c.POINTER(wintypes.POINT)]
            self.api.SetForegroundWindow.argtypes = [wintypes.HWND]
            self.callback_type = self.api.EnumWindows.argtypes[0]
        else:
            self.api = c.CDLL(ctypes.util.find_library('X11'))
            self.xt = c.CDLL(ctypes.util.find_library('Xtst'))
            self.api.XOpenDisplay.argtypes = [c.c_char_p]
            self.api.XOpenDisplay.restype = c.c_void_p
            self.display = self.api.XOpenDisplay(None)
            if not self.display: raise RuntimeError('Window E2E needs an X11 display')
            self.api.XDefaultRootWindow.argtypes = [c.c_void_p]
            self.api.XDefaultRootWindow.restype = c.c_ulong
            self.root = self.api.XDefaultRootWindow(self.display)
            self.api.XGetWindowAttributes.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(XWindowAttributes)]
            handler_type = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p)
            self.error_handler = handler_type(lambda display, event: 0)
            self.api.XSetErrorHandler.argtypes = [handler_type]
            self.api.XSetErrorHandler(self.error_handler)
            self.api.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
            self.api.XFetchName.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_void_p)]
            self.api.XFree.argtypes = [c.c_void_p]
            self.api.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
            self.api.XInternAtom.restype = c.c_ulong
            self.api.XGetWindowProperty.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_long, c.c_long, c.c_int, c.c_ulong,
                c.POINTER(c.c_ulong), c.POINTER(c.c_int), c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ubyte))]
            self.client_list = self.api.XInternAtom(self.display, b'_NET_CLIENT_LIST', 1)
            self.api.XTranslateCoordinates.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_ulong)]
            self.api.XSetInputFocus.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]
            self.api.XFlush.argtypes = [c.c_void_p]
            self.api.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
            self.api.XKeysymToKeycode.restype = c.c_ubyte
            self.xt.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
            self.xt.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
            self.xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]

    def windows(self, name="Nanova"):
        result = set()
        if sys.platform == 'win32':
            def visit(handle, _):
                title = c.create_unicode_buffer(128)
                self.api.GetWindowTextW(handle, title, len(title))
                if title.value == name: result.add(handle)
                return True
            callback = self.callback_type(visit)
            self.api.EnumWindows(callback, 0)
        else:
            def visit(handle, depth):
                title = c.c_void_p()
                if self.api.XFetchName(self.display, handle, c.byref(title)) and title.value:
                    if c.string_at(title.value) == name.encode():
                        attributes = XWindowAttributes()
                        if self.api.XGetWindowAttributes(self.display, handle, c.byref(attributes)) and attributes.map_state == 2:
                            result.add(handle)
                    self.api.XFree(title)
                if depth == 0: return
                root, parent, count = c.c_ulong(), c.c_ulong(), c.c_uint()
                children = c.POINTER(c.c_ulong)()
                if self.api.XQueryTree(self.display, handle, c.byref(root), c.byref(parent), c.byref(children), c.byref(count)):
                    for i in range(count.value): visit(children[i], depth-1)
                    if children: self.api.XFree(children)
            # A full tree scan can take longer than a two-frame game window.
            # The window manager lists its client handles in one property read.
            actual, count, remaining = c.c_ulong(), c.c_ulong(), c.c_ulong()
            format_bits = c.c_int(); values = c.POINTER(c.c_ubyte)()
            if self.client_list and self.api.XGetWindowProperty(self.display, self.root, self.client_list, 0, 4096, 0, 0,
                    c.byref(actual), c.byref(format_bits), c.byref(count), c.byref(remaining), c.byref(values)) == 0 and format_bits.value == 32:
                handles = c.cast(values, c.POINTER(c.c_ulong))
                for i in range(count.value): visit(handles[i], 0)
                if values: self.api.XFree(values)
            else:
                if values: self.api.XFree(values)
                visit(self.root, 4)
        return result

    def focus_point(self, handle):
        if sys.platform == 'win32':
            from ctypes import wintypes
            point = wintypes.POINT(80, 80)
            self.api.ClientToScreen(handle, c.byref(point))
            self.api.SetForegroundWindow(handle)
            return point.x, point.y
        x, y, child = c.c_int(), c.c_int(), c.c_ulong()
        self.api.XTranslateCoordinates(self.display, handle, self.root, 80, 80, c.byref(x), c.byref(y), c.byref(child))
        self.api.XSetInputFocus(self.display, handle, 1, 0)
        self.api.XFlush(self.display)
        return x.value, y.value

    def move(self, x, y):
        if sys.platform == 'win32': self.api.SetCursorPos(x, y)
        else:
            self.xt.XTestFakeMotionEvent(self.display, -1, x, y, 0)
            self.api.XFlush(self.display)

    def button(self, down, button=3):
        if sys.platform == 'win32': self.api.mouse_event(({1:(2,4),2:(32,64),3:(8,16)}[button])[0 if down else 1], 0, 0, 0, 0)
        else:
            self.xt.XTestFakeButtonEvent(self.display, button, int(down), 0)
            self.api.XFlush(self.display)

    def key(self, down, name):
        arrows={'F1':(0x70,0xffbe),'F2':(0x71,0xffbf),'LEFT':(0x25,0xff51),'UP':(0x26,0xff52),'RIGHT':(0x27,0xff53),'DOWN':(0x28,0xff54)}
        if sys.platform == 'win32':
            code=arrows[name][0] if name in arrows else ord(name)
            flags=(1 if name in ('LEFT','RIGHT','UP','DOWN') else 0) | (0 if down else 2)
            self.api.keybd_event(code,0,flags,0)
        else:
            symbol=arrows[name][1] if name in arrows else ord(name.lower())
            key=self.api.XKeysymToKeycode(self.display,symbol)
            if not key:raise RuntimeError('No X11 keycode for '+name)
            self.xt.XTestFakeKeyEvent(self.display,key,int(down),0);self.api.XFlush(self.display)

    def wheel(self):
        if sys.platform == 'win32': self.api.mouse_event(0x800, 0, 0, 240, 0)
        else:
            for _ in range(2):
                self.xt.XTestFakeButtonEvent(self.display, 4, 1, 0)
                self.xt.XTestFakeButtonEvent(self.display, 4, 0, 0)
            self.api.XFlush(self.display)

    def escape(self):
        if sys.platform == 'win32':
            self.api.keybd_event(0x1b, 0, 0, 0)
            self.api.keybd_event(0x1b, 0, 2, 0)
        else:
            key = self.api.XKeysymToKeycode(self.display, 0xff1b)
            self.xt.XTestFakeKeyEvent(self.display, key, 1, 0)
            self.xt.XTestFakeKeyEvent(self.display, key, 0, 0)
            self.api.XFlush(self.display)


def main():
    import os
    from control_window import run_window
    # The explicit legacy entry point accepts the same binary/config arguments.
    # Production input is now covered by the bounded keyboard and mouse scenario.
    binary=Path(sys.argv[1]).resolve()
    os.environ['NANOVA_VULKAN_VALIDATION']='1'
    with tempfile.TemporaryDirectory(prefix='nanova-window-input-') as folder:
        print(run_window(binary,Path(folder)))


if __name__ == '__main__':
    try: main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
