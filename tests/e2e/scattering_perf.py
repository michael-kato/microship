#!/usr/bin/env python3
"""Prepare or run bounded SSS/volume app comparisons at native resolution."""
from copy import deepcopy
import argparse,json,os,re,shutil,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--cases',nargs='+',choices=('normal','near','boundary','overlap','opaque','grown','far','inside'),default=['near']);p.add_argument('--diagnose',action='store_true');p.add_argument('--lights',choices=('all','key','none'),default='all');p.add_argument('--steps',type=int,choices=(2,8),default=8);p.add_argument('--shadow-grid',type=int,choices=(0,32),default=32);p.add_argument('--external',action='store_true');p.add_argument('--reverse',action='store_true');p.add_argument('--timing',action='store_true');p.add_argument('--run',action='store_true');p.add_argument('--output',type=Path,default=root/'build/scattering-perf');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
if a.diagnose and a.baseline:p.error('Use diagnosis or before/after comparison')
if not a.diagnose and not a.baseline:p.error('Comparison requires --baseline')
world=stage_world(root/'assets/levels/biology-virus.json',out);config=json.loads((root/'assets/rendering/biology.json').read_text());config['sampling']['samples_per_pixel']=3
assert config['output']['width']==1920 and config['output']['height']==1080
assert config['volume']==dict(rays_per_pixel=1,integration_steps=8)
config['volume']['integration_steps']=a.steps;config['lighting']['shadow_grid_resolution']=a.shadow_grid
# Static bakes use a different spatial approximation; compare them with static_lighting.py.
config['lighting'].pop('static_lighting_cache',None);config['lighting'].pop('force_lighting_rebuild',None)
if a.external:
 world.pop('effects',None);world.pop('rules',None);world['match']={'player':'virus','opponent':'immune'};config.pop('tilt_shift',None)
 target=world['bodies'][0]['position'];offset=[x-y for x,y in zip(config['camera']['position'],config['camera']['target'])]
 config['camera']['position']=[x+y for x,y in zip(target,offset)];config['camera']['target']=target
if a.lights!='all':
 for name,light in world['appearance']['lights'].items():
  if a.lights=='none' or name!='key':light['irradiance']=0
prepared=[]
for case in a.cases:
 data=deepcopy(world);settings=deepcopy(config)
 if case=='inside':
  settings['camera']['min_distance']=1;settings['camera']['position']=[x+y for x,y in zip(settings['camera']['target'],(0,.6,2))]
 if case=='grown':
  for building in data['content']['buildings']:
   building['growth_seconds']=.01;building['establishment_seconds']=.01;building['branches']['seconds']=.01
 if case=='boundary':data['appearance']['atmosphere'].update(center=data['bodies'][0]['position'],radius=26,edge_fade=.4)
 if case=='overlap':data['bodies'][2]['position']=[x+y for x,y in zip(data['bodies'][0]['position'],(2,0,0))]
 if case=='opaque':data['appearance']['thin_surface']['transmission']=0
 wp=out/(case+'-world.json');write_world(wp,data)
 variants=('full','no-sss','wrap-only','no-volume') if a.diagnose else ('baseline','current')
 if a.reverse:variants=tuple(reversed(variants))
 for variant in variants:
  cfg=deepcopy(settings)
  if variant=='no-sss':cfg['subsurface']['strength']=0
  if variant=='wrap-only':cfg['subsurface']['transmission']=0
  if variant=='no-volume':cfg['volume']['rays_per_pixel']=0
  name=case+'-'+variant;cp=out/(name+'-render.json');cp.write_text(json.dumps(cfg,indent=2)+'\n')
  binary=(a.baseline if variant=='baseline' else a.binary).resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
  shader=[]
  if a.external:
   target=out/('external-'+variant);target.mkdir(exist_ok=True)
   for source in (binary.parent/'assets/shaders').glob('*.spv'):shutil.copyfile(source,target/source.name)
   alternate=target/'external.comp.spv';shutil.copyfile(target/'ray_trace.comp.spv',alternate);shader=['--shader',str(alternate)]
  args=[*prefix,*shader,'--world',str(wp),'--config',str(cp),'--headless','--frames','2','--no-stats','--ticks','1' if case=='overlap' else '600','--world-seed','1234','--time','10','--step-seconds','0','--capture',str(out/(name+'.ppm')),'--state',str(out/(name+'-state.json'))]
  if case in ('near','opaque','grown'):args+=['--camera-zoom','1000']
  if case=='far':args+=['--camera-zoom','-1000']
  prepared.append((name,args))
