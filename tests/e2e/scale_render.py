#!/usr/bin/env python3
"""Capture the current biological game within the fixed GPU limits."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from gpu_environment import check_gpu_environment
from save_progress import archive_capture

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
check_gpu_environment()
binary=args.binary.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
config=binary.parent/'assets/rendering/biology.json'
values=json.loads(config.read_text())
assert (values['output']['width'],values['output']['height'])==(1920,1080)
budget=values['sampling']['samples_per_pixel']+values['volume']['rays_per_pixel']+1
assert budget<=8,'Surface, volume and background samples exceed the cap'
deadline=time.monotonic()+30
baseline=None
for side,home,enemy,ticks,frames in [('virus',0,1,1,1),('virus',0,1,1,2),('immune',1,0,1800,1)]:
    name=side+('-initial' if ticks==1 else '-growing-factory')+('-repeat' if frames==2 else '')
    image=out/(name+'.ppm');state=out/(name+'.json')
    p=subprocess.Popen([*command,'--headless','--frames',str(frames),'--no-stats','--world',str(binary.parent/'assets/levels'/('biology-'+side+'.json')),
                        '--config',str(config),'--ticks',str(ticks),'--capture',str(image),'--state',str(state)],
                       stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=sys.platform!='win32')
    try:stdout,stderr=p.communicate(timeout=min(10,max(.001,deadline-time.monotonic())))
    except subprocess.TimeoutExpired:
        if sys.platform=='win32':p.kill()
        else:os.killpg(p.pid,signal.SIGKILL)
        p.communicate();raise RuntimeError('GPU process timeout. No later case was launched.')
    (out/(name+'.log')).write_text(stdout+stderr)
    assert p.returncode==0,stdout+stderr
    if os.environ.get('MICROSHIP_VULKAN_VALIDATION')=='1':assert 'Vulkan validation: enabled.' in stdout
    data=image.read_bytes();assert re.match(rb'P6\s+1920\s+1080\s+255\s',data),'Capture size changed'
    archive_capture(image,'direct-rays-'+name)
    assert 'Renderer: direct ray tracing;' in stdout,stdout
    if side=='virus':
        if baseline is None:baseline=data
        else:assert data==baseline,'Frozen frame changed after another render'
    assert len(set(data[-1920*1080*3:]))>16,'Capture has no image variation'
    d=json.loads(state.read_text());assert not d['colonies'][enemy]['discovered']
    assert sum(s['health']>0 for s in d['colonies'][home]['structures'])==(1 if ticks==1 else 2)
print('Biological render E2E passed. Three sequential native 1080p captures; frozen frame independence, within the eight-sample cap.')
