#!/usr/bin/env python3
"""Compare the real renderer with a saved build at fixed image quality."""
import argparse,json,os,re,signal,statistics,subprocess,sys,time
from copy import deepcopy
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from gpu_environment import check_gpu_environment
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path);p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--case',choices=('near','normal','grown','inside','far','overlap','boundary','opaque'),default='near')
p.add_argument('--samples',type=int,choices=(3,6),default=6)
p.add_argument('--blur-profile',choices=('authored','linear','off'),default='authored')
p.add_argument('--subgroup-size',type=int,choices=(0,32,64),default=32)
p.add_argument('--baseline-subgroup-size',type=int,choices=(0,32,64))
p.add_argument('--pairs',type=int,choices=(1,3),default=1)
p.add_argument('--reverse',action='store_true')
p.add_argument('--no-capture',action='store_true')
p.add_argument('--timing',action='store_true');p.add_argument('--run',action='store_true')
p.add_argument('--output',type=Path,default=root/'build/render-perf');a=p.parse_args()
if a.no_capture and not a.timing:p.error('--no-capture requires --timing')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'tests/e2e/static_lighting.py'),str(a.binary),'--case',a.case,'--variants','cached','--output',str(out)],check=True,stdout=subprocess.PIPE,text=True)
base=json.loads((out/'commands.json').read_text())[0][1]
if a.blur_profile!='authored':
 wp=Path(base[base.index('--world')+1]);world=json.loads(wp.read_text())
 for body in world['bodies']:
  if 'medium' not in body:continue
  if a.blur_profile=='off':body['medium'].pop('depth_blur',None)
  else:body['medium']['depth_blur']=dict(pixels_per_unit=4,sharp_distance=1.5,max_radius=18)
 wp.write_text(json.dumps(world,indent=2)+'\n')
settings=json.loads(Path(base[base.index('--config')+1]).read_text());settings['sampling']['samples_per_pixel']=a.samples
commands=[]
for pair in range(a.pairs):
 for variant in ('baseline','current') if (pair+int(a.reverse))%2==0 else ('current','baseline'):
  name=str(pair)+'-'+variant;config=deepcopy(settings)
  config['lighting'].pop('subgroup_size',None)
  if variant=='current':config['lighting']['subgroup_size']=a.subgroup_size
  elif a.baseline_subgroup_size is not None:config['lighting']['subgroup_size']=a.baseline_subgroup_size
  cp=out/(name+'-render.json');cp.write_text(json.dumps(config,indent=2)+'\n')
  binary=(a.baseline if variant=='baseline' else a.binary).resolve()
  command=list(base)
  if binary.suffix.lower()!='.exe' and sys.platform!='win32':command=[str(binary)]+command[command.index('--world'):]
  else:command[command.index(str(a.binary.resolve()))]=str(binary)
  for flag,value in (('--config',cp),('--capture',out/(name+'.ppm')),('--state',out/(name+'-state.json'))):command[command.index(flag)+1]=str(value)
  if a.no_capture:
   index=command.index('--capture');del command[index:index+2]
  commands.append((name,command))
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:
 print('Prepared',len(commands),'sequential native 1080p two-frame application runs;',a.samples,'film + 1 volume + 1 backdrop starts. 2s/process, 5s/batch, no hardware tracing. No GPU work started.')
 raise SystemExit(0)
check_gpu_environment();env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0' if a.timing else '1'
report={'capture':not a.no_capture,'case':a.case,'blur_profile':a.blur_profile,'samples':a.samples,'validation':not a.timing,'runs':{}}
started=time.monotonic()
for name,command in commands:
 remaining=5-(time.monotonic()-started);assert remaining>0,'GPU batch expired'
 before=time.monotonic();process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
 try:output,errors=process.communicate(timeout=min(2,remaining))
 except subprocess.TimeoutExpired:
  if sys.platform=='win32':process.terminate()
  else:os.killpg(process.pid,signal.SIGTERM)
  try:output,errors=process.communicate(timeout=.2)
  except subprocess.TimeoutExpired:
   if sys.platform=='win32':process.kill()
   else:os.killpg(process.pid,signal.SIGKILL)
   output,errors=process.communicate()
  (out/(name+'.log')).write_text(output+errors);raise RuntimeError('GPU process limit reached: '+name) from None
 (out/(name+'.log')).write_text(output+errors)
 assert process.returncode==0,(name,output,errors)
 expected='Vulkan validation: not reported by loader/tooling.' if a.timing else 'Vulkan validation: enabled.'
 assert expected in output
 labels={'surface':'GPU surface','volume':'GPU volumes','grid':'GPU shadow grid','bake':'GPU static lighting bake','gpu':'total render','app':'Application frame','fog_focus_display':'GPU fog/bloom/display','cell_blur':'GPU medium blur','backdrop':'GPU backdrop'}
 frames={key:list(map(float,re.findall(re.escape(label)+r': ([0-9.]+) ms',output))) for key,label in labels.items()}
 assert all(len(v)==2 for v in frames.values()),frames
 if name.endswith('current'):assert 'Compute subgroups: '+str(a.subgroup_size)+' ' in output
 cache=re.findall(r'Static lighting: (\d+) baked, (\d+) reused;',output)
 assert len(cache)==2 and int(cache[1][0])==0 and int(cache[1][1])>0,cache
 report['runs'][name]=dict(seconds=time.monotonic()-before,frames_ms=frames)
report['batch_seconds']=time.monotonic()-started
images={};state=None
for name,_ in commands:
 current=json.loads((out/(name+'-state.json')).read_text())
 if state is None:state=current
 else:assert current==state,'Rendering changed simulation state'
 if a.no_capture:continue
 data=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',data);assert header
 pixels=data[header.end():];assert len(pixels)==1920*1080*3
 variant=name.split('-',1)[1]
 if variant in images:assert images[variant]==pixels,'Repeated frozen output changed'
 else:
  images[variant]=pixels
  archive_capture(out/(name+'.ppm'),'render-perf-'+a.case+'-'+variant+'-'+str(a.samples)+'-film')
report['image_difference']=None
if not a.no_capture:
 error=[abs(x-y) for x,y in zip(images['baseline'],images['current'])]
 report['image_difference']=dict(mean_rgb8=sum(error)/len(error),max_rgb8=max(error),changed_channels=sum(d!=0 for d in error))
report['summary']={}
for key in report['runs']['0-baseline']['frames_ms']:
 values={variant:[report['runs'][str(pair)+'-'+variant]['frames_ms'][key][-1] for pair in range(a.pairs)] for variant in ('baseline','current')}
 report['summary'][key]=dict(values_ms=values,median_ms={v:statistics.median(values[v]) for v in values},paired_savings_ms=[x-y for x,y in zip(values['baseline'],values['current'])])
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Image difference:',report['image_difference'])
for key in ('surface','volume','fog_focus_display','gpu','app'):print(key,report['summary'][key])
print('Batch seconds:',report['batch_seconds'])
if not a.no_capture:
 assert report['image_difference']['mean_rgb8']<.02 and report['image_difference']['max_rgb8']<=2,'Image difference exceeds the comparison limit'
