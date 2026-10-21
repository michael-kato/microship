#!/usr/bin/env python3
"""Check the production pause menu through the application's Win32 input queue."""
import argparse
import array
import wave
import json
import os
from pathlib import Path
import queue
import re
import signal
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]


def command(binary):
    return [str(binary)] if sys.platform == 'win32' else [
        sys.executable, str(ROOT / 'scripts/proton_run.py'), str(binary)]


class Process:
    def __init__(self, args, environment):
        self.lines = []
        self.errors = []
        self.queue = queue.Queue()
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, bufsize=1,
                                        start_new_session=True, env=environment)
        self.readers = []
        for pipe, target, output in [(self.process.stdout, self.lines, self.queue),
                                     (self.process.stderr, self.errors, None)]:
            reader = threading.Thread(target=self.read, args=(pipe, target, output))
            reader.start()
            self.readers.append(reader)

    @staticmethod
    def read(pipe, target, output):
        for line in pipe:
            target.append(line)
            if output is not None:
                output.put(line.rstrip('\r\n'))
        if output is not None:
            output.put(None)

    def wait_line(self, pattern, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.queue.get(timeout=max(.01, deadline - time.monotonic()))
            except queue.Empty:
                break
            if line is None:
                break
            match = re.search(pattern, line)
            if match:
                return match
        raise AssertionError((pattern, ''.join(self.lines), ''.join(self.errors)))

    def finish(self, timeout=5):
        code = self.process.wait(timeout=timeout)
        for reader in self.readers:
            reader.join(timeout=1)
        assert code == 0, (code, ''.join(self.lines), ''.join(self.errors))
        return ''.join(self.lines)

    def stop(self):
        if self.process.poll() is None:
            if sys.platform == 'win32':
                self.process.terminate()
            else:
                os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=.5)
            except subprocess.TimeoutExpired:
                if sys.platform == 'win32':
                    self.process.kill()
                else:
                    os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=2)
        for reader in self.readers:
            reader.join(timeout=1)


class Application:
    def __init__(self, args, name, settings, gpu=False, pause=True, ticks=0, physical=False):
        self.args = args
        self.name = name
        self.gpu = gpu
        self.state = args.output / (name + '_state.json')
        self.started = time.monotonic()
        if physical:
            from window_input import Desktop
            self.desktop = Desktop()
            self.prior_games = self.desktop.windows()
            self.prior_pauses = self.desktop.windows('Nanova - Pause')
        environment = os.environ.copy()
        if gpu:
            environment['NANOVA_VULKAN_VALIDATION'] = '1'
            if sys.platform != 'win32':
                environment['WINEDLLOVERRIDES'] = environment.get('WINEDLLOVERRIDES', '') + ';winepulse.drv,winealsa.drv=d'
        controls = ['--no-menu', '--world-seed', '1234', '--settings', str(settings),
                    '--state', str(self.state)]
        if gpu:
            controls += ['--frames', '2', '--frame-delay-ms', '250', '--samples', '1',
                         '--capture', str(args.output / (name + '.ppm'))]
        else:
            controls += ['--smoke']
        if ticks:
            controls += ['--ticks', str(ticks)]
        if pause:
            controls += ['--pause-on-start']
        self.app = Process(command(args.binary) + controls, environment)
        process = self.app.wait_line(r'Application process: (\d+)\.').group(1)
        helper_environment = environment.copy()
        helper_environment['NANOVA_VULKAN_VALIDATION'] = '0'
        self.driver = Process(command(args.binary.parent / 'nanova_input_probe.exe') + ['--ui', process], helper_environment)
        if not pause:
            if physical:
                found = set()
                while not found and time.monotonic() < self.started + 2:
                    found = self.desktop.windows() - self.prior_games
                    if not found:
                        time.sleep(.003)
                assert found, 'No game window'
                x, y = self.desktop.focus_point(next(iter(found)))
                self.desktop.move(x + 320, y + 20)
                self.desktop.button(True, 1)
                time.sleep(.02)
                self.desktop.button(False, 1)
                self.desktop.escape()
            else:
                self.ui('game_escape')
        self.ui('wait')

    def ui(self, text):
        self.driver.process.stdin.write(text + '\n')
        self.driver.process.stdin.flush()
        result = []
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            try:
                line = self.driver.queue.get(timeout=max(.01, deadline-time.monotonic()))
            except queue.Empty:
                break
            if line == 'OK':
                return result
            if line is None:
                break
            result.append(line)
        raise AssertionError((text, result, ''.join(self.driver.errors), ''.join(self.app.lines)))

    def done(self):
        try:
            self.driver.process.stdin.close()
            log = self.app.finish(timeout=max(.1, 10-(time.monotonic()-self.started)) if self.gpu else 5)
            self.driver.finish()
            pauses = re.findall(r'Pause session: tick (\d+) -> (\d+); time ([\d.e+-]+) -> ([\d.e+-]+);', log)
            assert pauses and all(a == b and c == d for a, b, c, d in pauses), log
            if self.gpu:
                assert log.count('Application frame:') == 2, log
                assert 'Vulkan validation: enabled.' in log
            return log
        finally:
            self.close()

    def close(self):
        self.driver.stop()
        self.app.stop()
        (self.args.output / (self.name + '.log')).write_text(''.join(self.app.lines + self.app.errors))
        (self.args.output / (self.name + '_input.log')).write_text(''.join(self.driver.lines + self.driver.errors))


