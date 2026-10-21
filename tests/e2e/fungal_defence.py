#!/usr/bin/env python3
"""Check paced spores and cell protection through complete application runs."""
from copy import deepcopy
import json, math
from pathlib import Path
import subprocess, sys, tempfile
from asset_data import stage_world, write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
quick='--quick' in sys.argv[2:]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-fungal-defence-') as directory:
    out=Path(directory)
    base=stage_world(root/'assets/levels/fungus-player-level.json',out)
    for start in base['match'].pop('starts'):base['bodies'][start['cell']]['owner']=start['faction']
    base['spawns']=[];base['rules'].pop('exploration',None)
    base['rules']['ai']['expansion_delay']=300;base['rules']['economy'].update(enemy_attack_seconds=300,manufacture_seconds=10000)
    for b in base['content']['buildings']:
        b.update(growth_seconds=.01,sprout_seconds=.01,establishment_seconds=.01)
        b['branches'].update(count=1,roots='single',grammar='axial',seconds=.01)
        for key in ('root_limit','root_minimum_radius','additional_root_scale','tree'):b['branches'].pop(key,None)
    def run(name,data=base,ticks=120,expected=0,orders=''):
        global count
        world=out/(name+'_world.json');write_world(world,data)
        state=out/(name+'_state.json');scene=out/(name+'_scene.json')
        replay=out/(name+'.orders');replay.write_text(orders)
        extra=['--replay',str(replay)] if orders else []
        p=subprocess.run([*command,'--smoke','--world',str(world),'--ticks',str(ticks),'--world-seed','1234','--state',str(state),'--scene-state',str(scene),*extra],capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        if expected:assert not state.exists();return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    spore=next(u for u in base['content']['units'] if u['name']=='fungal_spore')
    assert spore['speed']==5.04 and spore['brood']==1 and spore['attack']['damage']==3 and spore['attack']['cloud']['damage']==1
    assert spore['manufacture_seconds']==.5 and spore['cost']==.1
    if not quick:
        movement=deepcopy(base);movement['spawns']=[dict(type='fungal_spore',position=[0,0,20])]
        for unit in movement['content']['units']:unit['manufacture_seconds']=10000
        fast,_=run('speed_new',movement,ticks=300,orders='0 select all\n0 move 100 0 20\n')
        old_speed=deepcopy(movement)
        next(u for u in old_speed['content']['units'] if u['name']=='fungal_spore')['speed']=4.2
        slow,_=run('speed_old',old_speed,ticks=300,orders='0 select all\n0 move 100 0 20\n')
        assert math.isclose(math.dist(fast['units'][0]['velocity'],[0,0,0]),5.04,abs_tol=.01)
        assert math.isclose(math.dist(slow['units'][0]['velocity'],[0,0,0]),4.2,abs_tol=.01)
        assert fast['units'][0]['position'][0]>slow['units'][0]['position'][0]
    staged=stage_world(root/'assets/levels/fungus-player-level.json',out)
    staged['rules']['ai']['expansion_delay']=300
    for unit in staged['content']['units']:unit['manufacture_seconds']=10000
    for ticks,roots in (((1800,3),) if quick else ((1200,3),(1800,3),(3000,3))):
        growth,_=run('staged_factories_'+str(ticks),staged,ticks=ticks)
        colony=growth['colonies'][0]
        assert colony['root_count']==len(colony['attachments']) and colony['large_root_count']==3 and sum(limb['health']>0 for limb in colony['structures'][:3])==roots,(ticks,colony['structures'][:3])
    # Check each emitted unit, not only the final average. One ready leaf cannot burst.
    previous=0
    for ticks in ((30,60) if quick else (30,60,90,120,150,180)):
        game,_=run('paced_'+str(ticks),ticks=ticks);born=game['colonies'][0]['produced'][0]
        assert born-previous<=1 and born>=previous,(ticks,born,previous)
        previous=born
    assert previous>=(1 if quick else 5),'Spore production is slower than two spores per second'
    if not quick:
        old=deepcopy(base);old_spore=next(u for u in old['content']['units'] if u['name']=='fungal_spore')
        old_spore.pop('manufacture_seconds');old_spore.update(brood=16,cost=1.5);old['rules']['economy']['manufacture_seconds']=12
        before,_=run('old_brood',old,ticks=1800);after,_=run('new_paced',ticks=1800)
        assert after['colonies'][0]['produced'][0]>before['colonies'][0]['produced'][0]*1.2
        assert run('repeat',ticks=1800)[0]==after,'Fixed-seed production changed'
        capacity=deepcopy(base)
        for body in capacity['bodies'][2:]:body['owner']='fungus'
        for building in capacity['content']['buildings']:building.pop('haze',None)
        for unit in capacity['content']['units']:
            if unit['name']=='fungal_spore':unit.update(cost=.0001,manufacture_seconds=.001,separation=.01)
        full,_=run('capacity_full',capacity,ticks=650)
        waiting,_=run('capacity_waiting',capacity,ticks=680)
        a=full['colonies'][0];b=waiting['colonies'][0]
        assert len(full['units'])==full['unit_capacity']==2048 and a['capacity_blocked']
        assert a['produced']==b['produced'] and a['leaf_jobs']==b['leaf_jobs'] and b['capacity_blocked']
        assert math.isclose(b['energy']-a['energy'],1.25,abs_tol=.001),'A blocked job charged nutrients again'
    # Isolate the cell's haze from weapon damage, production and movement.
    defence=deepcopy(base)
    for u in defence['content']['units']:u.update(speed=.01,cost=100,manufacture_seconds=10000,shot_interval=10000,range=.01)
    watch=deepcopy(next(u for u in defence['content']['units'] if u['name']=='swarm_fighter'))
    watch.update(name='watch',health=120,hits=12,detaches=False,colonizes=False,trails=False)
    defence['content']['units'].append(watch)
    center=defence['bodies'][0]['position'];point=[center[0],center[1],center[2]+7]
    defence['spawns']=[dict(type='watch',position=point)]
    near,render=run('near_enemy',defence,ticks=240)
    victim=next(u for u in near['units'] if u['type']=='watch')
    assert victim['wounds']==1 and math.isclose(near['haze']['damage'],12,abs_tol=.01),(victim,near['haze'])
    clouds=[e for e in render['effect_segments'] if e[7]==5]
    assert clouds,'The protective cell haze has no visible effect'
    far=deepcopy(defence);far['spawns'][0]['position'][2]+=3
    distant,_=run('far_enemy',far,ticks=240)
    assert distant['haze']['damage']==0 and distant['units'][0]['wounds']==0
    if not quick:
        neutral=deepcopy(defence);neutral['content']['units'][-1]['colonizes']=True
        neutral['match']['starts']=[dict(faction='fungus',cell=0),dict(faction='virus',cell=1)]
        neutral['bodies'][0]['owner']=neutral['bodies'][1]['owner']='neutral'
        neutral['spawns'].append(dict(type='fungal_founder',position=[point[0]+100,point[1]+100,point[2]+100]))
        empty,hidden=run('neutral_cell',neutral,ticks=240)
        assert empty['haze']==dict(damage=0,healing=0)
        assert not any(e[7]==5 for e in hidden['effect_segments'])
        blocked=deepcopy(defence);blocked['bodies'][2].update(position=[center[0],center[1],center[2]+6],radius=.5)
        shielded,_=run('blocked_haze',blocked,ticks=240)
        assert shielded['haze']['damage']==0,'The haze passed through a different cell'
        overlap=deepcopy(defence);overlap['bodies'][2].update(owner='fungus',position=[center[0]+6.5,center[1],center[2]])
        overlap['spawns'][0]['position']=[center[0]+3.3,center[1],center[2]+5]
        covered,_=run('overlapping_haze',overlap,ticks=240)
        assert math.isclose(covered['haze']['damage'],12,abs_tol=.01),'Overlapping haze multiplied damage'
        lost=deepcopy(defence);tip=near['colonies'][0]['structures'][0]['position']
        breaker=deepcopy(spore);breaker.update(name='cell_breaker',faction='virus',speed=.01,range=.4,targets=['structures'])
        breaker['attack'].update(damage=1000,lifetime=.4,cloud=dict(radius=.1,seconds=.1,damage=.001))
        lost['content']['units'].append(breaker)
        lost['spawns'].append(dict(type='cell_breaker',position=[tip[0],tip[1],tip[2]+.1]))
        destroyed,_=run('lost_cell',lost,ticks=120)
        later,_=run('lost_cell_later',lost,ticks=240)
        assert not destroyed['colonies'][0]['active'] and later['haze']==destroyed['haze'],'A lost cell retained its haze'
    contact=deepcopy(defence)
    for building in contact['content']['buildings']:building.pop('haze',None)
    next(u for u in contact['content']['units'] if u['name']=='fungal_spore').update(spore)
    contact['spawns']=[dict(type='watch',position=[0,0,20]),dict(type='fungal_spore',position=[.1,0,20])]
    weaker,_=run('weaker_spore',contact,ticks=15)
    target=next(u for u in weaker['units'] if u['type']=='watch')
    assert weaker['faction_events']['spore_bursts']==1 and target['wounds']==0 and 3.1<target['exposure']<3.3,target
    # A burst wounds an allied unit once. The haze must repair actual wounds and partial damage.
    healing=deepcopy(defence);healing['content']['units'][-1]['faction']='fungus'
    attacker=deepcopy(spore);attacker.update(name='test_burst',faction='virus',speed=.01,range=.4,targets=['units'])
    attacker['attack'].update(damage=25,lifetime=.4,cloud=dict(radius=.1,seconds=.1,damage=.001))
    healing['content']['units'].append(attacker)
    healing['spawns'].append(dict(type='test_burst',position=[point[0]+.1,point[1],point[2]]))
    wounded,_=run('ally_wounded',healing,ticks=15)
    assert next(u for u in wounded['units'] if u['type']=='watch')['wounds']==2,wounded['haze']
    healed,_=run('ally_healed',healing,ticks=660)
    ally=next(u for u in healed['units'] if u['type']=='watch')
    assert ally['health']==120 and ally['wounds']==0 and ally['exposure']==0 and healed['haze']['healing']>24,(ally,healed['haze'])
    if quick:
        print(f'Fungal quick check passed: {count} application runs; staged factories, paced spores, contact damage, haze range and healing.')
        raise SystemExit(0)
    no_haze=deepcopy(healing)
    for b in no_haze['content']['buildings']:b.pop('haze',None)
    unhealed,_=run('no_haze_healing',no_haze,ticks=660)
    assert next(u for u in unhealed['units'] if u['type']=='watch')['wounds']==2
    lethal=deepcopy(healing);lethal['content']['units'][-1]['attack']['damage']=1000
    dead,_=run('no_resurrection',lethal,ticks=660)
    assert not any(u['type']=='watch' for u in dead['units'])
    for value in (-1,10001):
        invalid=deepcopy(base);invalid['content']['buildings'][0]['branches']['root_interval_seconds']=value
        run('invalid_root_interval_'+str(count),invalid,expected=1)
    for field,value in [('reach',0),('reach',21),('damage_per_second',-1),('heal_per_second',1001),('extra',1)]:
        invalid=deepcopy(defence);next(b for b in invalid['content']['buildings'] if b['faction']=='fungus')['haze'][field]=value
        run('invalid_'+str(count),invalid,expected=1)
print(f'Fungal defence E2E passed: {count} application runs; paced output, speed and damage settings, range, ownership, visibility, healing, death and invalid settings.')
