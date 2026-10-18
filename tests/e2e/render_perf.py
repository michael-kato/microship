#!/usr/bin/env python3
"""Compare the real renderer with a saved build at fixed image quality."""
import argparse,json,math,os,re,signal,statistics,subprocess,sys,time
from copy import deepcopy
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from gpu_environment import check_gpu_environment
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path);p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--case',choices=('near','normal','grown','inside','far','overlap','boundary','opaque'),default='near')
p.add_argument('--samples',type=int,choices=(1,2,3,6),default=6)
p.add_argument('--baseline-samples',type=int,choices=(1,2,3,6))
p.add_argument('--baseline-shadow-grid',type=int,choices=(8,16,32,64))
p.add_argument('--baseline-quadrature',choices=('midpoint','gauss_legendre'))
p.add_argument('--shadow-grid',type=int,choices=(8,16,32,64))
p.add_argument('--volume-stride',type=int,choices=(2,4),default=2)
p.add_argument('--baseline-volume-stride',type=int,choices=(2,4))
p.add_argument('--spatial-volume',action='store_true')
p.add_argument('--baseline-spatial-volume',action='store_true')
p.add_argument('--baseline-spatial-aa',action='store_true')
p.add_argument('--spatial-aa',action='store_true')
p.add_argument('--quadrature',choices=('midpoint','gauss_legendre'),default='midpoint')
p.add_argument('--volume-steps',type=int,choices=(2,3,4,8,32),default=8)
p.add_argument('--baseline-volume-steps',type=int,choices=(2,3,4,8,32))
p.add_argument('--quality-comparison',action='store_true',help='Report intentional sampling differences without the exact-refactor image limit')
p.add_argument('--blur-profile',choices=('authored','linear','off'),default='authored')
p.add_argument('--subgroup-size',type=int,choices=(0,32,64),default=32)
p.add_argument('--baseline-subgroup-size',type=int,choices=(0,32,64))
p.add_argument('--cold-start',action='store_true',help='Use the authorized 10s process and 30s batch limits for new pipeline compilation; still two frames')
p.add_argument('--window',action='store_true')
p.add_argument('--live',action='store_true',help='Advance the actual simulation by one fixed tick per rendered frame')
p.add_argument('--frames',type=int,choices=(2,120),default=2,help='120 needs the user-authorized sustained performance check')
p.add_argument('--pairs',type=int,choices=(1,3),default=1)
p.add_argument('--reverse',action='store_true')
p.add_argument('--no-capture',action='store_true')
p.add_argument('--timing',action='store_true');p.add_argument('--run',action='store_true')
p.add_argument('--output',type=Path,default=root/'build/render-perf');a=p.parse_args()
if a.cold_start and a.frames!=2:p.error('--cold-start is limited to two frames')
process_seconds=10 if a.cold_start else 8 if a.frames>2 else 2
batch_seconds=30 if a.cold_start else 20 if a.frames>2 else 5
if a.no_capture and not a.timing:p.error('--no-capture requires --timing')
if a.frames>2 and (not a.timing or not a.no_capture):p.error('Sustained checks require --timing --no-capture')
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
  config['volume']['integration_steps']=a.volume_steps
  config['volume'].pop('quadrature',None)
  if variant=='current':
   if a.quadrature!='midpoint':config['volume']['quadrature']=a.quadrature
   if a.spatial_aa:config['sampling']['spatial_antialiasing']=True
   if a.spatial_volume:config['volume']['spatial_filter']=True;config['volume']['spatial_stride']=a.volume_stride
   if a.shadow_grid is not None:config['lighting']['shadow_grid_resolution']=a.shadow_grid
  if variant=='baseline':
   if a.baseline_samples is not None:config['sampling']['samples_per_pixel']=a.baseline_samples
   if a.baseline_volume_steps is not None:config['volume']['integration_steps']=a.baseline_volume_steps
   if a.baseline_quadrature is not None:config['volume']['quadrature']=a.baseline_quadrature
   if a.baseline_shadow_grid is not None:config['lighting']['shadow_grid_resolution']=a.baseline_shadow_grid
   if a.baseline_spatial_aa:config['sampling']['spatial_antialiasing']=True
   if a.baseline_spatial_volume:config['volume']['spatial_filter']=True
   if a.baseline_volume_stride is not None:config['volume']['spatial_stride']=a.baseline_volume_stride
  config['lighting'].pop('subgroup_size',None)
  if variant=='current':config['lighting']['subgroup_size']=a.subgroup_size
  elif a.baseline_subgroup_size is not None:config['lighting']['subgroup_size']=a.baseline_subgroup_size
  cp=out/(name+'-render.json');cp.write_text(json.dumps(config,indent=2)+'\n')
  binary=(a.baseline if variant=='baseline' else a.binary).resolve()
  command=list(base)
  command[command.index('--frames')+1]=str(a.frames)
  if a.window:command.remove('--headless');command+=['--no-activate']
  if a.live:
   for flag in ('--time','--step-seconds'):
    index=command.index(flag);del command[index:index+2]
   command+=['--frame-ticks','1']
  if binary.suffix.lower()!='.exe' and sys.platform!='win32':command=[str(binary)]+command[command.index('--world'):]
  else:command[command.index(str(a.binary.resolve()))]=str(binary)
  for flag,value in (('--config',cp),('--capture',out/(name+'.ppm')),('--state',out/(name+'-state.json'))):command[command.index(flag)+1]=str(value)
  if a.no_capture:
   index=command.index('--capture');del command[index:index+2]
  commands.append((name,command))
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:
 print('Prepared',len(commands),'sequential native 1080p two-frame application runs;',a.samples,'film + 1 volume + 1 backdrop starts.',a.frames,'frames;',process_seconds,'s/process,',batch_seconds,'s/batch, no hardware tracing. No GPU work started.')
 raise SystemExit(0)
