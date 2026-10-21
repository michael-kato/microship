#!/usr/bin/env python3
"""Measure glow on/off in the real window using a prepared self_glow.py scene."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
import argparse
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import time
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from gpu_environment import check_gpu_environment
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prepared',type=Path,default=root/'build/self-glow-refined')
p.add_argument('--output',type=Path,default=root/'build/glow-steady')
p.add_argument('--run',action='store_true');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
base=dict(json.loads((a.prepared/'commands.json').read_text()));commands=[]
for pair in range(3):
    for mode in (['off','on'] if pair%2==0 else ['on','off']):
        name=str(pair)+'-'+mode;command=base['near-'+mode].copy()
        command.remove('--headless');command[command.index('--frames')+1]='120'
        for flag in ['--time','--step-seconds','--capture','--scene-state','--state']:
            index=command.index(flag);del command[index:index+2]
        command+=['--no-activate','--frame-ticks','1','--state',str(out/(name+'-state.json'))]
        commands.append((name,command))
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if not a.run:
    print('Prepared six sequential native 1920x1080 windows, 120 frames each, no capture, no validation, no hardware tracing. No GPU work started.')
    raise SystemExit(0)
check_gpu_environment();env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0'
if sys.platform!='win32':env['WINEDLLOVERRIDES']=env.get('WINEDLLOVERRIDES','')+';winepulse.drv,winealsa.drv=d'
start=time.monotonic();runs={};state=None
for name,command in commands:
    remaining=gpu_process_timeout_seconds()
    process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
    try:stdout,stderr=process.communicate(timeout=remaining)
    except subprocess.TimeoutExpired:
        if sys.platform=='win32':process.terminate()
        else:os.killpg(process.pid,signal.SIGTERM)
        try:stdout,stderr=process.communicate(timeout=.2)
        except subprocess.TimeoutExpired:
            if sys.platform=='win32':process.kill()
            else:os.killpg(process.pid,signal.SIGKILL)
            stdout,stderr=process.communicate()
        (out/(name+'.log')).write_text(stdout+stderr)
        raise RuntimeError(name+' exceeded its time limit') from None
    (out/(name+'.log')).write_text(stdout+stderr)
    assert process.returncode==0,(name,stdout,stderr)
    assert 'Vulkan validation: not reported by loader/tooling.' in stdout
    current=json.loads((out/(name+'-state.json')).read_text());assert current['tick']==720
    if state is None:state=current
    else:assert state==current,'Glow changed game state'
    fields={'gpu':r'total render: ([0-9.]+)','app':r'Application frame: ([0-9.]+)',
        'cadence':r'Application cadence: ([0-9.]+)','surface':r'GPU surface: ([0-9.]+)',
        'halo_display':r'GPU halo/display: ([0-9.]+)'}
    times={key:list(map(float,re.findall(pattern,stdout))) for key,pattern in fields.items()}
    assert all(len(v)==120 for v in times.values()),{key:len(v) for key,v in times.items()}
    runs[name]=times
summary={}
for mode in ['off','on']:
    summary[mode]={}
    for key in fields:
        samples=[value for pair in range(3) for value in runs[str(pair)+'-'+mode][key][20:]]
        ordered=sorted(samples)
        summary[mode][key]={'count':len(samples),'median_ms':statistics.median(samples),
            'p95_ms':ordered[int(.95*(len(ordered)-1))],'max_ms':max(samples)}
report={'resolution':[1920,1080],'scene':'complete factory, near camera, fixed seed, live simulation',
    'frames_per_process':120,'warmup_frames':20,'pairs':3,'summary':summary,'runs':runs,
    'batch_seconds':time.monotonic()-start,'game_states_equal':True,
    'limits':'One scene on this host, Proton window. No foreground input or full-match performance claim.'}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'summary':summary,'batch_seconds':report['batch_seconds']},indent=2))
