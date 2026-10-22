#!/usr/bin/env python3
"""Check factory emission controls and instance scope in the real application."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world,change
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
power=json.loads((root/'assets/presentation/scene.json').read_text())['factory_appearance']['emission_fresnel']['power']
with tempfile.TemporaryDirectory(prefix='nanovore-factory-emission-') as directory:
    out=Path(directory);base=stage_world(root/'assets/levels/virus-player-level.json',out)
    def run(name,data=base,expected=0):
        global count
        data=deepcopy(data)
        for building in data['content']['buildings']:
            building.update(growth_seconds=.01,sprout_seconds=.01,establishment_seconds=.01)
            building['branches'].update(seconds=.01,root_offset_seconds=0)
        data['rules']['ai']['expansion_delay']=300
        data['rules']['economy'].update(manufacture_seconds=1000,enemy_attack_seconds=300)
        world=out/(name+'_world.json');write_world(world,data)
        state=out/(name+'_state.json');scene=out/(name+'_scene.json')
        result=subprocess.run([*command,'--smoke','--world',str(world),'--ticks','600','--world-seed','1234','--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:assert result.stderr and not state.exists();return None,None
        game=json.loads(state.read_text());render=json.loads(scene.read_text())
        first=game['unit_capacity']+len(game['colonies']);buds=first+len(game['colonies'])*128*(game['factory_path_segments']+1)
        debris=buds+len(game['colonies'])*128
        tagged=0
        for instance in render['instances']:
            factory=first<=instance['slot']<buds or debris<=instance['slot']<debris+256
            assert bool(int(instance['style'][7])&8)==factory,(name,instance['slot'],'Wrong emission-mask scope')
            tagged+=factory
        assert tagged>0,name
        return game,render
    game,scene=run('default');assert scene['emission_fresnel']==[1,power] and scene['emission_color']==[1,.55,.25,1]
    off=change(base,'appearance.factory_appearance.emission_fresnel.strength',0)
    same,disabled=run('off',off);assert same==game and disabled['emission_fresnel']==[0,power]
    absent=deepcopy(base);absent['appearance'].pop('factory_appearance')
    same,optional=run('absent',absent);assert same==game and optional['emission_fresnel']==[0,1] and optional['emission_color']==[0,0,0,0]
    inherited=deepcopy(base);inherited['appearance']['factory_appearance'].pop('emission_color')
    same,render=run('surface_color',inherited);assert same==game and render['emission_color']==[0,0,0,0]
    same,render=run('black_emission',change(base,'appearance.factory_appearance.emission_color',[0,0,0]))
    assert same==game and render['emission_color']==[0,0,0,1]
    for test_power in (.01,16):
        same,render=run('power_'+str(test_power),change(base,'appearance.factory_appearance.emission_fresnel.power',test_power))
        assert same==game and render['emission_fresnel']==[1,test_power]
    for side in ('immune','fungus'):
        data=stage_world(root/'assets/levels'/(side+'-player-level.json'),out)
        _,render=run(side,data);assert render['emission_fresnel']==[1,power] and render['emission_color']==[1,.55,.25,1]
    for field,value in [('strength',-.01),('strength',1.01),('power',0),('power',16.01),('extra',1)]:
        run('invalid_'+str(count),change(base,'appearance.factory_appearance.emission_fresnel.'+field,value),expected=1)
    for color in ([1,.5],[1,.5,.25,1],[-.01,.5,.25],[1,1.01,.25]):
        run('invalid_color_'+str(count),change(base,'appearance.factory_appearance.emission_color',color),expected=1)
    missing=deepcopy(base);missing['appearance']['factory_appearance']['emission_fresnel'].pop('power')
    run('missing_power',missing,expected=1)
print(f'Factory emission E2E passed: {count} application runs; controls, disabled and absent defaults, all factions, instance scope, unchanged game results, and rejected settings.')
