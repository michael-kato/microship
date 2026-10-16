#!/usr/bin/env python3
"""Check seeded anatomy, growth, faction combat and audio in the Windows app."""
import array
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import wave
from asset_data import stage_world,write_world,change

root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='microship-growth-') as directory:
    out=Path(directory)
    def stage(side):return stage_world(root/'assets/levels'/('biology-'+side+'.json'),out)
    def run(name,data,ticks=1,extra=(),orders='',expected=0):
        global count
        level=out/(name+'-world.json');write_world(level,data)
        state=out/(name+'.json');scene=out/(name+'-scene.json');replay=out/(name+'.orders');replay.write_text(orders)
        args=[*command,'--smoke','--world',str(level),'--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene),*extra]
        if orders:args+=['--replay',str(replay)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        if expected:return None,None
        d=json.loads(state.read_text());r=json.loads(scene.read_text())
        assert d['sound_dropped']==0,(name,'sound event storage overflow')
        for u in d['units']:
            assert all(math.isfinite(x) for x in u['position'])
            if u['latch']>=0 and not u['latch_colony']:
                target=next((v for v in d['units'] if v['slot']==u['latch']),None)
                # A target can die in the last simultaneous damage pass.
                if target:assert target['generation']==u['latch_generation']
        return d,r
    base=stage('virus')
    a,ar=run('seed-a',base,extra=('--world-seed','1234'))
    assert (a,ar)==run('seed-repeat',base,extra=('--world-seed','1234'))
    b,br=run('seed-b',base,extra=('--world-seed','1235'))
    assert ar['meshes'][0]!=br['meshes'][0] and a['colonies'][0]['root']!=b['colonies'][0]['root']
    for data in (a,b):
        for index,c in enumerate(data['colonies']):
            if c['health']<=0:continue
            center=base['bodies'][index]['position'];radius=base['bodies'][index]['radius']
            assert 0<math.dist(c['root'],center)<radius,'Root did not start inside its cell'
            for branch in c['structures']:
                for x,y in zip(branch['stem'],branch['stem'][1:]):
                    delta=[v-u for u,v in zip(x,y)];radial=[u-v for u,v in zip(x,center)]
                    assert sum(u*v for u,v in zip(delta,radial))>0,'A stem grew toward the cell center'
                    assert math.dist(y,center)>math.dist(x,center)
    early,er=run('early',base,1000)
    late,lr=run('mature',base,1400)
    assert 0<early['colonies'][0]['structures'][1]['growth']<1
    assert late['colonies'][0]['structures'][1]['growth']==1
    assert len(lr['instances'])>len(er['instances']),'Growth did not add stem segments'
    reset,_=run('restart',base,1800,extra=('--world-seed','1234','--restart'))
    assert reset['colonies'][0]['root']==a['colonies'][0]['root'] and reset['tick']==0
    assert all(c['structures'][1]['health']==0 for c in reset['colonies'] if c['health']>0)
    audio=out/'growth.wav'
    audible,_=run('sound',base,1800,extra=('--audio-capture',str(audio)))
    with wave.open(str(audio)) as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes())==(1,2,48000,1800*800)
        samples=array.array('h',f.readframes(f.getnframes()))
        assert max(abs(x) for x in samples)<=30000 and sum(x!=0 for x in samples)>4800
    assert audible['sound_events']['growth']>0 and audible['sound_events']['mature']>0 and audible['sound_events']['birth']>11
    trials={side:stage(side) for side in ('immune-trial','fungus','swarm')}
    for side,data in trials.items():run(side+'-trial',data,1800)
    def duel(side):
        d=deepcopy(trials[side]);d['rules'].pop('exploration',None)
        d['rules']['economy']['manufacture_seconds']=1000;d['rules']['ai']['expansion_delay']=300
        for u in d['content']['units']:u['speed']=.01;u['cost']=100
        return d
    fungus=duel('immune-trial');fungus['spawns']=[dict(type='immune_fighter',position=[0,0,6]),dict(type='swarm_fighter',position=[.35,0,6])]
    for u in fungus['content']['units']:
        if u['name']=='swarm_fighter':u.update(hits=32,health=384,shot_interval=.15,detaches=False)
        if u['name']=='immune_fighter':u['shot_interval']=.25
    attached,_=run('digest-attach',fungus,30)
    assert attached['faction_events']['digestions']>0 and any(u['faction']==2 and u['latch']>=0 for u in attached['units'])
    fed,_=run('digest-heal',fungus,240)
    assert fed['faction_events']['digest_heals']>0,'Digestion did not heal wounds'
    assert all(u['faction']!=2 or u['ready_spikes']==22 for u in fed['units'])
    body=duel('fungus');body['spawns']=[dict(type='fungal_spore',position=[0,0,6]),dict(type='swarm_fighter',position=[2,0,6])]
    for u in body['content']['units']:
        if u['name']=='swarm_fighter':u.update(shot_interval=.1,detaches=False)
    shot,_=run('spore-destroyed',body,180)
    assert not any(u['faction']==3 for u in shot['units']) and shot['faction_events']['spore_bursts']==0
    body['spawns'][1]['position']=[.3,0,6]
    burst,_=run('spore-contact',body,10)
    assert burst['faction_events']['spore_bursts']==1 and burst['effects']['hits']>0
    expired=deepcopy(body);expired['spawns'][1]['position']=[1000,0,6]
    for u in expired['content']['units']:
        if u['name']=='fungal_spore':u['attack']['lifetime']=.1
    gone,_=run('spore-expiry',expired,30)
    assert not any(u['faction']==3 for u in gone['units']) and gone['faction_events']['spore_bursts']==0
    for u in expired['content']['units']:
        if u['name']=='fungal_spore':u['attack']['lifetime']=.01
    immediate,_=run('spore-lifetime-before-launch-cooldown',expired,1)
    assert not any(u['faction']==3 for u in immediate['units'])
    limbs=deepcopy(base);limbs['rules'].pop('exploration',None);limbs['spawns']=[]
    limbs['rules']['economy']['manufacture_seconds']=1000;limbs['rules']['ai']['expansion_delay']=300
    for producer in limbs['content']['buildings']:
        if producer['faction']=='fungus':continue
        producer['growth_seconds']=.1;producer['branches'].update(seconds=.1,regrow_seconds=10)
    mature,_=run('limbs-grown',limbs,120)
    tip=mature['colonies'][0]['structures'][2]['position'];position=[tip[0],tip[1],tip[2]+.4]
    limbs['spawns']=[dict(type='immune_bomber',position=position)]
    for unit in limbs['content']['units']:
        if unit['name']=='immune_bomber':unit.update(speed=.01,shot_interval=1.4)
    broken,_=run('limb-break',limbs,150)
    c=broken['colonies'][0]
    assert c['health']==80 and c['structures'][1]['health']==45,'Damage crossed into an unrelated limb'
    assert c['structures'][2]['health']==c['structures'][5]['health']==c['structures'][6]['health']==0,'Detached children stayed alive'
    assert broken['fractures']['limbs']>=3 and broken['fractures']['fragments']>0 and broken['sound_events']['break']>=3
    reset,_=run('limb-restart',limbs,150,extra=('--restart',))
    assert reset['fractures']['limbs']==reset['fractures']['fragments']==0
    run('unknown-attack',change(trials['immune-trial'],'content.units.2.attack.mode','missing'),expected=1)
    run('invalid-variation',change(base,'content.bodies.0.variation.shape',.5),expected=1)
    run('missing-parts',change(base,'content.bodies.2.variation.parts','absent.json'),expected=1)
    run('invalid-seed',change(base,'bodies.0.seed',-1),expected=1)
    run('invalid-fungal-production',change(trials['fungus'],'content.factions.2.production.default_unit','swarm_fighter'),expected=1)
    bad_audio=out/'audio.json';bad_audio.write_text('{"distance":30,"events":{}}')
    run('invalid-audio',base,extra=('--audio',str(bad_audio)),expected=1)
print(f'Growth/faction/audio E2E passed: {count} application runs; seeded anatomy, outward growth, restart, digestion, spores, PCM mix and invalid assets.')
