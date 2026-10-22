#!/usr/bin/env python3
"""Check faction motion through real flight and rendered poses."""
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world

root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova_faction_motion_') as directory:
 out=Path(directory);base=stage_world(binary.parent/'assets/levels/virus-player-level.json',out)
 base['match'].pop('starts');base['match'].update(auto_colonize=False,fully_established=True)
 base['rules']['ai']['expansion_delay']=300;base['rules']['economy']['enemy_attack_seconds']=10000
 base['bodies']=base['bodies'][:2]
 base['bodies'][0].update(position=[0,0,0]);base['bodies'][1].update(position=[100,0,0])
 for u in base['content']['units']:u['manufacture_seconds']=10000
 def fixture(faction,control=False,slow=False):
  d=deepcopy(base);enemy='immune' if faction!='immune' else 'virus';d['match'].update(player=faction,opponent=enemy)
  d['bodies'][0]['owner']=faction;d['bodies'][1]['owner']=enemy
  entry=next(f for f in d['content']['factions'] if f['name']==faction)
  unit=entry['production']['default_unit'];d['spawns']=[dict(type=unit,position=[0,0,7])]
  if slow:
   next(u for u in d['content']['units'] if u['name']==unit)['speed']=.01
  if control:
   p=json.loads((out/entry['personality']).resolve().read_text())
   p['motion'].update(acceleration=1,turn_rate=1,burst=0,wander=0,roll_degrees=0,cadence=0,organelle_amount=1,organelle_period=1,factory_amount=1,factory_period=1)
   path=out/(faction+'_control_personality.json');path.write_text(json.dumps(p));entry['personality']=path.name
  return d
 def run(name,d,ticks,orders=''):
  global count
  world=out/(name+'_world.json');write_world(world,d);state=out/(name+'_state.json');scene=out/(name+'_scene.json');replay=out/(name+'.orders');replay.write_text(orders)
  p=subprocess.run([*command,'--smoke','--world',str(world),'--world-seed','1234','--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene),*(['--replay',str(replay)] if orders else [])],capture_output=True,text=True,timeout=12);count+=1
  assert p.returncode==0,(name,p.stdout,p.stderr)
  return json.loads(state.read_text()),json.loads(scene.read_text())
 def unit_pose(state,scene):
  slot=state['units'][0]['slot'];return next(v for v in scene['instances'] if v['slot']==slot)['transform']
 def forward(matrix):
  v=[matrix[i] for i in (2,6,10)];n=math.sqrt(sum(x*x for x in v));return [x/n for x in v]
 # The Virus holds its facing between short turns; the control turns smoothly.
 command_flight='0 select all\n0 move 20 12 7\n'
 a=run('virus_20',fixture('virus'),20,command_flight);b=run('virus_21',fixture('virus'),21,command_flight)
 c=run('smooth_20',fixture('virus',True),20,command_flight);d=run('smooth_21',fixture('virus',True),21,command_flight)
 assert math.dist(forward(unit_pose(*a)),forward(unit_pose(*b)))<1e-5,'Virus facing did not hold between turns'
 assert math.dist(forward(unit_pose(*c)),forward(unit_pose(*d)))>1e-5,'The control did not exercise a turn'
 # Slow Immune units still roll. Factory and organelle poses also change.
 a=run('immune_wobble',fixture('immune',slow=True),120);b=run('immune_control',fixture('immune',True,True),120)
 assert math.dist(unit_pose(*a),unit_pose(*b))>.02,'Immune wobble did not reach the rendered ship'
 assert a[1]['meshes']==b[1]['meshes'],'Motion changed immutable model geometry'
 poses={v['slot']:v['transform'] for v in a[1]['instances']};slot=a[0]['units'][0]['slot']
 assert sum(v['slot']!=slot and v['slot'] in poses and math.dist(v['transform'],poses[v['slot']])>1e-4 for v in b[1]['instances'])>2,'Cell and factory motion did not differ'
 # Uncommanded fungal units take a different path while retaining a local task.
 a=run('fungal_wander',fixture('fungus'),120);b=run('fungal_control',fixture('fungus',True),120)
 assert math.dist(a[0]['units'][0]['flight_goal'],b[0]['units'][0]['flight_goal'])>1,'Fungal patrol did not wander'
 assert math.dist(a[0]['units'][0]['position'],b[0]['units'][0]['position'])>.1,'Wandering did not affect flight'
 assert a[0]['units'][0]['health']>0 and a[0]['units'][0]['faction']==3
 print(f'Faction motion E2E passed: {count} application runs; abrupt Virus turns, Immune roll, cell and factory poses, fungal wandering.')
