#!/usr/bin/env python3
"""Prepare or run bounded native-1080p captures of owned starts and bomber seeds."""
import argparse
from copy import deepcopy
import json,os,re,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--run',action='store_true');a=p.parse_args()
binary=a.binary.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(ROOT/'scripts/proton_run.py'),str(binary)]
settings=json.loads((binary.parent/'assets/rendering/renderer.json').read_text());assert settings['output']['width']==1920 and settings['output']['height']==1080
base=stage_world(binary.parent/'assets/levels/virus-player-level.json',out)
base['match']=dict(player='virus',opponent='immune');base['rules'].pop('exploration',None);base['rules']['ai']['expansion_delay']=300;base['rules']['economy']['enemy_attack_seconds']=10000
home=deepcopy(base['bodies'][0]);base['bodies']=[deepcopy(home) for _ in range(3)]
for body,x,owner in zip(base['bodies'],[-80,80,0],['virus','immune','neutral']):body.update(position=[x,0,0],owner=owner);body['site']['availability']=0
for u in base['content']['units']:u['manufacture_seconds']=10000
cases=[]
for faction in ('virus','immune'):
 name=faction+'_colonization_seed';world=deepcopy(base);world['match'].update(player=faction,opponent='immune' if faction=='virus' else 'virus');world['bodies'][0]['owner']=faction;world['bodies'][1]['owner']=world['match']['opponent'];world['spawns']=[dict(type=faction+'_bomber',position=[0,0,6])]
 wp=out/(name+'_world.json');write_world(wp,world);rp=out/(name+'.orders');rp.write_text('0 select all\n0 order 2\n')
 config=out/(name+'_render.json');config.write_text(json.dumps(settings,indent=2)+'\n')
 cases.append((name,['--world',str(wp),'--config',str(config),'--ticks','90','--replay',str(rp)]))
opening=deepcopy(settings);opening['camera'].update(position=[-103.92305,-60,20.88],target=[-103.92305,-60,0]);config=out/'owned_start_render.json';config.write_text(json.dumps(opening,indent=2)+'\n')
cases.append(('owned_start',['--world',str(binary.parent/'assets/levels/large_level.json'),'--config',str(config),'--ticks','900']))
(out/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
if not a.run:print('Prepared three two-frame captures. No GPU work started.');raise SystemExit(0)
env=os.environ.copy();env['NANOVORE_VULKAN_VALIDATION']='1';env['NANOVORE_GPU_TIMEOUT_SECONDS']='10'
records=[];start=time.monotonic()
for name,args in cases:
 common=[*command,*args,'--world-seed','1234','--step-seconds','0']
 for mode in ('cpu','gpu'):
  state=out/(name+'_'+mode+'_state.json');flags=['--smoke'] if mode=='cpu' else ['--headless','--frames','2','--no-stats','--capture',str(out/(name+'.ppm'))]
  result=subprocess.run([*common,*flags,'--state',str(state),'--scene-state',str(out/(name+'_'+mode+'_scene.json'))],capture_output=True,text=True,env=env,timeout=10)
  (out/(name+'_'+mode+'.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,(name,mode,result.stdout,result.stderr)
  if mode=='gpu':
   assert 'Vulkan validation: enabled.' in result.stdout and 'VUID-' not in result.stdout+result.stderr and 'Validation Error' not in result.stdout+result.stderr
   times=list(map(float,re.findall(r'total render: ([0-9.]+) ms',result.stdout)));assert len(times)==2
 cpu=json.loads((out/(name+'_cpu_state.json')).read_text());gpu=json.loads((out/(name+'_gpu_state.json')).read_text());assert cpu==gpu,'Rendering changed game state'
 scene=json.loads((out/(name+'_gpu_scene.json')).read_text());assert all(0<=i['mesh']<len(scene['meshes']) for i in scene['instances'])
 if name!='owned_start':assert gpu['settlement']['seed_launches']==1 and any(p['seed'] for p in gpu['projectile_state'])
 else:assert sum(c['active'] for c in gpu['colonies'])==3
 records.append(dict(case=name,gpu_ms=times,screenshot=str(archive_capture(out/(name+'.ppm'),name)),simulation_equal=True))
 assert time.monotonic()-start<30,'Capture batch exceeded 30 seconds'
(out/'report.json').write_text(json.dumps(dict(cases=records,seconds=time.monotonic()-start,limits='Two frames per process, default sampling, host validation. Sustained GPU and native Windows are not tested.'),indent=2)+'\n')
print('Faction opening render E2E passed:',out/'report.json')
