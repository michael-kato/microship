#!/usr/bin/env python3
"""Exercise charging, capture, digestion, rescue, and unit direction in the Windows app."""
from copy import deepcopy
import json,math,subprocess,sys,tempfile
from pathlib import Path
from asset_data import stage_world,write_world,change
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanovore-engulf-') as directory:
 out=Path(directory);base=stage_world(root/'assets/levels/virus-player-level.json',out)
 for start in base['match'].pop('starts'):base['bodies'][start['cell']]['owner']=start['faction']
 base['match'].pop('auto_colonize');base['bodies']=base['bodies'][:2];base['bodies'][0]['position']=[-40,0,0];base['bodies'][1]['position']=[40,0,0]
 base['rules'].pop('exploration');base['rules']['economy'].update(manufacture_seconds=1000,enemy_attack_seconds=300);base['rules']['ai']['expansion_delay']=300
 for u in base['content']['units']:
  u['cost']=100;u['manufacture_seconds']=1000
  if 'autonomy' in u:u['autonomy']['leash_radius']=100
 def unit(data,name):return next(u for u in data['content']['units'] if u['name']==name)
 def run(name,data,ticks=1,orders='',expected=0):
  global count
  wp=out/(name+'-world.json');write_world(wp,data);state=out/(name+'.json');scene=out/(name+'-scene.json')
  cmd=[*prefix,'--smoke','--world',str(wp),'--ticks',str(ticks),'--world-seed','1234','--state',str(state),'--scene-state',str(scene)]
  if orders:
   replay=out/(name+'.orders');replay.write_text(orders);cmd+=['--replay',str(replay)]
  result=subprocess.run(cmd,capture_output=True,text=True,timeout=10);count+=1
  assert result.returncode==expected,(name,result.stdout,result.stderr)
  if expected:return None,None
  return json.loads(state.read_text()),json.loads(scene.read_text())
 def find(s,kind):return next(u for u in s['units'] if u['type']==kind)
 charge=deepcopy(base);charge['spawns']=[dict(type='immune_fighter',position=[-4,0,8]),dict(type='virus_fighter',position=[0,0,8])]
 unit(charge,'virus_fighter').update(speed=.01,targets=['structures'],secondary_attack={'targets':['units'],'interval_factor':8})
 unit(charge,'virus_fighter').pop('secondary_attack')
 charged,_=run('charge',charge,45);slow=deepcopy(charge);unit(slow,'immune_fighter')['attack']['charge_speed']=1;unit(slow,'immune_fighter')['attack']['charge_acceleration']=1
 baseline,_=run('no-charge',slow,45)
 fast=find(charged,'immune_fighter');ordinary=find(baseline,'immune_fighter')
 assert fast['charging'] and math.dist(fast['position'],[0,0,8])<math.dist(ordinary['position'],[0,0,8])-.15,(fast,ordinary)
 assert math.dist(fast['velocity'],[0,0,0])>math.dist(ordinary['velocity'],[0,0,0])*1.3
 held=deepcopy(base);held['spawns']=[dict(type='immune_fighter',position=[0,0,8]),dict(type='virus_fighter',position=[.35,0,8])]
 unit(held,'immune_fighter')['speed']=.01;unit(held,'virus_fighter').update(hits=20,health=240,targets=['structures'],secondary_attack={'targets':['units'],'interval_factor':8})
 unit(held,'virus_fighter').pop('secondary_attack')
 first,_=run('held-a',held,45,'30 select all\n30 move 10 0 8\n');later,_=run('held-b',held,90,'30 select all\n30 move 10 0 8\n')
 a=find(first,'virus_fighter');b=find(later,'virus_fighter');assert a['held_by']>=0 and a['held_by']==b['held_by'] and a['position']==b['position'] and b['moving'],(a,b)
 assert b['wounds']>a['wounds'] and later['faction_events']['digestions']>0
 armed=deepcopy(held);unit(armed,'virus_fighter').update(targets=['units'],shot_interval=.1,detaches=False)
 armed_a,_=run('armed-held-a',armed,45);armed_b,_=run('armed-held-b',armed,90)
 assert find(armed_a,'virus_fighter')['held_by']>=0 and find(armed_b,'virus_fighter')['held_by']>=0
 assert armed_a['effects']['shots']==armed_b['effects']['shots'],'A captive continued firing'
 healed=deepcopy(held);unit(healed,'immune_fighter')['attack']['release_hits']=32
 healed['spawns'].append(dict(type='virus_bomber',position=[-2,0,8]))
 unit(healed,'virus_bomber').update(speed=.01,targets=['units'],shot_interval=.9);unit(healed,'virus_bomber').pop('payload');unit(healed,'virus_bomber').pop('secondary_attack')
 fed,_=run('healed',healed,240);assert fed['faction_events']['digest_heals']>0 and find(fed,'immune_fighter')['health']>0,fed['faction_events']
 rescue=deepcopy(held);unit(rescue,'virus_fighter').update(hits=3,health=36,speed=.01)
 # Bombers supply supporting fire without changing the victim's own attack behavior.
 for y in (-.35,0,.35):rescue['spawns'].append(dict(type='virus_bomber',position=[-2,y,8]))
 unit(rescue,'virus_bomber').update(speed=.01,targets=['units'],detaches=False,shot_interval=.5);unit(rescue,'virus_bomber').pop('payload');unit(rescue,'virus_bomber').pop('secondary_attack')
 saved,_=run('rescued',rescue,90);attacker=find(saved,'immune_fighter');prey=find(saved,'virus_fighter')
 assert saved['faction_events']['releases']>=1 and prey['held_by']==-1 and attacker['latch']==-1 and attacker['latch_recovery']>0,(saved['faction_events'],attacker,prey)
 alone=deepcopy(rescue);alone['spawns']=alone['spawns'][:2];lost,_=run('not-rescued',alone,240);assert not any(u['type']=='virus_fighter' for u in lost['units'])
 killed=deepcopy(rescue);unit(killed,'immune_fighter')['hits']=1;released,_=run('attacker-killed',killed,60)
 assert not any(u['type']=='immune_fighter' for u in released['units']) and find(released,'virus_fighter')['held_by']==-1
 # Compare the existing mesh with and without the rear pigment ramp.
 tinted,view=run('rear-pigment',held)
 neutral=deepcopy(held);unit(neutral,'immune_fighter').pop('rear_tint');unchanged,plain=run('rear-untinted',neutral)
 assert tinted==unchanged,'Unit pigment changed the simulation'
 mesh=len(held['bodies'])+next(i for i,u in enumerate(held['content']['units']) if u['name']=='immune_fighter')
 rear=sum(view['meshes'][mesh]['axis_colors'][0])/sum(plain['meshes'][mesh]['axis_colors'][0])
 front=sum(view['meshes'][mesh]['axis_colors'][1])/sum(plain['meshes'][mesh]['axis_colors'][1])
 assert rear<front-.2 and front>.9,(rear,front)
 # Restart must drop all attachments and recovery timers.
 wp=out/'restart-world.json';write_world(wp,rescue);path=out/'restart-state.json'
 result=subprocess.run([*prefix,'--smoke','--world',str(wp),'--ticks','90','--restart','--state',str(path)],capture_output=True,text=True,timeout=10);count+=1;assert result.returncode==0
 reset=json.loads(path.read_text());assert all(u['held_by']==-1 and u['latch']==-1 and u['latch_recovery']==0 for u in reset['units'])
 # Rendering changes cannot change the combat result.
 dark=deepcopy(rescue);dark['appearance']['ambient']=[0,0,0];unlit,_=run('same-combat-unlit',dark,90)
 assert all(unlit[k]==saved[k] for k in ('units','colonies','faction_events','outcome'))
 for key,value in [('charge_speed',.5),('charge_acceleration',9),('charge_radius',0),('release_hits',0),('release_seconds',11)]:
  bad=deepcopy(base);bad['spawns']=held['spawns'];unit(bad,'immune_fighter')['attack'][key]=value;run('invalid-'+key,bad,expected=1)
 bad=deepcopy(held);unit(bad,'immune_fighter')['attack'].pop('release_seconds');run('missing-release-seconds',bad,expected=1)
 bad=deepcopy(held);unit(bad,'virus_fighter')['attack']={'mode':'projectile','charge_speed':2};run('wrong-attack-mode',bad,expected=1)
 print('Charge distance gain:',math.dist(ordinary['position'],[0,0,8])-math.dist(fast['position'],[0,0,8]))
 print('Rescue events:',saved['faction_events'])
print(f'Immune E2E passed: {count} real-application runs; charge, held prey, feeding, allied rescue, death release, restart, render independence and invalid settings.')
