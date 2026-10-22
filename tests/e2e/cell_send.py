#!/usr/bin/env python3
"""Check dragging from one cell to another sends its stationed units, with the complete application."""
import json,math,re,subprocess,sys,tempfile
from pathlib import Path
from asset_data import established_world,write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-cell-send-') as folder:
 out=Path(folder);world=established_world(root/'tests/e2e/scenes/virus-two-cell-test-level.json',out)
 wp=out/'world.json';write_world(wp,world)
 config=json.loads((root/'assets/rendering/renderer.json').read_text());config['camera']['position']=[0,0,20]
 cp=out/'render.json';cp.write_text(json.dumps(config))
 def run(name,extra=()):
  global count
  state=out/(name+'-state.json')
  result=subprocess.run([*command,'--smoke','--ticks','120','--world',str(wp),'--config',str(cp),'--state',str(state),*extra],capture_output=True,text=True,timeout=10);count+=1
  assert result.returncode==0,(name,result.stdout,result.stderr)
  camera=list(map(float,re.findall(r'-?\d+(?:\.\d+)?',next(line for line in result.stdout.splitlines() if line.startswith('Initial camera:')))))
  return json.loads(state.read_text()),camera
 def dot(a,b):return sum(x*y for x,y in zip(a,b))
 def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
 def norm(a):return [x/math.sqrt(dot(a,a)) for x in a]
 def pixel(camera,point):
  forward=norm([b-a for a,b in zip(camera[:3],camera[3:])]);right=norm(cross(forward,config['camera']['up']));up=cross(right,forward)
  focal=1080/(2*math.tan(math.radians(config['camera']['vertical_fov_degrees']/2)));offset=[a-b for a,b in zip(point,camera[:3])];depth=dot(offset,forward)
  return [str(round(960+dot(offset,right)*focal/depth)),str(round(540-dot(offset,up)*focal/depth))]
 def drag(a,b):return ('--pointer-drag',*a,*b)
 idle,camera=run('idle')
 home,enemy=[pixel(camera,b['position']) for b in world['bodies']]
 stationed={u['generation'] for u in idle['units'] if u['faction']==1 and u['target']==0 and not u['moving']}
 assert stationed,'No virus units are stationed at the home cell'
 def unchanged(state):return {u['generation']:u['target'] for u in state['units']}=={u['generation']:u['target'] for u in idle['units']}
 # Home to enemy: every stationed unit is selected and ordered; nothing else changes.
 sent,_=run('send',drag(home,enemy))
 for u in sent['units']:
  if u['generation'] in stationed:assert u['target']==1 and u['selected'],u
  else:assert not u['selected'] and u['target']==next(v['target'] for v in idle['units'] if v['generation']==u['generation']),u
 assert sent['order']['cell']==1 and sent['order']['count']==len(stationed),sent['order']
 # Ending in open space keeps box selection; a cell without stationed units and the same cell send nothing.
 boxed,_=run('box_from_cell',drag(home,['960','80']));assert unchanged(boxed)
 assert run('from_enemy',drag(enemy,home))[0]['order']['count']==0
 same,_=run('same_cell',drag(home,[str(int(home[0])+30),home[1]]));assert unchanged(same) and same['order']['count']==0
print(f'Cell send E2E passed: {count} application runs; drag sends stationed units, box selection elsewhere, empty and same-cell drags ignored.')
