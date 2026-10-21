#!/usr/bin/env python3
"""Check three-faction maps and the developed central battle in the real game."""
from copy import deepcopy
import json,math,subprocess,sys,tempfile
from pathlib import Path
from asset_data import stage_world,write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0

def check_layout(layout,colonies):
 # Generated cells: homes on the field circle, the center cell near the middle,
 # neutral cells inside the disk, sized by Voronoi area, all at least the gap apart.
 points=[c['position'] for c in colonies];radii=[c['radius'] for c in colonies]
 homes=layout['home']['count'];R=layout['field_radius'];low,high=layout['neutral']['radius_range']
 assert len(colonies)==layout['cells'] and all(abs(p[2])<1e-6 for p in points)
 for k in range(homes):
  angle=math.radians(layout['first_home_degrees']+360*k/homes)
  assert math.dist(points[k][:2],[R*math.cos(angle),R*math.sin(angle)])<1e-3 and radii[k]==layout['home']['cell']['radius']
 assert math.hypot(*points[homes][:2])<=layout['center']['jitter']+1e-3 and abs(radii[homes]-layout['center']['cell']['radius'])<1e-6
 neutral=radii[homes+1:]
 assert all(math.hypot(*p[:2])<=R+1e-3 for p in points[homes+1:])
 assert abs(max(neutral)-high)<1e-5 and abs(min(neutral)-low)<1e-5 and len(set(round(r,4) for r in neutral))==len(neutral)
 gaps=[math.dist(p,q)-radii[i]-radii[j] for i,p in enumerate(points) for j,q in enumerate(points) if i<j]
 assert min(gaps)>=layout['minimum_gap']-1e-4,min(gaps)
 return points