def cpu_checks(args):
    active = None
    runs = 0
    try:
        preferences = args.output / 'player_settings.json'
        preferences.unlink(missing_ok=True)
        active = Application(args, 'resume', preferences)
        active.ui('escape')
        active.done()
        baseline = json.loads(active.state.read_text())
        runs += 1
        assert baseline['tick'] == 3
        assert not preferences.exists()
        for ticks, expected in [(0, baseline), (120, None)]:
            state = args.output / ('baseline_' + str(ticks) + '.json')
            controls = ['--smoke', '--world-seed', '1234', '--state', str(state)]
            if ticks:
                controls += ['--ticks', str(ticks)]
            result = subprocess.run(command(args.binary) + controls, capture_output=True, text=True, timeout=5)
            assert result.returncode == 0, result.stdout + result.stderr
            runs += 1
            if not ticks:
                assert json.loads(state.read_text()) == expected
            else:
                active = Application(args, 'active_match', preferences, ticks=ticks)
                active.ui('escape')
                active.done()
                runs += 1
                assert json.loads(active.state.read_text()) == json.loads(state.read_text())

        active = Application(args, 'cancel', preferences)
        active.ui('click|Settings')
        active.ui('set|Exposure|4')
        active.ui('click|Cancel')
        active.ui('click|Resume')
        active.done()
        runs += 1
        assert json.loads(active.state.read_text()) == baseline
        assert not preferences.exists(), 'Cancel wrote a settings file'

        active = Application(args, 'apply', preferences)
        active.ui('click|Settings')
        active.ui('set|Exposure|2')
        active.ui('click|Lighting')
        active.ui('set|Subsurface light wrap|0.65')
        active.ui('click|Volume')
        active.ui('set|Integration steps|2')
        active.ui('click|Effects')
        active.ui('set|Bloom strength|0.25')
        active.ui('click|Audio')
        active.ui('set|Master volume|50%')
        active.ui('set|Sound effects volume|25%')
        active.ui('click|Controls')
        active.ui('set|Keyboard camera speed|36')
        active.ui('set|Mouse zoom sensitivity|1.5')
        active.ui('set|Invert mouse wheel zoom|On')
        active.ui('click|Apply')
        active.ui('wait_text|Settings applied and saved.')
        active.ui('click|Cancel')
        active.ui('click|Resume')
        active.done()
        runs += 1
        saved = json.loads(preferences.read_text())
        assert saved['display']['exposure'] == 2
        assert abs(saved['lighting']['subsurface_wrap'] - .65) < .000001
        assert saved['volume']['steps'] == 2
        assert saved['effects']['bloom_strength'] == .25
        assert saved['audio'] == dict(master_volume=.5, effects_volume=.25)
        assert saved['controls']['invert_zoom'] and saved['controls']['zoom_sensitivity'] == 1.5
        assert json.loads(active.state.read_text()) == baseline, 'Settings changed simulation state'

        active = Application(args, 'reload_defaults', preferences)
        active.ui('click|Settings')
        assert active.ui('get|Exposure') == ['VALUE|2']
        active.ui('click|Restore Defaults')
        assert active.ui('get|Exposure') == ['VALUE|1.6']
        active.ui('click|Cancel')
        active.ui('click|Settings')
        assert active.ui('get|Exposure') == ['VALUE|2']
        active.ui('click|Restore Defaults')
        active.ui('click|Apply')
        active.ui('wait_text|Settings applied and saved.')
        active.ui('escape')
        active.ui('click|Resume')
        active.done()
        runs += 1
        assert abs(json.loads(preferences.read_text())['display']['exposure'] - 1.6) < .000001

        for choice in ('2', '3'):
            active = Application(args, 'faction_' + choice, preferences)
            active.ui('click|Main Menu')
            active.ui('escape')
            active.ui('click|Main Menu')
            active.ui('click|Return to Main Menu')
            active.ui('faction|' + choice)
            log = active.done()
            runs += 1
            assert json.loads(active.state.read_text())['player_faction'] == int(choice)
            assert log.count('Loaded world') == 2

        active = Application(args, 'quit', preferences)
        active.ui('click|Quit to Desktop')
        active.ui('click|Cancel')
        active.ui('click|Quit to Desktop')
        active.ui('click|Quit to Desktop')
        log = active.done()
        runs += 1
        assert 'Application lifecycle smoke passed' not in log, 'Quit resumed the match'

        active = Application(args, 'close', preferences)
        active.ui('close')
        active.done()
        runs += 1

        failure = args.output / 'missing_folder' / 'player_settings.json'
        active = Application(args, 'failed_save', failure)
        active.ui('click|Settings')
        active.ui('set|Exposure|4')
        active.ui('click|Apply')
        active.ui('wait_text|Settings were not saved. Previous settings restored.')
        assert active.ui('get|Exposure') == ['VALUE|1.6']
        active.ui('click|Cancel')
        active.ui('click|Resume')
        active.done()
        runs += 1
        assert not failure.exists()

        active = Application(args, 'invalid_combination', preferences)
        active.ui('click|Settings')
        active.ui('click|Volume')
        active.ui('set|Integration steps|8')
        active.ui('click|Apply')
        active.ui('wait_text|Gauss-Legendre integration needs 2 or 3 steps. For more steps, turn it off.')
        active.ui('click|Cancel')
        active.ui('click|Resume')
        active.done()
        runs += 1

        invalid = args.output / 'invalid_settings.json'
        cases = [dict(version=1, unknown=True), dict(version=1, audio=dict(master_volume=2)),
                 dict(version=1, volume=dict(steps=8, gauss=True)),
                 dict(version=1, volume=dict(rays=2, spatial_filter=True)),
                 dict(version=2), dict(version=1, display=dict(width=1920.5)),
                 dict(version=1, effects=dict(temporal=True)),
                 '{"version":1,"audio":{"master_volume":1,"master_volume":0}}']
        for case in cases:
            source = case if isinstance(case, str) else json.dumps(case)
            invalid.write_text(source)
            result = subprocess.run(command(args.binary) + ['--smoke', '--settings', str(invalid)],
                                    capture_output=True, text=True, timeout=5)
            assert result.returncode == 1 and str(invalid) in result.stderr, result.stdout + result.stderr
            assert invalid.read_text() == source
            runs += 1
        replay = args.output / 'audio_gain.orders'
        replay.write_text('0 select all\n0 move -40 -4 0\n')
        captures = []
        for name, master, effects in [('base', 1, 1), ('scaled', .5, .5), ('muted', 0, 1)]:
            path = args.output / ('audio_' + name + '.json')
            path.write_text(json.dumps(dict(version=1, audio=dict(master_volume=master, effects_volume=effects))))
            capture = args.output / ('audio_' + name + '.wav')
            result = subprocess.run(command(args.binary) + ['--smoke', '--world-seed', '1234',
                                    '--ticks', '60', '--replay', str(replay), '--settings', str(path),
                                    '--audio-capture', str(capture)], capture_output=True, text=True, timeout=5)
            assert result.returncode == 0, result.stdout + result.stderr
            with wave.open(str(capture), 'rb') as sound:
                assert sound.getnchannels() == 1 and sound.getframerate() == 48000
                assert sound.getsampwidth() == 2
                samples = array.array('h', sound.readframes(sound.getnframes()))
                if sys.byteorder != 'little':
                    samples.byteswap()
                captures.append(samples)
            runs += 1
        base, scaled, muted = captures
        assert len(base) == len(scaled) == len(muted) == 48000
        assert sum(value != 0 for value in base) > 100
        assert all(value == 0 for value in muted)
        assert all(abs(b - a*.25) <= 1 for a, b in zip(base, scaled) if abs(a) < 29999)
        print(f'Pause menu passed: {runs} real application runs; Win32 queue input; PCM gain and mute; no GPU work.')
    finally:
        if active is not None:
            active.close()


