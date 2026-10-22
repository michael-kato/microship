#!/usr/bin/env python3
"""Prepare faction form captures and a native box-selection check. GPU use requires --run."""
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
from save_progress import archive_capture
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,default=root/'build/forms-review');parser.add_argument('--run',action='store_true')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);binary=args.binary.resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
cases=[]
for name,side,ticks in [('virus-partial','virus',4800),('virus-tree','virus',12000),('immune-cell','immune',12000),('fungal-cell','fungus',12000)]:
    data=stage_world(root/'assets/levels'/(side+'-player-level.json'),out)
    world=out/(name+'-world.json');write_world(world,data)
    home=next(v['cell'] for v in data['match']['starts'] if v['faction']==data['match']['player'])
    orders=out/(name+'.orders');orders.write_text('' if side=='fungus' else f'0 select all\n0 order {home}\n600 ratio 65\n')
    capture=out/(name+'.ppm')
    invocation=[*command,'--headless','--frames','1','--no-stats','--world-seed','1234','--world',str(world),'--ticks',str(ticks),
                '--capture',str(capture),'--state',str(out/(name+'-state.json')),'--scene-state',str(out/(name+'-scene.json'))]
    if side!='fungus':invocation+=['--replay',str(orders)]
    cases.append((name,capture,invocation))
cases.append(('box-window',out/'box-window.ppm',[sys.executable,str(root/'tests/e2e/box_window.py'),str(binary),str(out)]))
(out/'commands.json').write_text(json.dumps([v[2] for v in cases],indent=2)+'\n')
if not args.run:
    print(f'Prepared {len(cases)} processes in {out}. Four one-frame captures and one two-frame native input check. No GPU work started.');sys.exit(0)
environment=os.environ.copy();environment['NANOVORE_VULKAN_VALIDATION']='1';started=time.monotonic();report=[]
for name,capture,invocation in cases:
    remaining=gpu_process_timeout_seconds()
    p=subprocess.run(invocation,capture_output=True,text=True,env=environment,timeout=remaining)
    (out/(name+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode or 'Vulkan validation: enabled.' not in p.stdout:raise RuntimeError(name+': '+p.stdout+p.stderr)
    saved=archive_capture(capture,'forms-'+name)
    frames=[float(v) for v in re.findall(r'Application frame: ([0-9.]+) ms',p.stdout)]
    report.append(dict(case=name,capture=str(saved),application_ms=frames));print(name,frames,'ms',flush=True)
    (out/'report.json').write_text(json.dumps(dict(elapsed=time.monotonic()-started,cases=report),indent=2)+'\n')
(out/'report.json').write_text(json.dumps(dict(elapsed=time.monotonic()-started,cases=report),indent=2)+'\n')
print('Faction forms and native box input passed the bounded GPU batch.')
