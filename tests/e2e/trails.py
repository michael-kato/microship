#!/usr/bin/env python3
"""Check rear trail attachment and path length with the real Windows application."""
from copy import deepcopy
import json,math,re,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
binary=Path(sys.argv[1]).resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
def sub(a,b):return [x-y for x,y in zip(a,b)]
def add(a,b):return [x+y for x,y in zip(a,b)]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def norm(a):return [x/math.sqrt(dot(a,a)) for x in a]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-trails-') as directory:
 out=Path(directory);data=stage_world(root/'assets/levels/virus-player-level.json',out)
 for unit in data['content']['units']:unit.update(trails=unit['name']=='virus_fighter')
 data['spawns']=[dict(type='virus_fighter',position=[-45,0,6.4]),dict(type='immune_fighter',position=[48,14,6.4])]
 replay=out/'flight.orders';replay.write_text('0 select all\n0 move -10 10 6.4\n')
 def run(name,world,expected=0):
  global count
  wp=out/(name+'-world.json');write_world(wp,world);state=out/(name+'-state.json');scene=out/(name+'-scene.json')
  result=subprocess.run([*prefix,'--smoke','--world',str(wp),'--ticks','240','--world-seed','1234','--replay',str(replay),'--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
  assert result.returncode==expected,(name,result.stdout,result.stderr)
  if expected:return
  camera=next(line for line in result.stdout.splitlines() if line.startswith('Initial camera:'))
  values=list(map(float,re.findall(r'-?\d+(?:\.\d+)?',camera)));eye,target=values[:3],values[3:]
  forward=norm(sub(target,eye));right=norm(cross(forward,[0,1,0]));up=cross(right,forward);tan=math.tan(math.radians(45)/2)
  def unproject(v):
   ray=norm([forward[c]+right[c]*(2*v[0]/1920-1)*tan*1920/1080+up[c]*(1-2*v[1]/1080)*tan for c in range(3)])
   return [eye[c]+v[2]*ray[c] for c in range(3)]
  drawing=json.loads(scene.read_text());segments=[(unproject(e[:3]),unproject(e[4:7])) for e in drawing['effect_segments'] if e[7]==0]
  assert segments,'No flight trail reached scene input'
  length=sum(math.sqrt(dot(sub(b,a),sub(b,a))) for a,b in segments)
  return json.loads(state.read_text()),drawing,segments,length
 short=run('short',data);full=deepcopy(data);full['effects']['projectile']['trail_length_scale']=1;long=run('full',full)
 assert short[0]==long[0],'Trail length changed game state'
 ratio=short[3]/long[3];assert abs(ratio-.67)<.0002,ratio
 unit=next(u for u in short[0]['units'] if u['type']=='virus_fighter');instance=next(v for v in short[1]['instances'] if v['slot']==unit['slot']);matrix=instance['transform']
 origin=next(u['trail_origin'] for u in data['content']['units'] if u['name']=='virus_fighter')
 expected=[matrix[row*4+3]+sum(matrix[row*4+c]*origin[c] for c in range(3)) for row in range(3)]
 assert math.sqrt(dot(sub(short[2][0][0],expected),sub(short[2][0][0],expected)))<.001,'Trail is not attached to the rear socket'
 assert dot(sub(expected,unit['position']),[matrix[2],matrix[6],matrix[10]])<0,'Trail starts ahead of the craft'
 for scale in (0,1.1):
  bad=deepcopy(data);bad['effects']['projectile']['trail_length_scale']=scale;run('bad-scale-'+str(count),bad,1)
 bad=deepcopy(data);bad['content']['units'][0]['trail_origin']=[0,0,float('nan')];run('bad-origin',bad,1)
 print('Trail length ratio:',ratio)
print(f'Trail E2E passed: {count} application runs; rear socket, 33% shorter path, unchanged game state, invalid data rejected.')
