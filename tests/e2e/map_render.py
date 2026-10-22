#!/usr/bin/env python3
"""Render the current 128-cell map with default lighting and native resolution."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from authored_assets import stage_world, write_world
from gpu_environment import check_gpu_environment
from save_progress import archive_capture

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--run', action='store_true')
args = parser.parse_args()
binary = args.binary.resolve()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
world = stage_world(binary.parent / 'assets/levels/large_level.json', out)
assert world['layout']['cells'] == 128
# Expose the complete map to exercise every cell's render and lighting storage.
world['rules'].pop('exploration', None)
write_world(out / 'world.json', world)
settings = json.loads((binary.parent / 'assets/rendering/renderer.json').read_text())
assert (settings['output']['width'], settings['output']['height']) == (1920, 1080)
settings['camera'].update(position=[0, 0, 3 * world['layout']['field_radius']], target=[0, 0, 0])
(out / 'render.json').write_text(json.dumps(settings, indent=2) + '\n')
command = [str(binary)] if sys.platform == 'win32' else [sys.executable, str(ROOT / 'scripts/proton_run.py'), str(binary)]
common = [*command, '--world', str(out / 'world.json'), '--config', str(out / 'render.json'),
          '--world-seed', '1234', '--ticks', '1', '--step-seconds', '0']
cases = [common + ['--smoke', '--state', str(out / 'cpu_state.json')],
         common + ['--headless', '--frames', '2', '--no-stats', '--state', str(out / 'gpu_state.json'),
                   '--scene-state', str(out / 'scene.json'), '--capture', str(out / 'map.ppm')]]
(out / 'commands.json').write_text(json.dumps(cases, indent=2) + '\n')
if not args.run:
    print('Prepared one CPU run and one two-frame 1080p GPU run. Sampling is unchanged. No GPU work started.')
    raise SystemExit(0)

check_gpu_environment()
env = os.environ.copy()
env['NANOVORE_VULKAN_VALIDATION'] = '1'
for name, invocation in zip(('cpu', 'gpu'), cases):
    result = subprocess.run(invocation, capture_output=True, text=True, env=env, timeout=10)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (name, result.stdout, result.stderr)
    if name == 'gpu':
        log = result.stdout
        assert 'Vulkan validation: enabled.' in log
        assert 'VUID-' not in log + result.stderr and 'Validation Error' not in log + result.stderr

state = json.loads((out / 'gpu_state.json').read_text())
assert state == json.loads((out / 'cpu_state.json').read_text()), 'Rendering changed simulation state'
assert len(state['colonies']) == 128 and all(c['visible'] for c in state['colonies'])
scene = json.loads((out / 'scene.json').read_text())
media = len(scene['media'])
assert media == 129  # One interior medium per cell and one global atmosphere.
cache = [tuple(map(int, row)) for row in re.findall(r'Static lighting: (\d+) baked, (\d+) reused;', log)]
assert len(cache) == 2 and cache[0][0] == media and cache[1] == (0, media), cache
texture = re.search(r'Lighting field:.*texture (\d+)x(\d+)x(\d+) RGBA32F', log)
assert texture, 'Missing lighting storage dimensions'
width, height, depth = map(int, texture.groups())
grid = width // 3
assert width == 3 * grid and height % grid == depth % grid == 0
assert (height // grid) * (depth // grid) >= media, 'Lighting storage does not cover every cell'
updates = list(map(int, re.findall(r'GPU radiance cascades: [0-9.]+ ms; (\d+) cells traced;', log)))
assert len(updates) == 2 and all(0 < n <= settings['radiance_cascades']['cells_per_frame'] for n in updates), updates
render_ms = list(map(float, re.findall(r'total render: ([0-9.]+) ms', log)))
assert len(render_ms) == 2 and all(math.isfinite(v) and v > 0 for v in render_ms)
raw = (out / 'map.ppm').read_bytes()
header = re.match(rb'P6\s+1920\s+1080\s+255\s', raw)
assert header and len(raw) - header.end() == 1920 * 1080 * 3
assert len(set(raw[header.end():])) > 16, 'The capture is empty'
saved = archive_capture(out / 'map.ppm', 'map_128_default_lighting')
report = dict(cells=128, media=media, gpu_frames=2, gpu_ms=render_ms,
              lighting_texture=[width, height, depth], cascade_updates=updates,
              simulation_equal=True, screenshot=str(saved),
              limits='Bounded host validation; sustained frame rate and native Windows are not tested.')
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('Map render E2E passed: 128 visible cells, 129 media, cache reuse, cascade updates and unchanged simulation.')
