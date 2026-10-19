#!/usr/bin/env python3
"""Prepare fixed-topology wall captures. GPU work requires --run and approval."""
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
from save_progress import archive_capture
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,default=root/'build/wall-review');parser.add_argument('--run',action='store_true')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);binary=args.binary.resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
cases=[]
for name,side,ticks,frames in [('virus-partial','virus',4800,1),('virus-wall','virus',12000,1),('fungal-wall','fungus',12000,1),('live-wall-refit','virus',4800,2)]:
    data=stage_world(root/'assets/levels'/(side+'-player-level.json'),out)
    world=out/(name+'-world.json');write_world(world,data)
    home=next(v['cell'] for v in data['match']['starts'] if v['faction']==data['match']['player'])
    orders=out/(name+'.orders');orders.write_text('' if side=='fungus' else f'0 select all\n0 order {home}\n600 ratio 65\n')
    capture=out/(name+'.ppm')
    invocation=[*command,'--frames',str(frames),'--no-stats','--world-seed','1234','--world',str(world),'--ticks',str(ticks),
                '--capture',str(capture),'--state',str(out/(name+'-state.json')),'--scene-state',str(out/(name+'-scene.json'))]
    invocation+=['--headless'] if frames==1 else ['--no-activate']
    if side!='fungus':invocation+=['--replay',str(orders)]
    cases.append((name,capture,invocation))
(out/'commands.json').write_text(json.dumps([v[2] for v in cases],indent=2)+'\n')
if not args.run:
    print(f'Prepared {len(cases)} processes in {out}. Three one-frame captures and one two-frame native window. No GPU work started.');sys.exit(0)
environment=os.environ.copy();environment['NANOVA_VULKAN_VALIDATION']='1';started=time.monotonic();report=[]
for name,capture,invocation in cases:
    remaining=30-(time.monotonic()-started)
    if remaining<=0:raise RuntimeError('The thirty-second GPU batch limit has expired')
    p=subprocess.run(invocation,capture_output=True,text=True,env=environment,timeout=min(10,remaining))
    (out/(name+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode or 'Vulkan validation: enabled.' not in p.stdout:raise RuntimeError(name+': '+p.stdout+p.stderr)
    saved=archive_capture(capture,'wall-'+name)
    frames=[float(v) for v in re.findall(r'Application frame: ([0-9.]+) ms',p.stdout)]
    updates=[int(v) for v in re.findall(r'Deformed mesh updates: ([0-9]+) BLAS refits',p.stdout)]
    item=dict(case=name,capture=str(saved),application_ms=frames,blas_refits=updates);report.append(item)
    (out/'report.json').write_text(json.dumps(dict(elapsed=time.monotonic()-started,cases=report),indent=2)+'\n')
    if name=='live-wall-refit' and not any(updates):raise RuntimeError('The native window did not exercise a live BLAS refit')
    print(item,flush=True)
state=json.loads((out/'live-wall-refit-state.json').read_text());reference=out/'refit-cpu-scene.json'
p=subprocess.run([*command,'--smoke','--world',str(world),'--world-seed','1234','--ticks',str(state['tick']),
    '--replay',str(orders),'--scene-state',str(reference)],capture_output=True,text=True,env=environment,timeout=10)
(out/'refit-cpu.log').write_text(p.stdout+p.stderr)
if p.returncode:raise RuntimeError('CPU reference failed: '+p.stdout+p.stderr)
live=json.loads((out/'live-wall-refit-scene.json').read_text());frozen=json.loads(reference.read_text())
for a,b in zip(live['meshes'],frozen['meshes']):
    if 'deformation' in a:
        assert a['deformation']['vertices']==b['deformation']['vertices'],'Wall positions depend on previous rendered frames'
        assert a['deformation']['indices']==b['deformation']['indices']
print('Live wall input matches a fresh CPU application run at the same simulation tick.')
print('Wall captures and the live BLAS refit passed the bounded GPU batch.')
