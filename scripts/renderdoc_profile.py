#!/usr/bin/env python3
"""Prepare native profiles. --run uses application timers unless --mode capture or replay is set.

Capture uses the standard RenderDoc launcher and the native headless target.
Capture never starts replay. Replay counters and compiler inspection are separate options.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from authored_assets import stage_world, write_world
from gpu_environment import gpu_process_timeout_seconds
from save_progress import archive_capture

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('binary', nargs='?', type=Path, default=root / 'build/linux_diagnostic/nanovore')
p.add_argument('--mode', choices=('timings', 'capture', 'replay'), default='timings')
p.add_argument('--renderdoc', type=Path, default=Path('/opt/renderdoc_1.44'))
p.add_argument('--world', type=Path, default=root / 'assets/levels/virus-player-level.json')
p.add_argument('--samples', type=int, choices=(1, 3, 6), help='Default: keep the selected config sampling')
p.add_argument('--config', type=Path, default=root / 'assets/rendering/renderer.json')
p.add_argument('--timeout', type=float, default=gpu_process_timeout_seconds())
p.add_argument('--sss', choices=('on', 'off'), default='on')
p.add_argument('--subgroup-size', type=int, choices=(0, 32, 64))
p.add_argument('--ticks', type=int, default=600)
p.add_argument('--capture-file', type=Path, help='Existing native .rdc file; required for replay')
p.add_argument('--counters', action='store_true', help='Replay: request GPU counters, which can replay work again')
p.add_argument('--compiler-statistics', action='store_true', help='Replay: inspect each surface/volume shader')
p.add_argument('--validation', action='store_true', help='Enable the host validation layer for this run')
p.add_argument('--run', action='store_true', help='Execute the selected mode; otherwise write its plan only')
p.add_argument('--output', type=Path, default=root / 'build/native_render_profile')
a = p.parse_args()
if not math.isfinite(a.timeout) or a.timeout <= 0 or not 1 <= a.ticks <= 1000000:
    p.error('Use a finite positive timeout and a tick count in [1, 1000000]')
if a.mode != 'replay' and (a.capture_file or a.counters or a.compiler_statistics):
    p.error('--capture-file, --counters and --compiler-statistics require --mode replay')
if a.mode == 'replay' and (not a.capture_file or a.capture_file.suffix.lower() != '.rdc' or not a.capture_file.is_file()):
    p.error('--mode replay requires an existing --capture-file PATH.rdc')
binary = a.binary.resolve()
if a.mode != 'replay':
    if not binary.is_file():
        p.error('Native headless executable missing; build build/linux_diagnostic/nanovore first')
    with binary.open('rb') as executable:
        native = executable.read(4) == b'\x7fELF'
    if not native:
        p.error('Use the native Linux headless executable. Windows/Proton capture is disabled.')
    if a.mode == 'capture':
        support = subprocess.run([str(binary), '--help'], capture_output=True, text=True, check=True)
        if 'Native RenderDoc capture support: enabled.' not in support.stdout:
            p.error('Build the native target with NANOVORE_RENDERDOC_CAPTURE=ON before capture')
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
renderdoc = a.renderdoc.resolve()
env = os.environ.copy()
# Remove the old custom capture path, including inherited launch variables.
for key in tuple(env):
    if key.startswith('NANOVORE_RENDERDOC_'):
        env.pop(key)
for key in ('VK_INSTANCE_LAYERS', 'LD_PRELOAD'):
    if 'renderdoc' in env.get(key, '').lower():
        p.error('Remove inherited RenderDoc injection from ' + key + ' before using this script')
if a.mode == 'timings':
    env['DISABLE_VULKAN_RENDERDOC_CAPTURE_1_44'] = '1'
else:
    env.pop('DISABLE_VULKAN_RENDERDOC_CAPTURE_1_44', None)
env['NANOVORE_VULKAN_VALIDATION'] = '1' if a.validation else '0'
if a.validation:
    env['VK_LAYER_PATH'] = str(root / 'build/toolchains/validation/usr/share/vulkan/explicit_layer.d')
    env['VK_INSTANCE_LAYERS'] = 'VK_LAYER_KHRONOS_validation'
plan = {'mode': a.mode, 'platform': 'native Linux headless', 'automatic_replay': False,
        'timeout_seconds': a.timeout, 'validation_requested': a.validation,
        'counters_requested': a.counters, 'compiler_statistics_requested': a.compiler_statistics}
config = None
if a.mode == 'replay':
    command = [str(renderdoc / 'bin/qrenderdoc'), '--python', str(root / 'scripts/renderdoc_analyze.py')]
    env.update(NANOVORE_RENDERDOC_OUTPUT=str(out), NANOVORE_RENDERDOC_CAPTURE_FILE=str(a.capture_file.resolve()),
               NANOVORE_RENDERDOC_SHADER_FOLDER=str(binary.parent / 'assets/shaders'),
               NANOVORE_RENDERDOC_COUNTERS='1' if a.counters else '0',
               NANOVORE_RENDERDOC_COMPILER_STATISTICS='1' if a.compiler_statistics else '0',
               NANOVORE_RENDERDOC_VALIDATION='1' if a.validation else '0')
    plan['capture_file'] = str(a.capture_file.resolve())
else:
    world = stage_world(a.world.resolve(), out)
    wp = out / 'profile_world.json'
    write_world(wp, world)
    config = json.loads(a.config.read_text())
    if a.samples is not None:
        config['sampling']['samples_per_pixel'] = a.samples
    if config['output']['width'] != 1920 or config['output']['height'] != 1080:
        p.error('Use native 1920x1080 output')
    if a.sss == 'off':
        config['subsurface']['strength'] = 0
    if a.subgroup_size == 0:
        config['lighting'].pop('subgroup_size', None)
    elif a.subgroup_size is not None:
        config['lighting']['subgroup_size'] = a.subgroup_size
    config['$schema'] = str(root / 'assets/schemas/render.schema.json')
    cp = out / 'profile_renderer.json'
    cp.write_text(json.dumps(config, indent=2) + '\n')
    subprocess.run([sys.executable, str(root / 'scripts/format_json.py'), str(cp)], check=True)
    command = [str(binary), '--world', str(wp), '--config', str(cp), '--headless', '--frames', '2',
               '--no-stats', '--ticks', str(a.ticks), '--world-seed', '1234', '--step-seconds', '0',
               '--camera-zoom', '1000', '--capture', str(out / 'display.ppm'), '--state', str(out / 'state.json')]
    plan.update(frames=2, ticks=a.ticks, render_settings=config)
    if a.mode == 'capture':
        env['NANOVORE_RENDERDOC_CAPTURE_FRAME'] = '2'
        command = [str(renderdoc / 'bin/renderdoccmd'), 'capture', '--wait-for-exit',
                   '--capture-file', str(out / 'native_capture'), *command]
        plan['captured_frames'] = [2]
plan['command'] = command
(out / 'profile_plan.json').write_text(json.dumps(plan, indent=2) + '\n')
if not a.run:
    print('Prepared', a.mode, 'for native Linux. No GPU work started. No automatic replay.')
    raise SystemExit(0)

# Write logs as they arrive, including when the application fails or is stopped.
started = time.monotonic()
log = out / 'profile_run.log'
status = 'failed'
try:
    with log.open('w') as stream:
        process = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            returncode = process.wait(timeout=a.timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=2)
            raise
    if returncode:
        raise RuntimeError('Profile failed with exit code ' + str(returncode) + '; see ' + str(log))
    text = log.read_text(errors='replace')
    report = dict(plan, elapsed_seconds=time.monotonic() - started)
    if a.mode == 'capture':
        if 'RenderDoc native frame 2 saved: 1.' not in text:
            raise RuntimeError('The native application did not confirm capture. See ' + str(log))
        captures = list(out.glob('native_capture*.rdc'))
        if not captures:
            raise RuntimeError('RenderDoc wrote no capture. See ' + str(log))
        capture = max(captures, key=lambda path: path.stat().st_mtime)
        report.update(capture=str(capture), capture_bytes=capture.stat().st_size)
        print('Native capture:', capture, '\nReplay was not started.')
    elif a.mode == 'replay':
        analysis = json.loads((out / 'analysis.json').read_text())
        if 'error' in analysis:
            raise RuntimeError(analysis['error'])
        report['analysis'] = analysis
    else:
        labels = {'surface': 'GPU surface', 'volume': 'GPU volumes', 'gpu': 'total render', 'application': 'Application frame'}
        report['frame_times_ms'] = {}
        for key, label in labels.items():
            values = [float(v) for v in re.findall(re.escape(label) + r': ([0-9.]+) ms', text)]
            if len(values) != 2:
                raise RuntimeError('Missing application timings for ' + key + '; see ' + str(log))
            report['frame_times_ms'][key] = values
        report['validation'] = 'enabled' if 'Vulkan validation: enabled.' in text else 'not reported'
        print('Application GPU timers:', report['frame_times_ms'])
    if a.mode != 'replay':
        archive_capture(out / 'display.ppm', 'native_profile_' + a.mode)
    status = 'complete'
except BaseException as error:
    report = dict(plan, elapsed_seconds=time.monotonic() - started, error=str(error))
    raise
finally:
    report['status'] = status
    (out / 'profile_report.json').write_text(json.dumps(report, indent=2) + '\n')
