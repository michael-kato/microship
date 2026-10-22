#!/usr/bin/env python3
"""Check capture costs, bomber seeds and carrier births in the real Windows game."""
from copy import deepcopy
import json,math,subprocess,sys,tempfile
from pathlib import Path
from asset_data import stage_world,write_world,change
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanovore-opening-') as directory:
 out=Path(directory)
 base=stage_world(binary.parent/'assets/levels/virus-player-level.json',out)
 base['match']=dict(player='virus',opponent='immune')
 base['rules'].pop('exploration',None);base['rules']['ai']['expansion_delay']=300
 base['rules']['economy']['enemy_attack_seconds']=10000
 home=deepcopy(base['bodies'][0]);base['bodies']=[deepcopy(home) for i in range(4)]
 for b,x,owner in zip(base['bodies'],[-80,80,0,15],['virus','immune','neutral','neutral']):
  b['position']=[x,0,0];b['owner']=owner;b['site']['availability']=0
 for u in base['content']['units']:u['manufacture_seconds']=10000
 def definition(d,name):return next(u for u in d['content']['units'] if u['name']==name)
 def run(name,data,ticks=600,orders='0 select all\n0 order 2\n',extra=(),expected=0):
  global count
  wp=out/(name+'_world.json');write_world(wp,data);state=out/(name+'_state.json');scene=out/(name+'_scene.json');rp=out/(name+'.orders');rp.write_text(orders)
  result=subprocess.run([*command,'--smoke','--world',str(wp),'--world-seed','1234','--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene),*(['--replay',str(rp)] if orders else []),*extra],capture_output=True,text=True,timeout=12);count+=1
  assert result.returncode==expected,(name,result.stdout,result.stderr)
  return (json.loads(state.read_text()),json.loads(scene.read_text())) if not expected else (None,None)
 def fixture(kind,number):
  d=deepcopy(base);f=definition(d,kind)['faction'];d['match'].update(player=f,opponent='immune' if f!='immune' else 'virus');d['bodies'][0]['owner']=f;d['bodies'][1]['owner']=d['match']['opponent']
  d['spawns']=[dict(type=kind,position=[(i-(number-1)/2)*.18,0,6]) for i in range(number)]
  return d
 for kind,required in [('immune_fighter',1),('virus_fighter',2),('fungal_spore',5)]:
  d=fixture(kind,required)
  if required>1:
   short=fixture(kind,required-1);s,_=run(kind+'_short',short)
   assert not s['colonies'][2]['active'] and len(s['units'])==required-1 and s['settlement']['colonists_spent']==0,(kind,s['settlement'])
  s,_=run(kind+'_exact',d)
  assert s['colonies'][2]['active'] and len(s['units'])==0 and s['settlement']['colonists_spent']==required,(kind,s['settlement'],s['units'])
  surplus=fixture(kind,required+2);s,_=run(kind+'_surplus',surplus)
  assert s['colonies'][2]['active'] and len(s['units'])==2 and s['settlement']['colonists_spent']==required
  assert s==run(kind+'_repeat',surplus)[0]
  reset,_=run(kind+'_restart',d,extra=('--restart',))
  assert reset['landings']==0 and reset['settlement']['colonists_spent']==0 and len(reset['units'])==required
  defended=deepcopy(d)
  enemy='virus_fighter' if kind=='immune_fighter' else 'immune_fighter'
  passive=definition(defended,enemy);passive.update(targets=['structures']);passive.pop('secondary_attack',None)
  passive['autonomy'].update(unit_radius=0,structure_radius=0);passive.pop('colonization',None);passive['colonizes']=False
  definition(defended,kind)['autonomy'].update(unit_radius=0,structure_radius=0)
  defended['spawns'].append(dict(type=enemy,position=[0,0,7.5]))
  blocked,_=run(kind+'_defended',defended,180)
  assert not blocked['colonies'][2]['active'] and blocked['settlement']['colonists_spent']==0 and len(blocked['units'])==required+1
  # A new move order cancels landing credit before the group can spend units.
  approaching,_=run(kind+'_approaching',d,80)
  assert not approaching['colonies'][2]['active'] and any(u['landing']>0 for u in approaching['units'])
  interrupted,_=run(kind+'_interrupted',d,130,'0 select all\n0 order 2\n80 move 0 0 15\n')
  assert not interrupted['colonies'][2]['active'] and all(u['landing']==0 for u in interrupted['units']) and len(interrupted['units'])==required
  resumed,_=run(kind+'_resumed',d,600,'0 select all\n0 order 2\n80 move 0 0 15\n180 order 2\n')
  assert resumed['colonies'][2]['active'] and resumed['settlement']['colonists_spent']==required
  # A distant member cannot be spent before it reaches the group.
  if required>1:
   absent=deepcopy(d);absent['spawns'][-1]['position']=[-50,0,8];s,_=run(kind+'_missing_member',absent,180)
   assert not s['colonies'][2]['active'] and len(s['units'])==required and s['settlement']['colonists_spent']==0
 for kind in ('virus_bomber','immune_bomber'):
  d=fixture(kind,1);s,scene=run(kind+'_falling',d,90)
  assert s['settlement']['seed_launches']==1 and len(s['units'])==1 and not s['colonies'][2]['active'],(kind,s['settlement'],s['units'],s['projectile_state'])
  seed=next(p for p in s['projectile_state'] if p['seed']);assert seed['type']==kind and math.dist(seed['position'],seed['previous'])>0
  visible=[i for i in scene['instances'] if math.dist(i['transform'][3::4],seed['position'])<1e-4]
  assert len(visible)==1 and 0<=visible[0]['mesh']<len(scene['meshes']),'Seed mesh is missing or invalid'
  settled,_=run(kind+'_settled',d,300)
  assert settled['colonies'][2]['active'] and len(settled['units'])==1 and settled['settlement']['colonists_spent']==0
  assert settled['units'][0]['generation']==s['units'][0]['generation'] and settled['settlement']['seed_launches']==1
  orphan=deepcopy(d);craft=definition(orphan,kind);craft.pop('payload',None)
  craft['attack']=deepcopy(definition(orphan,'fungal_spore')['attack']);craft['attack']['lifetime']=1.6
  dead,_=run(kind+'_seed_after_parent_death',orphan,300)
  assert dead['settlement']['seed_launches']==1 and dead['colonies'][2]['active'] and not dead['units']
  last=deepcopy(orphan);other=last['match']['opponent'];other_kind=other+'_bomber'
  last['match']['starts']=[dict(faction=last['match']['player'],cell=2),dict(faction=other,cell=1)]
  for b in last['bodies']:b['owner']='neutral'
  last['spawns'].append(dict(type=other_kind,position=[80,0,7]))
  restored,_=run(kind+'_last_seed_restores_faction',last,300)
  assert restored['outcome']==0 and restored['colonies'][2]['active'] and not any(u['type']==kind for u in restored['units'])
  invasion=deepcopy(d);invasion['bodies']=invasion['bodies'][:3]
  invasion['rules']['economy']['landing_seconds']=4;invasion['rules']['ai']['expansion_delay']=0
  enemy='virus_fighter' if kind=='immune_bomber' else 'immune_fighter'
  invader=definition(invasion,enemy);invader.update(speed=1.5,range=5,targets=['structures']);invader.pop('secondary_attack',None)
  invader['autonomy'].update(unit_radius=0,structure_radius=0,colonize_radius=30)
  invasion['spawns'].append(dict(type=enemy,position=[0,0,12]))
  cancelled,_=run(kind+'_seed_defended_in_flight',invasion,360)
  assert cancelled['settlement']['seed_launches']==1 and not cancelled['colonies'][2]['active'] and cancelled['blocked_landings']>0,(kind,cancelled['settlement'],cancelled['blocked_landings'],[(u['type'],u['position']) for u in cancelled['units']])
  again,_=run(kind+'_second_cell',d,1400,'0 select all\n0 order 2\n600 select bombers\n600 order 3\n')
  assert again['colonies'][2]['active'] and again['colonies'][3]['active'] and again['settlement']['seed_launches']==2
  assert sum(u['type']==kind for u in again['units'])==1 and again['settlement']['colonists_spent']==0
 carrier=fixture('immune_bomber',1);carrier['spawns'][0]['position']=[-80,0,8]
 early,_=run('carrier_before_interval',carrier,719,orders='')
 first,_=run('carrier_first_birth',carrier,721,orders='')
 later,_=run('carrier_second_birth',carrier,1442,orders='')
 assert early['settlement']['carrier_births']==0 and first['settlement']['carrier_births']==1 and later['settlement']['carrier_births']==2
 assert sum(u['type']=='immune_bomber' for u in later['units'])==1 and sum(u['type']=='immune_fighter' for u in later['units'])==2
 assert all(sum(c['produced'])==0 for c in later['colonies'])
 moving,_=run('carrier_orders',carrier,721,'0 select all\n0 move -50 20 10\n')
 assert moving['settlement']['carrier_births']==1 and all(u['moving'] and u['commanded'] for u in moving['units'])
 assert len({tuple(u['flight_goal']) for u in moving['units']})==1
 capacity=stage_world(binary.parent/'assets/levels/territory_stress.json',out)
 capacity['rules'].pop('exploration',None)
 for u in capacity['content']['units']:
  u.update(speed=.01,targets=['structures']);u.pop('secondary_attack',None)
  if 'autonomy' in u:u['autonomy'].update(unit_radius=0,structure_radius=0)
 definition(capacity,'immune_bomber')['carrier']['interval_seconds']=.1
 definition(capacity,'fungal_spore')['attack']['lifetime']=6
 full,_=run('carrier_storage_full',capacity,300,orders='')
 blocked=[u for u in full['units'] if u['carrier_blocked']]
 assert len(full['units'])==full['unit_capacity'] and blocked and all(abs(u['carrier_timer']-.1)<1e-5 for u in blocked)
 freed,_=run('carrier_storage_resumed',capacity,420,orders='')
 assert freed['effects']['deaths']>full['effects']['deaths'] and freed['settlement']['carrier_births']>full['settlement']['carrier_births']
 bomber=next(i for i,u in enumerate(base['content']['units']) if u['name']=='immune_bomber')
 fighter=next(i for i,u in enumerate(base['content']['units']) if u['name']=='virus_fighter')
 for name,path,value in [('zero_cost',f'content.units.{fighter}.colonization.required_units',0),('large_cost',f'content.units.{fighter}.colonization.required_units',17),('empty_seed',f'content.units.{bomber}.colonization.seed.model','missing.fmesh'),('wrong_child',f'content.units.{bomber}.carrier.unit','virus_fighter'),('recursive_carrier',f'content.units.{bomber}.carrier.unit','immune_bomber'),('zero_interval',f'content.units.{bomber}.carrier.interval_seconds',0),('unknown',f'content.units.{fighter}.colonization.invalid',True)]:
  run('invalid_'+name,change(carrier,path,value),1,orders='',expected=1)
print(f'Faction opening E2E passed: {count} real application runs; fixed group costs, atomic spend, surplus, missing members, seed flight, reusable bombers, carrier timing, orders, restart and invalid assets.')
