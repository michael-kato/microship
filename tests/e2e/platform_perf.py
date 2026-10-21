#!/usr/bin/env python3
"""Compare real Vulkan game runs through Proton 10, Proton 11 and native Linux."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
import argparse,json,os,re,signal,statistics,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from gpu_environment import check_gpu_environment
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--windows',type=Path,default=root/'build/windows-cross/nanova.exe')
p.add_argument('--native',type=Path,default=root/'build/linux-diagnostic/nanova')
p.add_argument('--platforms',nargs='+',choices=('proton10','proton11','native'),default=('proton10','proton11','native'))
p.add_argument('--case',choices=('near','normal','grown','inside','far','overlap','boundary','opaque'),default='near')
p.add_argument('--pairs',type=int,choices=(1,3),default=1)
p.add_argument('--samples',type=int,choices=(1,3,6))
p.add_argument('--config',type=Path,help='Use a named render config; default is the fixed six-film quality reference')
p.add_argument('--timing',action='store_true');p.add_argument('--no-capture',action='store_true')
p.add_argument('--run',action='store_true');p.add_argument('--output',type=Path,default=root/'build/platform-perf');a=p.parse_args()
if a.no_capture and not a.timing:p.error('--no-capture requires --timing')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'tests/e2e/static_lighting.py'),str(a.windows),'--case',a.case,'--variants','cached','--output',str(out)],check=True,stdout=subprocess.PIPE,text=True)
base=json.loads((out/'commands.json').read_text())[0][1]
cp=Path(base[base.index('--config')+1]);config=json.loads(a.config.read_text() if a.config else cp.read_text())
if a.samples is None:a.samples=config['sampling']['samples_per_pixel'] if a.config else 6
config['sampling']['samples_per_pixel']=a.samples;cp.write_text(json.dumps(config,indent=2)+'\n')
arguments=base[base.index('--world'):];commands=[];steam=Path.home()/'.local/share/Steam/steamapps/common'
versions={};prefixes={'proton10':root/'build/proton-prefix','proton11':root/'build/proton11-prefix'}
for platform in a.platforms:
 if platform!='native':
  runtime=steam/('Proton 10.0' if platform=='proton10' else 'Proton 11.0')
  assert (runtime/'proton').is_file() and (prefixes[platform]/'pfx/system.reg').is_file(),'Initialize the separate runtime prefix first'
  versions[platform]=(runtime/'version').read_text().strip()
 else:versions[platform]='Native Linux; same C++ game and Vulkan renderer; no window or live audio'
for pair in range(a.pairs):
 for platform in a.platforms if pair%2==0 else reversed(a.platforms):
  name=str(pair)+'-'+platform
  command=([str(a.native.resolve())] if platform=='native' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.windows.resolve())])+arguments
  for flag,value in (('--capture',out/(name+'.ppm')),('--state',out/(name+'-state.json'))):command[command.index(flag)+1]=str(value)
  command+=['--scene-state',str(out/(name+'-scene.json'))]
  if a.no_capture:
   index=command.index('--capture');del command[index:index+2]
  commands.append((name,platform,command))
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:
 print('Prepared',len(commands),'sequential native 1080p two-frame runs;',a.samples,'film + 1 volume + 1 backdrop starts.  no hardware tracing. No GPU work started.');raise SystemExit(0)
check_gpu_environment();started=time.monotonic();report=dict(versions=versions,render_settings=config,capture=not a.no_capture,validation=not a.timing,runs={})
for name,platform,command in commands:
 env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0' if a.timing else '1'
 if platform!='native':
  env['NANOVA_PROTON']=str(steam/('Proton 10.0' if platform=='proton10' else 'Proton 11.0')/'proton')
  env['STEAM_COMPAT_DATA_PATH']=str(prefixes[platform])
 elif not a.timing:
  env['VK_LAYER_PATH']=str(root/'build/toolchains/validation/usr/share/vulkan/explicit_layer.d')
  env['VK_INSTANCE_LAYERS']='VK_LAYER_KHRONOS_validation'
 remaining=gpu_process_timeout_seconds()
 before=time.monotonic();process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
 try:output,errors=process.communicate(timeout=remaining)
 except subprocess.TimeoutExpired:
  os.killpg(process.pid,signal.SIGTERM)
  try:output,errors=process.communicate(timeout=.2)
  except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);output,errors=process.communicate()
  (out/(name+'.log')).write_text(output+errors);raise RuntimeError('GPU process timeout: '+name) from None
 (out/(name+'.log')).write_text(output+errors)
 assert process.returncode==0,(name,output,errors)
 if not a.timing:assert 'Vulkan validation: enabled.' in output
 assert 'AMD Radeon RX 7800 XT' in output
 labels={'surface':'GPU surface','volume':'GPU volumes','gpu':'total render','app':'Application frame'}
 frames={key:list(map(float,re.findall(re.escape(label)+r': ([0-9.]+) ms',output))) for key,label in labels.items()}
 assert all(len(v)==2 for v in frames.values()),frames
 report['runs'][name]=dict(seconds=time.monotonic()-before,frames_ms=frames)
report['batch_seconds']=time.monotonic()-started
images={};states={};scenes={}
for name,platform,_ in commands:
 state=json.loads((out/(name+'-state.json')).read_text());scene=json.loads((out/(name+'-scene.json')).read_text())
 if platform in states:assert states[platform]==state and scenes[platform]==scene,'Repeated frozen state changed'
 else:states[platform]=state;scenes[platform]=scene
 if a.no_capture:continue
 data=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',data);assert header;pixels=data[header.end():]
 assert len(pixels)==1920*1080*3
 if platform in images:assert images[platform]==pixels,'Repeated image changed'
 else:images[platform]=pixels;archive_capture(out/(name+'.ppm'),'platform-'+platform+'-'+a.case)
reference=a.platforms[0];report['state_equal']={platform:state==states[reference] for platform,state in states.items()}
report['scene_equal']={platform:scene==scenes[reference] for platform,scene in scenes.items()}
report['image_differences']=None if a.no_capture else {}
for platform,pixels in images.items():
 error=[abs(x-y) for x,y in zip(images[reference],pixels)]
 report['image_differences'][platform]=dict(mean_rgb8=sum(error)/len(error),max_rgb8=max(error),changed_channels=sum(e!=0 for e in error))
report['summary']={}
for platform in a.platforms:
 report['summary'][platform]={key:dict(values_ms=[report['runs'][str(pair)+'-'+platform]['frames_ms'][key][-1] for pair in range(a.pairs)],median_ms=statistics.median(report['runs'][str(pair)+'-'+platform]['frames_ms'][key][-1] for pair in range(a.pairs))) for key in ('surface','volume','gpu','app')}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({key:report[key] for key in ('versions','summary','state_equal','scene_equal','image_differences','batch_seconds')},indent=2))
if not a.no_capture:
 assert all(d['mean_rgb8']<.02 and d['max_rgb8']<=2 for d in report['image_differences'].values()),'Platform image differences require review'
