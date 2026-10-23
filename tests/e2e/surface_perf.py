#!/usr/bin/env python3
"""Measure the close battle view with controlled shader work removed.

The test uses a private application bundle. It does not change game shaders.
Differences include compiler changes and are not independent GPU stage times.
"""
import argparse
from copy import deepcopy
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--frames', type=int, default=130)
parser.add_argument('--warmup-frames', type=int, default=10)
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--ticks', type=int, default=1800)
parser.add_argument('--output', type=Path, default=root / 'build/surface_perf')
args = parser.parse_args()
if args.rounds < 2 or args.warmup_frames < 2 or args.frames <= args.warmup_frames:
    parser.error('Use at least two rounds, two warmup frames, and one measured frame')
binary = args.binary.resolve()
if binary.suffix.lower() != '.exe':
    parser.error('Use the Windows executable')
out = (args.output / datetime.now().strftime('%Y%m%d_%H%M%S')).resolve()
app = out / 'app'
shaders = app / 'assets/shaders'
shaders.mkdir(parents=True)
shutil.copy2(binary, app / binary.name)
for asset in (binary.parent / 'assets').iterdir():
    if asset.name != 'shaders':
        (app / 'assets' / asset.name).symlink_to(asset, target_is_directory=asset.is_dir())
for shader in (binary.parent / 'assets/shaders').iterdir():
    if shader.is_file():
        shutil.copy2(shader, shaders / shader.name)
base = json.loads((root / 'assets/rendering/renderer.json').read_text())
assert base['output']['width'] == 1920 and base['output']['height'] == 1080
assert base['volume']['spatial_filter'], 'This check profiles the default guide surface program'
base['$schema'] = str(root / 'assets/schemas/render.schema.json')
configs = {}
for case in ('baseline', 'sss_off'):
    config = deepcopy(base)
    if case == 'sss_off':
        config['subsurface']['strength'] = 0
    path = out / (case + '_renderer.json')
    path.write_text(json.dumps(config, indent=2) + '\n')
    configs[case] = path
subprocess.run([sys.executable, str(root / 'scripts/format_json.py'), *map(str, configs.values())], check=True)
source = (root / 'assets/shaders/ray_trace.comp').read_text()
changes = {
    'no_thickness': ('subsurface_sample_weight*evaluate_subsurface(s,-direction)', 'vec3(0)'),
    'no_receiver_shadows': ('{ return light_visibility(origin,direction,0u,true); }', '{ return vec3(1); }'),
}
programs = {}
for case, (old, new) in changes.items():
    assert source.count(old) == 1, 'The diagnostic replacement must match one source expression'
    path = out / (case + '.comp')
    path.write_text(source.replace(old, new))
    compiled = out / (case + '.comp.spv')
    subprocess.run(['glslc', '--target-env=vulkan1.2', '-O', '-DDEVELOPMENT=1',
                    '-DNANOVORE_DEBUG_BUFFERS=1', '-DNANOVORE_NO_QUERY_COUNTERS=1',
                    '-I' + str(root / 'assets/shaders'), str(path), '-o', str(compiled)], check=True)
    programs[case] = compiled.read_bytes()
normal_shader = (shaders / 'debug_guide_surface.comp.spv').read_bytes()
cases = ('baseline', 'sss_off', 'no_thickness', 'no_receiver_shadows')
env = os.environ.copy()
env['NANOVORE_VULKAN_VALIDATION'] = '0'
report = {'workload': {'level': 'performance_battle.json', 'seed': 1234, 'ticks': args.ticks,
                      'resolution': [1920, 1080], 'sampling': base['sampling'], 'volume': base['volume'],
                      'frames_per_run': args.frames, 'warmup_frames': args.warmup_frames,
                      'rounds': args.rounds, 'simulation': 'frozen during rendering'},
          'method': 'Alternating real-application runs; removed work changes images and compiler output. '
                    'Deltas are not separate stage timings or guaranteed optimization gains.',
          'normal_surface_sha256': hashlib.sha256(normal_shader).hexdigest(), 'runs': []}
reference_state = None
for index in range(args.rounds):
    for case in (cases if index % 2 == 0 else reversed(cases)):
        (shaders / 'debug_guide_surface.comp.spv').write_bytes(programs.get(case, normal_shader))
        folder = out / (case + '_' + str(index + 1))
        cmd = [sys.executable, str(root / 'tests/e2e/performance.py'), str(app / binary.name),
               '--scene', 'zoomed_in', '--skip-cpu', '--ticks', str(args.ticks), '--frames', str(args.frames),
               '--warmup-frames', str(args.warmup_frames), '--config', str(configs.get(case, configs['baseline'])),
               '--output', str(folder)]
        subprocess.run(cmd, env=env, check=True, stdout=subprocess.DEVNULL)
        paths = list(folder.glob('*/report.json'))
        assert len(paths) == 1
        scene = json.loads(paths[0].read_text())['scenes']['zoomed_in']
        assert scene['validation'] == 'not reported', 'Timing runs must not enable validation'
        for frame in scene['frame_spans_ms']:
            children = sum(frame[node] for node in ('SLOT FENCE WAIT', 'TIMESTAMP READ', 'TIMING REPORT'))
            assert abs(frame['SLOT WAIT + QUERY'] - children) < .00001, 'Incomplete CPU slot markers'
        assert scene['spans_ms']['TIMESTAMP READ'] > 0, 'No reused frame slot was measured'
        state = json.loads(Path(scene['state']).read_text())
        if reference_state is None:
            reference_state = state
        assert state == reference_state, 'Diagnostic rendering changed the simulation'
        report['runs'].append({'case': case, 'round': index + 1, 'report': str(paths[0]),
                               'spans_ms': scene['spans_ms']})
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(case, index + 1, 'surface', round(scene['spans_ms']['SURFACE + SSS'], 3),
              'GPU', round(scene['spans_ms']['GPU FRAME'], 3), 'ms', flush=True)
(shaders / 'debug_guide_surface.comp.spv').write_bytes(normal_shader)
report['summary'] = {}
for case in cases:
    runs = [r['spans_ms'] for r in report['runs'] if r['case'] == case]
    report['summary'][case] = {node: statistics.mean(r[node] for r in runs) for node in runs[0]}
report['surface_deltas_ms'] = {
    case: [next(r['spans_ms']['SURFACE + SSS'] for r in report['runs'] if r['case'] == 'baseline' and r['round'] == index + 1)
           - next(r['spans_ms']['SURFACE + SSS'] for r in report['runs'] if r['case'] == case and r['round'] == index + 1)
           for index in range(args.rounds)] for case in cases if case != 'baseline'}
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('Report:', out / 'report.json')
