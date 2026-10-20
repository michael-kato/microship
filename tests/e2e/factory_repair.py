#!/usr/bin/env python3
"""Check nutrient-funded repairs and production priority in the application."""
from copy import deepcopy
import json, math
from pathlib import Path
import subprocess,sys,tempfile
from asset_data import stage_world,write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-factory-repair-') as directory:
    out=Path(directory);base=stage_world(root/'assets/levels/virus-player-level.json',out)
    for start in base['match'].pop('starts'):base['bodies'][start['cell']]['owner']=start['faction']
    base['spawns']=[];base['rules'].pop('exploration',None)
    base['rules']['ai']['expansion_delay']=300;base['rules']['economy'].update(enemy_attack_seconds=300,manufacture_seconds=.1)
    for b in base['content']['buildings']:
        b.update(growth_seconds=.1,sprout_seconds=.01,establishment_seconds=.01)
        b['branches'].update(seconds=.01,regrow_seconds=.5)
        b['repair'].update(health_per_second=100,nutrients_per_health=.1,delay_seconds=.5)
        b.pop('haze',None)
    def run(name,data=base,ticks=120,expected=0):
        global count
        world=out/(name+'_world.json');write_world(world,data);state=out/(name+'_state.json')
        p=subprocess.run([*command,'--smoke','--world',str(world),'--ticks',str(ticks),'--world-seed','1234','--state',str(state)],capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        return json.loads(state.read_text()) if not expected else None
    initial=run('probe');tip=initial['colonies'][0]['structures'][0]['position']
    attacker=deepcopy(next(u for u in base['content']['units'] if u['name']=='fungal_spore'))
    attacker.update(name='repair_test_hit',faction='immune',speed=.01,range=.4,targets=['structures'])
    attacker['attack'].update(damage=20.01,lifetime=.4,cloud=dict(radius=.1,seconds=.1,damage=.001))
    damaged=deepcopy(base);damaged['content']['units'].append(attacker)
    damaged['spawns']=[dict(type='repair_test_hit',position=[tip[0],tip[1],tip[2]+.1])]
    early=run('wounded',damaged,ticks=15)['colonies'][0]
    assert early['wounds']==1 and early['repairing'] and early['produced']==[0,0],early
    fixed=run('repaired',damaged,ticks=300)['colonies'][0]
    assert fixed['wounds']==0 and fixed['health']==240 and not fixed['repairing'],fixed
    assert math.isclose(fixed['repair_nutrients'],2,abs_tol=.001),fixed['repair_nutrients']
    assert sum(fixed['produced'])>0,'Unit production did not resume after repair'
    poor=deepcopy(damaged)
    for b in poor['content']['buildings']:b['collection']=.00001
    unfunded=run('no_nutrients',poor,ticks=300)['colonies'][0]
    assert unfunded['wounds']==1 and unfunded['repairing'] and unfunded['produced']==[0,0],unfunded
    # A dead root remains dead until its full replacement cost has been paid.
    broken=deepcopy(damaged);broken['content']['units'][-1]['attack']['damage']=1000
    lost=run('broken_root',broken,ticks=15)['colonies'][0]
    assert lost['health']==0 and lost['active'] and lost['repairing'],lost
    regrown=run('regrown_root',broken,ticks=1800)['colonies'][0]
    assert regrown['health']==240 and regrown['structures'][0]['generation']==2,regrown
    assert regrown['repair_nutrients']>=24 and not regrown['repairing'],regrown
    poor=deepcopy(broken)
    for b in poor['content']['buildings']:b['collection']=.00001
    unpaid=run('unpaid_root',poor,ticks=600)['colonies'][0]
    assert unpaid['health']==0 and unpaid['structures'][0]['generation']==1 and unpaid['active']
    assert run('repeat',broken,ticks=1800)['colonies'][0]==regrown
    for field,value in [('health_per_second',0),('nutrients_per_health',0),('delay_seconds',-1),('extra',1)]:
        invalid=deepcopy(base);invalid['content']['buildings'][0]['repair'][field]=value
        run('invalid_'+str(count),invalid,expected=1)
print(f'Factory repair E2E passed: {count} application runs; paid wounds and root replacement, production priority, empty supply, repeatability and invalid settings.')
