#!/usr/bin/env python3
"""Prepare bounded diagnostic runs; GPU execution requires --run.

Default diagnostics: native 1920x1080, 6 film + 1 volume + 1 background query, two frames, one round.
Runs the Windows executable through scripts/proton_run.py on Linux. Game defaults stay
unchanged. Set the workload and timeout to suit the measurement.
"""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import time
from gpu_environment import check_gpu_environment, gpu_process_timeout_seconds
from authored_assets import stage_world, write_world
from copy import deepcopy

NATIVE_WIDTH = 1920
NATIVE_HEIGHT = 1080
DEFAULT_FILM_SAMPLES = 6
DEFAULT_PROCESS_SECONDS = gpu_process_timeout_seconds()
FROZEN_SCENE_SECONDS = 60
MILLISECONDS_PER_SECOND = 1000

def setting(data, name, value):
    updated=deepcopy(data)
    for section in updated.values():
        if isinstance(section,dict) and name in section:
            section[name]=value
            return updated
    raise RuntimeError(f'Unknown rendering setting: {name}')

def numbers(pattern, log, frames, allow_zero=False):
    values = [float(value) for value in re.findall(pattern, log)]
    if len(values) != frames or any(value < 0 if allow_zero else value <= 0 for value in values):
        raise RuntimeError('Application did not report valid timings for every frame')
    return values

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--run', action='store_true', help='Explicitly launch GPU diagnostics')
    parser.add_argument('--width', type=int, default=NATIVE_WIDTH)
    parser.add_argument('--height', type=int, default=NATIVE_HEIGHT)
    parser.add_argument('--samples', type=int, default=DEFAULT_FILM_SAMPLES)
    parser.add_argument('--volume-samples', type=int, default=1)
    parser.add_argument('--frames', type=int, default=2)
    parser.add_argument('--rounds', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=DEFAULT_PROCESS_SECONDS,
                        help='Host timeout per process; cannot recover a hung GPU driver')
    parser.add_argument('--time', type=float, default=FROZEN_SCENE_SECONDS, help='Frozen scene timestamp; not workload duration')
    parser.add_argument('--output', type=Path, default=Path('build/profiling/diagnostic'))
    args = parser.parse_args()
    if args.binary.suffix.lower() != '.exe':
        parser.error('Profile the Windows .exe; Linux runs it through Proton')
    if min(args.width,args.height,args.samples,args.frames,args.rounds)<=0 or args.volume_samples<0 or args.timeout<=0:
        parser.error('Use positive dimensions, samples, frames, rounds and timeout; volume samples can be zero')
    if args.run:
        check_gpu_environment()
    binary = args.binary.resolve()
    assets = binary.parent / 'assets'
    output = args.output.resolve()
    levels, configs = output / 'levels', output / 'rendering'
    levels.mkdir(parents=True, exist_ok=True)
    configs.mkdir(exist_ok=True)
    startup=json.loads((assets/'startup.json').read_text())
    world=stage_world(assets/startup['world'],levels)
    config=json.loads((assets/startup['render']).read_text())
    config['$schema']=Path(os.path.relpath(assets/'schemas/render.schema.json',configs)).as_posix()
    for name,value in (('width',args.width),('height',args.height),
                       ('samples_per_pixel',args.samples),('rays_per_pixel',args.volume_samples)):
        config=setting(config,name,value)
    background=int('soft' in world['appearance'].get('backdrop',{}))
    single=deepcopy(world)
    for building in single['content']['buildings']:building['branches'].update(count=1,roots='single')
    spheres=deepcopy(world)
    for body in spheres['content']['bodies']:
        body['geometry']='sphere';body.pop('model',None);body.pop('variation',None)
    cases=[('full',world,config),('single-structure',single,config),('sphere-bodies',spheres,config),
           ('half-samples',world,setting(config,'samples_per_pixel',max(1,args.samples//2)))]
    if 'cell_medium' in world['appearance']:
        no_volumes=deepcopy(world)
        no_volumes['appearance'].pop('cell_medium')
        cases.append(('no-volumes',no_volumes,config))
        if args.volume_samples:
            cases.extend([('no-in-scattering',world,setting(config,'rays_per_pixel',0)),
                          ('half-volume-steps',world,setting(config,'integration_steps',max(2,config['volume']['integration_steps']//2)))])
    command = [str(binary)]
    if binary.suffix.lower() == '.exe' and sys.platform != 'win32':
        command = [sys.executable, str(Path(__file__).resolve().with_name('proton_run.py')), str(binary)]
    results = {name: {'trace_ms': [], 'surface_ms': [], 'volume_ms': [], 'bloom_ms': [], 'application_ms': [], 'runs': []}
               for name, _, _ in cases}
    for name, world_text, config_text in cases:
        write_world(levels/(name+'.json'),world_text)
        (configs/(name+'.json')).write_text(json.dumps(config_text,indent=2)+'\n')
    plan = {'binary': str(binary), 'scene_seconds': args.time,
            'width': args.width, 'height': args.height, 'samples_per_pixel': args.samples,
            'volume_rays_per_pixel': args.volume_samples,
            'background_queries_per_pixel': background,
            'total_candidates_per_pixel': args.samples+args.volume_samples+background,
            'frames_per_run': args.frames, 'rounds': args.rounds,
            'process_timeout_seconds': args.timeout,
            'cases': [name for name, _, _ in cases],
            'note': 'Native-resolution direct ray tracing with fixed film samples and deterministic volume integration. No 256-sample reference is run.'}
    (output / 'plan.json').write_text(json.dumps(plan, indent=2))
    if not args.run:
        print(f'Prepared {len(cases)} cases at {args.width}x{args.height}, {args.samples} spp. '
              'No application launched. Add --run to execute.')
        return 0
    for round_index in range(args.rounds):
        # Reverse alternate rounds to reduce systematic warmup/order bias.
        for name, _, _ in (cases if round_index % 2 == 0 else reversed(cases)):
            started = time.perf_counter()
            process = subprocess.Popen([*command, '--headless', '--frames', str(args.frames),
                                  '--time', str(args.time),
                                  '--world', str(levels / (name + '.json')),
                                  '--config', str(configs / (name + '.json'))],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                       start_new_session=(sys.platform != 'win32'))
            try:
                stdout, stderr = process.communicate(timeout=args.timeout)
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
            result['bloom_ms'] += numbers(r'GPU (?:fog/)?bloom/display: ([\d.]+) ms', log, args.frames)
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
              f' {statistics.mean(result["bloom_ms"]):10.3f} {app:10.3f} {MILLISECONDS_PER_SECOND/app:9.2f}')
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Profiling failed: {error}', file=sys.stderr)
        sys.exit(1)
