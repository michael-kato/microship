#!/usr/bin/env python3
"""Prepare or render fixed wall-blur, depth-blur and miniature-focus application views."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
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
cases=[('clear',['--wall-blur','off','--depth-blur','off','--tilt-shift','off']),
       ('wall',['--depth-blur','off','--tilt-shift','off']),('depth',['--tilt-shift','off']),('miniature',[])]
prepared=[]
for name,extra in cases:
    command=[sys.executable,str(root/'tests/e2e/volume_optimizations.py'),str(a.binary.resolve()),'--case','near','--output',str(out/name),*extra]
    if a.run:command+=['--run']
    prepared.append((name,command))
(out/'commands.json').write_text(json.dumps([c for _,c in prepared],indent=2)+'\n')
started=time.monotonic()
for name,command in prepared:
    remaining=gpu_process_timeout_seconds()
    result=subprocess.run(command,capture_output=True,text=True,timeout=max(.1,remaining) if a.run else 5)
    (out/(name+'.log')).write_text(result.stdout+result.stderr);print(result.stdout,end='',flush=True)
    assert result.returncode==0,(name,result.stdout,result.stderr)
if not a.run:print('Prepared native-1080p application captures. No GPU work started.');sys.exit(0)
gpu_elapsed=time.monotonic()-started
# Metrics use only these fixed real-application images, not isolated shader runs.
def pixels(name):
    data=(out/name/'near-current.ppm').read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',data)
    assert header;data=data[header.end():];assert len(data)==1920*1080*3;return data
clear,wall,depth,miniature=(pixels(n) for n,_ in cases)
def energy(data,box):
    x0,y0,x1,y1=box
    return sum((data[(y*1920+x)*3+c]-data[(y*1920+x+1)*3+c])**2+
               (data[(y*1920+x)*3+c]-data[((y+1)*1920+x)*3+c])**2
               for y in range(y0,y1) for x in range(x0,x1) for c in range(3))
def difference(a,b,box):
    x0,y0,x1,y1=box
    return sum(abs(a[(y*1920+x)*3+c]-b[(y*1920+x)*3+c]) for y in range(y0,y1) for x in range(x0,x1) for c in range(3))/((x1-x0)*(y1-y0)*3)
front=(440,240,570,360);nucleus=(660,240,1160,720);rear=(800,70,1070,220);outside=(0,0,100,850)
# The HUD has a translucent panel. Check the opaque text pixels, not its background.
hud_pixels=[(y*1920+x)*3 for y in range(945,1035) for x in range(40,585) if max(clear[(y*1920+x)*3:(y*1920+x)*3+3])>175]
report=dict(wall_front_edge_ratio=energy(wall,front)/energy(clear,front),wall_nucleus_edge_ratio=energy(wall,nucleus)/energy(clear,nucleus),
    wall_rear_edge_ratio=energy(wall,rear)/energy(clear,rear),vacuum_change=difference(wall,clear,outside),
    focus_center_change=difference(depth,miniature,(700,440,1100,640)),focus_rear_edge_ratio=energy(miniature,rear)/energy(depth,rear),
    hud_text_change=max(abs(depth[i+c]-miniature[i+c]) for i in hud_pixels for c in range(3)),gpu_batch_seconds=gpu_elapsed)
assert report['wall_nucleus_edge_ratio']<.8 and report['wall_rear_edge_ratio']<.8,'Transmitted silhouettes remain sharp'
assert difference(wall,clear,front)>.1,'Near-wall organelles did not receive wall blur'
assert report['vacuum_change']==0,'Wall blur escaped the cell footprint'
assert report['focus_center_change']<.01 and report['focus_rear_edge_ratio']<.8,'The miniature focus band failed'
assert report['hud_text_change']==0,'The focus filter softened HUD text'
states=[json.loads((out/n/'near-current-state.json').read_text()) for n,_ in cases];assert all(s==states[0] for s in states)
(out/'appearance-check.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
print('Application appearance checks passed: transmitted silhouettes softened, near-wall detail blurred, vacuum unchanged, focus band retained, HUD text sharp, and matching simulation state.')
