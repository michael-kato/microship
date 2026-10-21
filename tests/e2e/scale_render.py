#!/usr/bin/env python3
"""Render the primary three-faction stress scene and check frozen-frame equality."""
import argparse,json,os,re,signal,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from gpu_environment import check_gpu_environment, gpu_process_timeout_seconds
from save_progress import archive_capture
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();check_gpu_environment()
binary=args.binary.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
config=binary.parent/'assets/rendering/renderer.json';values=json.loads(config.read_text())
assert (values['output']['width'],values['output']['height'])==(1920,1080)
world=binary.parent/'assets/levels/three_faction_stress.json';baseline=None
for frames in (1,2):
 name='three_faction_stress_'+str(frames);image=out/(name+'.ppm');state=out/(name+'.json')
 p=subprocess.Popen([*command,'--headless','--frames',str(frames),'--no-stats','--world',str(world),'--config',str(config),'--ticks','300','--world-seed','1234','--capture',str(image),'--state',str(state)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=sys.platform!='win32')
 try:stdout,stderr=p.communicate(timeout=gpu_process_timeout_seconds())
 except subprocess.TimeoutExpired:
  if sys.platform=='win32':p.kill()
  else:os.killpg(p.pid,signal.SIGKILL)
  p.communicate();raise RuntimeError('GPU process timeout. No later case was launched.')
 (out/(name+'.log')).write_text(stdout+stderr);assert p.returncode==0,stdout+stderr
 if os.environ.get('NANOVA_VULKAN_VALIDATION')=='1':assert 'Vulkan validation: enabled.' in stdout
 data=image.read_bytes();assert re.match(rb'P6\s+1920\s+1080\s+255\s',data)
 archive_capture(image,name)
 if baseline is None:baseline=data
 else:assert data==baseline,'A repeated frozen frame changed'
 game=json.loads(state.read_text());assert len(game['colonies'])==15 and {u['faction'] for u in game['units']}=={1,2,3}
 assert all(c['active'] for c in game['colonies']) and game['prepared_production_ticks']==3600 and game['effects']['shots']>0
 assert len(set(data[-1920*1080*3:]))>16
print('Stress render E2E passed: 15 developed cells, three armies, frozen-frame equality, native 1080p and default sampling.')
