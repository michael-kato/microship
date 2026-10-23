#!/usr/bin/env python3
"""The two performance scenes: the battle stress level at maximum zoom-out and maximum zoom-in.

python3 tests/e2e/performance.py build/windows-cross/nanovore.exe [--ticks 1800] [--frames 30]

Both scenes pre-simulate the same battle, then render with the F1 profiler. The report gives
CPU simulation cost per tick over the 600 ticks after the pre-simulation, and each profiler
span as milliseconds and percent of its frame, so results compare with live F1 captures.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('binary', type=Path)
parser.add_argument('--ticks', type=int, default=1800, help='battle ticks simulated before measuring')
parser.add_argument('--frames', type=int, default=30, help='rendered frames per scene; the first two are not averaged')
parser.add_argument('--output', type=Path, default=root / 'build/performance')
args = parser.parse_args()
binary = args.binary.resolve()
out = (args.output / datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
out.mkdir(parents=True, exist_ok=True)
command = [str(binary)] if sys.platform == 'win32' else [sys.executable, str(root / 'scripts/proton_run.py'), str(binary)]
level = ['--world', str(root / 'assets/levels/performance_battle.json'), '--world-seed', '1234']
WINDOW = 600
PHASES = ('ai', 'visuals', 'recovery', 'movement', 'combat', 'growth', 'sight', 'appearance', 'fog')


def run(name, extra):
    log = out / (name + '.log')
    with log.open('w') as file:
        result = subprocess.run(command + level + extra, stdout=file, stderr=subprocess.STDOUT, cwd=root)
    text = log.read_text(errors='replace')
    if result.returncode:
        sys.exit(f'{name} failed with exit code {result.returncode}; see {log}')
    return text


def benchmark(ticks):
    text = run(f'cpu_{ticks}', ['--smoke', '--matches', '1', '--ticks', str(ticks), '--cpu-benchmark'])
    lines = [json.loads(line) for line in text.splitlines() if line.startswith('{"cpu_')]
    totals = next(line for line in lines if 'cpu_benchmark' in line)
    phases = next(line for line in lines if 'cpu_phases' in line)
    return totals['total_tick_ms'], {p: phases['mean_ms'][p] * totals['ticks'] for p in PHASES}, totals


# Deterministic runs: the measured window is the difference of two equal prefixes.
before_ms, before_phases, _ = benchmark(args.ticks)
after_ms, after_phases, after = benchmark(args.ticks + WINDOW)
tick_ms = (after_ms - before_ms) / WINDOW
phase_ms = {p: (after_phases[p] - before_phases[p]) / WINDOW for p in PHASES}
report = {'ticks': args.ticks, 'window_ticks': WINDOW, 'cpu': {'tick_ms': tick_ms, 'peak_units': after['peak_units'],
    'phases_ms': phase_ms}, 'scenes': {}}
print(f'CPU simulation, ticks {args.ticks}-{args.ticks + WINDOW}: {tick_ms:.2f} ms per tick; '
      f'{tick_ms * 60 / 10:.1f}% of real time ({after["peak_units"]} peak units)')
for p in sorted(PHASES, key=lambda p: -phase_ms[p]):
    if phase_ms[p] >= .005:
        print(f'  {p:<10} {phase_ms[p]:7.3f} ms  {100 * phase_ms[p] / tick_ms:5.1f}%')

pattern = re.compile(r'^Profile (.+): start [-\d.]+ ms; last ([\d.]+) ms;')
for name, zoom in (('zoomed_out', '-1000'), ('zoomed_in', '1000')):
    capture = out / (name + '.ppm')
    text = run(name, ['--ticks', str(args.ticks), '--headless', '--stats', '--frames', str(args.frames),
                      '--camera-zoom', zoom, '--capture', str(capture)])
    frames, current = [], {}
    for line in text.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        if match.group(1) in current:
            frames.append(current)
            current = {}
        current[match.group(1)] = float(match.group(2))
    frames.append(current)
    measured = frames[2:] if len(frames) > 2 else frames
    spans = {node: statistics.mean(f[node] for f in measured) for node in measured[0]}
    report['scenes'][name] = {'frames': len(measured), 'spans_ms': spans, 'capture': str(capture)}
    gpu, cpu = spans['GPU FRAME'], spans['CPU FRAME']
    print(f'{name}: GPU {gpu:.2f} ms, CPU {cpu:.2f} ms without simulation, mean of {len(measured)} frames')
    for node, ms in sorted(spans.items(), key=lambda item: -item[1]):
        if node in ('GPU FRAME', 'CPU FRAME') or ms < .05:
            continue
        frame = gpu if node in ('SCENE', 'AS UPDATE', 'BACKDROP', 'LIGHTING FIELDS', 'SURFACE + SSS', 'VOLUME', 'POST', 'EFFECTS',
            'CELL BLUR', 'FOG', 'TILT SHIFT', 'BLOOM + DISPLAY', 'OUTPUT', 'HUD + PROFILER', 'OUTPUT COPY') else cpu
        print(f'  {node:<20} {ms:7.2f} ms  {100 * ms / frame:5.1f}%')
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'Report: {out / "report.json"}')
