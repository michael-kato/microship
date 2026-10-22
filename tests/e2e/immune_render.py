#!/usr/bin/env python3
"""Prepare or run two immune views and one bounded OS-control check."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
from copy import deepcopy
import argparse,json,os,re,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
from control_window import run_window
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/immune-appearance');p.add_argument('--run',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
data=stage_world(root/'assets/levels/immune-player-level.json',out);data['match']['auto_colonize']=False;data['rules']['ai']['expansion_delay']=300
config=json.loads((root/'assets/rendering/renderer.json').read_text());config['sampling']['samples_per_pixel']=3;cp=out/'render.json';cp.write_text(json.dumps(config))
charge=deepcopy(data);charge['spawns']=[dict(type='immune_fighter',position=[44,14,6.4]),dict(type='virus_fighter',position=[48,14,6.4])]
held=deepcopy(data);held['spawns']=[dict(type='immune_fighter',position=[47.65,14,6.4]),dict(type='virus_fighter',position=[48,14,6.4])]
for world in (charge,held):
 for u in world['content']['units']:
  if u['name']=='virus_fighter':u.update(speed=.01,hits=32,health=384)
  if world is held and u['name']=='immune_fighter':u['speed']=.01
  if world is held and u['name']=='virus_fighter':
   u['targets']=['structures'];u.pop('secondary_attack',None)
prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
cases=[('charge',charge,45),('held',held,120)];commands=[]
for name,world,ticks in cases:
 wp=out/(name+'-world.json');write_world(wp,world)
 commands.append([*prefix,'--world',str(wp),'--config',str(cp),'--ticks',str(ticks),'--select-group','3','--world-seed','1234','--time','0','--step-seconds','0','--headless','--frames','1','--capture',str(out/(name+'.ppm')),'--state',str(out/(name+'-state.json')),'--scene-state',str(out/(name+'-scene.json')),'--no-stats'])
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:print('Prepared two single-frame immune views and one two-frame control window, native 1920x1080. No GPU work started.');sys.exit(0)
# Validate the authored combat moments before starting the GPU batch.
for (name,_,ticks),cmd in zip(cases,commands):
 state=out/(name+'-preflight.json')
 probe=subprocess.run([*prefix,'--smoke','--world',str(out/(name+'-world.json')),'--ticks',str(ticks),'--world-seed','1234','--state',str(state)],capture_output=True,text=True,timeout=10)
 assert probe.returncode==0,(name,probe.stdout,probe.stderr)
 unit=next(u for u in json.loads(state.read_text())['units'] if u['faction']==2)
 assert unit['charging'] if name=='charge' else unit['latch']>=0,(name,unit)
os.environ['NANOVORE_VULKAN_VALIDATION']='1';report={};start=time.monotonic()
for (name,_,_),cmd in zip(cases,commands):
 remaining=gpu_process_timeout_seconds()
 result=subprocess.run(cmd,capture_output=True,text=True,timeout=remaining);(out/(name+'.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,(name,result.stdout,result.stderr)
 assert 'Vulkan validation: enabled.' in result.stdout
 archive_capture(out/(name+'.ppm'),'immune-'+name)
 state=json.loads((out/(name+'-state.json')).read_text());scene=json.loads((out/(name+'-scene.json')).read_text());unit=next(u for u in state['units'] if u['faction']==2)
 if name=='charge':assert unit['charging'] and any(e[7]==4 for e in scene['effect_segments']),unit
 else:assert unit['latch']>=0 and not any(e[7]==4 for e in scene['effect_segments']),unit
 report[name]={'application_ms':list(map(float,re.findall(r'Application frame: ([0-9.]+)',result.stdout)))}
remaining=gpu_process_timeout_seconds();report['controls']=run_window(a.binary.resolve(),out,remaining);report['batch_seconds']=time.monotonic()-start
archive_capture(out/'controls.ppm','immune-controls')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
