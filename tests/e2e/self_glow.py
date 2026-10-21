#!/usr/bin/env python3
"""Prepare or run native 1080p self-glow comparisons in the real application."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from gpu_environment import check_gpu_environment
from save_progress import archive_capture

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,default=root/'build/self-glow')
parser.add_argument('--run',action='store_true')
a=parser.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
world=stage_world(root/'assets/levels/virus-player-level.json',out)
for building in world['content']['buildings']:
    building['growth_seconds']=.01;building['establishment_seconds']=.01;building['branches']['seconds']=.01
config=json.loads((root/'assets/rendering/renderer.json').read_text())
binary=a.binary.resolve()
prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
commands=[]
for view in ['near','wide']:
    for mode in ['off','surface','on']:
        name=view+'-'+mode;folder=out/name;folder.mkdir(exist_ok=True)
        data=stage_world(root/'assets/levels/virus-player-level.json',folder);settings=deepcopy(config)
        for building in data['content']['buildings']:
            building['growth_seconds']=.01;building['establishment_seconds']=.01;building['branches']['seconds']=.01
        if mode=='off':
            for key in data['appearance']['self_glow']:data['appearance']['self_glow'][key]=0
        if mode!='on':settings['bloom']['strength']=0
        if view=='wide':settings['camera']['position']=[0,9,30]
        write_world(folder/'world.json',data)
        (folder/'render.json').write_text(json.dumps(settings,indent=2)+'\n')
        command=prefix+['--headless','--frames','2','--no-stats','--ticks','600','--world-seed','1234',
            '--time','10','--step-seconds','0','--world',str(folder/'world.json'),'--config',str(folder/'render.json'),
            '--capture',str(folder/'display.ppm'),'--state',str(folder/'state.json'),'--scene-state',str(folder/'scene.json')]
        if view=='near':command+=['--camera-zoom','1000']
        commands.append((name,command))
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:
    print('Prepared six sequential native 1920x1080 captures, two frames/process, host validation, no hardware tracing. No GPU work started.')
    raise SystemExit(0)
check_gpu_environment();env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='1'
start=time.monotonic();report={};state=None
for name,command in commands:
    folder=out/name;remaining=gpu_process_timeout_seconds()
    proc=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
    try:stdout,stderr=proc.communicate(timeout=remaining)
    except subprocess.TimeoutExpired:
        if sys.platform=='win32':proc.terminate()
        else:os.killpg(proc.pid,signal.SIGTERM)
        try:stdout,stderr=proc.communicate(timeout=.2)
        except subprocess.TimeoutExpired:
            if sys.platform=='win32':proc.kill()
            else:os.killpg(proc.pid,signal.SIGKILL)
            stdout,stderr=proc.communicate()
        (folder/'run.log').write_text(stdout+stderr)
        raise RuntimeError(name+' exceeded the process limit') from None
    (folder/'run.log').write_text(stdout+stderr)
    assert proc.returncode==0,(name,stdout,stderr)
    assert 'Vulkan validation: enabled.' in stdout,(name,stdout)
    assert 'VUID-' not in stdout+stderr and 'Validation Error' not in stdout+stderr,(name,stderr)
    current=json.loads((folder/'state.json').read_text())
    if state is None:state=current
    else:assert state==current,'Glow changed game state'
    structures=current['colonies'][0]['structures']
    assert 3<len(structures)<=world['content']['buildings'][0]['branches']['count'] and all(s['growth']>=.999 for s in structures)
    scene=json.loads((folder/'scene.json').read_text())
    if not name.endswith('off'):
        emission=world['appearance']['self_glow']
        assert scene['meshes'][0]['emission']==[0,0]
        assert all(scene['meshes'][i['mesh']]['emission']==[emission['organelles'],emission['organelles']] for i in scene['instances'] if int(i['style'][7])&4)
        assert all(m['wall_emission']==0 for m in scene['meshes'])
        assert any(m['emission']==[0,emission['factory_stems']] for m in scene['meshes'])
        styles=[i['style'][4] for i in scene['instances'] if i['mask']==1]
        for key in ['factory_leaves','units']:assert emission[key] in styles,(key,styles)
    stats={key:list(map(float,re.findall(pattern,stdout))) for key,pattern in [
        ('gpu_ms',r'total render: ([0-9.]+)'),('app_ms',r'Application frame: ([0-9.]+)'),
        ('post_ms',r'GPU fog/bloom/display: ([0-9.]+)')]}
    assert all(len(v)==2 for v in stats.values()),stats
    if not name.endswith('surface'):stats['screenshot']=str(archive_capture(folder/'display.ppm','self-glow-'+name))
    report[name]=stats
report['batch_seconds']=time.monotonic()-start
for view in ['near','wide']:
    source=[]
    for mode in ['off','on']:
        data=(out/(view+'-'+mode)/'display.ppm').read_bytes()
        header=re.match(rb'P6\s+1920\s+1080\s+255\s',data);assert header
        source.append(data[header.end():])
    assert source[0]!=source[1],'Glow did not change the display'
    viewer=root/'build'/('self-glow-'+view+'.html')
    subprocess.run([sys.executable,str(root/'scripts/compare_images.py'),report[view+'-off']['screenshot'],report[view+'-on']['screenshot'],'--output',str(viewer),'--no-open'],check=True)
report['game_states_equal']=True
report['limits']='Frozen native-resolution captures, not a sustained performance benchmark.'
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
