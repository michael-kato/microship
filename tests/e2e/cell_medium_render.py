#!/usr/bin/env python3
"""Prepare or render fixed single-wall, depth-blur and angle-fade application views."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/cell-medium-review');p.add_argument('--run',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
cases=[('blur',[]),('clear',['--depth-blur','off']),('no-fade',['--depth-blur','off','--edge-fade','off'])]
prepared=[]
for name,extra in cases:
    command=[sys.executable,str(root/'tests/e2e/volume_optimizations.py'),str(a.binary.resolve()),'--case','near','--output',str(out/name),*extra]
    if a.run:command+=['--run']
    prepared.append((name,command))
(out/'commands.json').write_text(json.dumps([c for _,c in prepared],indent=2)+'\n')
started=time.monotonic()
for name,command in prepared:
    remaining=5-(time.monotonic()-started)
    result=subprocess.run(command,capture_output=True,text=True,timeout=max(.1,remaining) if a.run else 5)
    (out/(name+'.log')).write_text(result.stdout+result.stderr);print(result.stdout,end='',flush=True)
    assert result.returncode==0,(name,result.stdout,result.stderr)
if not a.run:print('Prepared native-1080p application captures. No GPU work started.');sys.exit(0)
gpu_elapsed=time.monotonic()-started
# Analyze only these frozen seed-1234, near-camera views. Each reference comes from the app.
def pixels(name):
    data=(out/name/'near-current.ppm').read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',data)
    assert header;data=data[header.end():];assert len(data)==1920*1080*3;return data
blur,clear,no_fade=(pixels(n) for n in ('blur','clear','no-fade'))
def energy(data,box):
    x0,y0,x1,y1=box;total=0
    for y in range(y0,y1):
        for x in range(x0,x1):
            i=(y*1920+x)*3
            total+=sum(abs(data[i+c]-data[i+3+c])+abs(data[i+c]-data[i+1920*3+c]) for c in range(3))
    return total

def difference(a,b,box):
    x0,y0,x1,y1=box
    total=sum(abs(a[(y*1920+x)*3+c]-b[(y*1920+x)*3+c]) for y in range(y0,y1) for x in range(x0,x1) for c in range(3))
    return total/((x1-x0)*(y1-y0)*3)
front=(440,240,570,360);rear=(800,70,1070,220);outside=(0,0,100,850)
report=dict(front_detail_ratio=energy(blur,front)/energy(clear,front),rear_detail_ratio=energy(blur,rear)/energy(clear,rear),outside_change=difference(blur,clear,outside),
    fade_edge_change=difference(clear,no_fade,(140,100,330,850)),fade_center_change=difference(clear,no_fade,(800,350,1100,650)),gpu_batch_seconds=gpu_elapsed)
assert report['rear_detail_ratio']<.9 and report['rear_detail_ratio']<report['front_detail_ratio']
assert .9<report['front_detail_ratio']<1.15 and report['outside_change']==0
assert report['fade_edge_change']>report['fade_center_change']*2
states=[json.loads((out/n/'near-current-state.json').read_text()) for n,_ in cases];assert states[0]==states[1]==states[2]
(out/'appearance-check.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
print('Application appearance checks passed: rear detail softened, front detail retained, outside pixels unchanged, angle fade concentrated at the membrane edge, and matching simulation state.')