(out/'commands.json').write_text(json.dumps(prepared,indent=2)+'\n')
if not a.run:print('Prepared',len(prepared),'sequential native 1920x1080 processes, 2 frames each, 3 film + 1 volume + 1 backdrop starts; 2s/process, 5s/batch. Diagnosis can disable SSS or volume. No GPU work started.');raise SystemExit(0)
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0' if a.timing else '1';report={};start=time.monotonic()
for name,args in prepared:
 remaining=5-(time.monotonic()-start);assert remaining>0,'Five-second GPU batch expired'
 before=time.monotonic();process=subprocess.run(args,capture_output=True,text=True,env=env,timeout=min(2,remaining));(out/(name+'.log')).write_text(process.stdout+process.stderr)
 assert process.returncode==0,(name,process.stdout,process.stderr)
 expected='Vulkan validation: not reported by loader/tooling.' if a.timing else 'Vulkan validation: enabled.';assert expected in process.stdout
 labels={'SURFACE + SSS':'GPU surface','VOLUME':'GPU volumes','GPU RENDER':'total render','CPU FRAME':'Application frame'}
 values={label:list(map(float,re.findall(re.escape(text)+r': ([0-9.]+) ms',process.stdout))) for label,text in labels.items()}
 assert all(len(v)==2 for v in values.values()),(name,values)
 frames=[{label:value[n] for label,value in values.items()} for n in (0,1)]
 camera_line=next(line for line in process.stdout.splitlines() if line.startswith('Initial camera:'))
 camera=list(map(float,re.findall(r'-?\d+(?:\.\d+)?',camera_line)));distance=sum((camera[i]-camera[i+3])**2 for i in range(3))**.5
 if name.startswith('far-'):assert abs(distance-config['camera']['max_distance'])<.002,(name,camera_line)
 if name.startswith('inside-'):assert distance<world['bodies'][0]['radius'],(name,camera_line)
 if name.startswith('grown-') and not a.external:
  colony=json.loads((out/(name+'-state.json')).read_text())['colonies'][0]
  assert colony['growth']==1 and colony['structures'] and all(limb['growth']==1 for limb in colony['structures']),'Grown fixture is incomplete'
 report[name]=dict(seconds=time.monotonic()-before,camera_distance=distance,frames=frames)
report['batch_seconds']=time.monotonic()-start;report['validation']=not a.timing
# Image analysis runs after all GPU processes have stopped.
if not a.diagnose:
 for case in a.cases:
  before=(out/(case+'-baseline.ppm')).read_bytes();after=(out/(case+'-current.ppm')).read_bytes()
  def pixels(raw):
   header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header
   result=raw[header.end():];assert len(result)==1920*1080*3;return result
  old,new=pixels(before),pixels(after);total=changed=over_one=maximum=0
  for x,y in zip(old,new):
   delta=abs(x-y);total+=delta;changed+=delta!=0;over_one+=delta>1;maximum=max(maximum,delta)
  report[case+'-difference']=dict(mean_rgb8=total/len(old),max_rgb8=maximum,changed=changed,over_one=over_one)
  assert json.loads((out/(case+'-baseline-state.json')).read_text())==json.loads((out/(case+'-current-state.json')).read_text()),'Rendering changed simulation'
for name,_ in prepared:archive_capture(out/(name+'.ppm'),'scattering-'+name+('-timing' if a.timing else ''))
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
for name,_ in prepared:print(name,{k:report[name]['frames'][1][k] for k in ('SURFACE + SSS','VOLUME','GPU RENDER','CPU FRAME')})
print({k:v for k,v in report.items() if 'difference' in k});print('GPU batch:',report['batch_seconds'],'seconds; validation:',not a.timing)
if not a.diagnose:assert all(not report[c+'-difference']['over_one'] for c in a.cases),'Image difference exceeds one RGB8 level'
