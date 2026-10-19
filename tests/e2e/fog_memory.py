#!/usr/bin/env python3
"""Check sight decay, falloff, scouting, and enemy hiding in the real application."""
from copy import deepcopy
import argparse,contextlib,json,math,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
count=0
with (contextlib.nullcontext(str(a.output.resolve())) if a.output else tempfile.TemporaryDirectory(prefix='nanova-fog-')) as directory:
 out=Path(directory);out.mkdir(parents=True,exist_ok=True);data=stage_world(root/'assets/levels/virus-player-level.json',out)
 data['spawns']=[dict(type='virus_fighter',position=[-45,0,6.4]),dict(type='immune_fighter',position=[-40,0,6.4])]
 for unit in data['content']['units']:
  unit['range']=.001
  if unit['name']=='virus_fighter':unit['speed']=6
  if unit['name']=='immune_fighter':unit['speed']=.001
 replay=out/'scout.orders';replay.write_text('0 select all\n0 move -45 200 6.4\n')
 def run(name,world,ticks=1,extra=(),expected=0):
  global count
  wp=out/(name+'-world.json');write_world(wp,world);state=out/(name+'-state.json');scene=out/(name+'-scene.json')
  result=subprocess.run([*prefix,'--smoke','--world',str(wp),'--ticks',str(ticks),'--world-seed','1234','--replay',str(replay),'--state',str(state),'--scene-state',str(scene),*extra],capture_output=True,text=True,timeout=10);count+=1
  assert result.returncode==expected,(name,result.stdout,result.stderr)
  if expected:return
  return json.loads(state.read_text()),json.loads(scene.read_text())
 def sample(scene,x,y):
  g=scene['fog']['grid'];u=(x-g['origin'][0])/g['spacing'];v=(y-g['origin'][1])/g['spacing'];n=g['side']
  assert 0<=u<n-1 and 0<=v<n-1
  ix,iy=int(u),int(v);fx,fy=u-ix,v-iy;row=iy*n+ix;r=g['reveal']
  return (r[row]*(1-fx)+r[row+1]*fx)*(1-fy)+(r[row+n]*(1-fx)+r[row+n+1]*fx)*fy
 initial,scene=run('initial',data)
 assert scene['fog']['sight_feather']==12
 inner=sample(scene,-33,0);outer=sample(scene,-21,0);assert .6<inner<.95 and .03<outer<.4,(inner,outer)
 assert next(u for u in initial['units'] if u['faction']==2)['visible']
 departed,old=run('departed',data,720);later,half=run('half',data,900)
 before,after=sample(old,-45,0),sample(half,-45,0);assert before>.005 and abs(after/before-.5)<.002,(before,after)
 enemy=next(u for u in departed['units'] if u['faction']==2);assert not enemy['visible']
 assert not any(i['slot']==enemy['slot'] for i in old['instances']),'Fog memory revealed an enemy'
 immediate=deepcopy(data);immediate['appearance']['fog']['memory_half_life_seconds']=0
 other,clear=run('immediate',immediate,720);assert other==departed,'Fog memory changed gameplay'
 assert sample(clear,-45,0)==0,'Current sight still includes the old position'
 far,expanded=run('expanded',data,1800);assert expanded['fog']['grid']['spacing']>scene['fog']['grid']['spacing'],'Scouting did not expand the field'
 moved,moved_scene=run('camera',data,900,('--camera-drag','1000','400','--camera-zoom','-1000'))
 assert moved==later and moved_scene['fog']==half['fog'],'Camera movement changed sight memory'
 reset,reset_scene=run('restart',data,900,('--restart',));fresh,fresh_scene=run('fresh',data,1)
 # Restart restores tick zero; first-tick flight changes are less than one field sample.
 assert reset['tick']==0 and sample(reset_scene,-45,0)>.99
 assert sample(reset_scene,-45,60)<sample(half,-45,60),'Restart kept the old sight path'
 for value in (-1,61):
  bad=deepcopy(data);bad['appearance']['fog']['memory_half_life_seconds']=value;run('bad-'+str(count),bad,expected=1)
 bad=deepcopy(data);bad['appearance']['fog']['unknown']=True;run('unknown',bad,expected=1)
 print(dict(old_reveal=before,reveal_three_seconds_later=after,ratio=after/before,inner=inner,outer=outer))
print(f'Fog memory E2E passed: {count} application runs; decay, broad falloff, current enemy hiding, camera independence, field growth, restart, and rejected settings.')
