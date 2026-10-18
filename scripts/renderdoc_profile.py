#!/usr/bin/env python3
"""Prepare or run a bounded RenderDoc capture of the real Windows application."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from authored_assets import stage_world,write_world
from gpu_environment import check_gpu_environment
from save_progress import archive_capture

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path)
p.add_argument('--renderdoc',type=Path,default=Path('/opt/renderdoc_1.44'))
p.add_argument('--world',type=Path,default=root/'assets/levels/biology-virus.json')
p.add_argument('--samples',type=int,choices=(3,6),default=6)
p.add_argument('--sss',choices=('on','off'),default='on')
p.add_argument('--run',action='store_true')
p.add_argument('--output',type=Path,default=root/'build/renderdoc-profile')
a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
renderdoc=a.renderdoc.resolve();binary=a.binary.resolve()
world=stage_world(a.world.resolve(),out);wp=out/'world.json';write_world(wp,world)
config=json.loads((root/'assets/rendering/biology.json').read_text());config['sampling']['samples_per_pixel']=a.samples
assert config['output']['width']==1920 and config['output']['height']==1080
assert config['volume']['rays_per_pixel']==1 and config['volume']['integration_steps']==8
if a.sss=='off':config['subsurface']['strength']=0
cp=out/'render.json';cp.write_text(json.dumps(config,indent=2)+'\n')
command=[sys.executable,str(root/'scripts/proton_run.py'),str(binary),'--world',str(wp),'--config',str(cp),
    '--headless','--frames','2','--no-stats','--ticks','600','--world-seed','1234','--time','10','--step-seconds','0',
    '--camera-zoom','1000','--capture',str(out/'display.ppm'),'--state',str(out/'state.json')]
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
if not a.run:
    print('Prepared RenderDoc: native 1920x1080,',a.samples,'film + 1 volume + 1 backdrop starts; two game frames; capture frame two. One bounded replay process collects standard counters and compiler data. 2s/process, 5s/batch; no hardware tracing. No GPU work started.')
    raise SystemExit(0)
check_gpu_environment()
subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-I',str(renderdoc/'include'),
    str(root/'scripts/renderdoc_capture.c'),'-o',str(out/'capture.so'),'-ldl'],check=True)
manifest=json.loads((renderdoc/'etc/vulkan/implicit_layer.d/renderdoc_capture.json').read_text())
layer=manifest['layer'];layer['library_path']=str(out/'capture.so');layer['name']='VK_LAYER_NANOVA_RenderDocCapture'
for key in ('pre_instance_functions','enable_environment','disable_environment'):layer.pop(key,None)
layers=out/'layers';layers.mkdir(exist_ok=True);(layers/'renderdoc.json').write_text(json.dumps(manifest,indent=2)+'\n')
env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0'
env.update(NANOVA_RENDERDOC_LIBRARY=str(renderdoc/'lib/librenderdoc.so'),NANOVA_RENDERDOC_CAPTURE=str(out/'nanova'),
    VK_LAYER_PATH=str(layers),VK_INSTANCE_LAYERS=layer['name'],DISABLE_VULKAN_RENDERDOC_CAPTURE_1_44='1')
started=time.monotonic()
def run(name,args,environment):
    remaining=5-(time.monotonic()-started)
    if remaining<=0:raise RuntimeError('Five-second RenderDoc batch expired')
    before=time.monotonic()
    process=subprocess.Popen(args,env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
    try:stdout,stderr=process.communicate(timeout=min(2,remaining))
    except subprocess.TimeoutExpired:
        os.killpg(process.pid,signal.SIGTERM)
        try:stdout,stderr=process.communicate(timeout=.2)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL);stdout,stderr=process.communicate()
        (out/(name+'.log')).write_bytes(stdout+stderr)
        raise RuntimeError(name+' reached its process limit') from None
    (out/(name+'.log')).write_bytes(stdout+stderr)
    if process.returncode:raise RuntimeError(name+' failed; see '+str(out/(name+'.log')))
    return time.monotonic()-before
capture_seconds=run('capture',command,env)
captures=list(out.glob('nanova*.rdc'));assert captures,'RenderDoc wrote no capture'
capture=max(captures,key=lambda path:path.stat().st_mtime)
assert 'RenderDoc ended second headless frame: 1.' in (out/'capture.log').read_text()
replay_env=os.environ.copy();replay_env['VK_LAYER_PATH']=str(root/'build/toolchains/validation/usr/share/vulkan/explicit_layer.d')
replay_env.update(NANOVA_RENDERDOC_OUTPUT=str(out),NANOVA_RENDERDOC_CAPTURE_FILE=str(capture),
    NANOVA_RENDERDOC_SHADER_FOLDER=str(binary.parent/'assets/shaders'))
replay_seconds=run('replay',[str(renderdoc/'bin/qrenderdoc'),'--python',str(root/'scripts/renderdoc_analyze.py')],replay_env)
report=json.loads((out/'analysis.json').read_text());assert 'error' not in report,report.get('error')
report.update(capture_seconds=capture_seconds,replay_seconds=replay_seconds,batch_seconds=time.monotonic()-started,
    samples=a.samples,sss=a.sss,width=1920,height=1080,capture_bytes=capture.stat().st_size,
    limits='Two game frames; one captured frame; one bounded replay process; 2s/process, 5s/batch; no hardware tracing.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
archive_capture(out/'display.ppm','renderdoc-'+a.sss+'-'+str(a.samples)+'-film')
print('Capture:',capture)
print('Captured event duration sum:',report['event_duration_sum_ms'],'ms; excludes gaps and is not application frame time.')
for shader in report['shaders']:
    event=next(item for item in report['events'] if item['event']==shader['event'])
    print(shader['shader'],event['gpu_ms'],'ms',shader.get('compiler_statistics',{}))
print('Batch:',report['batch_seconds'],'seconds')
