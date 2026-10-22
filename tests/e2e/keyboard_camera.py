#!/usr/bin/env python3
"""Check time-scaled map camera movement in the real Windows application."""
from copy import deepcopy
import json,math,re,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
settings=json.loads((root/'assets/rendering/renderer.json').read_text());count=0
with tempfile.TemporaryDirectory(prefix='nanova-keyboard-') as folder:
 out=Path(folder)
 def run(name,pan=None,config=settings,expected=0,extra=()):
  global count
  cp=out/(name+'-render.json');cp.write_text(json.dumps(config));state=out/(name+'-state.json')
  cmd=[*prefix,'--smoke','--ticks','60','--world-seed','1234','--config',str(cp),'--state',str(state),*extra]
  if pan is not None:cmd+=['--camera-pan',*map(str,pan)]
  result=subprocess.run(cmd,capture_output=True,text=True,timeout=10);count+=1;assert result.returncode==expected,(name,result.stdout,result.stderr)
  if expected:assert not state.exists();return None,None
  line=next(line for line in result.stdout.splitlines() if line.startswith('Initial camera:'));values=list(map(float,re.findall(r'-?\d+(?:\.\d+)?',line)))
  return values,json.loads(state.read_text())
 baseline,state=run('baseline');speed=settings['camera']['keyboard_pan_speed']
 for x,y in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,1),(1,-1),(-1,-1),(0,0)):
  camera,same=run('direction-'+str(count),(x,y,1));magnitude=max(1,math.hypot(x,y));delta=[speed*x/magnitude,speed*y/magnitude,0]
  assert same==state,'Camera movement changed simulation or sight'
  # The camera log has six significant digits; large coordinates round more.
  for i in range(6):assert math.isclose(camera[i],baseline[i]+delta[i%3],rel_tol=1e-5,abs_tol=.0002),(x,y,camera,baseline,delta)
 for seconds in (.25,.5,2):
  camera,same=run('seconds-'+str(count),(1,0,seconds));assert math.isclose(camera[0],baseline[0]+speed*seconds,rel_tol=1e-5,abs_tol=.0002) and same==state
 # Zooming out past the authored view scales speed by distance; zooming in keeps the authored speed.
 authored=math.dist(settings['camera']['position'],settings['camera']['target'])
 for wheel in (-8,-3,3):
  zoomed,_=run('zoom-'+str(count),extra=('--camera-zoom',str(wheel)));camera,same=run('zoom-pan-'+str(count),(1,0,1),extra=('--camera-zoom',str(wheel)))
  expected=speed*max(1,math.dist(zoomed[:3],zoomed[3:])/authored)
  assert math.isclose(camera[3]-zoomed[3],expected,rel_tol=1e-4,abs_tol=.0005) and same==state,(wheel,camera,zoomed,expected)
 disabled=deepcopy(settings);disabled['camera']['keyboard_pan_speed']=0
 assert run('disabled',(1,1,1),disabled)==(baseline,state)
 rotated=deepcopy(settings);rotated['camera']['up']=[1,0,0]
 initial,_=run('rotated-initial',config=rotated);moved,_=run('rotated-right',(1,0,1),rotated)
 assert math.isclose(moved[0],initial[0],rel_tol=1e-5,abs_tol=.0002) and math.isclose(moved[1],initial[1]-speed,rel_tol=1e-5,abs_tol=.0002) and moved[2]==initial[2]
 reset,_=run('restart',(1,1,1),extra=('--restart',));assert reset==baseline
 for pan in ((2,0,1),(0,-2,1),(0,0,-1),(0,0,61),('nan',0,1)):
  run('invalid-pan-'+str(count),pan,expected=2)
 for value in ('-1','251','nan','1'):
  run('invalid-delay-'+value,extra=('--frame-delay-ms',value),expected=2)
 for value in (-1,1001):
  bad=deepcopy(settings);bad['camera']['keyboard_pan_speed']=value;run('invalid-speed-'+str(count),config=bad,expected=1)
print(f'Keyboard camera E2E passed: {count} application runs; cardinal and diagonal movement, elapsed time, zoom-scaled speed, map plane, rotated view, restart, unchanged sight and orders, optional settings and rejected input.')
