#!/usr/bin/env python3
"""Play every faction pairing with the built-in AI on both sides and report balance.

Each pairing, role and start cell runs one real application process in
--smoke --matches mode (simulation only, no GPU) that plays every seed back to
back and stops each match at its outcome. Both sides use the same AI, fog is
off and both sides swap start cells, so the remaining differences come from
faction content. The report lists win rates, decision times, production and
losses, and flags suspect results.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from itertools import combinations
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time
from asset_data import stage_world, write_world

TICKS_PER_SECOND = 60

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--level', type=Path, default=root/'assets/levels/virus-player-level.json')
parser.add_argument('--seeds', type=int, default=10, help='World seeds per pairing, role and start cell')
parser.add_argument('--minutes', type=float, default=15, help='Match time limit in game minutes')
parser.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2)//2))
parser.add_argument('--factions', help='Comma-separated subset, for example virus,immune')
parser.add_argument('--output', type=Path, default=root/'build/faction-balance')
a = parser.parse_args()
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
binary = a.binary.resolve()
prefix = [str(binary)] if sys.platform == 'win32' else [sys.executable, str(root/'scripts/proton_run.py'), str(binary)]
base = stage_world(a.level, out)
names = [f['name'] for f in base['content']['factions']]
if a.factions: names = [n for n in names if n in a.factions.split(',')]
ticks = int(a.minutes*60*TICKS_PER_SECOND)
attack_seconds = base['rules']['economy'].get('enemy_attack_seconds', 0)

unit_faction = {u['name']: u['faction'] for u in base['content']['units']}

def roster(faction):
    """Starting craft from the faction's own player level, as offsets from its start cell."""
    for level in sorted((root/'assets/levels').glob('*.json')):
        data = json.loads(level.read_text())
        if data['match']['player'] != faction: continue
        cell = next(s['cell'] for s in data['match']['starts'] if s['faction'] == faction)
        home = data['bodies'][cell]['position']
        return [(s['type'], [p-h for p, h in zip(s['position'], home)]) for s in data['spawns'] if unit_faction[s['type']] == faction]
    raise SystemExit(f'No player level defines a starting roster for {faction}.')

def world_for(player, opponent, player_cell):
    data = json.loads(json.dumps(base))
    match = data['match']
    match.update(player=player, opponent=opponent, auto_colonize=False, player_ai=True)
    match['starts'] = [dict(faction=player, cell=player_cell), dict(faction=opponent, cell=1-player_cell)]
    # Both sides get their faction's player roster, so start strength is symmetric.
    data['spawns'] = []
    for faction, cell in ((player, player_cell), (opponent, 1-player_cell)):
        home = data['bodies'][cell]['position']
        data['spawns'] += [dict(type=kind, position=[h+o for h, o in zip(home, offset)]) for kind, offset in roster(faction)]
    data['rules'].pop('exploration', None)  # No fog: the AI never used it.
    return data

jobs = []
for x, y in combinations(names, 2):
    for player, opponent in ((x, y), (y, x)):
        for cell in (0, 1):
            name = f'{player}-vs-{opponent}-cell{cell}'
            path = out/(name+'-world.json'); write_world(path, world_for(player, opponent, cell))
            jobs.append((name, player, opponent, cell, path))

def play(job):
    name, player, opponent, cell, path = job
    result = subprocess.run([*prefix, '--smoke', '--world', str(path), '--ticks', str(ticks), '--world-seed', '1',
                             '--matches', str(a.seeds)], capture_output=True, text=True, timeout=600)
    assert result.returncode == 0, (name, result.stdout[-2000:], result.stderr[-2000:])
    matches = []
    for line in result.stdout.splitlines():
        if not line.startswith('{'): continue
        data = json.loads(line)
        stats = {s['faction']: s for s in data['faction_stats']}
        colonies = {n: stats[n]['colonies'] for n in (player, opponent)}
        alive = {n: stats[n]['units'] for n in (player, opponent)}
        if data['outcome']:
            winner = player if data['outcome'] > 0 else opponent
            decided = data['outcome_tick']/TICKS_PER_SECOND
        else:
            # Undecided at the limit: lead by colonies, then surviving units.
            lead = sorted((colonies[n], alive[n], n) for n in (player, opponent))
            winner = None if lead[0][:2] == lead[1][:2] else lead[1][2]
            decided = None
        matches.append(dict(name=name, player=player, opponent=opponent, cell=cell, seed=data['seed'], outcome=data['outcome'],
                            winner=winner, decided_seconds=decided, colonies=colonies, alive=alive,
                            stats={n: stats[n] for n in (player, opponent)}, capacity_blocked=data['capacity_blocked'],
                            unit_capacity=data['unit_capacity']))
    assert len(matches) == a.seeds, (name, result.stdout[-2000:])
    return matches

