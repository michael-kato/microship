#!/usr/bin/env python3
"""Use real OS input in one bounded native game window."""
import json,re,subprocess,sys,threading,time
from pathlib import Path
from window_input import Desktop
root=Path(__file__).resolve().parents[2]
def run_window(binary,out,limit=2,keys=('W','D'),axes=(1,1),issue_order=True,screen_key=None):
 out.mkdir(parents=True,exist_ok=True);desktop=Desktop();existing=desktop.windows();state=out/'controls-state.json';capture=out/'controls.ppm'
 prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
 cmd=[*prefix,'--no-menu','--frames','2','--samples','3','--world-seed','1234','--no-stats','--capture',str(capture),'--state',str(state)]
 started=time.monotonic();process=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1);stdout=[];stderr=[]
 def read(pipe,lines):
  for line in pipe:
   lines.append(line)
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
  if screen_key:
   for key in ('3',screen_key):desktop.key(True,key);desktop.key(False,key)
  # Send held keys during renderer startup so Proton can deliver them before polling.
  for key in keys:desktop.key(True,key)
  # Queue the complete input sequence before the first poll. Two-frame checks
  # otherwise depend on Proton delivery finishing during one short GPU frame.
  if issue_order:
   desktop.move(x+1400,y+500);desktop.button(True,3);desktop.button(False,3)
  # Keep the selection release position as the final pointer sample.
  desktop.move(x+1320,y+850)
  process.wait(timeout=max(.01,limit-(time.monotonic()-started)))
 finally:
  for key in keys:desktop.key(False,key)
  if process.poll() is None:
   process.terminate()
   try:process.wait(timeout=.2)
   except subprocess.TimeoutExpired:process.kill();process.wait()
  for reader in readers:reader.join()
  output=''.join(stdout);errors=''.join(stderr);(out/'controls.log').write_text(output+errors)
 assert process.returncode==0,(output,errors)
 assert 'Vulkan validation: enabled.' in output
 assert len(re.findall(r'Application frame:',output))==2,output
 pans=re.findall(r'Camera input: drag ([0-9.eE+-]+) ([0-9.eE+-]+);',output)
 assert all(float(x)==0 and float(y)==0 for x,y in pans),'Selection or orders moved the camera'
 positions=[list(map(float,row)) for row in re.findall(r'Scene time: [0-9.eE+-]+; camera: ([0-9.eE+-]+) ([0-9.eE+-]+) ([0-9.eE+-]+)',output)];assert len(positions)==2,output
 movement=[positions[1][i]-positions[0][i] for i in range(3)]
 keyboard=re.findall(r'Camera keyboard: pan ([0-9.eE+-]+) ([0-9.eE+-]+); seconds ([0-9.eE+-]+)\.',output)
 if axes!=(0,0):
  assert keyboard and tuple(map(float,keyboard[-1][:2]))==axes,(keyboard,output)
  seconds=float(keyboard[-1][2]);assert seconds>0
  length=max(1,(axes[0]**2+axes[1]**2)**.5);speed=json.loads((root/'assets/rendering/renderer.json').read_text())['camera']['keyboard_pan_speed'];expected=[speed*seconds*axes[0]/length,speed*seconds*axes[1]/length,0]
  assert all(abs(a-b)<.0003 for a,b in zip(movement,expected)),(movement,expected,output)
 else:assert not keyboard and movement==[0,0,0],(movement,keyboard)
 result=json.loads(state.read_text());selected=[u for u in result['units'] if u['selected']]
 expected_count=6 if screen_key=='F' else 1 if screen_key=='B' else 7
 assert len(selected)==expected_count,('Selected count',len(selected),expected_count)
 if screen_key:assert all(u['kind']==(0 if screen_key=='F' else 1) and u['faction']==1 for u in selected)
 if issue_order:assert result['order']['count']==7 and result['order']['cell']==-1 and all(u['moving'] for u in selected),(result['order'],[u['moving'] for u in selected])
 else:assert result['order']['count']==0,'An order was issued without a right click'
 return dict(selected=len(selected),screen_key=screen_key,keyboard_axes=axes,camera_movement=movement,right_click_orders=7 if issue_order else 0,frames=2,seconds=time.monotonic()-started,application_ms=list(map(float,re.findall(r'Application frame: ([0-9.]+)',output))))

if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/control-window');p.add_argument('--run',action='store_true');a=p.parse_args()
 if not a.run:print('Prepared one two-frame 1920x1080 OS input check. Use --run to start it.');sys.exit(0)
 import os;os.environ['NANOVA_VULKAN_VALIDATION']='1'
 print(json.dumps(run_window(a.binary.resolve(),a.output.resolve()),indent=2))
