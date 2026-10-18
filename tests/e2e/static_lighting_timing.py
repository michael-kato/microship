#!/usr/bin/env python3
"""Prepare or run three alternating-order app timing pairs within one GPU budget."""
import argparse,json,os,re,statistics,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--case',choices=('near','grown','far'),default='near');p.add_argument('--run',action='store_true');p.add_argument('--output',type=Path,default=root/'build/static-lighting-timing');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'tests/e2e/static_lighting.py'),str(a.binary),'--baseline',str(a.baseline),'--variants','baseline','cached','--case',a.case,'--timing','--output',str(out)],check=True)
prepared=json.loads((out/'commands.json').read_text());commands=[]
for pair in range(3):
 for variant,base in prepared if pair%2==0 else reversed(prepared):
  name=f'{pair}-{variant}';cmd=list(base)
  cmd[cmd.index('--capture')+1]=str(out/(name+'.ppm'));cmd[cmd.index('--state')+1]=str(out/(name+'-state.json'))
  commands.append((name,cmd))
(out/'timing-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:print('Prepared six sequential native 1920x1080 processes, two frames each; 3 film + 1 volume + 1 backdrop starts; validation off; 2s/process, 5s/batch; no tracing. No GPU work started.');raise SystemExit(0)
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0';report={};start=time.monotonic()
for name,cmd in commands:
 remaining=5-(time.monotonic()-start);assert remaining>0,'Five-second GPU batch expired'
 result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=min(2,remaining));(out/(name+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(name,result.stdout,result.stderr)
 assert 'Vulkan validation: not reported by loader/tooling.' in result.stdout
 labels={'surface':'GPU surface','volume':'GPU volumes','grid':'GPU shadow grid','gpu':'total render','cpu':'Application frame'}
 timings={key:list(map(float,re.findall(re.escape(label)+r': ([0-9.]+) ms',result.stdout))) for key,label in labels.items()}
 assert all(len(v)==2 for v in timings.values()),(name,timings)
 report[name]=timings
report['batch_seconds']=time.monotonic()-start
# Artifact checks occur after all GPU processes stop.
images={};reference_state=None
for name,_ in commands:
 state=json.loads((out/(name+'-state.json')).read_text())
 if reference_state is None:reference_state=state
 else:assert state==reference_state,'Rendering changed simulation state'
 raw=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header
 variant=name.split('-',1)[1];pixels=raw[header.end():];assert len(pixels)==1920*1080*3
 if variant in images:assert pixels==images[variant],'A repeated frozen image changed'
 else:images[variant]=pixels
 archive_capture(out/(name+'.ppm'),'static-lighting-'+a.case+'-'+name+'-timing')
summary={}
for stage in ('surface','volume','gpu','cpu'):
 before=[report[f'{i}-baseline'][stage][1] for i in range(3)];after=[report[f'{i}-cached'][stage][1] for i in range(3)]
 summary[stage]=dict(baseline=before,cached=after,baseline_median=statistics.median(before),cached_median=statistics.median(after),paired_savings=[x-y for x,y in zip(before,after)])
report['summary']=summary
report['image_difference']=dict(mean_rgb8=sum(abs(x-y) for x,y in zip(images['baseline'],images['cached']))/len(images['cached']),max_rgb8=max(abs(x-y) for x,y in zip(images['baseline'],images['cached'])))
(out/'timing-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(seconds=report['batch_seconds'],summary=summary,image_difference=report['image_difference']),indent=2))
