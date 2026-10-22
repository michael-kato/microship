#!/usr/bin/env python3
"""Check attached bombs, launch, exhaustion and regrowth in the Windows application."""
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import established_world,write_world,producers,branches,change
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanovore-payloads-') as directory:
    out=Path(directory)
    data=branches(producers(established_world(root/'tests/e2e/scenes/virus-two-cell-test-level.json',out),collection=.0001),count=1)
    for u in data['content']['units']:
        u.update(speed=.01,colonizes=False)
        if u['name']=='virus_bomber':
            u.update(targets=['units'],shot_interval=.2);u.pop('secondary_attack',None)
        if u['name']=='immune_fighter':u.update(range=.01,hits=32,health=384);u['attack'].pop('blob',None)  # A passive target.
    data['spawns']=[dict(type='virus_bomber',position=[-.6,0,5.2]),dict(type='immune_fighter',position=[.6,0,5.2])]
    bomber_index=next(i for i,u in enumerate(data['content']['units']) if u['name']=='virus_bomber')
    def run(name,d,ticks,expected=0,extra=()):
        global count
        world=out/(name+'-world.json');write_world(world,d);state=out/(name+'.json');scene=out/(name+'-scene.json')
        p=subprocess.run([*command,'--smoke','--world',str(world),'--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene),*extra],capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        if expected:return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    initial,view=run('attached',data,1)
    bomber=lambda s:next(u for u in s['units'] if u['type']=='virus_bomber')
    assert bomber(initial)['ready_payloads']==6 and not initial['projectile_state']
    # Each attached dart is the same small, closed mesh used by the flying bomb.
    pods=[i for i in view['instances'] if view['meshes'][i['mesh']]['triangles']==20]
    assert len(pods)==6,'The body does not show six separate bombs'
    assert len({tuple(round(v,4) for v in i['transform'][3::4]) for i in pods})==6
    fired,fired_view=run('fired',data,6)
    assert bomber(fired)['ready_payloads']==5 and fired['effects']['shots']==1 and fired['effects']['hits']==0
    projectile=fired['projectile_state'][0];assert projectile['payload'] and projectile['type']=='virus_bomber' and projectile['scale']==.65
    assert math.dist(projectile['previous'],bomber(fired)['position'])<.6,'The bomb did not leave its craft'
    assert len([i for i in fired_view['instances'] if fired_view['meshes'][i['mesh']]['triangles']==20])==7,'Missing flying or regrowing bomb'
    empty,_=run('empty',data,100)
    assert bomber(empty)['ready_payloads']==0 and empty['effects']['shots']==empty['effects']['hits']==6
    assert next(u for u in empty['units'] if u['type']=='immune_fighter')['wounds']==6,'Bomb impacts did not apply exactly six hits'
    waiting,_=run('waiting',data,150)
    assert waiting['effects']['shots']==waiting['effects']['hits']==6,'Empty craft applied invisible damage'
    reloaded,_=run('regrown',data,800)
    assert reloaded['effects']['shots']==reloaded['effects']['hits']==12,'The six bombs did not regrow'
    assert empty==run('repeat',data,100)[0],'Bomb combat is not repeatable'
    reset,_=run('restart',data,100,extra=('--restart',))
    assert reset['tick']==0 and bomber(reset)['ready_payloads']==6 and reset['effects']['shots']==0
    path=f'content.units.{bomber_index}.payload'
    for key,value in [('sockets',[]),('sockets',[[0,0,0]]*7),('sockets',[[11,0,0]]),('reload_seconds',0),('unknown',True),('model','missing.fmesh')]:
        run('invalid-'+str(count),change(data,path+'.'+key,value),1,expected=1)
    run('wrong-attack',change(data,f'content.units.{bomber_index}.attack',{'mode':'digest'}),1,expected=1)
print(f'Payload E2E passed: {count} application runs; six visible bombs, launch, damage, exhaustion, regrowth, restart and invalid assets.')