check_gpu_environment();env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0' if a.timing else '1'
report={'process_limit_seconds':process_seconds,'batch_limit_seconds':batch_seconds,'window':a.window,'live':a.live,'frames':a.frames,'capture':not a.no_capture,'case':a.case,'blur_profile':a.blur_profile,'samples':a.samples,'volume_stride':a.volume_stride,'spatial_volume':a.spatial_volume,'spatial_aa':a.spatial_aa,'shadow_grid':a.shadow_grid,'quadrature':a.quadrature,'volume_steps':a.volume_steps,'baseline_samples':a.baseline_samples,'baseline_volume_steps':a.baseline_volume_steps,'quality_comparison':a.quality_comparison,'validation':not a.timing,'runs':{}}
started=time.monotonic()
for name,command in commands:
 remaining=batch_seconds-(time.monotonic()-started);assert remaining>0,'GPU batch expired'
 before=time.monotonic();process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
 try:output,errors=process.communicate(timeout=min(process_seconds,remaining))
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
 labels={'surface':'GPU surface','volume':'GPU volumes','grid':'GPU shadow grid','bake':'GPU static lighting bake','gpu':'total render','app':'Application frame','cadence':'Application cadence','fog_focus_display':'GPU fog/bloom/display','cell_blur':'GPU medium blur','backdrop':'GPU backdrop'}
 frames={key:list(map(float,re.findall(re.escape(label)+r': ([0-9.]+) ms',output))) for key,label in labels.items()}
 assert all(len(v)==a.frames for k,v in frames.items() if k!='cadence'),frames
 if not frames['cadence']:del frames['cadence']
 if name.endswith('current'):assert 'Compute subgroups: '+str(a.subgroup_size)+' ' in output
 cache=re.findall(r'Static lighting: (\d+) baked, (\d+) reused;',output)
 assert len(cache)==a.frames,cache
 if not a.live:assert int(cache[1][0])==0 and int(cache[1][1])>0,cache
 report['runs'][name]=dict(seconds=time.monotonic()-before,frames_ms=frames,cache_counts=cache)
report['batch_seconds']=time.monotonic()-started
images={};state=None
for name,_ in commands:
 current=json.loads((out/(name+'-state.json')).read_text())
 if a.live:assert current['tick']==(1 if a.case=='overlap' else 600)+a.frames,current['tick']
 if state is None:state=current
 else:assert current==state,'Rendering changed simulation state'
 if a.no_capture:continue
 data=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',data);assert header
 pixels=data[header.end():];assert len(pixels)==1920*1080*3
 variant=name.split('-',1)[1]
 if variant in images:assert images[variant]==pixels,'Repeated frozen output changed'
 else:
  images[variant]=pixels
  archive_capture(out/(name+'.ppm'),'render-perf-'+a.case+'-'+variant+'-'+str(a.baseline_samples if variant=='baseline' and a.baseline_samples is not None else a.samples)+'-film')
report['image_difference']=None
if not a.no_capture:
 error=[abs(x-y) for x,y in zip(images['baseline'],images['current'])]
 rmse=math.sqrt(sum(d*d for d in error)/len(error))
 report['image_difference']=dict(mean_rgb8=sum(error)/len(error),rmse_rgb8=rmse,psnr_db=20*math.log10(255/rmse) if rmse else None,max_rgb8=max(error),changed_channels=sum(d!=0 for d in error))
report['summary']={}
for key in report['runs']['0-baseline']['frames_ms']:
 if key not in report['runs']['0-current']['frames_ms']:continue
 values={variant:[report['runs'][str(pair)+'-'+variant]['frames_ms'][key][-1] for pair in range(a.pairs)] for variant in ('baseline','current')}
 report['summary'][key]=dict(values_ms=values,median_ms={v:statistics.median(values[v]) for v in values},paired_savings_ms=[x-y for x,y in zip(values['baseline'],values['current'])])
if a.frames>2:
 report['steady']={}
 for variant in ('baseline','current'):
  samples={key:[v for pair in range(a.pairs) for v in report['runs'][str(pair)+'-'+variant]['frames_ms'][key][20:]] for key in report['runs']['0-'+variant]['frames_ms'] if key in ('surface','volume','gpu','app','cadence')}
  report['steady'][variant]={key:dict(count=len(v),median_ms=statistics.median(v),p95_ms=sorted(v)[int(.95*(len(v)-1))],max_ms=max(v)) for key,v in samples.items()}
 print('Steady:',report['steady'])
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Image difference:',report['image_difference'])
for key in ('surface','volume','fog_focus_display','gpu','app'):print(key,report['summary'][key])
print('Batch seconds:',report['batch_seconds'])
if not a.no_capture and not a.quality_comparison:
 assert report['image_difference']['mean_rgb8']<.02 and report['image_difference']['max_rgb8']<=2,'Image difference exceeds the comparison limit'
