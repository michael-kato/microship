#!/usr/bin/env python3
"""Measure the proposed fighter ratios in controlled fights in the real application."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
from asset_data import stage_world,write_world

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,required=True)
p.add_argument('--seeds',type=int,default=5);p.add_argument('--ticks',type=int,default=1800)
a=p.parse_args();binary=a.binary.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert 1<=a.seeds<=100 and 1<=a.ticks<=3600
base=stage_world(binary.parent/'assets/levels/virus-player-level.json',out)
base['match']=dict(player='immune',opponent='virus',auto_colonize=False)
base['rules'].pop('exploration',None);base['rules']['ai']['expansion_delay']=300
base['rules']['economy']['enemy_attack_seconds']=10000
for u in base['content']['units']:
 u['manufacture_seconds']=10000
 if 'autonomy' in u:u['autonomy']['leash_radius']=100
for b in base['bodies']:b['site']['availability']=0
home=deepcopy(base['bodies'][0]);base['bodies']=[dict(deepcopy(home),position=[-80,0,0]),dict(deepcopy(home),position=[80,0,0])]
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(ROOT/'scripts/proton_run.py'),str(binary)]
rosters={'immune':('immune_fighter',1),'virus':('virus_fighter',2),'fungus':('fungal_spore',5)}
report=dict(ticks=a.ticks,seeds=a.seeds,rosters={k:v[1] for k,v in rosters.items()},fights=[])
for pair in [('immune','virus'),('immune','fungus'),('virus','fungus')]:
 for side in (0,1):
  for seed in range(1,a.seeds+1):
   world=deepcopy(base);world['match'].update(player=pair[0],opponent=pair[1]);world['spawns']=[]
   for rank,faction in enumerate(pair):
    world['bodies'][rank]['owner']=faction;kind,count=rosters[faction]
    x=(-2 if rank==side else 2)
    world['spawns'] += [dict(type=kind,position=[x,(i-(count-1)/2)*.45,8]) for i in range(count)]
   name='_'.join(pair)+f'_{side}_{seed}';wp=out/(name+'_world.json');write_world(wp,world);state=out/(name+'_state.json')
   result=subprocess.run([*command,'--smoke','--world',str(wp),'--world-seed',str(seed),'--ticks',str(a.ticks),'--state',str(state)],capture_output=True,text=True,timeout=15)
   (out/(name+'.log')).write_text(result.stdout+result.stderr)
   assert result.returncode==0,(name,result.stdout,result.stderr)
   data=json.loads(state.read_text());stats={f['faction']:f for f in data['faction_stats']}
   alive={f:stats[f]['units'] for f in pair};winner=next((f for f in pair if alive[f]>0 and alive[pair[1] if f==pair[0] else pair[0]]==0),'undecided')
   assert data['effects']['deaths']>0,(name,'No combat occurred')
   health={f:sum(u['health'] for u in data['units'] if u['faction']=={'virus':1,'immune':2,'fungus':3}[f]) for f in pair}
   report['fights'].append(dict(pair=list(pair),side=side,seed=seed,survivors=alive,surviving_health=health,winner=winner,last_death_tick=data['effects']['last_death_tick']))
   (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 for faction in pair:print(pair,faction,'wins',sum(f['winner']==faction for f in report['fights'] if f['pair']==list(pair)),flush=True)
print('Fighter ratio measurements saved:',out/'report.json')
