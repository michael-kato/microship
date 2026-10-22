#!/usr/bin/env python3
"""Check live wall-bake invalidation against a fresh real-application render."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--thickness-cache',action='store_true');p.add_argument('--run',action='store_true');p.add_argument('--output',type=Path,default=root/'build/static-lighting-live');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
world=stage_world(root/'assets/levels/virus-player-level.json',out);wp=out/'world.json';write_world(wp,world)
cfg=json.loads((root/'assets/rendering/renderer.json').read_text())
if a.thickness_cache:cfg['lighting']['organelle_cache']=True
assert cfg['output']['width']==1920 and cfg['output']['height']==1080
cfg['lighting']['static_lighting_cache']=True
cp=out/'render.json';cp.write_text(json.dumps(cfg,indent=2)+'\n')
prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
base=[*prefix,'--world',str(wp),'--config',str(cp),'--world-seed','1234','--camera-zoom','1000','--no-stats']
command=[*base,'--frames','2','--frame-delay-ms','100','--no-activate','--ticks','600','--capture',str(out/'live.pfm'),'--state',str(out/'live-state.json'),'--scene-state',str(out/'live-scene.json')]
(out/'commands.json').write_text(json.dumps([command],indent=2)+'\n')
if not a.run:print('Prepared one native 1920x1080 two-frame window and one fresh single-frame render. default renderer sampling; host validation no tracing. No GPU work started.');raise SystemExit(0)
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='1';start=time.monotonic()
def run(name,args):
 remaining=gpu_process_timeout_seconds()
 try:result=subprocess.run(args,env=env,capture_output=True,text=True,timeout=remaining)
 except subprocess.TimeoutExpired as error:
  stdout=error.stdout or b'';stderr=error.stderr or b''
  (out/(name+'.log')).write_bytes((stdout.encode() if isinstance(stdout,str) else stdout)+(stderr.encode() if isinstance(stderr,str) else stderr))
  raise RuntimeError(name+' reached its process timeout') from None
 (out/(name+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(name,result.stdout,result.stderr)
 assert 'Vulkan validation: enabled.' in result.stdout
 return result.stdout
log=run('live',command)
cache=[tuple(map(int,v)) for v in re.findall(r'Static lighting: (\d+) baked, (\d+) reused; (\d+) bytes; (\d+) surface bakes',log)]
assert len(cache)==2 and all(row[0]>0 and row[3]>0 for row in cache),cache
assert re.search(r'Deformed mesh updates: [1-9]',log),'No live mesh refit'
state=json.loads((out/'live-state.json').read_text());assert state['tick']>600,state['tick']
fresh=[*base,'--headless','--frames','1','--ticks',str(state['tick']),'--time',str(state['time']),'--step-seconds','0','--capture',str(out/'fresh.pfm'),'--state',str(out/'fresh-state.json'),'--scene-state',str(out/'fresh-scene.json')]
(out/'commands.json').write_text(json.dumps([command,fresh],indent=2)+'\n')
run('fresh',fresh);seconds=time.monotonic()-start
live=json.loads((out/'live-scene.json').read_text());fresh_scene=json.loads((out/'fresh-scene.json').read_text())
# Revision counts describe how many refits were needed to reach the same mesh.
for data in (live,fresh_scene):
 for mesh in data['meshes']:
  if 'deformation' in mesh:mesh['deformation'].pop('revision',None)
assert live==fresh_scene,'Live render input differs from a fresh application at the same tick'
assert state==json.loads((out/'fresh-state.json').read_text()),'Live simulation differs at the same tick'
# Radiance cascades can differ between a live frame and a fresh process.
report=dict(seconds=seconds,final_tick=state['tick'],cache=cache,scene_equal=True)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
