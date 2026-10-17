#!/usr/bin/env python3
"""Check rectangle selection and orders through the complete Windows application."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-selection-') as directory:
    out=Path(directory);world=root/'tests/e2e/scenes/biology-two-virus.json'
    config=json.loads((root/'assets/rendering/biology.json').read_text());config['camera']['position']=[0,11,36]
    path=out/'camera.json';path.write_text(json.dumps(config))
    def run(name,extra=()):
        global count
        state=out/(name+'.json')
        p=subprocess.run([*command,'--smoke','--world',str(world),'--config',str(path),'--ticks','1','--state',str(state),*extra],capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==0,(name,p.stdout,p.stderr)
        return json.loads(state.read_text()),next(line for line in p.stdout.splitlines() if line.startswith('Initial camera:'))
    baseline,camera=run('baseline');eye=[0,11,36]
    def norm(v):
        d=math.sqrt(sum(x*x for x in v));return [x/d for x in v]
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    forward=norm([-x for x in eye]);right=[1,0,0];up=[0,-forward[2],forward[1]]
    focal=1080/(2*math.tan(math.radians(22.5)))
    projected={}
    for u in baseline['units']:
        v=[a-b for a,b in zip(u['position'],eye)];depth=dot(v,forward)
        projected[u['generation']]=(960+dot(v,right)*focal/depth,540-dot(v,up)*focal/depth)
    own=[u for u in baseline['units'] if u['faction']==1]
    fighter=next(u for u in own if u['kind']==0);x,y=projected[fighter['generation']]
    rectangles=[(math.floor(x-3),math.floor(y-3),math.ceil(x+3),math.ceil(y+3)),
                (min(int(projected[u['generation']][0]) for u in own)-6,min(int(projected[u['generation']][1]) for u in own)-6,
                 max(int(projected[u['generation']][0]) for u in own)+6,max(int(projected[u['generation']][1]) for u in own)+6),
                (20,20,60,60)]
    for index,rect in enumerate(rectangles):
        left,top,right_edge,bottom=rect
        expected={u['generation'] for u in own if left<=projected[u['generation']][0]<=right_edge and top<=projected[u['generation']][1]<=bottom}
        for reverse in (False,True):
            coords=(right_edge,bottom,left,top) if reverse else rect
            state,log=run(f'box-{index}-{reverse}',('--select-group','3','--pointer-drag',*map(str,coords)))
            selected={u['generation'] for u in state['units'] if u['selected']}
            assert selected==expected,(rect,selected,expected)
            assert log==camera,'Rectangle selection moved the camera'
        state,_=run('additive-'+str(index),('--select-group','2','--additive','--pointer-drag',*map(str,rect)))
        bombers={u['generation'] for u in own if u['kind']==1}
        assert {u['generation'] for u in state['units'] if u['selected']}==expected|bombers
        state,_=run('orders-'+str(index),('--pointer-drag',*map(str,rect),'--command','1250','540'))
        assert state['order']['count']==len(expected)
        assert all(u['target']==1 for u in state['units'] if u['selected'])
    state,_=run('whole-view',('--pointer-drag','0','0','1919','915'))
    assert sum(u['selected'] for u in state['units'])==len(own) and all(not u['selected'] for u in state['units'] if u['faction']!=1)
print(f'Selection E2E passed: {count} application runs; forward and reverse rectangles, additive selection, empty rectangles, orders, faction limits and camera position.')
