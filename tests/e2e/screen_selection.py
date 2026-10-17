#!/usr/bin/env python3
"""Check screen group selection with the complete Windows application."""
from copy import deepcopy
import json,math,re,subprocess,sys,tempfile
from pathlib import Path
from asset_data import established_world,write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-screen-selection-') as folder:
 out=Path(folder);base=established_world(root/'tests/e2e/scenes/biology-two-virus.json',out)
 # Include each screen edge, the bottom HUD, the rear half-space, and enemies.
 positions=[(0,0,0),(3,0,0),(-40,0,0),(40,0,0),(0,30,0),(0,-30,0),(0,-6,0),(0,0,30)]
 base['spawns']=[dict(type=side+'_'+role,position=p) for side in ('virus','immune') for role in ('fighter','bomber') for p in positions]
 config=json.loads((root/'assets/rendering/biology.json').read_text());config['camera']['position']=[0,0,20]
 def run(name,world,settings,extra=()):
  global count
  wp=out/(name+'-world.json');write_world(wp,world);cp=out/(name+'-render.json');cp.write_text(json.dumps(settings))
  replay=out/(name+'.orders');replay.write_text('0 cell '+str(0 if world['match']['player']=='virus' else 1)+'\n0 ratio 37\n')
  state=out/(name+'-state.json')
  result=subprocess.run([*command,'--smoke','--ticks','1','--world',str(wp),'--config',str(cp),'--replay',str(replay),'--state',str(state),*extra],capture_output=True,text=True,timeout=10);count+=1
  assert result.returncode==0,(name,result.stdout,result.stderr)
  camera=list(map(float,re.findall(r'-?\d+(?:\.\d+)?',next(line for line in result.stdout.splitlines() if line.startswith('Initial camera:')))))
  return json.loads(state.read_text()),camera
 def dot(a,b):return sum(x*y for x,y in zip(a,b))
 def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
 def norm(a):return [x/math.sqrt(dot(a,a)) for x in a]
 def expected(state,camera,settings,faction,group):
  forward=norm([b-a for a,b in zip(camera[:3],camera[3:])]);right=norm(cross(forward,settings['camera']['up']));up=cross(right,forward)
  focal=1080/(2*math.tan(math.radians(settings['camera']['vertical_fov_degrees']/2)));selected=set()
  for u in state['units']:
   if u['faction']!=faction or u['kind']!=group:continue
   offset=[a-b for a,b in zip(u['position'],camera[:3])];depth=dot(offset,forward)
   if depth<=0:continue
   x=960+dot(offset,right)*focal/depth;y=540-dot(offset,up)*focal/depth
   if 0<=x<=1919 and 0<=y<=915:selected.add(u['generation'])
  return selected
 def without_selection(state):
  result=deepcopy(state);result.pop('message',None)
  for u in result['units']:u.pop('selected')
  return result
 for side,faction in (('virus',1),('immune',2)):
  world=deepcopy(base);world['match']['player']=side;world['match']['opponent']='immune' if side=='virus' else 'virus'
  for view,pan,up in [('normal',(),[0,1,0]),('pan',('--camera-pan','1','0','2'),[0,1,0]),('empty',('--camera-pan','1','0','60'),[0,1,0]),('rotated',(),[1,0,0])]:
   settings=deepcopy(config);settings['camera']['up']=up
   baseline,camera=run(side+'-'+view,world,settings,(*pan,'--select-group','3'))
   home=0 if side=='virus' else 1;assert baseline['colonies'][home]['fighter_percent']==37 and baseline['colonies'][home]['ratio_enabled']
   for group in (0,1):
    state,actual_camera=run(side+'-'+view+'-'+str(group),world,settings,(*pan,'--select-group','3','--select-screen-group',str(group+1)))
    selected={u['generation'] for u in state['units'] if u['selected']};wanted=expected(baseline,camera,settings,faction,group)
    assert selected==wanted,(side,view,group,selected,wanted)
    assert actual_camera==camera,(actual_camera,camera)
    a,b=without_selection(state),without_selection(baseline)
    assert a==b,{k:(b.get(k),a.get(k)) for k in a if a[k]!=b.get(k)}
    if view=='normal':assert len(selected)==2,(side,group,selected)
    if view=='empty':assert not selected
print(f'Screen selection E2E passed: {count} application runs; both factions and groups, camera pan and rotation, all edges, HUD, behind-camera craft, enemies, replacement, empty selection and unchanged production.')
