#!/usr/bin/env python3
"""Prepare bounded diagnostic runs; GPU execution requires --run.

Default diagnostics: native 1920x1080, 8 surface + 0 volume spp, two frames, one round, timing queries only.
Runs the Windows executable through scripts/proton_run.py on Linux. Game defaults stay
unchanged. No hardware tracing is supported after the recorded driver lockup.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
from gpu_environment import check_gpu_environment


def setting(text, name, value):
    updated, count = re.subn(rf'(?m)^{re.escape(name)}\s+[^\n#]+', f'{name} {value}', text)
    if count != 1:
        raise RuntimeError(f'Expected one {name} in rendering data')
    return updated


def numbers(pattern, log, frames, allow_zero=False):
    values = [float(value) for value in re.findall(pattern, log)]
    if len(values) != frames or any(value < 0 if allow_zero else value <= 0 for value in values):
        raise RuntimeError('Application did not report valid timings for every frame')
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--run', action='store_true', help='Explicitly launch GPU diagnostics')
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--samples', type=int, default=8)
    parser.add_argument('--volume-samples', type=int, default=0)
    parser.add_argument('--frames', type=int, default=2)
    parser.add_argument('--rounds', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=10,
                        help='Host timeout per process; cannot recover a hung GPU driver')
    parser.add_argument('--time', type=float, default=60, help='Frozen scene/sun timestamp; not workload duration')
    parser.add_argument('--reference-shader', type=Path,
                        help='Compare the prior lighting shader with the current shader in the full scene')
    parser.add_argument('--reference-world', type=Path,
                        help='Optional frozen prior world for the reference case (changes scene as well as shader)')
    parser.add_argument('--output', type=Path, default=Path('build/profiling/diagnostic'))
    args = parser.parse_args()
    if args.binary.suffix.lower() != '.exe':
        parser.error('Profile the Windows .exe; Linux runs it through Proton')
    if args.reference_world and not args.reference_shader:
        parser.error('--reference-world requires --reference-shader')
    if not ((args.width,args.height) in ((1920,1080),(2560,1440))
            and 1 <= args.samples <= 8 and 0 <= args.volume_samples <= 8-args.samples and 1 <= args.frames <= 2
            and 1 <= args.rounds <= 2 and 0 < args.timeout <= 10):
        parser.error('Diagnostics require native 1920x1080 or 2560x1440, 1-8 total surface/volume spp, 1-2 frames, '
                     '1-2 rounds, and a process timeout of at most 10 seconds')
    if args.run:
        check_gpu_environment()
    binary = args.binary.resolve()
    assets = binary.parent / 'assets'
    output = args.output.resolve()
    levels, configs = output / 'levels', output / 'rendering'
    levels.mkdir(parents=True, exist_ok=True)
    configs.mkdir(exist_ok=True)
    # Preserve relative mesh references in the diagnostic worlds.
    models = output / 'models'
    models.mkdir(exist_ok=True)
    for model in (assets / 'models').glob('*.fmesh'):
        shutil.copy2(model, models / model.name)
    world = (assets / 'levels/first.world').read_text()
    config = (assets / 'rendering/first.render').read_text()
    config = setting(config, 'spatial_denoise', 0)
    for name, value in (('width', args.width), ('height', args.height),
                        ('samples_per_pixel', args.samples), ('volume_samples_per_pixel', args.volume_samples)):
        config = setting(config, name, value)
    reuse = int(re.search(r'(?m)^spatial_sun_reuse\s+(\d+)',config)[1])
    if args.samples+args.volume_samples+reuse > 8:
        parser.error('The SPMIS source candidate counts toward the eight-sample cap; lower an explicit diagnostic budget')
    cases = [
        ('full', world, config),
        ('no-ships', re.sub(r'(?m)^ship\s+.*$', '', world), config),
        ('sphere-bodies', re.sub(r'(?m)^model\s+.*$', '', world), config),
        ('one-bounce', world, setting(config, 'max_bounces', 1)),
        ('half-samples', world, setting(config, 'samples_per_pixel', max(1, args.samples // 2))),
    ]
    if re.search(r'(?m)^(global_atmosphere|global_atmosphere_edge_fade|atmosphere|haze|haze_edge_fade)\s+', world):
        cases.append(('no-volumes', re.sub(r'(?m)^(global_atmosphere|global_atmosphere_edge_fade|atmosphere|haze|haze_edge_fade)\s+.*$', '', world), config))
        if args.volume_samples:
            cases.extend([
                ('half-volume-samples', world, setting(config,'volume_samples_per_pixel',args.volume_samples//2)),
                ('uniform-volume-proposal', world, setting(config,'volume_local_bounds',0)),
            ])
    if args.reference_shader:
        prior_world = args.reference_world.read_text() if args.reference_world else world
        cases = [('reference', prior_world, config), ('full', world, config)]
    command = [str(binary)]
    if binary.suffix.lower() == '.exe' and sys.platform != 'win32':
        command = [sys.executable, str(Path(__file__).resolve().with_name('proton_run.py')), str(binary)]
    results = {name: {'trace_ms': [], 'surface_ms': [], 'volume_ms': [], 'sun_sources_ms': [], 'bloom_ms': [], 'application_ms': [], 'runs': []}
               for name, _, _ in cases}
    for name, world_text, config_text in cases:
        (levels / (name + '.world')).write_text(world_text)
        (configs / (name + '.render')).write_text(config_text)
    plan = {'binary': str(binary), 'scene_seconds': args.time,
            'width': args.width, 'height': args.height, 'samples_per_pixel': args.samples,
            'volume_samples_per_pixel': args.volume_samples, 'spatial_sun_reuse': reuse,
            'total_candidates_per_pixel': args.samples+args.volume_samples+reuse,
            'frames_per_run': args.frames, 'rounds': args.rounds,
            'process_timeout_seconds': args.timeout, 'total_timeout_seconds': 30,
            'cases': [name for name, _, _ in cases],
            'note': 'Native-resolution capped diagnostics with reconstruction disabled; not the current eight-surface-plus-two-volume default or a 256-sample reference.'}
    if args.reference_shader:
        plan['reference_shader'] = str(args.reference_shader.resolve())
    if args.reference_world:
        plan['reference_world'] = str(args.reference_world.resolve())
        plan['note'] += ' Reference world differs; this is a combined scene/shader comparison, not equal-quality timing.'
    (output / 'plan.json').write_text(json.dumps(plan, indent=2))
    if not args.run:
        print(f'Prepared {len(cases)} cases at {args.width}x{args.height}, {args.samples} spp. '
              'No application launched. Add --run only when GPU testing is resumed.')
        return 0
    deadline = time.perf_counter() + 30
    for round_index in range(args.rounds):
        # Reverse alternate rounds to reduce systematic warmup/order bias.
        for name, _, _ in (cases if round_index % 2 == 0 else reversed(cases)):
            started = time.perf_counter()
            remaining = deadline - time.perf_counter()
            if remaining <= 0:
                raise RuntimeError('Diagnostic run reached its 30-second host time limit')
            shader = ['--shader', str(args.reference_shader.resolve())] if name == 'reference' else []
            process = subprocess.Popen([*command, '--headless', '--frames', str(args.frames),
                                  '--time', str(args.time),
                                  '--world', str(levels / (name + '.world')),
                                  '--config', str(configs / (name + '.render')), *shader],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                       start_new_session=(sys.platform != 'win32'))
            try:
                stdout, stderr = process.communicate(timeout=min(args.timeout, remaining))
            except subprocess.TimeoutExpired:
                # Stop this process group only. Wine may detach descendants;
                # this timeout cannot guarantee recovery of a wedged driver.
                if sys.platform == 'win32':
                    process.kill()
                else:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
                (output / f'{name}-{round_index + 1}.txt').write_text('Host timeout; diagnostics stopped.\n')
                raise RuntimeError(f'{name} timed out; no subsequent GPU cases were launched')
            log = stdout + stderr
            (output / f'{name}-{round_index + 1}.txt').write_text(log)
            if process.returncode:
                raise RuntimeError(f'{name} failed: {log}')
            result = results[name]
            result['trace_ms'] += numbers(r'GPU trace: ([\d.]+) ms', log, args.frames)
            result['surface_ms'] += numbers(r'GPU surface: ([\d.]+) ms', log, args.frames)
            result['volume_ms'] += numbers(r'GPU volumes: ([\d.]+) ms', log, args.frames, allow_zero=True)
            result['sun_sources_ms'] += numbers(r'GPU sun sources: ([\d.]+) ms', log, args.frames, allow_zero=True)
            result['bloom_ms'] += numbers(r'GPU bloom/display: ([\d.]+) ms', log, args.frames)
            result['application_ms'] += numbers(r'Application frame: ([\d.]+) ms', log, args.frames)
            result['runs'].append({'round': round_index + 1,
                                   'process_seconds': time.perf_counter() - started})
            print(f'{name} round {round_index+1}: trace {statistics.mean(result["trace_ms"]):.3f} ms', flush=True)
    summary = dict(plan)
    summary['results'] = results
    (output / 'results.json').write_text(json.dumps(summary, indent=2))
    print('\nCase                         Trace ms   Bloom ms     App ms       FPS')
    for name, _, _ in cases:
        result = results[name]
        app = statistics.mean(result['application_ms'])
        print(f'{name:28} {statistics.mean(result["trace_ms"]):9.3f}'
              f' {statistics.mean(result["bloom_ms"]):10.3f} {app:10.3f} {1000/app:9.2f}')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Profiling failed: {error}', file=sys.stderr)
        sys.exit(1)