if not jobs: raise SystemExit('Need at least two factions.')
print(f'Playing {len(jobs)*a.seeds} AI-vs-AI matches of up to {a.minutes:g} game minutes ({ticks} ticks) in {len(jobs)} processes with {a.jobs} jobs...', flush=True)
started = time.monotonic()
with ThreadPoolExecutor(a.jobs) as pool: results = [m for batch in pool.map(play, jobs) for m in batch]
(out/'results.json').write_text(json.dumps(results, indent=1)+'\n')

def mean(values): return statistics.mean(values) if values else float('nan')
issues = []
print(f'\nMatchups ({a.seeds} seeds x 2 start cells x 2 roles each; {time.monotonic()-started:.0f} s wall):')
print(f'{"pairing":<18}{"wins A":>7}{"wins B":>7}{"undecided":>10}{"decided min":>12}{"built A":>9}{"built B":>9}{"lost A":>8}{"lost B":>8}{"peak A":>8}{"peak B":>8}')
for x, y in combinations(names, 2):
    games = [r for r in results if {r['player'], r['opponent']} == {x, y}]
    wins = {x: sum(r['winner'] == x for r in games), y: sum(r['winner'] == y for r in games)}
    decisive = [r for r in games if r['outcome']]
    undecided = len(games)-len(decisive)
    decided = [r['decided_seconds']/60 for r in decisive]
    built = {n: mean([r['stats'][n]['built'] for r in games]) for n in (x, y)}
    lost = {n: mean([r['stats'][n]['lost'] for r in games]) for n in (x, y)}
    peak = {n: mean([r['stats'][n]['peak_units'] for r in games]) for n in (x, y)}
    print(f'{x+"/"+y:<18}{wins[x]:>7}{wins[y]:>7}{undecided:>10}{mean(decided):>12.1f}{built[x]:>9.0f}{built[y]:>9.0f}'
          f'{lost[x]:>8.0f}{lost[y]:>8.0f}{peak[x]:>8.0f}{peak[y]:>8.0f}')
    share = max(wins.values())/max(1, sum(wins.values()))
    if sum(wins.values()) >= 4 and share >= .8:
        issues.append(f'{x}/{y}: {max(wins, key=wins.get)} wins {max(wins.values())} of {sum(wins.values())} decided or led games.')
    if undecided > len(games)/2:
        issues.append(f'{x}/{y}: {undecided} of {len(games)} matches undecided after {a.minutes:g} minutes (stalemate).')
    for side in (x, y):
        role = [r for r in games if r['player'] == side]
        role_wins = sum(r['winner'] == side for r in role)
        other = [r for r in games if r['opponent'] == side]
        other_wins = sum(r['winner'] == side for r in other)
        if abs(role_wins-other_wins) >= max(3, len(role)//2):
            issues.append(f'{x}/{y}: {side} wins {role_wins}/{len(role)} as player but {other_wins}/{len(other)} as opponent (role asymmetry).')
    for cell in (0, 1):
        at = [r for r in games if (r['player'] if r['cell'] == cell else r['opponent']) == x]
        cell_wins = sum(r['winner'] == x for r in at)
        if len(at) >= 4 and cell_wins in (0, len(at)) and wins[x] not in (0, len(games)):
            issues.append(f'{x}/{y}: {x} starting at cell {cell} wins {cell_wins}/{len(at)} (start-position bias).')
for r in results:
    for side in (r['player'], r['opponent']):
        s = r['stats'][side]
        if s['built'] == 0: issues.append(f'{r["name"]} seed {r["seed"]}: {side} built no units.')
        if s['peak_units'] >= r['unit_capacity']*.9 and not r['outcome']: issues.append(f'{r["name"]} seed {r["seed"]}: {side} reached {s["peak_units"]} units (storage capacity {r["unit_capacity"]}).')
    if r['decided_seconds'] is not None and r['decided_seconds'] < attack_seconds:
        issues.append(f'{r["name"]} seed {r["seed"]}: decided at {r["decided_seconds"]:.0f} s, before the {attack_seconds:g} s assault time.')
    if r['capacity_blocked'] and not r['outcome']: issues.append(f'{r["name"]} seed {r["seed"]}: {r["capacity_blocked"]} sites blocked by unit storage at the end.')
print('\nFlagged:' if issues else '\nNo balance flags.')
for line in issues: print(' -', line)
(out/'report.txt').write_text('\n'.join(issues)+'\n')
print(f'\nResults: {out/"results.json"}')
