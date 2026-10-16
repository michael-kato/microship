#!/usr/bin/env python3
"""Drive a real microship window with OS mouse/keyboard events, then exit it."""

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
            self.api.XTranslateCoordinates.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_ulong)]
            self.api.XSetInputFocus.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]
            self.api.XFlush.argtypes = [c.c_void_p]
            self.api.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
            self.api.XKeysymToKeycode.restype = c.c_uint
            self.xt.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
            self.xt.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
            self.xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]

    def windows(self):
        result = set()
        if sys.platform == 'win32':
            def visit(handle, _):
                title = c.create_unicode_buffer(128)
                self.api.GetWindowTextW(handle, title, len(title))
                if title.value == 'microship': result.add(handle)
                return True
            callback = self.callback_type(visit)
            self.api.EnumWindows(callback, 0)
        else:
            def visit(handle, depth):
                title = c.c_void_p()
                if self.api.XFetchName(self.display, handle, c.byref(title)) and title.value:
                    if c.string_at(title.value) == b'microship':
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

    def button(self, down):
        if sys.platform == 'win32': self.api.mouse_event(2 if down else 4, 0, 0, 0, 0)
        else:
            self.xt.XTestFakeButtonEvent(self.display, 1, int(down), 0)
            self.api.XFlush(self.display)

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
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
    from gpu_environment import check_gpu_environment
    check_gpu_environment()
    binary, config_path = (Path(arg).resolve() for arg in sys.argv[1:])
    desktop = Desktop()
    existing = desktop.windows()
    with tempfile.TemporaryDirectory(prefix='microship-window-e2e-') as directory:
        config = Path(directory) / 'native.json'
        contents = json.loads(config_path.read_text())
        contents['output'].update(width=1920,height=1080)
        contents['sampling']['samples_per_pixel']=6
        contents['volume']['rays_per_pixel']=1
        config.write_text(json.dumps(contents,indent=2))
        command = [str(binary)]
        if binary.suffix.lower() == '.exe' and sys.platform != 'win32':
            command = [sys.executable, str(Path(__file__).resolve().parents[2] / 'scripts/proton_run.py'), str(binary)]
        process = subprocess.Popen([*command, '--config', str(config), '--time', '60', '--frames', '2'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline, handle = time.monotonic()+8, None
            while time.monotonic() < deadline:
                new = desktop.windows() - existing
                if new:
                    handle = next(iter(new))
                    break
                if process.poll() is not None: break
                time.sleep(.01)
            if handle is None:
                output, errors = process.communicate(timeout=2)
                raise RuntimeError('microship did not create its production window\n'+output+'\n'+errors)
            x,y = desktop.focus_point(handle)
            desktop.move(x,y)
            time.sleep(.03)
            desktop.button(True)
            time.sleep(.03)
            desktop.move(x+8,y-4)
            time.sleep(.03)
            desktop.button(False)
            desktop.wheel()
            time.sleep(.15)
            desktop.escape()
            output, errors = process.communicate(timeout=8)
            inputs = re.findall(r'Camera input: drag ([\d.-]+) ([\d.-]+); wheel ([\d.-]+)', output)
            totals = tuple(sum(float(row[i]) for row in inputs) for i in range(3))
            if process.returncode or totals != (8.0,-4.0,2.0) or 'Rendered ' not in output:
                raise RuntimeError(f'Mouse drag/wheel or Escape failed: {totals}\n{output}\n{errors}')
        finally:
            if process.poll() is None:
                if handle is not None: desktop.escape()
                try: process.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
    print('Window-input E2E passed: actual mouse drag, wheel zoom, Escape, rendering and shutdown.')


if __name__ == '__main__':
    try: main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
