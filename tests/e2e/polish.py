#!/usr/bin/env python3
"""Exercise camera controls and polished flight via the complete Windows application."""
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from asset_data import established_world,change,remove,write_world

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-polish-') as directory:
    out=Path(directory)
    config=out/'camera.json'
    config.write_text(json.dumps(change(json.loads((root/'assets/rendering/renderer.json').read_text()),'camera.position',[0,11,36])))
    world=root/'tests/e2e/scenes/virus-two-cell-test-level.json'
    authored=established_world(world,out);definitions={u['name']:u for u in authored['content']['units']}
    acceleration=authored['rules']['flight']['acceleration']
    def speed_limit(u):
        definition=definitions[u['type']]
        return definition['speed']*definition.get('attack',{}).get('charge_speed',1)
    def run(name,ticks=1,orders='',extra=(),scene=world,expected=0):
        global count
        state=out/(name+'.json');replay=out/(name+'.orders');replay.write_text(orders)
        args=[*command,'--smoke','--config',str(config),'--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
        if orders:args+=['--replay',str(replay)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        return (json.loads(state.read_text()) if not expected else None),p.stdout
    def camera(log):
        # Decimal points are allowed; use the complete line rather than the first period.
        line=next(line for line in log.splitlines() if line.startswith('Initial camera: '))
        eye,target=line[len('Initial camera: '):-1].split('; target: ')
        return tuple(map(float,eye.split())),tuple(map(float,target.split()))
    def sub(a,b):return tuple(x-y for x,y in zip(a,b))
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    def unit(a):return tuple(x/math.sqrt(dot(a,a)) for x in a)
    def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
    data,log=run('camera-baseline');eye,target=camera(log)
    forward=unit(sub(target,eye));right=unit(cross(forward,(0,1,0)));up=cross(right,forward)
    focal=1080/(2*math.tan(math.radians(45/2)))
    for x,y in [(960,540),(1350,350),(520,750)]:
        _,log=run('cursor-zoom-'+str(x),extra=('--camera-pointer',str(x),str(y),'--camera-zoom','2'))
        next_eye,_=camera(log)
        distance=math.sqrt(dot(sub(eye,target),sub(eye,target)))
        anchor=tuple(target[i]+distance*((x-960)*right[i]+(540-y)*up[i])/focal for i in range(3))
        ray=sub(anchor,next_eye);depth=dot(ray,forward)
        sx=960+dot(ray,right)*focal/depth;sy=540-dot(ray,up)*focal/depth
        assert abs(sx-x)<.15 and abs(sy-y)<.15,('Cursor anchor moved',sx,sy,x,y)
    _,log=run('clamped-zoom',extra=('--camera-zoom','1000'))
    a,b=camera(log);assert abs(math.sqrt(dot(sub(a,b),sub(a,b)))-8)<.001
    plant='0 select bombers\n0 order 0\n'
    snapshots=[]
    for tick in (900,901,902):
        data,_=run('flight-'+str(tick),tick,plant);snapshots.append(data)
        assert data['order']=={'cell':0,'tick':0,'count':1}
        assert any(abs(u['bank'])>.01 for u in data['units'] if u['kind']==0),'No banking'
        for u in data['units']:
            speed=speed_limit(u)
            assert math.sqrt(dot(u['velocity'],u['velocity']))<=speed+.0001
    for before,after in zip(snapshots,snapshots[1:]):
        previous={u['generation']:u for u in before['units']}
        for u in after['units']:
            if u['generation'] in previous:
                # Attached units follow their target. These are flight bounds.
                if u['latch']>=0 or previous[u['generation']]['latch']>=0 or u['held_by']>=0 or previous[u['generation']]['held_by']>=0:continue
                distance=sub(u['position'],previous[u['generation']]['position'])
                speed=speed_limit(u)
                assert math.sqrt(dot(distance,distance))<=speed/60+.002,'Visible flight jumped'
                delta=sub(u['velocity'],previous[u['generation']]['velocity'])
                boost=definitions[u['type']].get('attack',{}).get('charge_acceleration',1) if u['charging'] or previous[u['generation']]['charging'] else 1
                assert math.sqrt(dot(delta,delta))<=acceleration*boost/60+.00001,'Acceleration exceeded authored bound'
    selected,_=run('selection-order',2,'0 select fighters\n0 order 1\n')
    assert selected['order']['count']==6 and sum(u['selected'] for u in selected['units'])==6
    assert all(u['target']==1 for u in selected['units'] if u['selected'])
    for name,x,expected in [('fighters',80,6),('bombers',280,1),('all',480,7)]:
        clicked,_=run('hud-'+name,extra=('--click',str(x),'980'))
        assert sum(u['selected'] for u in clicked['units'])==expected,'HUD fleet button failed'
    directed,_=run('pointer-order',extra=('--select-group','1','--command','1250','540'))
    assert directed['order']['cell']==1 and directed['order']['count']==6,'Pointer order failed'
    focused,log=run('cell-focus',900,plant,extra=('--focus',))
    _,focus=camera(log);assert focus==(-6,0,0),'Home focus failed'
    priority,_=run('hud-production',900,plant,extra=('--click','1778','1030'))
    assert priority['colonies'][0]['fighter_percent']==25,'Production slider failed'
    # Project an actual application-exported unit for the same screen-space pick path.
    baseline,_=run('pick-baseline');fighter=next(u for u in baseline['units'] if u['faction']==1 and u['kind']==0)
    ray=sub(fighter['position'],eye);depth=dot(ray,forward)
    x=round(960+dot(ray,right)*focal/depth);y=round(540-dot(ray,up)*focal/depth)
    for name,extra,expected in [('replace',(),1),('additive',('--additive',),5)]:
        picked,_=run('unit-'+name,extra=('--select-group','1','--click',str(x),str(y),*extra))
        assert sum(u['selected'] for u in picked['units'])==expected,'Unit pick/additive selection failed'
    # Malformed authored flight data must fail at the actual application boundary.
    text=established_world(world,out)
    for name,field,value in [('zero-acceleration','acceleration',0),('bad-separation','separation',-1),('bad-bank','bank',2)]:
        scene=out/(name+'-world.json');write_world(scene,change(text,'rules.flight.'+field,value))
        run(name,scene=scene,expected=1)
print(f'Polish E2E passed: {count} real-application runs; cursor anchoring, zoom bounds, bounded flight, selection/orders and invalid assets.')
