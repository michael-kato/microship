#!/usr/bin/env python3
"""Check local tasks and factory clearance in the real Windows application."""
import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world,change

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args();root=Path(__file__).resolve().parents[2];binary=args.binary.resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='microship-navigation-') as directory:
    out=Path(directory)
    base=stage_world(root/'assets/levels/biology-virus.json',out)
    base['rules']['economy']['manufacture_seconds']=1000
    base['rules']['ai']['expansion_delay']=300
    base['rules'].pop('exploration',None)
    definitions={u['name']:u for u in base['content']['units']}
    def run(name,world,ticks=1,orders='',expected=0):
        global count
        level=out/(name+'-world.json');write_world(level,world)
        state=out/(name+'.json');replay=out/(name+'.orders');replay.write_text(orders)
        p=subprocess.run([*command,'--smoke','--world',str(level),'--ticks',str(ticks),'--state',str(state),
                          *(['--replay',str(replay)] if orders else [])],capture_output=True,text=True,timeout=10)
        count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        if expected:return None
        data=json.loads(state.read_text())
        for u in data['units']:
            assert all(math.isfinite(x) for x in u['position']+u['velocity']+u['flight_goal'])
            maximum=definitions[u['type']]['speed']
            assert math.dist(u['velocity'],[0,0,0])<=maximum+.00001,(name,'speed',u)
            if u['task']=='attack-unit':
                target=next((v for v in data['units'] if v['slot']==u['attention']),None)
                if target:assert target['generation']==u['attention_generation']
        return data
    # One nearby defender and three distant defenders share the same home.
    local=deepcopy(base);center=local['bodies'][0]['position']
    offsets=[(6,0,8),(-6,0,8),(0,6,8),(0,-6,8)]
    local['spawns']=[dict(type='virus_fighter',position=[a+b for a,b in zip(center,p)]) for p in offsets]
    local['spawns'].append(dict(type='immune_fighter',position=[center[0]+8,center[1],center[2]+8]))
    for u in local['content']['units']:
        if u['faction']=='immune':u['speed']=.01
    before=run('response-delay',local)
    assert all(u['task']=='patrol' for u in before['units'] if u['faction']==1)
    threat=run('personal-response',local,30)
    own=[u for u in threat['units'] if u['faction']==1]
    assert own[0]['task']=='attack-unit' and own[0]['attention']==4,own
    assert all(u['task']=='patrol' and u['attention']==-1 for u in own[1:]),own
    assert threat==run('repeat-response',local,30),'Local tasks are not repeatable'
    disabled=deepcopy(local)
    for u in disabled['content']['units']:
        if u['faction']=='virus':u['autonomy']['unit_radius']=0
    held=run('disabled-pursuit',disabled,30)
    assert all(u['task']=='patrol' for u in held['units'] if u['faction']==1)
    ordered=run('orders-take-priority',local,30,'0 select fighters\n0 move -52 -4 20\n')
    assert all(u['task']=='move' and u['attention']==-1 for u in ordered['units'] if u['faction']==1)
    leash=deepcopy(local);leash['spawns']=leash['spawns'][:1]+leash['spawns'][-1:]
    leash['spawns'][0]['position']=[center[0]+18,center[1],center[2]+8]
    leash['spawns'][1]['position']=[center[0]+20,center[1],center[2]+8]
    leashed=run('personal-leash',leash,30)
    assert leashed['units'][0]['task']=='patrol','An idle craft pursued outside its home area'
    probe=run('factory-probe',base)
    point=probe['colonies'][1]['structures'][0]['position'];point=[point[0],point[1],point[2]+2.5]
    raid=deepcopy(base);raid['spawns']=[dict(type='virus_bomber',position=point)]
    idle_raid=run('local-structure-task',raid,30,'0 select bombers\n0 move '+' '.join(map(str,point))+'\n')
    bomber=idle_raid['units'][0]
    assert bomber['task']=='attack-structure' and bomber['attention']==1 and not bomber['commanded'],bomber
    # Nearby unclaimed cells can be settled without a cell order.
    settle=deepcopy(base);body=settle['bodies'][2]
    settle['spawns']=[dict(type='virus_fighter',position=[c+n*(body['radius']+.25) for c,n in zip(body['position'],body['site']['normal'])])]
    settled=run('local-colonization',settle,360)
    assert settled['landings']==1 and settled['colonies'][2]['faction']==1,'Idle settlement did not use the personal radius'
    patrol=deepcopy(base);patrol['spawns']=[p for p in patrol['spawns'] if p['type'].startswith('virus_')]
    samples=[]
    for tick in range(1,1802,30 if args.output else 120):
        state=run('patrol-'+str(tick),patrol,tick)
        samples.append(dict(tick=tick,units=state['units']))
        assert state['flight']['contacts']==0,('Factory collision correction',tick,state['flight'])
        assert all(u['task']=='patrol' for u in state['units'])
    assert state['flight']['avoidance_events']>0,'The patrol never used factory avoidance'
    assert len(state['flight']['obstacles'])>0
    old=run('acceleration-before',patrol,900);new=run('acceleration-after',patrol,901)
    previous={u['generation']:u for u in old['units']}
    maximum_change=patrol['rules']['flight']['acceleration']/60+.00001
    for u in new['units']:
        prior=previous[u['generation']]
        assert math.dist(u['velocity'],prior['velocity'])<=maximum_change,('Acceleration',u,prior)
        assert math.dist(u['position'],prior['position'])<=definitions[u['type']]['speed']/60+.0001,('Position jump',u,prior)
    reset=run('patrol-end',patrol,1800)
    # The same complete run must remain deterministic with the extra diagnostics.
    assert reset==run('repeat-patrol',patrol,1800)
    for name,world in [
        ('negative-task-radius',change(base,'content.units.0.autonomy.unit_radius',-1)),
        ('zero-leash',change(base,'content.units.0.autonomy.leash_radius',0)),
        ('invalid-cruise',change(base,'content.units.0.autonomy.cruise_fraction',2)),
        ('missing-autonomy-field',change(base,'content.units.0.autonomy',dict(unit_radius=5))),
        ('invalid-head-radius',change(base,'content.buildings.0.navigation.head_radius',0)),
        ('zero-lookahead',change(base,'rules.flight.lookahead_seconds',0)),
    ]:run(name,world,expected=1)
    if args.output:
        args.output.mkdir(parents=True,exist_ok=True)
        (args.output/'paths.json').write_text(json.dumps(dict(samples=samples,obstacles=state['flight']['obstacles'],
            bodies=patrol['bodies'],definitions=definitions,flight=state['flight']),indent=2)+'\n')
print(f'Navigation E2E passed: {count} application runs; personal response, task ranges, orders, settlement, clearance and bounded flight.')
