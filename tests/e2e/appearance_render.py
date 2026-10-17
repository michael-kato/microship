#!/usr/bin/env python3
"""Prepare fixed-camera appearance cases. --run opts in to five bounded GPU processes."""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,default=root/'build/appearance-review')
parser.add_argument('--run',action='store_true')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
binary=args.binary.resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
virus=stage_world(root/'assets/levels/biology-virus.json',out)
immune=stage_world(root/'assets/levels/biology-immune.json',out)
minimal=deepcopy(virus);minimal['appearance']['fog']['blur_radius']=1
wide=('--camera-drag','-1748','0','--camera-zoom','-1000')
cases=[('neutral-start',virus,1,'',()),
       ('virus-branches',virus,12000,'0 select all\n0 order 0\n600 ratio 65\n',()),
       ('fog-wide',virus,12000,'0 select all\n0 order 0\n600 ratio 65\n',wide),
       ('fog-minimal',minimal,12000,'0 select all\n0 order 0\n600 ratio 65\n',wide),
       ('immune-factories',immune,12000,'0 select all\n0 order 1\n600 ratio 65\n',())]
prepared=[]
for name,data,ticks,replay,extra in cases:
    world=out/(name+'-world.json');write_world(world,data)
    orders=out/(name+'.orders');orders.write_text(replay)
    capture=out/(name+'.ppm')
    invocation=[*command,'--world',str(world),'--ticks',str(ticks),'--world-seed','1234','--frames','1',
                '--headless','--no-stats','--capture',str(capture),'--state',str(out/(name+'-state.json')),
                '--scene-state',str(out/(name+'-scene.json')),*extra]
    if replay:invocation+=['--replay',str(orders)]
    prepared.append((name,capture,invocation))
(out/'commands.json').write_text(json.dumps([v[2] for v in prepared],indent=2)+'\n')
if not args.run:
    print(f'Prepared {len(prepared)} native 1920x1080 cases in {out}. No GPU processes started. Use --run only after approval.')
    sys.exit(0)
environment=os.environ.copy();environment['NANOVA_VULKAN_VALIDATION']='1'
started=time.monotonic();report=[]
for name,capture,invocation in prepared:
    remaining=30-(time.monotonic()-started)
    if remaining<=0:raise RuntimeError('The thirty-second GPU batch limit has expired')
    process=subprocess.run(invocation,capture_output=True,text=True,env=environment,timeout=min(10,remaining))
    (out/(name+'.log')).write_text(process.stdout+process.stderr)
    if process.returncode or 'Vulkan validation: enabled.' not in process.stdout:
        raise RuntimeError(name+' failed validation or application shutdown: '+process.stdout+process.stderr)
    saved=archive_capture(capture,'appearance-'+name)
    frames=[float(v) for v in re.findall(r'Application frame: ([0-9.]+) ms',process.stdout)]
    report.append(dict(case=name,capture=str(saved),application_ms=frames))
    print(name+': '+str(frames)+' ms',flush=True)
(out/'report.json').write_text(json.dumps(dict(elapsed=time.monotonic()-started,cases=report),indent=2)+'\n')
print('Appearance capture batch passed. These bounded frames do not measure steady performance.')
