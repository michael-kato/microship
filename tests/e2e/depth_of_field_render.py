#!/usr/bin/env python3
"""Check cell-plane focus and gentle blur in the real application at 1080p."""
import argparse
from copy import deepcopy
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from authored_assets import stage_world, write_world

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--output', type=Path, default=ROOT / 'build/depth_of_field')
parser.add_argument('--config', type=Path, default=ROOT / 'assets/rendering/renderer.json')
parser.add_argument('--run', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
binary = args.binary.resolve()
command = [str(binary)] if sys.platform == 'win32' else [
    sys.executable, str(ROOT / 'scripts/proton_run.py'), str(binary)]
config = json.loads(args.config.read_text())

# A flat, vertex-colored checkerboard makes focus errors visible across the view.
vertices, triangles = [], []
for y in range(32):
    for x in range(32):
        first = len(vertices)
        color = .9 if (x + y) % 2 else .02
        for dx, dy in ((0, 0), (1, 0), (1, 1), (0, 1)):
            vertices.append((2 * (x + dx) / 32 - 1, 2 * (y + dy) / 32 - 1,
                             0, 1.35, 0, 0, 1, 0, color, color, color, 1))
        triangles.extend(((first, first + 1, first + 2, 0),
                          (first, first + 2, first + 3, 0)))
with (out / 'focus_plane.fmesh').open('wb') as file:
    file.write(struct.pack('<8sII', b'FLMESH01', len(vertices), len(triangles)))
    for vertex in vertices:
        file.write(struct.pack('<12f', *vertex))
    for triangle in triangles:
        file.write(struct.pack('<4I', *triangle))

cases = []
for name, extra in [('near', ['--camera-zoom', '1000']), ('normal', []),
                    ('far', ['--camera-zoom', '-1000'])]:
    cases.append((name, None, config, extra, 600))
plane = stage_world(ROOT / 'assets/levels/virus-player-level.json', out)
plane['match'] = dict(player='virus', opponent='immune', auto_colonize=False)
plane['rules'].pop('exploration', None)
plane['appearance'] = dict(ambient=[1, 1, 1])
plane['content']['bodies'].append(dict(name='focus_plane', label='PLANE',
    geometry='mesh', model='focus_plane.fmesh', hosts_site=False))
plane['spawns'] = [dict(type='virus_fighter', position=[-2000, 0, 0]),
                   dict(type='immune_fighter', position=[2000, 0, 0])]
for name, distance, height, tilted in [('plane_near', 8, 0, False),
                                     ('plane_far', 600, 0, False),
                                     ('plane_pan', 20.88, 11, True)]:
    world, settings = deepcopy(plane), deepcopy(config)
    world['bodies'] = [dict(type='focus_plane', owner='neutral',
        position=[0, 0, height], radius=distance * 1.5)]
    for owner, x in [('virus', -2000), ('immune', 2000)]:
        home = deepcopy(plane['bodies'][0])
        home.update(owner=owner, position=[x, 0, height], radius=2.1)
        world['bodies'].append(home)
    settings['camera'].update(position=[0, .3 * distance if tilted else 0,
        height + distance * (math.sqrt(.91) if tilted else 1)], target=[0, 0, height])
    settings['bloom']['strength'] = 0
    cases.append((name, world, settings,
                  ['--camera-drag', '140', '-80'] if tilted else [], 1))

commands = []
for name, world, settings, extra, ticks in cases:
    world_args = []
    if world:
        path = out / (name + '_level.json')
        write_world(path, world)
        world_args = ['--world', str(path)]
    for enabled in (True, False):
        label = name + ('_on' if enabled else '_off')
        current = deepcopy(settings)
        if not enabled:
            current['depth_of_field']['max_radius'] = 0
        path = out / (label + '_renderer.json')
        path.write_text(json.dumps(current, indent=2) + '\n')
        commands.append((label, [*command, *world_args, '--config', str(path),
            '--headless', '--frames', '2', '--ticks', str(ticks), '--time', '10',
            '--step-seconds', '0', '--world-seed', '1234', '--no-stats',
            '--capture', str(out / (label + '.ppm')),
            '--state', str(out / (label + '_state.json')), *extra]))
(out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
if not args.run:
    print('Prepared twelve 1080p application captures. No GPU work started.')
    sys.exit(0)

environment = os.environ.copy()
environment['NANOVORE_VULKAN_VALIDATION'] = '1'
for name, invocation in commands:
    result = subprocess.run(invocation, env=environment, capture_output=True, text=True)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (name, result.stdout, result.stderr)
    assert 'Vulkan validation: enabled.' in result.stdout, name

def pixels(name):
    raw = (out / (name + '.ppm')).read_bytes()
    header = re.match(rb'P6\s+1920\s+1080\s+255\s', raw)
    assert header, name
    return raw[header.end():]

report = {'settings': config['depth_of_field'], 'cases': {}}
for name, *_ in cases:
    on, off = pixels(name + '_on'), pixels(name + '_off')
    delta = bytes(abs(a - b) for a, b in zip(on, off))
    report['cases'][name] = dict(mean_rgb8=sum(delta) / len(delta),
        changed_pixels=sum(any(delta[i:i + 3]) for i in range(0, len(delta), 3)),
        maximum_rgb8=max(delta))
    assert json.loads((out / (name + '_on_state.json')).read_text()) == json.loads(
        (out / (name + '_off_state.json')).read_text()), name
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
for name in ('plane_near', 'plane_far', 'plane_pan'):
    assert report['cases'][name]['changed_pixels'] == 0, report
for name in ('near', 'normal', 'far'):
    assert report['cases'][name]['mean_rgb8'] < .1, report
assert 0 < config['depth_of_field']['max_radius'] <= 1.5, report
print(json.dumps(report, indent=2))
print('Cell-plane focus, zoom limits, tilted pan, and simulation state passed.')
