#!/usr/bin/env python3
"""Change content definitions and inspect complete application results."""
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from asset_data import established_world, units, branches, change, remove, write_world

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-content-') as directory:
    out=Path(directory)
    def run(name,text,ticks=1,orders='',expected=0):
        global count
        world=out/(name+'-world.json');write_world(world,text)
        replay=out/(name+'.orders');replay.write_text(orders)
        state=out/(name+'.json');scene=out/(name+'-scene.json')
        args=[*command,'--smoke','--world',str(world),'--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene)]
        if orders:args+=['--replay',str(replay)]
        result=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.returncode,result.stdout,result.stderr)
        if expected:
            assert result.stderr.strip() and not state.exists(),(name,result.stdout,result.stderr)
            return
        game=json.loads(state.read_text());drawing=json.loads(scene.read_text())
        # Test the reported bounds with all eight transformed mesh corners.
        for instance in drawing['instances']:
            assert instance['mask'] in (1,4)
            mesh=drawing['meshes'][instance['mesh']]
            if instance['mask']!=1:continue
            transform=instance['transform']
            for bits in range(8):
                corner=[mesh['high' if bits&(1<<axis) else 'low'][axis] for axis in range(3)]
                for row in range(3):
                    value=transform[row*4+3]+sum(transform[row*4+axis]*corner[axis] for axis in range(3))
                    assert drawing['bounds_min'][row]-.001<=value<=drawing['bounds_max'][row]+.001,(name,'bounds',value)
        return game,drawing,result.stdout

    text=established_world(root/'assets/levels/virus-player-level.json',out)
    order='0 cell 0\n0 priority bomber\n'
    base,scene,log=run('baseline',text,1800,order)
    body=text['bodies'][0];center=body['position']
    cell_meshes=[scene['meshes'][0]]
    for instance in scene['instances']:
        if not int(instance['style'][7])&4:continue
        mesh=scene['meshes'][instance['mesh']]
        midpoint=[(a+b)/2 for a,b in zip(mesh['low'],mesh['high'])]
        if math.dist(midpoint,center)<body['radius']:cell_meshes.append(mesh)
    roughness=[min(m['roughness'][0] for m in cell_meshes),max(m['roughness'][1] for m in cell_meshes)]
    assert .02<=roughness[0]<.35 and .85<roughness[1]<=1, 'Cell surfaces lost their gloss and matte range'
    assert min(m['ior'][0] for m in cell_meshes)<max(m['ior'][1] for m in cell_meshes), 'Cell surfaces use one optical material'
    names=dict([('virus_fighter','type_a'),('virus_bomber','type_b'),('immune_fighter','type_c'),
                ('immune_bomber','type_d'),('virus_factory','growth_a'),('immune_factory','growth_b'),
                ('virus','team_a'),('immune','team_b'),('fighter','guard'),('fighters','guards'),
                ('bomber','breacher'),('bombers','breachers'),('VIRUS','TEAM_A'),('IMMUNE','TEAM_B')])
    def rename(value):
        if isinstance(value,dict):return {k:rename(v) for k,v in value.items()}
        if isinstance(value,list):return [rename(v) for v in value]
        return names.get(value,value) if isinstance(value,str) else value
    renamed=rename(text)
    renamed_state,_,renamed_log=run('renamed-content',renamed,1800,'0 cell 0\n0 priority breacher\n')
    assert renamed_state==rename(base),'Definition names changed game results'
    assert 'spawn type_a' in renamed_log and 'body unclaimed team_a' in renamed_log

    def reverse_keys(value):
        if isinstance(value,dict):return {k:reverse_keys(v) for k,v in reversed(list(value.items()))}
        if isinstance(value,list):return [reverse_keys(v) for v in value]
        return value
    reordered,_,_=run('reordered-fields',reverse_keys(text),1800,order)
    assert reordered==base,'Object field order changed game results'
    definitions=change(text,'content',text['content'])
    for items in definitions['content'].values():items.reverse()
    assert run('reordered-definitions',definitions,1800,order)[0]==base,'Definition order changed game results'
    assert base==run('deterministic',text,1800,order)[0]

    # A product can exist without a starting placement.
    mesh=Path(os.path.relpath(root/'assets/models/immune_macrophage.fmesh',out)).as_posix()
    auxiliary=dict(name='auxiliary',faction='virus',group='fighter',model=mesh,scale=.3,yaw_degrees=0,
                   tint=[1,1,1],speed=4,health=90,range=5,cost=.1,shot_interval=.4,separation=1,
                   targets=['units','structures'],colonizes=False,detaches=False,trails=True,pick_radius=.5,cue_radius=.4,hits=5)
    new=change(text,'content.factions.0.production.default_unit','auxiliary')
    new['content']['units'].append(auxiliary)
    grown,drawing,_=run('new-product-without-placement',new,1200,'0 cell 0\n0 priority fighter\n')
    produced=[u for u in grown['units'] if u['faction']==1 and u['health']==90]
    assert produced and grown['births']>11,'New asset type was not produced'
    for unit in produced:
        instance=next(i for i in drawing['instances'] if i['slot']==unit['slot'])
        if unit['generation']==min(u['generation'] for u in produced):
            for column in range(3):
                size=math.sqrt(sum(instance['transform'][row*4+column]**2 for row in range(3)))
                assert abs(size-.3)<.00001,'The new type scale did not reach the render input'

    # Selection keys come from content data.
    selected,_,_=run('renamed-selection',renamed,1,'0 select guards\n')
    assert sum(u['selected'] for u in selected['units'])==6
    passive=units(text,group='fighter',colonize=False)
    stayed,_,_=run('colonization-capability',passive,1800,'0 select fighters\n0 move -30 0 6.4\n240 order 2\n')
    assert stayed['colonies'][2]['faction']==0,'A type without colonization consumed a unit'

    compact=established_world(root/'tests/e2e/scenes/virus-two-cell-test-level.json',out)
    changed=units(compact,group='fighter',targets=['structures'])
    for u in changed['content']['units']:u.pop('secondary_attack',None)
    for spawn in changed['spawns']:
        if spawn['type'].startswith('immune_'):spawn['position'][0]=1000
    changed['rules']['economy']=dict(storage=180,manufacture_seconds=1000,landing_seconds=1.5,enemy_attack_seconds=300)
    changed=branches(changed,count=1)
    destroyed,_,_=run('target-capability',changed,2400,'0 select fighters\n0 order 1\n')
    assert destroyed['outcome']==1 and destroyed['sound_events']['win']==1,'Targeting still depends on group names'
    assert destroyed['colonies'][1]['health']==0 or destroyed['colonies'][1]['faction']==destroyed['player_faction']

    # Five-hit types use the same combat loop and keep bounded embedded geometry.
    duel=units(compact,group='fighter',speed=.01,health=100,hits=5)
    for u in duel['content']['units']:
        if u['group']=='fighter' and u['faction']!='fungus':u.update(attack={'mode':'projectile'},range=3.2,shot_interval=1)
    seen=set();spawns=[]
    for spawn in duel['spawns']:
        name=spawn['type']
        if name in seen:continue
        seen.add(name);friendly=name.startswith('virus_')
        x=(-.6 if friendly else .6) if name.endswith('_fighter') else (-1000 if friendly else 1000)
        spawn['position']=[x,0,5.2];spawns.append(spawn)
    duel['spawns']=spawns
    duel['rules']['economy']=dict(storage=180,manufacture_seconds=1000,landing_seconds=1.5,enemy_attack_seconds=300)
    damaged,_,_=run('five-hit-survivors',duel,120)
    assert all(u['wounds']==3 and u['health']==40 for u in damaged['units'] if u['kind']==0)
    dead,_,_=run('five-hit-destruction',duel,210)
    assert not any(u['kind']==0 for u in dead['units']) and dead['effects']['hits']==10

    # A decorative body does not need production or resource metadata.
    decorative=change(text,'content.bodies',text['content']['bodies'])
    decorative['content']['bodies'].append(dict(name='decoration',label='WALL',geometry='sphere',hosts_site=False))
    decorative['bodies'].append(dict(type='decoration',owner='neutral',position=[500,0,0],radius=2,tint=[1,1,1]))
    extra_body,_,_=run('decorative-body',decorative,1800,order)
    assert extra_body['colonies'][:5]==base['colonies']
    assert extra_body['colonies'][5]['health']==0 and extra_body['colonies'][5]['structures']==[]

    # Faction IDs and names are asset data.
    other=change(text,'content.factions.1.id',8)
    remapped,_,_=run('faction-id',other)
    assert any(u['faction']==8 for u in remapped['units']) and remapped['player_faction']==1

    duplicate=change(text,'content.units',text['content']['units']+[text['content']['units'][0]])
    for name,bad in [
        ('unknown-placement',change(text,'spawns.0.type','absent')),
        ('duplicate-type',duplicate),
        ('duplicate-faction',change(text,'content.factions.1.id',1)),
        ('unknown-faction',change(text,'content.units.0.faction','missing')),
        ('unknown-group',change(text,'content.units.0.group','missing')),
        ('broken-product',change(text,'content.factions.0.production.default_unit','absent')),
        ('cross-faction-product',change(text,'content.factions.0.production.default_unit','immune_fighter')),
        ('missing-producer',remove(text,'content.buildings.0')),
        ('missing-plane',remove(text,'content.buildings.0.growth_plane.normal')),
        ('invalid-hits',units(text,hits=0)),
        ('invalid-rounded-stem',change(text,'content.buildings.0.growth_meshes.rounded',2)),
        ('invalid-factory-bulges',change(text,'content.buildings.0.growth_meshes.bulges',9)),
        ('zero-length-color-distance',change(text,'content.buildings.0.growth_meshes.length_color',dict(color=[.1,.1,.1],distance=0,strength=.5))),
        ('invalid-length-color-strength',change(text,'content.buildings.0.growth_meshes.length_color',dict(color=[.1,.1,.1],distance=8,strength=1.01))),
        ('invalid-factory-glow',change(text,'content.buildings.0.growth_meshes.glow_scale',1.01)),
        ('invalid-factory-tint',change(text,'content.buildings.0.growth_meshes.tint_strength',-.01)),
        ('fractional-hits',units(text,hits=2.5)),
        ('invalid-targets',units(text,targets=['anything'])),
        ('duplicate-targets',units(text,targets=['units','units'])),
        ('invalid-capability',units(text,colonize=2)),
        ('nonfinite-speed',units(text,speed=float('nan'))),
        ('unknown-field',change(text,'component',{})),
        ('missing-opponent-home',change(text,'bodies.1.owner','neutral')),
        ('missing-content',change(text,'content','absent.json')),
    ]:run(name,bad,expected=1)
print(f'Data assets E2E passed: {count} application runs; names, order, new products, capabilities, render input and invalid references.')
