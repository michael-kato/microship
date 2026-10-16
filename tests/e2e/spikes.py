#!/usr/bin/env python3
"""Check spike hits, regrowth and recovery in the real Windows application."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from asset_data import stage_world,units,producers,branches,change,remove,write_world

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='microship-spikes-') as directory:
    out=Path(directory)
    def stage(side):
        return stage_world(root/'tests/e2e/scenes'/('biology-two-'+side+'.json'),out)
    def run(name,text,ticks=1,orders='',expected=0,extra=()):
        global count
        scene=out/(name+'-world.json');write_world(scene,text)
        replay=out/(name+'.orders');replay.write_text(orders)
        state=out/(name+'.json')
        args=[*command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
        if orders:args+=['--replay',str(replay)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        return json.loads(state.read_text()) if not expected else None
    def duel(text):
        result=units(text,group='fighter',speed=.01)
        definitions={u['name']:u for u in result['content']['units']}
        seen=set();spawns=[]
        for spawn in result['spawns']:
            name=spawn['type']
            if name in seen:continue
            seen.add(name);definition=definitions[name];friendly=definition['faction']=='virus'
            x=(-.6 if friendly else .6) if definition['group']=='fighter' else (-1000 if friendly else 1000)
            spawn['position']=[x,0,5.2];spawns.append(spawn)
        result['spawns']=spawns
        result['rules']['economy']=dict(storage=180,manufacture_seconds=1000,landing_seconds=1.5,enemy_attack_seconds=300)
        return branches(result,count=1)
    def check_hits(state):
        for u in state['units']:
            maximum=36 if u['kind']==0 else 60
            assert u['wounds'] in (0,1,2)
            assert abs(u['health']-maximum*(3-u['wounds'])/3)<.00001,'Health does not match the hit count'
        for c in state['colonies']:
            for s in c['structures']:
                if s['health']>0:assert abs(s['health']-80*(3-s['wounds'])/3)<.00001
    for side,home,player in [('virus',0,1),('immune',1,2)]:
        text=stage(side);combat=duel(text)
        one=run(side+'-one-hit',combat,20);check_hits(one)
        fighters=[u for u in one['units'] if u['kind']==0]
        assert len(fighters)==2 and all(u['wounds']==1 for u in fighters),'First impact did not leave one wound'
        assert one['recovery']['detached']==1 and next(u for u in fighters if u['faction']==1)['ready_spikes']==21
        two=run(side+'-two-hits',combat,60);check_hits(two)
        assert all(u['wounds']==2 for u in two['units'] if u['kind']==0),'Second impact did not leave two wounds'
        assert two['effects']['low_health']==2,'Two-hit survivors were not counted'
        dead=run(side+'-third-hit',combat,120);check_hits(dead)
        assert not any(u['kind']==0 for u in dead['units']),'Third impact did not remove fighters'
        assert dead['effects']['hits']==6 and dead['effects']['deaths']==2,'Three-hit duel produced the wrong damage'
        assert dead['outcome']==0 and all(c['health']==80 for c in dead['colonies']),'Fighters damaged factories'
        assert dead==run(side+'-repeat',combat,120),'Replay was not deterministic'
        slow=change(combat,'effects.projectile.shot_interval',10)
        regrown=run(side+'-regrown',slow,480);check_hits(regrown)
        assert regrown['recovery']['regrown']==1 and regrown['recovery']['healed']==0
        assert all(u['ready_spikes']==22 for u in regrown['units']),'Detached spike did not regrow'
        # Keep both damaged fighters within the authored friendly recovery radius.
        recovery=change(slow,'rules.recovery.orbit_clearance',10)
        healed=run(side+'-friendly-recovery',recovery,480);check_hits(healed)
        delayed=change(recovery,'rules.recovery.damage_delay',60)
        injured=run(side+'-delayed-recovery',delayed,480);check_hits(injured)
        assert healed['recovery']['healed']>0 and injured['recovery']['healed']==0,'Recovery ignored the damage delay'
        assert any(u['faction']==player and u['wounds']>0 for u in injured['units'])
        assert all(u['wounds']==0 for u in healed['units'] if u['faction']==player),'Friendly orbit did not remove wounds'
        reset=run(side+'-restart',combat,60,extra=('--restart',))
        assert reset['tick']==0 and reset['births']==4 and reset['recovery']=={'healed':0,'regrown':0,'detached':0}
        assert all(c['health']==80 for c in reset['colonies']) and all(u['wounds']==0 and u['ready_spikes']==22 for u in reset['units'])
        # Supply enough nutrients to measure the manufacturing rate.
        supplied=producers(text,collection=100)
        for body in supplied['bodies']:body['position'][0]*=10
        for spawn in supplied['spawns']:spawn['position'][0]+=(-54 if spawn['type'].startswith('virus_') else 54)
        normal=run(side+'-slow-ai',supplied,1800)
        fast=run(side+'-fast-ai',change(supplied,'rules.ai',dict(production_rate=1,expansion_delay=300)),1800)
        assert normal['effects']['hits']==fast['effects']['hits']==0,'Combat changed the production comparison'
        own=lambda s:sum(u['faction']==player for u in s['units'])
        other=lambda s:sum(u['faction']!=player for u in s['units'])
        assert own(normal)==own(fast) and other(normal)<other(fast),(side,'AI production counts',own(normal),own(fast),other(normal),other(fast))
    text=stage('virus')
    for name,bad in [
        ('bad-count',change(text,'rules.spikes.count',33)),
        ('missing-recovery',remove(text,'rules.recovery')),
        ('bad-recovery',change(text,'rules.recovery.heal_interval',0)),
        ('bad-ai-rate',change(text,'rules.ai.production_rate',0)),
        ('bad-clarity',change(text,'appearance.thin_surface.clarity',1.1)),
        ('bad-injury',change(text,'effects.injury.first',[-1,0,0])),
        ('missing-spike-mesh',change(text,'effects.spikes.model','missing.fmesh')),
    ]:run(name,bad,expected=1)
print(f'Spike E2E passed: {count} application runs; three-hit damage, spike regrowth, recovery, AI rate, both sides and invalid assets.')
