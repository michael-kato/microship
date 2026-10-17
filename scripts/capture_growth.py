#!/usr/bin/env python3
"""Prepare growth scenes; --run captures three bounded native 1080p app runs."""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from authored_assets import stage_world,write_world
from gpu_environment import check_gpu_environment
from save_progress import archive_capture

NATIVE_WIDTH = 1920
NATIVE_HEIGHT = 1080
MAX_SAMPLE_STARTS = 8
DISABLED_MANUFACTURE_SECONDS = 1000
DISABLED_AI_EXPANSION_SECONDS = 300
FAST_GROWTH_SECONDS = .1
MAX_PROCESS_SECONDS = 10
ATTACKER_TIP_OFFSET = .4
FIXTURE_ATTACKER_SPEED = .01
FIXTURE_ATTACK_INTERVAL = .12
CAMERA_HEIGHT = 9
CAMERA_DISTANCE = 30
SMALL_CELL_RADIUS = 2.1
GROWTH_CAPTURE_TICKS = 1800
SPORE_CAPTURE_TICKS = 1812
DAMAGE_CAPTURE_TICKS = 105
BLAST_POSITION_RADIUS_SCALE = 1.5
BLAST_TARGET_OFFSET = .35
BLAST_CAPTURE_TICKS = 12
MIN_TIMEOUT_SECONDS = .001

FIXTURE_REGROW_SECONDS=10
MAX_BATCH_SECONDS=30

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--run',action='store_true')
parser.add_argument('--case',action='append',choices=['organelle-growth','immune-factories','small-cell-factories','fungal-seeking-spores','immune-breaks-factory-limbs','fungal-blast'])
args=parser.parse_args();root=Path(__file__).resolve().parents[1];binary=args.binary.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
check_gpu_environment()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
config=json.loads((root/'assets/rendering/biology.json').read_text())
assert (config['output']['width'],config['output']['height'])==(NATIVE_WIDTH,NATIVE_HEIGHT)
assert config['sampling']['samples_per_pixel']+config['volume']['rays_per_pixel']+1<=MAX_SAMPLE_STARTS
limbs=stage_world(root/'assets/levels/biology-virus.json',out);limbs['rules'].pop('exploration',None);limbs['spawns']=[]
limbs['rules']['economy']['manufacture_seconds']=DISABLED_MANUFACTURE_SECONDS;limbs['rules']['ai']['expansion_delay']=DISABLED_AI_EXPANSION_SECONDS
for p in limbs['content']['buildings']:
    if p['faction']!='fungus':p['growth_seconds']=FAST_GROWTH_SECONDS;p['branches'].update(seconds=FAST_GROWTH_SECONDS,regrow_seconds=FIXTURE_REGROW_SECONDS)
