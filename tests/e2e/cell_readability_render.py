#!/usr/bin/env python3
"""Prepare or run native cell depth and SSS application captures."""
from copy import deepcopy
import argparse,json,math,os,re,struct,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/cell-readability');p.add_argument('--run',action='store_true');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
base=stage_world(root/'assets/levels/virus-player-level.json',out);base['match']['auto_colonize']=False
# A wall-only authored diagnostic cell exposes opaque depth markers to the real renderer.
body=next(b for b in base['content']['bodies'] if b['name']=='unclaimed');raw=(out/body['model']).resolve().read_bytes();nv,nt=struct.unpack_from('<II',raw,8);stride=64 if raw[:8]==b'FLMESH02' else 48
tri=raw[16+nv*stride:];wall=[tri[i*16:(i+1)*16] for i in range(nt) if struct.unpack_from('<I',tri,i*16+12)[0]==3]
(out/'wall.fmesh').write_bytes(raw[:8]+struct.pack('<II',nv,len(wall))+raw[16:16+nv*stride]+b''.join(wall));body['model']='wall.fmesh';body.pop('variation')
base['content']['bodies'].append(dict(name='depth_probe',label='PROBE',geometry='sphere',hosts_site=False));base['spawns']=[]
# Keep both required match factions outside the optical probe.
base['spawns']=[dict(type='virus_fighter',position=[-60,-4,8]),dict(type='immune_fighter',position=[48,30,8])]
base['rules']['ai']['expansion_delay']=300
base['appearance'].pop('atmosphere');base['appearance'].pop('fog');base['bodies']=base['bodies'][:2]
center=base['bodies'][0]['position'];radius=base['bodies'][0]['radius'];n=[0,3/math.sqrt(109),10/math.sqrt(109)];probes=[]
for x,depth,tint in [(-1.3,.18,[.95,.25,.05]),(0,.5,[.1,.9,.2]),(1.3,.82,[.05,.3,1])]:
 half=math.sqrt(radius*radius-x*x);point=[center[i]+(x if i==0 else n[i]*half*(1-2*depth)) for i in range(3)];probes.append(point)
 base['bodies'].append(dict(type='depth_probe',owner='neutral',position=point,radius=.35,tint=tint))
config=json.loads((root/'assets/rendering/renderer.json').read_text());config['sampling']['samples_per_pixel']=3;config['camera']['position']=[0,3,10];config.pop('tilt_shift')
clear=deepcopy(base)
for b in clear['bodies']:
 if 'medium' in b:b['medium']['depth_blur']['max_radius']=0
legacy=deepcopy(clear);legacy['appearance']['thin_surface']['blur_radius']=18
normal=stage_world(root/'assets/levels/virus-player-level.json',out);normal['rules']['ai']['expansion_delay']=300
without_sss=deepcopy(config);without_sss['subsurface']['strength']=0
cases=[('depth-ramp',base,config,1,False),('depth-clear',clear,config,1,False),('depth-immediate',legacy,config,1,False),('sss-debug',normal,config,2400,True),('sss-disabled-debug',normal,without_sss,2400,True)]
prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
commands=[]
for name,world,settings,ticks,debug in cases:
 wp=out/(name+'-world.json');cp=out/(name+'-render.json');write_world(wp,world);cp.write_text(json.dumps(settings))
 cmd=[*prefix,'--world',str(wp),'--config',str(cp),'--headless','--frames','1','--ticks',str(ticks),'--time','0','--step-seconds','0','--world-seed','1234','--no-stats','--capture',str(out/(name+'.ppm'))]
 if debug:cmd+=['--debug-views']
 commands.append(cmd)
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:print('Prepared five 1920x1080 one-frame application cases. No GPU work started.');sys.exit(0)
# CPU preflight catches invisible or misplaced markers before any GPU work.
probe=out/'probe-scene.json'
check=subprocess.run([*prefix,'--smoke','--world',str(out/'depth-ramp-world.json'),'--config',str(out/'depth-ramp-render.json'),'--ticks','1','--scene-state',str(probe)],capture_output=True,text=True,timeout=10)
assert check.returncode==0,(check.stdout,check.stderr)
scene=json.loads(probe.read_text());assert all(any(i['slot']==256+n for i in scene['instances']) for n in (0,2,3,4))
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='1';images={};report={};start=time.monotonic()
for (name,*_),cmd in zip(cases,commands):
 remaining=5-(time.monotonic()-start);assert remaining>0,'Five-second GPU batch limit expired'
 result=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=min(2,remaining));(out/(name+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(name,result.stdout,result.stderr)
 assert 'Vulkan validation: enabled.' in result.stdout
 raw=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header;images[name]=raw[header.end():]
 report[name]={'application_ms':list(map(float,re.findall(r'Application frame: ([0-9.]+)',result.stdout)))}
report['batch_seconds']=time.monotonic()-start
for name in images:archive_capture(out/(name+'.ppm'),'cell-'+name)
def delta(a,b,box):
 x0,y0,x1,y1=box
 return sum(abs(images[a][(y*1920+x)*3+c]-images[b][(y*1920+x)*3+c]) for y in range(y0,y1) for x in range(x0,x1) for c in range(3))/((x1-x0)*(y1-y0)*3)
f=[-v for v in n];up=[0,n[2],-n[1]];eye=[center[i]+[0,3,10][i] for i in range(3)];focal=1080/(2*math.tan(math.radians(22.5)))
for name,point in zip(('front','middle','back'),probes):
 v=[point[i]-eye[i] for i in range(3)];z=sum(v[i]*f[i] for i in range(3));x=960+v[0]*focal/z;y=540-sum(v[i]*up[i] for i in range(3))*focal/z;radius=.35*focal/z
 # Measure a ring inside each opaque marker. Flat centers cannot establish blur.
 pixels=[(yy*1920+xx)*3+c for yy in range(int(y-radius),int(y+radius+1)) for xx in range(int(x-radius),int(x+radius+1)) if .8*radius<math.hypot(xx+.5-x,yy+.5-y)<.95*radius for c in range(3)]
 for variant in ('ramp','immediate'):
  report[name+'_'+variant+'_delta']=sum(abs(images['depth-'+variant][i]-images['depth-clear'][i]) for i in pixels)/len(pixels)
assert report['front_ramp_delta']<.02,report
assert report['front_immediate_delta']>.2,report
assert report['middle_ramp_delta']>.1 and report['back_ramp_delta']>.2,report
# Panel 9 is SSS contribution in the four-column, three-row F2 display.
report['sss_contribution_delta']=delta('sss-debug','sss-disabled-debug',(500,765,930,1025))
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['sss_contribution_delta']>.02,report
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
