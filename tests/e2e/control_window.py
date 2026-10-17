#!/usr/bin/env python3
"""Use real OS input in one bounded native game window."""
import json,re,subprocess,sys,threading,time
from pathlib import Path
from window_input import Desktop
root=Path(__file__).resolve().parents[2]
def run_window(binary,out,limit=2):
 out.mkdir(parents=True,exist_ok=True);desktop=Desktop();existing=desktop.windows();state=out/'controls-state.json';capture=out/'controls.ppm'
 prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
 cmd=[*prefix,'--no-menu','--frames','2','--samples','3','--world-seed','1234','--no-stats','--capture',str(capture),'--state',str(state)]
 started=time.monotonic();process=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1);ready=threading.Event();stdout=[];stderr=[]
 def read(pipe,lines):
  for line in pipe:
   lines.append(line)
   if line.startswith('Scene time:'):ready.set()
 readers=[threading.Thread(target=read,args=(process.stdout,stdout)),threading.Thread(target=read,args=(process.stderr,stderr))]
 for reader in readers:reader.start()
 try:
  handle=None
  while time.monotonic()-started<min(1,limit) and process.poll() is None:
   found=desktop.windows()-existing
   if found:handle=next(iter(found));break
   time.sleep(.002)
  assert handle,'The Nanova window did not appear'
  x,y=desktop.focus_point(handle);x-=80;y-=80
  desktop.move(x+600,y+250);desktop.button(True,1);desktop.move(x+1320,y+850);desktop.button(False,1)
  assert ready.wait(max(.01,limit-(time.monotonic()-started))),'First frame did not start'
  # The first frame has consumed selection. These events feed the second frame.
  desktop.move(x+900,y+500);desktop.button(True,3);desktop.move(x+980,y+480);desktop.button(False,3)
  desktop.move(x+1400,y+500);desktop.button(True,2);desktop.button(False,2)
  process.wait(timeout=max(.01,limit-(time.monotonic()-started)))
 finally:
  if process.poll() is None:
   process.terminate()
   try:process.wait(timeout=.2)
   except subprocess.TimeoutExpired:process.kill();process.wait()
  for reader in readers:reader.join()
  output=''.join(stdout);errors=''.join(stderr);(out/'controls.log').write_text(output+errors)
 assert process.returncode==0,(output,errors)
 assert 'Vulkan validation: enabled.' in output
 assert len(re.findall(r'Application frame:',output))==2,output
 pans=re.findall(r'Camera input: drag ([0-9.eE+-]+) ([0-9.eE+-]+);',output);total=[sum(float(v[i]) for v in pans) for i in range(2)];assert total==[80,-20],(total,output)
 result=json.loads(state.read_text());selected=[u for u in result['units'] if u['selected']]
 assert len(selected)==7 and result['order']['count']==7 and result['order']['cell']==-1 and all(u['moving'] for u in selected),result
 return dict(selected=7,pan_pixels=total,middle_click_orders=7,frames=2,seconds=time.monotonic()-started,application_ms=list(map(float,re.findall(r'Application frame: ([0-9.]+)',output))))
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/control-window');p.add_argument('--run',action='store_true');a=p.parse_args()
 if not a.run:print('Prepared one two-frame 1920x1080 OS input check. Use --run to start it.');sys.exit(0)
 print(json.dumps(run_window(a.binary.resolve(),a.output.resolve()),indent=2))
