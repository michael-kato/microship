#!/usr/bin/env python3
"""Measure real combat ticks and compare full game state with a baseline build."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--world', type=Path)
    parser.add_argument('--dense', action='store_true', help='Prepare 90 seconds of production with Fungal sight')
    parser.add_argument('--ticks', type=int, default=3600)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--seed', type=int, default=1234)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/combat_profile')
    args = parser.parse_args()
    if args.ticks < 1 or args.repeats < 1:
        parser.error('ticks and repeats must be positive')
    binary = args.binary.resolve()
    world = (args.world or binary.parent / 'assets/levels/three_faction_stress.json').resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.dense:
        from asset_data import stage_world, write_world
        scene = stage_world(world, output)
        scene['match']['production_seconds'] = 90
        scene['match']['player'] = 'fungus'
        scene['rules']['exploration'] = dict(sight_radius=18.0)
        world = output / 'dense_combat_world.json'
        write_world(world, scene)
    builds = [('baseline', args.baseline.resolve()), ('candidate', binary)] if args.baseline else [('candidate', binary)]
    report = dict(world=str(world), dense=args.dense, ticks=args.ticks, seed=args.seed, runs=[])

    def run(build, name, flags):
        command = [str(build)] if sys.platform == 'win32' else [sys.executable, str(ROOT / 'scripts/proton_run.py'), str(build)]
        result = subprocess.run(command + ['--smoke', '--world', str(world), '--world-seed', str(args.seed)] + flags,
                                capture_output=True, text=True, timeout=180)
        (output / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]

    # Compare every exported game field after combat, including wounds, orders,
    # clouds, factory growth, and visibility. Timing records are not game state.
    states = []
    for label, build in builds:
        state = output / (label + '_state.json')
        run(build, label + '_state', ['--ticks', str(min(args.ticks, 1200)), '--state', str(state)])
        states.append(json.loads(state.read_text()))
    if len(states) == 2:
        assert states[0] == states[1], 'The optimization changed the exported game state'
        report['state_equal'] = True

    summaries = {}
    for repeat in range(args.repeats):
        # Reverse the order each pair to reduce warm-cache and clock bias.
        for label, build in builds[::1 if repeat % 2 == 0 else -1]:
            records = run(build, label + '_' + str(repeat),
                          ['--ticks', str(args.ticks), '--matches', '1', '--cpu-benchmark'])
            summary = next(row for row in records if 'faction_stats' in row)
            if summaries:
                assert summary == next(iter(summaries.values())), 'The match result changed'
            summaries[label] = summary
            timing = next(row for row in records if row.get('cpu_benchmark'))
            assert timing['ticks'] == args.ticks and timing['peak_units'] > 0, timing
            for key in ('mean_ms', 'p50_ms', 'p95_ms', 'p99_ms', 'max_ms'):
                assert math.isfinite(timing[key]) and timing[key] > 0, timing
            phases = next((row for row in records if row.get('cpu_phases')), None)
            report['runs'].append(dict(build=label, repeat=repeat, timing=timing, phases=phases))
            (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
            print(label, repeat, json.dumps(timing), flush=True)
    message = ('Combat comparison passed: full state and match results match; raw times saved to'
               if args.baseline else 'Combat benchmark passed: raw times saved to')
    print(message, output)


if __name__ == '__main__':
    main()
