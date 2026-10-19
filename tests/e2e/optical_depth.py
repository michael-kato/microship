#!/usr/bin/env python3
"""Prepare or run bounded real-application optical-depth comparisons."""
import argparse,json,os,re,statistics,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path)
p.add_argument('--baseline',type=Path)
p.add_argument('--world',type=Path,default=root/'assets/levels/virus-player-level.json')
p.add_argument('--case',choices=('near','normal','grown','inside','far','overlap','boundary','opaque'),default='near')
p.add_argument('--variants',nargs='+',choices=('baseline','off','lookup','refresh','grid','grid-refresh','texture','texture-refresh','organelle','organelle-refresh','wrap'),default=('off','lookup'))
p.add_argument('--shadow-grid',type=int,choices=(0,8,32,64),default=32)
p.add_argument('--pairs',type=int,choices=(1,3),default=1)
p.add_argument('--no-static-cache',action='store_true')
p.add_argument('--timing',action='store_true')
p.add_argument('--run',action='store_true')
p.add_argument('--output',type=Path,default=root/'build/optical-depth')
a=p.parse_args()
if 'baseline' in a.variants and not a.baseline:p.error('Baseline variant requires --baseline')
if a.pairs==3 and (len(a.variants)!=2 or not a.timing):p.error('Three pairs require two variants and --timing')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'tests/e2e/static_lighting.py'),str(a.binary),'--case',a.case,'--world',str(a.world),'--variants','cached','--output',str(out)],check=True)
base=json.loads((out/'commands.json').read_text())[0][1]
settings=json.loads(Path(base[base.index('--config')+1]).read_text())
settings['lighting']['shadow_grid_resolution']=a.shadow_grid
if a.no_static_cache:settings['lighting']['static_lighting_cache']=False
commands=[]
for pair in range(a.pairs):
 for variant in a.variants if pair%2==0 else reversed(a.variants):
  name=f'{pair}-{variant}';cfg=json.loads(json.dumps(settings));cfg['volume'].pop('optical_depth_lut',None);cfg['volume'].pop('lighting_grid',None)
  if variant=='baseline':cfg['lighting'].pop('subgroup_size',None)
  if variant!='baseline':cfg['volume']['optical_depth_lut']=variant!='off'
  if variant in ('grid','grid-refresh'):cfg['volume']['lighting_grid']=True
  cfg['lighting'].pop('texture_grid',None);cfg['lighting'].pop('organelle_cache',None)
  if variant in ('texture','texture-refresh','organelle','organelle-refresh','wrap'):cfg['volume']['lighting_grid']=True;cfg['lighting']['texture_grid']=True
  if variant=='wrap':cfg['subsurface']['transmission']=0
  if variant in ('organelle','organelle-refresh'):cfg['lighting']['organelle_cache']=True
  cfg['lighting']['force_lighting_rebuild']=variant in ('refresh','grid-refresh','texture-refresh','organelle-refresh')
  cp=out/(name+'-render.json');cp.write_text(json.dumps(cfg,indent=2)+'\n')
  cmd=list(base)
  if variant=='baseline':cmd[cmd.index(str(a.binary.resolve()))]=str(a.baseline.resolve())
  cmd[cmd.index('--config')+1]=str(cp)
  cmd[cmd.index('--capture')+1]=str(out/(name+'.ppm'))
  cmd[cmd.index('--state')+1]=str(out/(name+'-state.json'))
  commands.append((name,cmd))
(out/'optical-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:
 print('Prepared',len(commands),'sequential native 1920x1080 app processes; 2 frames, 3 film + 1 volume + 1 backdrop starts, 8 integration steps; 2s/process, 5s/batch; no tracing. No GPU work started.');raise SystemExit(0)
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0' if a.timing else '1'
report={};start=time.monotonic()
for name,cmd in commands:
 remaining=5-(time.monotonic()-start);assert remaining>0,'Five-second GPU batch expired'
 before=time.monotonic()
 try:result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=min(2,remaining))
 except subprocess.TimeoutExpired as error:
  (out/(name+'.log')).write_bytes((error.stdout or b'')+(error.stderr or b''));raise
 (out/(name+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(name,result.stdout,result.stderr)
 status='Vulkan validation: not reported by loader/tooling.' if a.timing else 'Vulkan validation: enabled.'
 assert status in result.stdout
 labels={'surface':'GPU surface','volume':'GPU volumes','gpu':'total render','cpu':'Application frame'}
 frames={key:list(map(float,re.findall(re.escape(label)+r': ([0-9.]+) ms',result.stdout))) for key,label in labels.items()}
 assert all(len(v)==2 for v in frames.values()),frames
 tables=list(map(int,re.findall(r'Optical depth tables: (\d+) rebuilt',result.stdout)))
 variant=name.split('-',1)[1]
 if variant in ('lookup','refresh','grid','grid-refresh','texture','texture-refresh','organelle','organelle-refresh','wrap'):
  assert len(tables)==2 and tables[0]>0,tables
  assert (tables[1]>0)==(variant in ('refresh','grid-refresh','texture-refresh','organelle-refresh')),tables
 report[name]=dict(seconds=time.monotonic()-before,frames_ms=frames,table_rebuilds=tables)
report['batch_seconds']=time.monotonic()-start;report['validation']=not a.timing
images={};reference_state=None
for name,_ in commands:
 state=json.loads((out/(name+'-state.json')).read_text())
 if reference_state is None:reference_state=state
 else:assert reference_state==state,'Rendering changed simulation state'
 raw=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header
 pixels=raw[header.end():];assert len(pixels)==1920*1080*3
 variant=name.split('-',1)[1]
 if variant in images:assert images[variant]==pixels,'Repeated frozen output changed'
 images[variant]=pixels
 archive_capture(out/(name+'.ppm'),'optical-depth-'+a.case+'-'+name+('-timing' if a.timing else ''))
report['differences']={}
reference=a.variants[0]
for variant,pixels in images.items():
 delta=[abs(x-y) for x,y in zip(images[reference],pixels)]
 report['differences'][variant]=dict(mean_rgb8=sum(delta)/len(delta),max_rgb8=max(delta),over_one=sum(x>1 for x in delta))
if 'off' in images and 'lookup' in images:
 error=report['differences']['lookup']
 assert error['mean_rgb8']<.05 and error['max_rgb8']<=2,'Lookup image error exceeds the comparison limit'
if 'baseline' in images and 'off' in images:assert images['baseline']==images['off'],'Disabled lookup changed the reference'
if 'organelle' in images and 'organelle-refresh' in images:assert images['organelle']==images['organelle-refresh'],'Reused organelle cache differs from fresh bake'
if 'texture' in images and 'texture-refresh' in images:assert images['texture']==images['texture-refresh'],'Reused texture differs from fresh bake'
if 'grid' in images and 'grid-refresh' in images:assert images['grid']==images['grid-refresh'],'Reused grid differs from fresh bake'
if 'lookup' in images and 'refresh' in images:assert images['lookup']==images['refresh'],'Reused lookup differs from fresh bake'
if a.pairs==3:
 report['summary']={}
 for stage in ('surface','volume','gpu','cpu'):
  samples={variant:[report[f'{i}-{variant}']['frames_ms'][stage][1] for i in range(3)] for variant in a.variants}
  report['summary'][stage]=dict(samples=samples,medians={k:statistics.median(v) for k,v in samples.items()},paired_savings=[samples[a.variants[0]][i]-samples[a.variants[1]][i] for i in range(3)])
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
