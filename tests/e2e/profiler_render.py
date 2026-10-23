#!/usr/bin/env python3
"""Measure normal and close views with bounded real-application GPU runs."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
import argparse,json,math,os,re,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/profiler-render');p.add_argument('--run',action='store_true');a=p.parse_args()
out=a.output.resolve();binary=a.binary.resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
cases=[('normal',()),('near',('--camera-zoom','1000')),('buffers',('--debug-views',))]
if not a.run:print('Prepared three sequential native 1920x1080 two-frame profiler captures; default film samples, one volume ray, one backdrop query No GPU work started.');raise SystemExit(0)
os.environ['NANOVORE_VULKAN_VALIDATION']='1';out.mkdir(parents=True,exist_ok=True);report={};started=time.monotonic()
parents={'CPU FRAME':['INPUT','SIMULATION','SCENE + AUDIO','RENDER'],'RENDER':['ACQUIRE','UPLOAD + AS RECORD','PASS RECORD','SUBMIT','SLOT WAIT + QUERY','PRESENT WAIT','CAPTURE'],
 'SLOT WAIT + QUERY':['SLOT FENCE WAIT','TIMESTAMP READ','TIMING REPORT'],
 'GPU FRAME':['SCENE','POST','OUTPUT'],'SCENE':['AS UPDATE','BACKDROP','LIGHTING FIELDS','SURFACE + SSS','VOLUME'],'POST':['EFFECTS','FOG','DEPTH OF FIELD','BLOOM + DISPLAY'],'OUTPUT':['HUD + PROFILER','OUTPUT COPY']}
for name,extra in cases:
 remaining=gpu_process_timeout_seconds()
 cmd=[*command,'--headless','--frames','2','--stats','--world-seed','1234','--ticks','600','--time','10','--step-seconds','0','--capture',str(out/(name+'.ppm')),*extra]
 now=time.monotonic();result=subprocess.run(cmd,capture_output=True,text=True,timeout=remaining);seconds=time.monotonic()-now;(out/(name+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(name,result.stdout,result.stderr)
 assert 'Vulkan validation: enabled.' in result.stdout
 frames=[{} for _ in range(2)]
 for label,start,last,mean,samples in re.findall(r'Profile (.*?): start ([0-9.]+) ms; last ([0-9.]+) ms; mean ([0-9.]+) ms; samples ([0-9]+)\.',result.stdout):
  index=int(samples)-1;assert index in (0,1);frames[index][label]=dict(start=float(start),last=float(last),mean=float(mean))
 for index,frame in enumerate(frames):
  assert len(frame)==32,(name,index,frame)
  for label,node in frame.items():
   assert all(math.isfinite(v) and v>=0 for v in node.values()),node
   expected=sum(f[label]['last'] for f in frames[:index+1])/(index+1)
   assert abs(node['mean']-expected)<.00001,(label,node,expected)
  # Split surface frames time membranes and opaque hits; fused debug views report no children.
  split=[frame[c]['last'] for c in ('MEMBRANES','OPAQUE HITS')]
  if frame['SURFACE + SSS']['last']>0:
   assert (sum(split)==0)==(name=='buffers') and (sum(split)==0 or abs(sum(split)-frame['SURFACE + SSS']['last'])<.0001),(name,split,frame['SURFACE + SSS'])
  for parent,children in parents.items():
   root_node=frame[parent];child_nodes=[frame[c] for c in children]
   assert abs(sum(c['last'] for c in child_nodes)-root_node['last'])<(.2 if parent=='CPU FRAME' else .0001),(name,parent,child_nodes,root_node)
   assert all(c['start']>=root_node['start']-.0001 and c['start']+c['last']<=root_node['start']+root_node['last']+.0001 for c in child_nodes),(name,parent)
  # GPU spans arrive when a frame completes; the capture drains the final frame.
  assert frame['SUBMIT']['last']>0 and (index==0 or frame['GPU FRAME']['last']>0)
 report[name]=dict(seconds=seconds,frames=frames)
report['batch_seconds']=time.monotonic()-started
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
for name,_ in cases:
 stages=report[name]['frames'][-1];print(name, 'CPU',stages['CPU FRAME']['last'],'GPU',stages['GPU FRAME']['last'],'ms; stages',[(n,stages[n]['last']) for n in parents['SCENE']+parents['POST']])
print('Profiler E2E passed: nested spans, full GPU time accounting, finite values and arithmetic means; batch',report['batch_seconds'],'seconds.')