probe=out/'limb-probe-world.json';write_world(probe,limbs);state=out/'limb-probe.json'
p=subprocess.run([*command,'--smoke','--world',str(probe),'--ticks','120','--state',str(state)],capture_output=True,text=True,timeout=MAX_PROCESS_SECONDS)
(out/'limb-probe.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stdout+p.stderr
tip=json.loads(state.read_text())['colonies'][0]['structures'][0]['position'];position=[tip[0],tip[1],tip[2]+ATTACKER_TIP_OFFSET]
limbs['spawns']=[dict(type='immune_bomber',position=position)]
for u in limbs['content']['units']:
    if u['name']=='immune_bomber':u.update(speed=FIXTURE_ATTACKER_SPEED,shot_interval=FIXTURE_ATTACK_INTERVAL)
level=out/'limb-damage-world.json';write_world(level,limbs)
damage_config=deepcopy(config);center=limbs['bodies'][0]['position'];damage_config['camera']['target']=center
damage_config['camera']['position']=[center[0],center[1]+CAMERA_HEIGHT,center[2]+CAMERA_DISTANCE]
camera=out/'limb-camera.json';camera.write_text(json.dumps(damage_config,indent=2)+'\n')
regular=out/'render.json';regular.write_text(json.dumps(config,indent=2)+'\n')
small=stage_world(root/'assets/levels/biology-virus.json',out);small['bodies'][0]['radius']=SMALL_CELL_RADIUS
small_level=out/'small-cell-world.json';write_world(small_level,small)
cases=[('organelle-growth',binary.parent/'assets/levels/biology-virus.json',regular,GROWTH_CAPTURE_TICKS,1),
       ('immune-factories',binary.parent/'assets/levels/biology-immune.json',regular,GROWTH_CAPTURE_TICKS,1),
       ('small-cell-factories',small_level,regular,GROWTH_CAPTURE_TICKS,1),
       ('fungal-seeking-spores',binary.parent/'assets/levels/biology-fungus.json',regular,SPORE_CAPTURE_TICKS,1),
       ('immune-breaks-factory-limbs',level,camera,DAMAGE_CAPTURE_TICKS,2)]
if args.case and 'fungal-blast' in args.case:
    blast=stage_world(root/'assets/levels/biology-fungus.json',out);blast['rules'].pop('exploration',None)
    body=blast['bodies'][0];point=[c+n*body['radius']*BLAST_POSITION_RADIUS_SCALE for c,n in zip(body['position'],body['site']['normal'])]
    blast['spawns']=[dict(type='fungal_spore',position=point),dict(type='swarm_fighter',position=[point[0]+BLAST_TARGET_OFFSET,point[1],point[2]])]
    blast_level=out/'fungal-blast-world.json';write_world(blast_level,blast)
    cases.append(('fungal-blast',blast_level,camera,BLAST_CAPTURE_TICKS,1))
if args.case:cases=[c for c in cases if c[0] in args.case]
if not args.run:
    print(f'Prepared {len(cases)} scenes and checked attachment positions in the CPU application. No GPU started.');sys.exit(0)
deadline=time.monotonic()+MAX_BATCH_SECONDS;results=[]
for name,world,render,ticks,frames in cases:
    capture=out/(name+'.ppm');state=out/(name+'.json')
    p=subprocess.Popen([*command,'--headless','--frames',str(frames),'--no-stats','--world',str(world),'--config',str(render),
                        '--ticks',str(ticks),'--capture',str(capture),'--state',str(state)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=sys.platform!='win32')
    try:stdout,stderr=p.communicate(timeout=min(MAX_PROCESS_SECONDS,max(MIN_TIMEOUT_SECONDS,deadline-time.monotonic())))
    except subprocess.TimeoutExpired:
        if sys.platform=='win32':p.kill()
        else:os.killpg(p.pid,signal.SIGKILL)
        p.communicate();raise RuntimeError('GPU timeout; no later case was launched')
    (out/(name+'.log')).write_text(stdout+stderr);assert p.returncode==0,stdout+stderr
    assert re.match(rb'P6\s+1920\s+1080\s+255\s',capture.read_bytes())
    if os.environ.get('NANOVA_VULKAN_VALIDATION')=='1':assert 'Vulkan validation: enabled.' in stdout
    archive_capture(capture,name)
    d=json.loads(state.read_text())
    if name=='immune-breaks-factory-limbs':
        assert d['fractures']['limbs']>=3 and d['fractures']['fragments']>0
        assert d['colonies'][0]['health']==0 and d['colonies'][0]['active']
    if name=='fungal-blast':assert d['faction_events']['spore_bursts']>0 and d['effects']['pops']>0
    results.append(dict(name=name,application_ms=[float(x) for x in re.findall(r'Application frame: ([\d.]+) ms',stdout)],
                        gpu_trace_ms=[float(x) for x in re.findall(r'GPU trace: ([\d.]+) ms',stdout)],fractures=d['fractures']))
(out/'verification.json').write_text(json.dumps(results,indent=2)+'\n')
print(f'Growth captures passed: {len(cases)} sequential native 1080p processes, at most two frames each, within the eight-sample and 30-second caps.')
