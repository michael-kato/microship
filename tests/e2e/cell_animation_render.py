#!/usr/bin/env python3
"""Prepare or run bounded native-resolution biological motion checks."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from gpu_environment import check_gpu_environment, gpu_process_timeout_seconds
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/cell_animation_render');p.add_argument('--run',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
world=stage_world(root/'assets/levels/virus-player-level.json',out)
for b in world['content']['buildings']:b.update(growth_seconds=.01,establishment_seconds=.01);b['branches']['seconds']=.01
write_world(out/'world.json',world)
config=json.loads((root/'assets/rendering/renderer.json').read_text());config['camera']['position']=[0,2,11]
assert config['output']['width']==1920 and config['output']['height']==1080
assert config['sampling']['samples_per_pixel']<=6 and config['volume']['rays_per_pixel']<=1,'Prepared settings exceed the ordinary GPU sample budget'
(out/'render.json').write_text(json.dumps(config,indent=2)+'\n')
binary=a.binary.resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
commands=[]
for name,seconds,step,frames in [('pose_10',10,0,2),('pose_14',14,0,1),('after_pose_10',10,4,2)]:
    commands.append((name,[*prefix,'--headless','--frames',str(frames),'--no-stats','--ticks','600','--world-seed','1234','--time',str(seconds),'--step-seconds',str(step),'--world',str(out/'world.json'),'--config',str(out/'render.json'),'--capture',str(out/(name+'.ppm')),'--scene-state',str(out/(name+'_scene.json'))]))
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:
    print('Prepared three sequential native 1920x1080 processes, at most two frames each, host validation. No GPU work started.');raise SystemExit()
check_gpu_environment();env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='1';start=time.monotonic();report={}
for name,command in commands:
    remaining=gpu_process_timeout_seconds()
    process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
    try:stdout,stderr=process.communicate(timeout=remaining)
    except subprocess.TimeoutExpired:
        if sys.platform=='win32':process.kill()
        else:os.killpg(process.pid,signal.SIGKILL)
        process.communicate();raise
    (out/(name+'.log')).write_text(stdout+stderr)
    assert process.returncode==0,(name,stdout,stderr)
    assert 'Vulkan validation: enabled.' in stdout and 'VUID-' not in stdout+stderr and 'Validation Error' not in stdout+stderr
    capture=out/(name+'.ppm');assert re.match(rb'P6\s+1920\s+1080\s+255\s',capture.read_bytes())
    report[name]={'gpu_ms':list(map(float,re.findall(r'total render: ([0-9.]+)',stdout))),'screenshot':str(archive_capture(capture,'cell_animation_'+name))}
# Radiance cascades retain lighting history. Compare scene input, not final pixels.
assert json.loads((out/'pose_14_scene.json').read_text())==json.loads((out/'after_pose_10_scene.json').read_text()),'Current scene depends on an earlier pose'
assert (out/'pose_10.ppm').read_bytes()!=(out/'pose_14.ppm').read_bytes(),'Motion did not change the image'
report['scene_matches_fresh_process']=True;report['batch_seconds']=time.monotonic()-start
report['limits']='Bounded captures with host validation, not a sustained performance test.'
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
