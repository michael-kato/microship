#!/usr/bin/env python3
"""Check selected-unit target lines through complete application runs."""
from copy import deepcopy
import json,math,subprocess,sys,tempfile
from pathlib import Path
from asset_data import stage_world,write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-targets-') as directory:
 out=Path(directory);base=stage_world(root/'assets/levels/virus-player-level.json',out);base['match']['auto_colonize']=False
 def run(name,world=base,ticks=60,orders=''):
  global count
  wp=out/(name+'-world.json');write_world(wp,world);state=out/(name+'-state.json');scene=out/(name+'-scene.json')
  cmd=[*prefix,'--smoke','--world',str(wp),'--ticks',str(ticks),'--world-seed','1234','--state',str(state),'--scene-state',str(scene)]
  if orders:
   rp=out/(name+'.orders');rp.write_text(orders);cmd+=['--replay',str(rp)]
  result=subprocess.run(cmd,capture_output=True,text=True,timeout=10);count+=1;assert result.returncode==0,(name,result.stdout,result.stderr)
  return json.loads(state.read_text()),json.loads(scene.read_text())
 def lines(scene):return [e for e in scene['effect_segments'] if e[7]==4]
 selected,view=run('moving',ticks=20,orders='0 select all\n0 move -32 2 8\n');segments=lines(view);assert len(segments)==7
 # Use the current camera preset, translated to the player's home cell.
 camera=json.loads((binary.parent/'assets/rendering/renderer.json').read_text())['camera']
 home=base['bodies'][next(v['cell'] for v in base['match']['starts'] if v['faction']==base['match']['player'])]['position']
 offset=[a-b for a,b in zip(camera['position'],camera['target'])];eye=[a+b for a,b in zip(home,offset)]
 def normalize(v):return [x/math.dist(v,[0,0,0]) for x in v]
 def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
 f=normalize([-x for x in offset]);right=normalize(cross(f,camera['up']));up=cross(right,f)
 focal=1080/(2*math.tan(math.radians(22.5)))
 def project(point):
  v=[a-b for a,b in zip(point,eye)];z=sum(a*b for a,b in zip(v,f));return [960+sum(a*b for a,b in zip(v,right))*focal/z,540-sum(a*b for a,b in zip(v,up))*focal/z]
 endpoint=project([-32,2,8])
 assert all(math.dist(e[4:6],endpoint)<.001 for e in segments)
 for e,u in zip(segments,[u for u in selected['units'] if u['selected']]):assert math.dist(e[:2],project(u['position']))<.001
 _,unselected=run('unselected');assert not lines(unselected)
 disabled=deepcopy(base);disabled['effects']['target_line']['alpha']=0
 same,none=run('lines-disabled',disabled,20,orders='0 select all\n0 move -32 2 8\n');assert not lines(none) and same==selected
 # A long move keeps the target active while its marker fades.
 slow=deepcopy(base)
 for u in slow['content']['units']:u['speed']=.01
 for ticks,alpha in ((30,.1),(45,.05),(60,0),(120,0)):
  state,scene=run('fade-'+str(ticks),slow,ticks,'0 select all\n0 move -32 2 8\n')
  fading=lines(scene)
  if alpha:assert len(fading)==7 and all(abs(e[11]-alpha)<.00001 for e in fading),(ticks,fading)
  else:assert not fading and all(u['moving'] for u in state['units'] if u['selected'])
 for command in ('select all','move -30 3 8'):
  _,scene=run('refresh-'+command.split()[0],slow,315,'0 select all\n0 move -32 2 8\n300 '+command+'\n')
  assert len(lines(scene))==7 and all(abs(e[11]-.1)<.00001 for e in lines(scene))
 # An attached immune craft and its selected captive both suppress target lines.
 held=deepcopy(base);held['spawns']=[dict(type='immune_fighter',position=[-52,-1,6.4]),dict(type='virus_fighter',position=[-51.65,-1,6.4])]
 for u in held['content']['units']:u['speed']=.01
 for side in ('virus','immune'):
  held['match']['player']=side;held['match']['opponent']='immune' if side=='virus' else 'virus'
  state,scene=run('held-'+side,held,30,'0 select all\n');selected_units=[u for u in state['units'] if u['selected']]
  assert len(selected_units)==1 and (selected_units[0]['latch']>=0 or selected_units[0]['held_by']>=0),(side,selected_units)
  assert not lines(scene),(side,lines(scene))
 # Long diagonal orders must fit the fixed effect tile storage.
 crowd=deepcopy(base);crowd['spawns']=[dict(type='virus_fighter',position=[-52+(i%9)*.1,-1+(i//9)*.1,6.4]) for i in range(63)]+[dict(type='immune_fighter',position=[48,17,6.4])]
 state,scene=run('long-orders',crowd,1,'0 select all\n0 move 1000 -1000 0\n');assert len(lines(scene))==63
print(f'Target line E2E passed: {count} application runs; selection, projected endpoints, disabled appearance, captured units, and long orders.')
