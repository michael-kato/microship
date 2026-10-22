#!/usr/bin/env python3
"""Check that a Win32 camera pan reuses static lighting while mobile units move."""
import argparse,json,os,re,subprocess,sys,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--thickness-cache',action='store_true');p.add_argument('--run',action='store_true');p.add_argument('--output',type=Path,default=root/'build/static-lighting-camera');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
PROCESS_TIMEOUT_SECONDS=10
world=stage_world(root/'assets/levels/virus-player-level.json',out);world['match']['auto_colonize']=False
# Keep the cell mesh fixed in this camera-only fixture; mobile actors still move.
for faction in world['content']['factions']:faction.get('cell_appearance',{}).pop('displacement',None)
wp=out/'world.json';write_world(wp,world)
cfg=json.loads((root/'assets/rendering/renderer.json').read_text())
if a.thickness_cache:cfg['lighting']['organelle_cache']=True
cp=out/'render.json';cp.write_text(json.dumps(cfg,indent=2)+'\n')
prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
command=[*prefix,'--world',str(wp),'--config',str(cp),'--frames','2','--frame-delay-ms','250','--world-seed','1234','--camera-zoom','1000','--no-stats','--no-activate','--ticks','1','--capture',str(out/'camera.ppm'),'--state',str(out/'state.json')]
(out/'commands.json').write_text(json.dumps([command],indent=2)+'\n')
if not a.run:print('Prepared one native 1920x1080 two-frame Win32 camera window, default renderer sampling; host validation, 10s process cap, no tracing. No GPU work started.');raise SystemExit(0)
env=os.environ.copy();env['NANOVORE_VULKAN_VALIDATION']='1'
# This input check does not test audio; avoid blocking on the host sound device.
if sys.platform!='win32':env['WINEDLLOVERRIDES']=env.get('WINEDLLOVERRIDES','')+';winepulse.drv,winealsa.drv=d'
started=time.monotonic()
helper=a.binary.resolve().parent/'nanovore_input_probe.exe'
assert helper.is_file(),'Build nanovore_input_probe first'
process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
stdout=[];stderr=[];ready=threading.Event()
def read(pipe,lines):
 for line in pipe:
  lines.append(line)
  if line.startswith('Scene time:'):ready.set()
readers=[threading.Thread(target=read,args=(process.stdout,stdout)),threading.Thread(target=read,args=(process.stderr,stderr))]
for thread in readers:thread.start()
try:
 assert ready.wait(max(.01,PROCESS_TIMEOUT_SECONDS-(time.monotonic()-started))),'No first frame'
 helper_prefix=[str(helper)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(helper)]
 helper_env=env.copy();helper_env['NANOVORE_VULKAN_VALIDATION']='0'
 helper_result=subprocess.run([*helper_prefix,'--pan'],env=helper_env,capture_output=True,text=True,timeout=max(.01,PROCESS_TIMEOUT_SECONDS-(time.monotonic()-started)))
 assert helper_result.returncode==0,(helper_result.stdout,helper_result.stderr)
 process.wait(timeout=max(.01,PROCESS_TIMEOUT_SECONDS-(time.monotonic()-started)))
finally:
 if process.poll() is None:
  process.terminate()
  try:process.wait(timeout=.2)
  except subprocess.TimeoutExpired:process.kill();process.wait()
 for thread in readers:thread.join()
 output=''.join(stdout);errors=''.join(stderr);(out/'camera.log').write_text(output+errors)
seconds=time.monotonic()-started
if process.returncode==0:archive_capture(out/'camera.ppm','static-lighting-camera-reuse')
assert process.returncode==0,(output,errors)
assert 'Vulkan validation: enabled.' in output
cache=[tuple(map(int,v)) for v in re.findall(r'Static lighting: (\d+) baked, (\d+) reused; (\d+) bytes; (\d+) surface bakes',output)]
assert len(cache)==2 and cache[0][0]>0 and cache[0][3]>0 and cache[1][0]==0 and cache[1][1]>0 and cache[1][3]==0,cache
pans=re.findall(r'Camera input: drag ([0-9.eE+-]+) ([0-9.eE+-]+);',output)
assert len(pans)==1 and tuple(map(float,pans[0]))==(80,-20),pans
positions=re.findall(r'Scene time: [0-9.eE+-]+; camera: (.*)',output);assert len(positions)==2 and positions[0]!=positions[1],positions
state=json.loads((out/'state.json').read_text());assert state['tick']>0
report=dict(seconds=seconds,cache=cache,pan_pixels=[80,-20],camera=positions,tick=state['tick'])
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
