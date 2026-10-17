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
from asset_data import established_world,write_world,change

repo_root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(repo_root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-growth-') as directory:
    out=Path(directory)
    def stage(side):return established_world(repo_root/'assets/levels'/('biology-'+side+'.json'),out)
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
            assert c['root_count']==len(c['attachments'])
            for k,branch in enumerate(c['structures']):
                if branch['parent']<0:assert branch['stem'][0]==c['attachments'][k], 'A root missed its organelle'

                for x,y in zip(branch['stem'],branch['stem'][1:]):
                    delta=[v-u for u,v in zip(x,y)];radial=[u-v for u,v in zip(x,center)]
                    assert sum(u*v for u,v in zip(delta,radial))>0,'A stem grew toward the cell center'
                    assert math.dist(y,center)>math.dist(x,center)
    early,er=run('early',base,2000)
    late,lr=run('mature',base,2800)
    assert 0<early['colonies'][0]['structures'][10]['growth']<1
    assert late['colonies'][0]['structures'][10]['growth']==1
    assert len(lr['instances'])>len(er['instances']),'Growth did not add stem segments'
    reset,_=run('restart',base,1800,extra=('--world-seed','1234','--restart'))
    assert reset['colonies'][0]['root']==a['colonies'][0]['root'] and reset['tick']==0
    assert all(c['structures'][c['root_count']]['health']==0 for c in reset['colonies'] if c['health']>0)
    audio=out/'growth.wav'
    audible,_=run('sound',base,3000,extra=('--audio-capture',str(audio)))
    with wave.open(str(audio)) as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes())==(1,2,48000,3000*800)
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
    fungus=duel('immune-trial');fungus['spawns']=[dict(type='immune_fighter',position=[0,0,6]),dict(type='swarm_fighter',position=[.35,0,6]),dict(type='swarm_fighter',position=[2,0,6])]
    for u in fungus['content']['units']:
        if u['name']=='swarm_fighter':u.update(hits=32,health=384,shot_interval=.15,detaches=False)
        if u['name']=='immune_fighter':
            u['shot_interval']=.25
            # Isolate feeding here. Allied rescue uses the shipping threshold in immune_combat.
            u['attack']['release_hits']=32
    attached,_=run('digest-attach',fungus,30)
    assert attached['faction_events']['digestions']>0 and any(u['faction']==2 and u['latch']>=0 for u in attached['units'])
    fed,_=run('digest-heal',fungus,240)
    assert fed['faction_events']['digest_heals']>0,'Digestion did not heal wounds'
    assert all(u['faction']!=2 or u['ready_spikes']==11 for u in fed['units'])
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
    # Authored size thresholds remove organs and their attachment points together.
    assert [len(c['attachments']) for c in a['colonies']]==[10,10,4,6,4]
    small=deepcopy(base);small['bodies'][0]['radius']=2.1
    reduced,rr=run('small-anatomy',small)
    assert reduced['colonies'][0]['root_count']==4
    assert rr['meshes'][0]['triangles']<ar['meshes'][0]['triangles'], 'Small cells retained all organ geometry'
    # Compare the former and new manufacture times with the same scene and roots.
    slower,_=run('slower-production',base,1800)
    faster=deepcopy(base);faster['rules']['economy']['manufacture_seconds']=2.5
    faster,_=run('former-production-time',faster,1800)
    assert 11<slower['births']<faster['births'], 'Manufacture time did not reduce unit births'
    limbs=deepcopy(base);limbs['rules'].pop('exploration',None);limbs['spawns']=[]
    limbs['rules']['economy']['manufacture_seconds']=1000;limbs['rules']['ai']['expansion_delay']=300
    for producer in limbs['content']['buildings']:
        producer['growth_seconds']=.1;producer['branches'].update(seconds=.1,regrow_seconds=10)
    mature,_=run('limbs-grown',limbs,120)
    tip=mature['colonies'][0]['structures'][0]['position'];position=[tip[0],tip[1],tip[2]+.4]
    limbs['spawns']=[dict(type='immune_bomber',position=position)]
    for unit in limbs['content']['units']:
        if unit['name']=='immune_bomber':unit.update(speed=.01,shot_interval=.12)
    damaged,_=run('root-loses-section',limbs,22)
    c=damaged['colonies'][0];root=c['structures'][0]
    assert 0<root['health']<240 and 0<root['visible_growth']<1, root
    assert damaged['fractures']['fragments']>0, 'A damaged section did not detach'
    assert c['structures'][10]['health']==0, 'A child survived loss of its attachment section'
    assert all(v['health']==240 for v in c['structures'][1:10]), 'Damage crossed to another organelle root'
    broken,_=run('root-break',limbs,105)
    c=broken['colonies'][0]
    assert c['health']==0 and c['active'] and broken['outcome']==0, c
    assert all(v['health']>0 for v in c['structures'][1:10]), 'One root destroyed unrelated factories'
    assert c['production']>0 and c['energy']>0, 'Surviving factories stopped collection'
    reset,_=run('limb-restart',limbs,105,extra=('--restart',))
    assert reset['fractures']['limbs']==reset['fractures']['fragments']==0
    assert reset['colonies'][0]['health']==240 and reset['colonies'][0]['active']
    # A one-hit fixture lets one expendable spore remove a root, then leave the cell alone.
    regrow=deepcopy(limbs);regrow['match']['opponent']='fungus';regrow['bodies'][1]['owner']='fungus'
    regrow['spawns']=[dict(type='fungal_spore',position=position)]
    for producer in regrow['content']['buildings']:
        producer['hits']=1;producer['branches']['regrow_seconds']=.5
    lost,_=run('root-awaits-regrowth',regrow,10)
    assert lost['colonies'][0]['health']==0 and lost['colonies'][0]['active']
    rebuilt,_=run('independent-root-regrowth',regrow,120)
    root=rebuilt['colonies'][0]['structures'][0]
    assert root['health']==240 and root['generation']==2 and root['wounds']==0, root
    assert rebuilt['colonies'][0]['structures'][1]['generation']==1, 'Regrowth replaced an unrelated root'
    # Settle through normal flight and landing, then measure the complete capacity ramp.
    for side,cell,kind in [('virus',2,'virus_fighter'),('virus',3,'virus_fighter'),
                           ('virus',4,'virus_fighter'),('immune',2,'immune_fighter')]:
        colony=stage(side);colony['rules'].pop('exploration',None)
        colony['rules']['economy']['manufacture_seconds']=1000
        colony['rules']['ai']['expansion_delay']=300
        colony['rules']['economy']['enemy_attack_seconds']=300
        body=colony['bodies'][cell]
        colony['spawns']=[dict(type=kind,position=[c+n*(body['radius']+.25) for c,n in zip(body['position'],body['site']['normal'])])]
        name=f'establish-{side}-{cell}'
        initial,_=run(name+'-land',colony,360)
        c=initial['colonies'][cell];assert initial['landings']==1 and c['active']
        start=360-round(c['establishment_age']*60)
        previous=0
        for seconds in (20,60,121,178,182):
            state,_=run(name+'-'+str(seconds),colony,start+seconds*60)
            c=state['colonies'][cell];structures=c['structures']
            alive=[v for v in structures if v['health']>0]
            growing=[v for v in alive if v['growth']<1]
            assert len(growing)<=1,(name,seconds,'Parallel emergence',structures)
            assert all(v['health']==0 and v['growth']==0 for v in structures[len(alive):]),'A later factory emerged first'
            assert all(v['parent']<0 or structures[v['parent']]['growth']==1 for v in alive),'A child grew before its support'
            definition=next(b for b in colony['content']['buildings'] if b['faction']==side)
            maximum=1+(len(structures)-c['root_count'])*definition['branches']['production']
            assert previous<c['production']<=maximum+.00001,(name,seconds,c['production'])
            previous=c['production']
            if seconds<180:
                assert len(alive)==math.ceil(seconds/(180/len(structures))), (name,seconds,len(alive))
                assert c['production']<maximum
            else:
                assert len(alive)==len(structures) and not growing
                assert abs(c['production']-maximum)<.00001
        reset,_=run(name+'-restart',colony,start+182*60,extra=('--restart',))
        assert not reset['colonies'][cell]['active'] and reset['colonies'][cell]['establishment_age']==0
    for seconds in (60,120):
        tuned=deepcopy(colony)
        for producer in tuned['content']['buildings']:producer['establishment_seconds']=seconds
        midpoint,_=run('establish-tuned-half-'+str(seconds),tuned,start+seconds*30)
        complete,_=run('establish-tuned-full-'+str(seconds),tuned,start+(seconds+1)*60)
        assert sum(v['growth']==1 for v in midpoint['colonies'][cell]['structures']) in (7,8)
        assert all(v['growth']==1 for v in complete['colonies'][cell]['structures'])
    productive=deepcopy(colony);productive['rules']['economy']['manufacture_seconds']=12
    producing,_=run('establish-produces-before-full',productive,start+90*60)
    assert any(u['target']==cell and u['faction']==2 for u in producing['units']), 'A growing colony did not produce units'
    run('zero-establishment',change(base,'content.buildings.0.establishment_seconds',0),expected=1)
    run('negative-establishment',change(base,'content.buildings.0.establishment_seconds',-1),expected=1)
    run('excess-establishment',change(base,'content.buildings.0.establishment_seconds',10001),expected=1)
    run('text-establishment',change(base,'content.buildings.0.establishment_seconds','90'),expected=1)
    parts_path=(out/base['content']['bodies'][2]['variation']['parts']).resolve()
    metadata=json.loads(parts_path.read_text());metadata['parts'][0]['minimum_scale']=-1
    bad_parts=out/'invalid-size-parts.json';bad_parts.write_text(json.dumps(metadata))
    run('negative-organ-threshold',change(base,'content.bodies.2.variation.parts',bad_parts.name),expected=1)
    run('invalid-grammar',change(base,'content.buildings.0.branches.grammar','unknown'),expected=1)
    run('invalid-root-mode',change(base,'content.buildings.0.branches.roots','unknown'),expected=1)
    run('insufficient-roots',change(base,'content.buildings.0.branches.count',9),expected=1)
    run('unknown-attack',change(trials['immune-trial'],'content.units.2.attack.mode','missing'),expected=1)
    run('invalid-variation',change(base,'content.bodies.0.variation.shape',.5),expected=1)
    run('missing-parts',change(base,'content.bodies.2.variation.parts','absent.json'),expected=1)
    run('invalid-seed',change(base,'bodies.0.seed',-1),expected=1)
    run('invalid-fungal-production',change(trials['fungus'],'content.factions.2.production.default_unit','swarm_fighter'),expected=1)
    bad_audio=out/'audio.json';bad_audio.write_text('{"distance":30,"events":{}}')
    run('invalid-audio',base,extra=('--audio',str(bad_audio)),expected=1)
print(f'Growth/faction/audio E2E passed: {count} application runs; seeded anatomy, outward growth, restart, digestion, spores, PCM mix and invalid assets.')