with tempfile.TemporaryDirectory(prefix='nanova-three-factions-') as directory:
 out=Path(directory)
 def run(name,world,ticks=1,expected=0,extra=()):
  global count
  if isinstance(world,dict):p=out/(name+'_world.json');write_world(p,world)
  else:p=world
  state=out/(name+'_state.json')
  result=subprocess.run([*command,'--smoke','--world',str(p),'--world-seed','1234','--ticks',str(ticks),'--state',str(state),*extra],capture_output=True,text=True,timeout=25);count+=1
  assert result.returncode==expected,(name,result.stdout,result.stderr)
  return json.loads(state.read_text()) if not expected else None
 layouts=[]
 for side in ('virus','immune','fungus'):
  path=root/'assets/levels'/(side+'_large_level.json');d=json.loads(path.read_text())
  assert d['layout']['cells']==15 and {v['faction'] for v in d['match']['starts']}=={'virus','immune','fungus'}
  assert [v['cell'] for v in d['match']['starts']]==[0,1,2] and [s['cell'] for s in d['spawns']]==[0,1,2]
  initial=run(side+'_initial',path);assert {u['faction'] for u in initial['units']}=={1,2,3}
  layout=check_layout(d['layout'],initial['colonies'])
  layouts.append(layout)
  game=run(side+'_settled',path,2400)
  homes={v['faction']:v['cell'] for v in d['match']['starts']}
  assert all(game['colonies'][i]['active'] for i in homes.values()),(side,homes,[(c['faction'],c['active']) for c in game['colonies']])
  assert {game['colonies'][i]['faction'] for i in homes.values()}=={1,2,3} and game['outcome']==0
  assert all(c['production']>0 for c in game['colonies'] if c['active'])
 # The world seed selects the layout; the same seed repeats it.
 assert layouts[0]==layouts[1]==layouts[2]
 large=root/'assets/levels/virus_large_level.json';large_data=json.loads(large.read_text())
 others=[check_layout(large_data['layout'],run('seed_'+str(seed),large,extra=('--world-seed',str(seed)))['colonies']) for seed in (7,99,4242)]
 assert all(o!=layouts[0] for o in others) and len({tuple(map(tuple,o)) for o in others})==3
 # One opponent expires before settling. The third faction's founder must prevent victory.
 pending=stage_world(root/'assets/levels/virus_large_level.json',out)
 spore=next(u for u in pending['content']['units'] if u['name']=='fungal_spore')
 for u in pending['content']['units']:
  u['manufacture_seconds']=10000
  if u['name']=='immune_fighter':u['attack']=deepcopy(spore['attack']);u['attack']['lifetime']=.05
 spore['speed']=.01;spore['attack']['lifetime']=120
 game=run('third_founder_prevents_win',pending,900)
 assert game['colonies'][0]['active'] and not game['colonies'][1]['active'] and not game['colonies'][2]['active']
 assert any(u['faction']==3 for u in game['units']) and game['outcome']==0
 stress=root/'assets/levels/three_faction_stress.json'
 a=run('stress_initial',stress,extra=('--restart',))
 assert len(a['colonies'])==15 and 0<len(a['units'])<a['unit_capacity'] and a['prepared_production_ticks']==3600
 assert all(c['active'] and all(s['health']>0 and s['growth']==1 for s in c['structures']) for c in a['colonies'])
 assert all(sum(c['faction']==f for c in a['colonies'])==5 for f in (1,2,3))
 assert all(sum(u['faction']==f for u in a['units'])==sum(sum(c['produced']) for c in a['colonies'] if c['faction']==f)>0 for f in (1,2,3))
 shorter=stage_world(stress,out);shorter['match']['production_seconds']=30
 half=run('half_minute',shorter,extra=('--restart',))
 assert all(x['units']<=y['units'] for x,y in zip(half['faction_stats'],a['faction_stats']))
 assert half['faction_stats'][2]['units']<a['faction_stats'][2]['units']
 slowed=stage_world(stress,out)
 for u in slowed['content']['units']:
  if u['faction']=='virus':u['manufacture_seconds']=10000
 changed=run('virus_production_disabled',slowed,extra=('--restart',))
 assert changed['faction_stats'][0]['units']==0
 assert [f['units'] for f in changed['faction_stats'][1:]]==[f['units'] for f in a['faction_stats'][1:]]
 initial_generations={u['generation'] for u in a['units']}
 print('One minute of production:',[(f['faction'],f['units']) for f in a['faction_stats']])
 assert all(u['moving'] and math.dist(u['flight_goal'],[0,0,0])<.001 for u in a['units'])
 b=run('stress_approach',stress,300)
 for f in (1,2,3):
  first=[u for u in a['units'] if u['faction']==f];later=[u for u in b['units'] if u['faction']==f and u['generation'] in initial_generations]
  assert later and sum(math.dist(u['position'],[0,0,0]) for u in later)/len(later)<sum(math.dist(u['position'],[0,0,0]) for u in first)/len(first)
 c=run('stress_battle',stress,1200)
 assert c['effects']['shots']>100 and c['effects']['hits']>100 and c['effects']['deaths']>100
 assert all(f['lost']>0 for f in c['faction_stats']),c['faction_stats']
 print('Battle after 20 seconds:',c['effects'])
 assert all(f['colonies']==5 for f in c['faction_stats']) and c['outcome']==0
 reset=run('stress_restart',stress,300,extra=('--restart',))
 assert reset['tick']==0 and len(reset['units'])==len(a['units']) and all(c['growth']==1 for c in reset['colonies'])
 bad=deepcopy(pending);bad['spawns']=[s for s in bad['spawns'] if s['type']!='fungal_spore'];run('missing_third_founder',bad,expected=1)
 bad=deepcopy(pending);bad['match']['starts'][2]['cell']=0;run('duplicate_start',bad,expected=1)
 bad=deepcopy(pending);bad['layout']['minimum_gap']=40;run('impossible_gap',bad,expected=1)
 bad=deepcopy(pending);bad['spawns'][0]['position']=[0,0,7];run('spawn_position_and_cell',bad,expected=1)
 bad=deepcopy(pending);bad['bodies']=[];run('bodies_and_layout',bad,expected=1)
 bad=json.loads(stress.read_text());bad['match']['production_seconds']=0;run('zero_production',bad,expected=1)
 bad=json.loads(stress.read_text());bad['match']['production_seconds']=120;run('production_overflow',bad,expected=1)
 bad=json.loads(stress.read_text());bad['spawns']=[dict(type='virus_fighter',position=[0,0,0])];run('mixed_starting_units',bad,expected=1)
print(f'Three factions E2E passed: {count} application runs; seeded Voronoi 15-cell maps, homes on the field circle, a center cell, area-sized neutral cells and gaps, seed variation, three settlements, pending third faction, one minute of actual factory production, full development, shared battle point, combat losses on every side, restart and invalid setup rejection.')
