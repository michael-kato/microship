#!/usr/bin/env python3
"""Prepare or run bounded native-1080p anatomical backdrop application captures."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
from copy import deepcopy
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/background-review');p.add_argument('--run',action='store_true');p.add_argument('--variants',default='flesh,bone,red_organs,blue_organs');p.add_argument('--view',choices=['wide','home','isolated','overview'],default='wide');p.add_argument('--timing',action='store_true');p.add_argument('--frames',type=int,choices=[1,2],default=1)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
base=stage_world(root/'assets/levels/virus-player-level.json',out);base['appearance']['backdrop'].pop('variants')
config=json.loads((root/'assets/rendering/renderer.json').read_text());config['sampling']['samples_per_pixel']=3
if a.view=='wide':config['camera'].update(position=[0,24,95],target=[0,0,0])
if a.view in ('isolated','overview'):
    base.pop('rules',None);base.pop('effects',None);base['match']={'player':'virus','opponent':'immune'};base['bodies']=[];base['spawns']=[dict(type='virus_fighter',position=[500,0,0])]
    base['appearance'].pop('fog',None);base['appearance'].pop('atmosphere',None);config.pop('tilt_shift',None);config['camera'].update(position=[0,15,165 if a.view=='overview' else 100],target=[0,0,0])
commands=[]
for name in a.variants.split(','):
    assert name in ('flesh','bone','red_organs','blue_organs','veins','ligaments','intestines','villi','legacy','matte','shadow','repeat','behind_medium')
    world=deepcopy(base);backdrop=world['appearance']['backdrop'];model='soft_bloodstream_background' if name=='legacy' else 'anatomy_'+('flesh' if name in ('matte','shadow','repeat','behind_medium') else name)
    backdrop['model']=Path(os.path.relpath(root/'assets/models'/(model+'.fmesh'),out)).as_posix()
    if name=='legacy':backdrop['soft']['blur_radius']=6
    if name=='matte':
        import struct
        raw=bytearray((root/'assets/models/anatomy_flesh.fmesh').read_bytes());n=struct.unpack_from('<I',raw,8)[0]
        for v in range(n):struct.pack_into('<f',raw,16+48*v+44,1)
        (out/'matte.fmesh').write_bytes(raw);backdrop['model']='matte.fmesh'
    if name=='shadow':
        assert a.view=='isolated'
        # A real cell between the studio key and the folded bed must not cast onto it.
        world['bodies']=[deepcopy(base_body) for base_body in stage_world(root/'assets/levels/virus-player-level.json',out)['bodies'][:1]]
        world['bodies'][0].update(position=[-24,30,-8],radius=15,owner='neutral');world['appearance'].pop('thin_surface',None)
    if name=='behind_medium':
        assert a.view=='isolated'
        world['appearance']['atmosphere']=dict(center=[0,0,-100],radius=10,edge_fade=.2,scattering=[1,1,1],absorption=0,anisotropy=0,shadows=False)
    wp=out/(name+'-world.json');write_world(wp,world);cp=out/(name+'-render.json');cp.write_text(json.dumps(config,indent=2))
    prefix=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
    commands.append((name,[*prefix,'--world',str(wp),'--config',str(cp),'--ticks','600','--world-seed','1234','--headless','--frames',str(a.frames),'--capture',str(out/(name+'.ppm')),'--state',str(out/(name+'-state.json')),'--no-stats']))
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:print('Prepared native 1920x1080 backdrop checks; no GPU work started.');sys.exit(0)
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0' if a.timing else '1';start=time.monotonic();report={}
for name,command in commands:
    remaining=gpu_process_timeout_seconds()
    result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=remaining);(out/(name+'.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0,(name,result.stdout,result.stderr)
    if not a.timing:assert 'Vulkan validation: enabled.' in result.stdout
    raw=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);assert header and len(raw)-header.end()==1920*1080*3
    report[name]={'archive':str(archive_capture(out/(name+'.ppm'),'anatomy-'+a.view+'-'+name)),'log':result.stdout}
report['batch_seconds']=time.monotonic()-start
# Measurements below use only the real application display captures.
def pixels(name):
    raw=(out/(name+'.ppm')).read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',raw);return raw[header.end():]
def delta(a,b,box):
    x0,y0,x1,y1=box
    return sum(abs(a[(y*1920+x)*3+c]-b[(y*1920+x)*3+c]) for y in range(y0,y1) for x in range(x0,x1) for c in range(3))/((x1-x0)*(y1-y0)*3)
names={name for name,_ in commands};metrics={}
if 'flesh' in names:
    flesh=pixels('flesh')
    if 'matte' in names:
        metrics['wet_mean_rgb8_change']=delta(flesh,pixels('matte'),(0,0,1920,1080))
        assert metrics['wet_mean_rgb8_change']>.5,'Wet reflections were lost'
    if 'repeat' in names:assert flesh==pixels('repeat'),'A preceding scene changed the current frame'
    if 'behind_medium' in names:
        metrics['hidden_medium_rgb8_change']=delta(flesh,pixels('behind_medium'),(0,0,1920,1080))
        assert metrics['hidden_medium_rgb8_change']<.001,'Medium behind the backdrop leaked into the image'
    if 'shadow' in names:
        metrics['gameplay_shadow_rgb8_change']=delta(flesh,pixels('shadow'),(950,540,1150,700))
        assert metrics['gameplay_shadow_rgb8_change']==0,'Gameplay geometry cast onto the backdrop'
if len(names.intersection({'flesh','bone','red_organs','blue_organs','veins','ligaments','intestines','villi'}))>1:
    states=[json.loads((out/(name+'-state.json')).read_text()) for name in names]
    assert all(state==states[0] for state in states),'Background art changed game state'
report['metrics']=metrics
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('Backdrop application checks passed; batch seconds:',report['batch_seconds'])
