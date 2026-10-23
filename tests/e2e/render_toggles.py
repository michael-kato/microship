#!/usr/bin/env python3
"""Check rendering hotkeys in a bounded real game window."""
import argparse,json,os,re,signal,subprocess,sys,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from gpu_environment import check_gpu_environment
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path)
p.add_argument('--samples',type=int,choices=(1,3,6),default=1)
p.add_argument('--cold-start',action='store_true')
p.add_argument('--case',choices=('baseline','off','restore','repeat','buffers','buffers-return','glow-off','glow-restore'),default='off')
p.add_argument('--output',type=Path,default=root/'build/render-toggles')
p.add_argument('--reference',type=Path,help='Fresh baseline output folder for exact restoration checks')
p.add_argument('--run',action='store_true');a=p.parse_args()
process_limit=10 if a.cold_start else 2
binary=a.binary.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
command+=['--no-menu','--frames','2','--frame-delay-ms','250','--samples',str(a.samples),'--stats','--no-activate',
 '--ticks','600','--time','10','--step-seconds','0','--world-seed','1234','--camera-zoom','1000',
 '--capture',str(out/'display.ppm'),'--state',str(out/'state.json')]
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
if not a.run:
 print('Prepared one native 1920x1080 two-frame game window,',a.samples,'film + 1 volume grid + 1 backdrop starts; host validation,',process_limit,'s process; no tracing. No GPU work started.')
 raise SystemExit(0)
check_gpu_environment()
helper=binary.parent/'nanovore_input_probe.exe';assert helper.is_file(),'Build nanovore_input_probe first'
helper_prefix=[str(helper)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(helper)]
env=os.environ.copy();env['NANOVORE_VULKAN_VALIDATION']='1'
if sys.platform!='win32':env['WINEDLLOVERRIDES']=env.get('WINEDLLOVERRIDES','')+';winepulse.drv,winealsa.drv=d'
helper_env=env.copy();helper_env['NANOVORE_VULKAN_VALIDATION']='0'
started=time.monotonic();process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
stdout=[];stderr=[];ready=threading.Event()
def read(pipe,lines):
 for line in pipe:
  lines.append(line)
  if line.startswith('Scene time:'):ready.set()
readers=[threading.Thread(target=read,args=(process.stdout,stdout)),threading.Thread(target=read,args=(process.stderr,stderr))]
for reader in readers:reader.start()
helper_process=None
try:
 before=['pause'];after=[]
 if a.case=='glow-restore':before+=['10']
 if a.case=='restore':before+=['5','6','7','8','9']
 if a.case in ('buffers','buffers-return'):before+=['buffers']
 if a.case in ('glow-off','glow-restore'):after=['10']
 elif a.case in ('off','restore'):after=['5','6','7','8','9']
 elif a.case=='repeat':after=['repeat']
 elif a.case=='buffers':after=['5']
 elif a.case=='buffers-return':after=['buffers']
 helper_process=subprocess.Popen([*helper_prefix,'--render-keys',*before,'--next',*after],
  env=helper_env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
 assert ready.wait(max(.01,process_limit-.3-(time.monotonic()-started))),'No first frame within input budget'
 helper_output,helper_errors=helper_process.communicate(input='\n',timeout=max(.01,process_limit-(time.monotonic()-started)))
 assert helper_process.returncode==0,(helper_output,helper_errors)
 process.wait(timeout=max(.01,process_limit-(time.monotonic()-started)))
finally:
 if helper_process is not None and helper_process.poll() is None:
  if sys.platform=='win32':helper_process.terminate()
  else:os.killpg(helper_process.pid,signal.SIGTERM)
  try:helper_process.wait(timeout=.2)
  except subprocess.TimeoutExpired:
   if sys.platform=='win32':helper_process.kill()
   else:os.killpg(helper_process.pid,signal.SIGKILL)
   helper_process.wait()
 if process.poll() is None:
  if sys.platform=='win32':process.terminate()
  else:os.killpg(process.pid,signal.SIGTERM)
  try:process.wait(timeout=.2)
  except subprocess.TimeoutExpired:
   if sys.platform=='win32':process.kill()
   else:os.killpg(process.pid,signal.SIGKILL)
   process.wait()
 for reader in readers:reader.join()
 output=''.join(stdout);errors=''.join(stderr);(out/'window.log').write_text(output+errors)
seconds=time.monotonic()-started
assert process.returncode==0,(output,errors)
assert seconds<=process_limit,seconds
assert 'Vulkan validation: enabled.' in output
settings=re.findall(r'Render settings: disabled (\d+); SSS (ON|OFF|N/A); volume rays (\d+); cell blur (\d+); tilt radius ([0-9.]+); fog radius ([0-9]+\.[0-9]+)\.',output)
assert len(settings)==2,settings
expected={'baseline':[0,0],'off':[0,31],'restore':[31,0],'repeat':[0,0],'buffers':[0,1],'buffers-return':[0,0],'glow-off':[0,32],'glow-restore':[32,0]}[a.case]
assert [int(s[0]) for s in settings]==expected,(settings,expected)
for s in settings:
 mask=int(s[0]);assert s[1]==('OFF' if mask&1 else 'ON'),s
 for bit,value in zip((2,4,8,16),s[2:]):assert (float(value)==0)==bool(mask&bit),s
profiles=re.findall(r'Profile (.*?): start ([0-9.]+) ms; last ([0-9.]+) ms; mean ([0-9.]+) ms; samples (\d+)\.',output)
assert len(profiles)==62,len(profiles)
frames=[profiles[:31],profiles[31:]]
assert [int(f[0][-1]) for f in frames]==([1,1] if a.case in ('off','restore','buffers','glow-off','glow-restore') else [1,2]),frames
for f in frames:
 for _,_,last,mean,samples in f:
  if samples=='1':assert abs(float(last)-float(mean))<.00001,(last,mean)
if a.case=='off':
 for name,_,last,_,_ in frames[1]:
  if name in ('VOLUME','CELL BLUR','FOG','TILT SHIFT'):assert float(last)<.05,(name,last)
glow_settings=re.findall(r'Surface self-glow: (ON|OFF); halo strength ([0-9.]+)\.',output)
assert len(glow_settings)==2,glow_settings
for mask,settings_glow in zip(expected,glow_settings):
 assert (settings_glow[0]=='OFF')==bool(mask&32),(mask,settings_glow)
 assert (float(settings_glow[1])==0)==bool(mask&32),(mask,settings_glow)
state=json.loads((out/'state.json').read_text())
assert state['tick']==600,state['tick']
cache=re.findall(r'Static lighting: (\d+) baked, (\d+) reused;',output)
assert len(cache)==2 and int(cache[1][0])==0 and int(cache[1][1])>0,cache
reference_checked=None
if a.reference:
 reference=a.reference.resolve()
 assert json.loads((reference/'state.json').read_text())==state,'Render keys changed simulation state'
 def pixels(path):
  data=path.read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',data)
  assert header,'Expected native RGB8 output'
  return data[header.end():]
 # The profiler contains measured durations and is expected to differ.
 assert pixels(reference/'display.ppm')[300*1920*3:]==pixels(out/'display.ppm')[300*1920*3:],'Restored image differs below the profiler'
 reference_checked=str(reference)
report=dict(case=a.case,seconds=seconds,settings=settings,profile_frames=frames,static_cache=cache,fresh_reference=reference_checked,
 input='Win32 key press/repeat/release; focus-loss repeats in repeat case',simulation_tick=state['tick'])
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(case=a.case,seconds=seconds,masks=expected)))
