#!/usr/bin/env python3
"""Check Win32 F1 messages and OS middle-button pan in one native window."""
import argparse,json,math,os,re,subprocess,sys,threading,time
from pathlib import Path
from window_input import Desktop
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/profiler-window');p.add_argument('--run',action='store_true');a=p.parse_args()
if not a.run:print('Prepared one two-frame native 1920x1080 F1/middle-pan check, 3 film samples, 1 volume ray, 1 backdrop query, host validation, 2s limit. No GPU work started.');raise SystemExit(0)
helper=a.binary.resolve().parent/'nanovore_input_probe.exe'
if not helper.is_file():raise SystemExit('Build nanovore_input_probe before this check.')
os.environ['NANOVORE_VULKAN_VALIDATION']='1';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);desktop=Desktop();existing=desktop.windows();state=out/'state.json';capture=out/'profiler.ppm'
prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
started=time.monotonic();process=subprocess.Popen([*prefix,'--no-menu','--frames','2','--frame-delay-ms','250','--samples','3','--world-seed','1234','--camera-zoom','1000','--no-stats','--capture',str(capture),'--state',str(state)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
stdout=[];stderr=[];ready=threading.Event()
def read(pipe,lines):
 for line in pipe:
  lines.append(line)
  if line.startswith('Scene time:'):ready.set()
readers=[threading.Thread(target=read,args=(process.stdout,stdout)),threading.Thread(target=read,args=(process.stderr,stderr))]
for reader in readers:reader.start()
try:
 handle=None
 while time.monotonic()-started<1 and process.poll() is None:
  found=desktop.windows()-existing
  if found:handle=next(iter(found));break
  time.sleep(.002)
 assert handle,'No game window'
 x,y=desktop.focus_point(handle);x-=80;y-=80
 desktop.move(x+900,y+500);desktop.button(True,1);desktop.button(False,1)
 assert ready.wait(max(.01,1.5-(time.monotonic()-started))),'No first frame'
 helper_command=[str(helper)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(helper)]
 helper_environment=os.environ.copy();helper_environment['NANOVORE_VULKAN_VALIDATION']='0'
 key_process=subprocess.Popen(helper_command,env=helper_environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 desktop.move(x+900,y+500);time.sleep(.003);desktop.button(True,2);time.sleep(.003)
 desktop.move(x+980,y+480);time.sleep(.003);desktop.button(False,2)
 key_output,key_errors=key_process.communicate(timeout=max(.01,2-(time.monotonic()-started)))
 assert key_process.returncode==0,(key_output,key_errors)
 process.wait(timeout=max(.01,2-(time.monotonic()-started)))
finally:
 desktop.button(False,2)
 if process.poll() is None:
  process.terminate()
  try:process.wait(timeout=.2)
  except subprocess.TimeoutExpired:process.kill();process.wait()
 for reader in readers:reader.join()
 output=''.join(stdout);errors=''.join(stderr);(out/'window.log').write_text(output+errors)
assert process.returncode==0,(output,errors)
assert 'Vulkan validation: enabled.' in output
assert re.findall(r'FPS stats ([01])',output) in (['0','1'],['1','1']),'F1 did not enable the profiler'
pans=re.findall(r'Camera input: drag ([0-9.eE+-]+) ([0-9.eE+-]+);',output)
assert len(pans)==1 and tuple(map(float,pans[0]))==(80,-20),(pans,output)
positions=[[float(value.rstrip('.')) for value in row] for row in re.findall(r'Scene time: [0-9.eE+-]+; camera: ([0-9.eE+-]+) ([0-9.eE+-]+) ([0-9.eE+-]+)',output)];assert len(positions)==2
movement=[b-a for a,b in zip(*positions)];pixel=2*8*math.tan(math.radians(22.5))/1080;expected=[-80*pixel,-20*pixel*20/math.sqrt(436),20*pixel*6/math.sqrt(436)]
assert all(abs(a-b)<.0001 for a,b in zip(movement,expected)),(movement,expected)
result=json.loads(state.read_text());assert result['order']['count']==0,'Middle pan issued an order'
assert len(re.findall(r'^Profile .*samples 2\.',output,re.M))==31,'Incomplete profiler traces'
report=dict(seconds=time.monotonic()-started,frames=2,profiler='Win32 F1 press/repeat/release enabled once',pan_pixels=[80,-20],camera_movement=movement,orders=0)
sys.path.insert(0,str(root/'scripts'));from save_progress import archive_capture
archive_capture(capture,'profiler-middle-pan');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
