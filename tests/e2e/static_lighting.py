#!/usr/bin/env python3
"""Prepare or run bounded application checks for static lighting bakes."""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path)
p.add_argument('--baseline',type=Path)
p.add_argument('--world',type=Path,default=root/'assets/levels/biology-virus.json')
p.add_argument('--case',choices=('near','normal','grown','inside','far','overlap','boundary','opaque'),default='near')
p.add_argument('--variants',nargs='+',choices=('baseline','cached','refresh','off'),default=('cached','refresh'))
p.add_argument('--reverse',action='store_true')
p.add_argument('--timing',action='store_true')
p.add_argument('--run',action='store_true')
p.add_argument('--output',type=Path,default=root/'build/static-lighting')
a=p.parse_args()
if 'baseline' in a.variants and not a.baseline:p.error('Baseline variant requires --baseline')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
world=stage_world(a.world.resolve(),out)
settings=json.loads((root/'assets/rendering/biology-quality.json').read_text())
settings['sampling']['samples_per_pixel']=3
assert settings['output']['width']==1920 and settings['output']['height']==1080
assert all(settings['volume'][k]==v for k,v in dict(rays_per_pixel=1,integration_steps=8).items())
if a.case=='inside':
 settings['camera']['min_distance']=1
 settings['camera']['position']=[x+y for x,y in zip(settings['camera']['target'],(0,.6,2))]
if a.case=='grown':
 for building in world['content']['buildings']:
  building['growth_seconds']=.01;building['establishment_seconds']=.01;building['branches']['seconds']=.01
if a.case=='overlap':world['bodies'][2]['position']=[x+y for x,y in zip(world['bodies'][0]['position'],(2,0,0))]
if a.case=='boundary':world['appearance']['atmosphere'].update(center=world['bodies'][0]['position'],radius=26,edge_fade=.4)
if a.case=='opaque':world['appearance']['thin_surface']['transmission']=0
wp=out/'world.json';write_world(wp,world)
prepared=[]
for variant in reversed(a.variants) if a.reverse else a.variants:
 cfg=deepcopy(settings)
 if variant=='baseline':
  cfg['lighting'].pop('subgroup_size',None)
  cfg['volume'].pop('optical_depth_lut',None);cfg['volume'].pop('lighting_grid',None)
  cfg['lighting'].pop('texture_grid',None);cfg['lighting'].pop('organelle_cache',None)
  cfg['lighting'].pop('static_lighting_cache',None);cfg['lighting'].pop('force_lighting_rebuild',None)
 else:
  cfg['lighting']['static_lighting_cache']=variant!='off';cfg['lighting']['force_lighting_rebuild']=variant=='refresh'
 cp=out/(variant+'-render.json');cp.write_text(json.dumps(cfg,indent=2)+'\n')
 binary=(a.baseline if variant=='baseline' else a.binary).resolve()
 command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
 command+=['--world',str(wp),'--config',str(cp),'--headless','--frames','2','--no-stats','--ticks','1' if a.case=='overlap' else '600','--world-seed','1234','--time','10','--step-seconds','0','--capture',str(out/(variant+'.ppm')),'--state',str(out/(variant+'-state.json'))]
 if a.case in ('near','grown','opaque'):command+=['--camera-zoom','1000']
 if a.case=='far':command+=['--camera-zoom','-1000']
 prepared.append((variant,command))
(out/'commands.json').write_text(json.dumps(prepared,indent=2)+'\n')
if not a.run:
 print('Prepared',len(prepared),'sequential native 1920x1080 app processes: 2 frames, 3 film + 1 volume + 1 backdrop starts; 2s/process, 5s/batch; no tracing. No GPU work started.')
 raise SystemExit(0)
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0' if a.timing else '1'
report={};start=time.monotonic()
for variant,command in prepared:
 remaining=5-(time.monotonic()-start);assert remaining>0,'Five-second GPU batch expired'
 before=time.monotonic()
 result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=min(2,remaining))
 (out/(variant+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(variant,result.stdout,result.stderr)
 expected='Vulkan validation: not reported by loader/tooling.' if a.timing else 'Vulkan validation: enabled.'
 assert expected in result.stdout
 labels={'surface':'GPU surface','volume':'GPU volumes','grid':'GPU shadow grid','bake':'GPU static lighting bake','dynamic':'GPU dynamic lighting','gpu':'total render','cpu':'Application frame'}
 frames={key:list(map(float,re.findall(re.escape(label)+r': ([0-9.]+) ms',result.stdout))) for key,label in labels.items()}
 assert all(len(v)==2 for key,v in frames.items() if variant!='baseline' or key not in ('bake','dynamic')),frames
 cache=[tuple(map(int,v)) for v in re.findall(r'Static lighting: (\d+) baked, (\d+) reused; (\d+) bytes',result.stdout)]
 if variant in ('cached','refresh'):
  assert len(cache)==2 and cache[0][0]>0,(variant,cache)
  if variant=='cached':assert cache[1][0]==0 and cache[1][1]>0,(variant,cache)
  else:assert cache[1][0]>0 and cache[1][1]==0,(variant,cache)
 report[variant]={'seconds':time.monotonic()-before,'frames_ms':frames,'cache':cache}
report['batch_seconds']=time.monotonic()-start;report['validation']=not a.timing
# Images and state are checked only after all GPU processes have stopped.
reference=None;images={}
for variant,_ in prepared:
 state=json.loads((out/(variant+'-state.json')).read_text())
 if reference is None:reference=state
 else:assert state==reference,'Rendering changed simulation state'
 raw=(out/(variant+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header
 images[variant]=raw[header.end():];assert len(images[variant])==1920*1080*3
 report[variant]['capture']=str(archive_capture(out/(variant+'.ppm'),'static-lighting-'+a.case+'-'+variant))
if 'cached' in images:
 for variant in images:
  if variant=='cached':continue
  total=changed=over_one=maximum=0
  for x,y in zip(images[variant],images['cached']):
   d=abs(x-y);total+=d;changed+=d!=0;over_one+=d>1;maximum=max(maximum,d)
  report[variant+'-difference']={'mean_rgb8':total/len(images['cached']),'max_rgb8':maximum,'changed':changed,'over_one':over_one}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
if 'refresh' in images and 'cached' in images:assert images['refresh']==images['cached'],'A reused bake differs from a fresh bake'
