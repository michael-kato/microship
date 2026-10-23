#!/usr/bin/env python3
"""Prepare or run bounded application views of fog falloff and memory."""
from copy import deepcopy
import argparse,json,os,re,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from gpu_environment import gpu_process_timeout_seconds
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/fog-appearance');p.add_argument('--run',action='store_true');p.add_argument('--timing',action='store_true');p.add_argument('--frames',type=int,choices=[1,2],default=1);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
data=stage_world(root/'assets/levels/virus-player-level.json',out)
data['spawns']=[dict(type='virus_fighter',position=[-45,0,6.4]),dict(type='immune_fighter',position=[-40,0,6.4])]
for u in data['content']['units']:
 u['range']=.001
 if u['name']=='virus_fighter':u['speed']=6
 if u['name']=='immune_fighter':u['speed']=.001
replay=out/'scout.orders';replay.write_text('0 select all\n0 move -45 200 6.4\n')
config=json.loads((root/'assets/rendering/renderer.json').read_text());config['sampling']['samples_per_pixel']=3
cp=out/'render.json';cp.write_text(json.dumps(config,indent=2))
narrow=deepcopy(data);narrow['appearance']['fog']['sight_feather']=2
immediate=deepcopy(data);immediate['appearance']['fog']['memory_half_life_seconds']=0
cases=[('broad',data,1),('narrow',narrow,1),('departed',data,360),('fading',data,540),('immediate',immediate,360)]
commands=[]
for name,world,ticks in cases:
 wp=out/(name+'-world.json');write_world(wp,world)
 prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
 commands.append([*prefix,'--world',str(wp),'--config',str(cp),'--world-seed','1234','--replay',str(replay),'--ticks',str(ticks),'--time','0','--step-seconds','0','--headless','--frames',str(a.frames),'--capture',str(out/(name+'.ppm')),'--state',str(out/(name+'-state.json')),'--no-stats'])
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:print('Prepared five native 1920x1080 cases; no GPU work started.');sys.exit(0)
env=os.environ.copy();env['NANOVORE_VULKAN_VALIDATION']='0' if a.timing else '1';start=time.monotonic();report={};images={}
for (name,_,_),command in zip(cases,commands):
 remaining=gpu_process_timeout_seconds()
 result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=remaining);(out/(name+'.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,(name,result.stdout,result.stderr)
 if not a.timing:assert 'Vulkan validation: enabled.' in result.stdout
 raw=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header;images[name]=raw[header.end():]
 report[name]={key:list(map(float,re.findall(pattern,result.stdout))) for key,pattern in [('gpu_ms',r'total render: ([0-9.]+)'),('app_ms',r'Application frame: ([0-9.]+)'),('fog_display_ms',r'GPU fog/bloom/display: ([0-9.]+)')]}
report['batch_seconds']=time.monotonic()-start
# Fixed-image measurements use a region behind the departing scout. The scene
# time is frozen, so backdrop pulsing and membrane animation do not affect it.
def delta(a,b,box):
 x0,y0,x1,y1=box
 return sum(abs(images[a][(y*1920+x)*3+c]-images[b][(y*1920+x)*3+c]) for y in range(y0,y1) for x in range(x0,x1) for c in range(3))/((x1-x0)*(y1-y0)*3)
report['wide_falloff_rgb8_change']=delta('broad','narrow',(1050,200,1550,700))
report['memory_rgb8_change']=delta('departed','immediate',(1100,400,1350,650))
report['decay_rgb8_change']=delta('departed','fading',(1100,400,1350,650))
assert report['wide_falloff_rgb8_change']>.1 and report['memory_rgb8_change']>.1 and report['decay_rgb8_change']>.1,report
assert json.loads((out/'departed-state.json').read_text())==json.loads((out/'immediate-state.json').read_text())
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
