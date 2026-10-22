#!/usr/bin/env python3
"""Check mature-head production, faction selection and continued play in the Windows app."""
from copy import deepcopy
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world,established_world,write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-match-') as directory:
    out=Path(directory)
    def run(name,data=None,ticks=1,orders='',extra=()):
        global count
        args=[*command,'--smoke','--ticks',str(ticks),'--state',str(out/(name+'.json')),'--scene-state',str(out/(name+'-scene.json')),*extra]
        if data:
            path=out/(name+'-world.json');write_world(path,data);args+=['--world',str(path)]
        if orders:
            path=out/(name+'.orders');path.write_text(orders);args+=['--replay',str(path)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==0,(name,p.stdout,p.stderr)
        return json.loads((out/(name+'.json')).read_text()),json.loads((out/(name+'-scene.json')).read_text())
    for choice,faction in ((1,1),(2,2),(3,3)):
        s,scene=run('menu-'+str(choice),extra=('--menu-select',str(choice)))
        assert s['player_faction']==faction and not any(c['active'] for c in s['colonies'])
        assert scene['medium_shadows'][-1] is False and all(scene['medium_shadows'][:-1])
        assert scene['fog']['tint'][0]>scene['fog']['tint'][2]>scene['fog']['tint'][1]
        assert any(i['mask']==4 and i['style'][7]==1 for i in scene['instances'])
    # Check menu data through the executable-relative startup path, without a GPU.
    copied=out/'menu-app';(copied/'assets').mkdir(parents=True);shutil.copy2(binary,copied/'nanova.exe')
    manifest=json.loads((binary.parent/'assets/startup.json').read_text())
    for key in ('world','render','shader','audio'):manifest[key]=os.path.relpath(binary.parent/'assets'/manifest[key],copied/'assets')
    manifest['menu']='menu.json';(copied/'assets/startup.json').write_text(json.dumps(manifest))
    menu=json.loads((root/'assets/presentation/start-menu.json').read_text())
    for choice in menu['choices']:choice['world']=os.path.relpath((root/'assets/presentation'/choice['world']).resolve(),copied/'assets')
    invalid=[]
    empty=deepcopy(menu);empty['choices']=[];invalid.append(empty)
    extra=deepcopy(menu);extra['unknown']=True;invalid.append(extra)
    color=deepcopy(menu);color['choices'][0]['color']=[2,0,0];invalid.append(color)
    missing=deepcopy(menu);missing['choices'][0]['world']='missing.json';invalid.append(missing)
    for bad in invalid:
        (copied/'assets/menu.json').write_text(json.dumps(bad))
        invocation=[str(copied/'nanova.exe')] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(copied/'nanova.exe')]
        p=subprocess.run([*invocation,'--smoke','--menu-select','1'],capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==1 and p.stderr and 'Application lifecycle smoke passed' not in p.stdout
    for side,home in (('virus',0),('immune',1)):
        data=stage_world(root/'assets/levels'/(side+'-player-level.json'),out)
        data['rules']['ai']['expansion_delay']=300;data['rules']['economy']['enemy_attack_seconds']=300
        fast=deepcopy(data);fast['rules']['economy']['manufacture_seconds']=.05
        for building in fast['content']['buildings']:building.update(establishment_seconds=4,growth_seconds=.1,sprout_seconds=.1,collection=100)
        for building in fast['content']['buildings']:building['branches']['root_offset_seconds']=0
        for unit in fast['content']['units']:unit.update(cost=.01,manufacture_seconds=.05)
        full,_=run(side+'-all-leaves',fast,1050,f'0 select all\n0 order {home}\n')
        colony=full['colonies'][home]
        for k,branch in enumerate(colony['structures']):
            if branch['health']>0 and branch['visible_growth']==1:assert colony['leaf_births'][k]>0,(side,k,colony['leaf_births'])
        assert full==run(side+'-repeat',fast,1050,f'0 select all\n0 order {home}\n')[0]
    # Destroy a real enemy factory, then issue orders after the win.
    won=established_world(root/'assets/levels/virus-player-level.json',out)
    won['bodies']=won['bodies'][:2];won['rules'].pop('exploration',None)
    for i,body in enumerate(won['bodies']):body['position']=[-8 if i==0 else 8,0,0]
    won['spawns']=[dict(type='virus_bomber',position=[-8,3,6]),dict(type='virus_fighter',position=[-8,2,6]),dict(type='immune_fighter',position=[1000,0,0])]
    for b in won['content']['buildings']:
        b.update(growth_seconds=.1,collection=100 if b['faction']=='virus' else .001,hits=1)
        b['branches'].update(count=1,roots='single',regrow_seconds=300,seconds=300)
    for u in won['content']['units']:
        if u['name']=='virus_bomber':u.update(range=30,shot_interval=.1)
    won['rules']['ai']['expansion_delay']=300;won['rules']['economy'].update(manufacture_seconds=1,enemy_attack_seconds=300)
    attack='0 select bombers\n0 order 1\n'
    first,_=run('won',won,300,attack)
    continued,_=run('continued',won,600,attack+'300 select all\n300 move -4 -4 8\n')
    assert first['outcome']==continued['outcome']==1 and continued['tick']==600
    assert continued['births']>first['births'] and continued['order']['count']>0
    assert continued['units'][0]['position']!=first['units'][0]['position']
    assert continued['sound_events']['win']==1,'Victory sound repeated'
    reset,_=run('win-restart',won,300,extra=('--restart',))
    assert reset['tick']==0 and reset['outcome']==0 and all(v==0 for c in reset['colonies'] for v in c['leaf_births'])
print(f'Match flow E2E passed: {count} application runs; menu choices, mature factory heads, continued movement and production, one win sound, restart and backdrop/fog input.')