def gpu_check(args):
    preferences = args.output / 'gpu_settings.json'
    preferences.write_text(json.dumps(dict(version=1)))
    active = Application(args, 'graphics_apply', preferences, gpu=True, pause=False)
    try:
        active.ui('game_dirty')
        active.ui('click|Settings')
        active.ui('set|Exposure|2')
        active.ui('click|Apply')
        active.ui('wait_text|Settings applied and saved.')
        active.ui('click|Cancel')
        active.ui('click|Resume')
        log = active.done()
        assert log.count('Renderer: direct ray tracing;') == 2, log
        assert 'Camera keyboard:' not in log and 'Camera input:' not in log, log
        assert json.loads(active.state.read_text())['order']['count'] == 0
        assert json.loads(preferences.read_text())['display']['exposure'] == 2
        print('Pause graphics passed: apply, renderer rebuild, input isolation, resume, two native-1080p frames, host Vulkan validation.')
    finally:
        active.close()



def physical_gpu_check(args):
    from PIL import ImageGrab
    preferences = args.output / 'physical_settings.json'
    preferences.write_text(json.dumps(dict(version=1)))
    active = Application(args, 'physical_controls', preferences, gpu=True, pause=False, physical=True)
    desktop = active.desktop

    def menu_origin():
        found = set()
        deadline = time.monotonic() + 2
        while not found and time.monotonic() < deadline:
            found = desktop.windows('Nanova - Pause') - active.prior_pauses
            if not found:
                time.sleep(.003)
        assert found, 'No pause menu window'
        x, y = desktop.focus_point(next(iter(found)))
        return x - 80, y - 80

    def capture(name, x, y):
        time.sleep(.03)
        # Keep only the menu's native client pixels. Other desktop windows
        # are outside the capture. The game render remains native 1080p.
        ImageGrab.grab(bbox=(x, y, x+900, y+700)).save(args.output / name)

    try:
        x, y = menu_origin()
        capture('pause_menu.png', x, y)
        desktop.move(x+450, y+285)
        desktop.button(True, 1)
        time.sleep(.015)
        desktop.button(False, 1)
        active.ui('get|Exposure')
        capture('pause_display.png', x, y)
        desktop.move(x+100, y+188)
        desktop.button(True, 1)
        time.sleep(.015)
        desktop.button(False, 1)
        active.ui('get|Subsurface strength')
        capture('pause_lighting.png', x, y)
        desktop.escape()
        active.ui('wait_text|The match is paused.')
        menu_origin()
        time.sleep(.02)
        if sys.platform == 'win32':
            desktop.api.keybd_event(0x0d, 0, 0, 0)
            desktop.api.keybd_event(0x0d, 0, 2, 0)
        else:
            key = desktop.api.XKeysymToKeycode(desktop.display, 0xff0d)
            desktop.xt.XTestFakeKeyEvent(desktop.display, key, 1, 0)
            desktop.xt.XTestFakeKeyEvent(desktop.display, key, 0, 0)
            desktop.api.XFlush(desktop.display)
        log = active.done()
        assert log.count('Renderer: direct ray tracing;') == 1
        assert 'Camera keyboard:' not in log and 'Camera input:' not in log
        print('Pause OS input passed: game Esc, Settings click, Lighting click, Esc back, Enter resume; two native-1080p frames; host Vulkan validation.')
    finally:
        active.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/pause_menu_review')
    parser.add_argument('--gpu', action='store_true')
    parser.add_argument('--os-input', action='store_true', help='Use physical input in the bounded GPU window')
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if not (args.binary.parent / 'nanova_input_probe.exe').is_file():
        raise SystemExit('Build nanova_input_probe before this check.')
    if args.os_input and not args.gpu:
        parser.error('--os-input requires --gpu')
    if args.gpu:
        if args.os_input:
            physical_gpu_check(args)
        else:
            gpu_check(args)
    else:
        cpu_checks(args)


if __name__ == '__main__':
    main()
