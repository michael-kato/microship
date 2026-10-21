#!/usr/bin/env python3
"""Prepare paired native-1080p zoom cases. --cpu checks inputs; --run starts the bounded GPU batch."""
import argparse
from copy import deepcopy
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

from authored_assets import stage_world,write_world
from save_progress import archive_capture
from gpu_environment import gpu_process_timeout_seconds

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,default=root/'build/zoom-profile')
parser.add_argument('--film-samples',type=int,default=3)
parser.add_argument('--process-seconds',type=float,default=gpu_process_timeout_seconds())
mode=parser.add_mutually_exclusive_group();mode.add_argument('--cpu',action='store_true');mode.add_argument('--run',action='store_true')
args=parser.parse_args();binary=args.binary.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
base=stage_world(root/'assets/levels/virus-player-level.json',out)
config=json.loads((root/'assets/rendering/renderer-quality.json').read_text())
assert config['output']['width']==1920 and config['output']['height']==1080
assert config['sampling']['samples_per_pixel']==6 and all(config['volume'][k]==v for k,v in dict(rays_per_pixel=1,integration_steps=8).items())
if args.process_seconds<=0 or args.film_samples<=0:parser.error('Use positive samples and timeout')
config['sampling']['samples_per_pixel']=args.film_samples
budget=dict(film_samples=args.film_samples,volume_film_rays=1,backdrop_queries=1,frames_per_process=2,
    process_seconds=args.process_seconds,validation=True)
(out/'budget.json').write_text(json.dumps(budget,indent=2)+'\n')
orders=out/'settle.orders';orders.write_text('0 select all\n0 order 0\n600 ratio 65\n')
cases=[]
for name,zoom in [('far',-1000),('normal',0),('near',1000),('near-no-scattering',1000),('near-no-global-atmosphere',1000)]:
    data=deepcopy(base);settings=deepcopy(config)
    if name=='near-no-scattering':settings['volume']['rays_per_pixel']=0
    if name=='near-no-global-atmosphere':data['appearance'].pop('atmosphere')
    world=out/(name+'-world.json');write_world(world,data)
    render=out/(name+'-render.json');render.write_text(json.dumps(settings,indent=2)+'\n')
    common=[*command,'--world',str(world),'--config',str(render),'--ticks','4800','--replay',str(orders),
        '--world-seed','1234','--camera-zoom',str(zoom),'--time','80','--step-seconds','0','--no-stats']
    cases.append(dict(name=name,common=common,capture=out/(name+'.ppm')))
(out/'commands.json').write_text(json.dumps([c['common']+['--headless','--frames','2','--capture',str(c['capture']),
    '--state',str(out/(c['name']+'-state.json')),'--scene-state',str(out/(c['name']+'-scene.json'))] for c in cases],indent=2)+'\n')

def coverage_proxy(distance):
    # Nominal spherical cell coverage on a 16-pixel grid, not triangle coverage.
    offset=config['camera']['position'];length=math.sqrt(sum(v*v for v in offset));direction=[v/length for v in offset]
    forward=[-v for v in direction];right=[1,0,0];up=[0,-forward[2],forward[1]]
    tangent=math.tan(math.radians(config['camera']['vertical_fov_degrees']/2));covered=total=0
    radius=base['bodies'][0]['radius']
    for y in range(8,1080,16):
        for x in range(8,1920,16):
            sx=(2*x/1920-1)*tangent*1920/1080;sy=(1-2*y/1080)*tangent
            ray=[f+r*sx+u*sy for f,r,u in zip(forward,right,up)];norm=math.sqrt(sum(v*v for v in ray))
            along=sum(-v*w for v,w in zip(direction,ray))*distance/norm
            covered+=along>0 and distance*distance-along*along<=radius*radius;total+=1
    return covered/total

if args.cpu:
    reference=None;meshes=None;result=[]
    for case in cases:
        name=case['name'];state=out/(name+'-cpu-state.json');scene=out/(name+'-cpu-scene.json')
        p=subprocess.run(case['common']+['--smoke','--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10)
        (out/(name+'-cpu.log')).write_text(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError(name+': '+p.stdout+p.stderr)
        simulation=json.loads(state.read_text());drawing=json.loads(scene.read_text())
        if reference is None:reference=simulation;meshes=drawing['meshes']
        assert simulation==reference,'Zoom or diagnostic rendering changed simulation state'
        assert drawing['meshes']==meshes,'Zoom changed geometry or wall displacement'
        line=next(v for v in p.stdout.splitlines() if v.startswith('Initial camera:'))
        numbers=[float(v) for v in re.findall(r'[-+0-9.eE]+',line.split(':',1)[1].replace('target',''))]
        distance=math.dist(numbers[:3],numbers[3:6])
        result.append(dict(case=name,distance=distance,nominal_cell_sphere_coverage=coverage_proxy(distance),media=len(drawing['media']),triangles=sum(m['triangles'] for m in drawing['meshes']),camera=line))
    (out/'cpu-report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2));print('Five CPU application cases passed: identical simulation and geometry; only camera or diagnostic presentation changed.')
    sys.exit(0)
if not args.run:
    print(f'Prepared five native 1920x1080 cases in {out}; {args.film_samples} film samples, two frames per process, {args.process_seconds:g} seconds per process. No GPU work started.')
    sys.exit(0)

environment=os.environ.copy();environment['NANOVA_VULKAN_VALIDATION']='1';started=time.monotonic();report=[]
labels={'application':'Application frame','surface':'GPU surface','volume':'GPU volumes','backdrop':'GPU backdrop',
    'acceleration':'GPU acceleration update','effects':'GPU effects','display':'GPU fog/bloom/display','gpu_total':'total render'}
for case in cases:
    name=case['name']
    invocation=case['common']+['--headless','--frames','2','--capture',str(case['capture']),
        '--state',str(out/(name+'-state.json')),'--scene-state',str(out/(name+'-scene.json'))]
    p=subprocess.run(invocation,capture_output=True,text=True,env=environment,timeout=args.process_seconds)
    (out/(name+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode or 'Vulkan validation: enabled.' not in p.stdout:raise RuntimeError(name+': '+p.stdout+p.stderr)
    frames={key:[float(v) for v in re.findall(re.escape(label)+r': ([0-9.]+) ms',p.stdout)] for key,label in labels.items()}
    assert all(len(values)==2 for values in frames.values()),'Missing frame timestamps'
    state=json.loads((out/(name+'-state.json')).read_text());scene=json.loads((out/(name+'-scene.json')).read_text())
    if report:
        assert state==reference,'Zoom changed simulation state'
        assert scene['meshes']==meshes,'Zoom changed geometry'
    else:reference=state;meshes=scene['meshes']
    item=dict(case=name,frames_ms=frames,mean_ms={key:statistics.mean(values) for key,values in frames.items()},
        capture=str(archive_capture(case['capture'],'zoom-'+name)))
    report.append(item);(out/'report.json').write_text(json.dumps(dict(elapsed=time.monotonic()-started,budget=budget,cases=report),indent=2)+'\n')
    print(name,item['mean_ms'],flush=True)
print('Bounded zoom diagnosis complete. Two frames per case do not establish steady performance.')
