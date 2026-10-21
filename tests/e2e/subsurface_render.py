#!/usr/bin/env python3
"""Prepare or run bounded SSS checks in the real Windows application."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
from copy import deepcopy
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/subsurface-review');p.add_argument('--run',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
data=stage_world(root/'assets/levels/virus-player-level.json',out)
for field in ('rules','effects'):data.pop(field,None)
data['match']={'player':'virus','opponent':'immune'};data['bodies']=[]
data['spawns']=[dict(type='virus_fighter',position=[0,0,0])]
data['appearance']={'ambient':[0,0,0],'gradient':{'top':[0,0,0],'bottom':[0,0,0]},'lights':{
    'key':dict(direction=[0,0,1],color=[1,1,1],irradiance=0),
    'fill':dict(direction=[0,1,0],color=[1,1,1],irradiance=0),
    'rim':dict(direction=[0,0,-1],color=[1,1,1],irradiance=6)}}
unit=data['content']['units'][0];unit.update(model=Path(os.path.relpath(root/'assets/models/virus_leaf.fmesh',out)).as_posix(),scale=2,yaw_degrees=0)
config=json.loads((root/'assets/rendering/renderer.json').read_text());config['sampling']['samples_per_pixel']=3
config['camera'].update(position=[0,0,8],target=[0,0,0]);config.pop('tilt_shift',None)
# A non-scattering blocker must still block the light-entry side of the thin leaf.
blocker=bytearray((root/'assets/models/virus_virion.fmesh').read_bytes());n=struct.unpack_from('<I',blocker,8)[0]
for v in range(n):struct.pack_into('<4f',blocker,16+v*64+48,0,0,0,0)
(out/'blocker.fmesh').write_bytes(blocker)
blocker_unit=data['content']['units'][2];blocker_unit.update(model='blocker.fmesh',scale=4,yaw_degrees=0)
blocked=deepcopy(data);blocked['spawns'].append(dict(type=blocker_unit['name'],position=[0,.3,-3]))
# An open front surface must not use another instance as a thickness exit.
leaf=(root/'assets/models/virus_leaf.fmesh').read_bytes();n,t=struct.unpack_from('<II',leaf,8)
opened=bytearray(leaf[:16+n*64]+leaf[16+n*64:16+n*64+4*16]);struct.pack_into('<I',opened,12,4);(out/'open.fmesh').write_bytes(opened)
open_world=deepcopy(blocked);open_world['content']['units'][0]['model']='open.fmesh'
# Increasing solid thickness must reduce transmitted light; conductors must not scatter.
thick=bytearray(leaf)
for v in range(n):
    z=struct.unpack_from('<f',thick,16+v*64+8)[0];struct.pack_into('<f',thick,16+v*64+8,z*4)
(out/'thick.fmesh').write_bytes(thick);thick_world=deepcopy(data);thick_world['content']['units'][0]['model']='thick.fmesh'
metal=bytearray(leaf)
for triangle in range(t):struct.pack_into('<I',metal,16+n*64+triangle*16+12,2)
(out/'metal.fmesh').write_bytes(metal);metal_world=deepcopy(data);metal_world['content']['units'][0]['model']='metal.fmesh'
off=deepcopy(config);off['subsurface']['strength']=0
cases=[('off',data,off),('on',data,config),('blocked',blocked,config),('open',open_world,config),('thick',thick_world,config),('metal',metal_world,config)]
commands=[]
for name,world,settings in cases:
    wp=out/(name+'-world.json');write_world(wp,world);cp=out/(name+'-render.json');cp.write_text(json.dumps(settings,indent=2))
    prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
    commands.append([*prefix,'--world',str(wp),'--config',str(cp),'--ticks','1','--world-seed','1234','--headless','--frames','1','--capture',str(out/(name+'.ppm')),'--state',str(out/(name+'-state.json')),'--no-stats'])
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:print('Prepared six native 1920x1080 one-frame SSS checks; no GPU work started.');sys.exit(0)
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='1';start=time.monotonic()
for (name,_,_),command in zip(cases,commands):
    remaining=gpu_process_timeout_seconds()
    result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=remaining);(out/(name+'.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0,(name,result.stdout,result.stderr)
    assert 'Vulkan validation: enabled.' in result.stdout
    archive_capture(out/(name+'.ppm'),'subsurface-'+name)
gpu_elapsed=time.monotonic()-start
images={}
for name,_,_ in cases:
    raw=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header;images[name]=raw[header.end():]
# Green pixels through the leaf center: only geometry-aware transmission can light this area.
def energy(raw):return sum(raw[(y*1920+x)*3+1] for y in range(440,600) for x in range(920,1000))
values={name:energy(raw) for name,raw in images.items()}
assert values['on']>values['off']+10000,('Thin tissue did not transmit light',values)
assert values['blocked']<values['on']*.02 and values['open']<values['on']*.02 and values['metal']==0,('Blocked/open geometry leaked light',values)
assert values['thick']<values['on']*.5,('Thickness did not attenuate light',values)
assert json.loads((out/'off-state.json').read_text())==json.loads((out/'on-state.json').read_text()),'SSS changed game state'
report=dict(gpu_batch_seconds=gpu_elapsed,leaf_green_energy=values)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
print('Application SSS checks passed: thin tissue transmits light; a blocker stops it; an open mesh cannot use another instance as its exit.')
