#!/usr/bin/env python3
"""Check tree growth and faction cell changes through real application runs."""
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world,change

root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-forms-') as directory:
    out=Path(directory)
    def run(name,data,ticks=1,orders='',extra=(),expected=0):
        global count
        path=out/(name+'-world.json');write_world(path,data)
        replay=out/(name+'.orders');replay.write_text(orders);state=out/(name+'.json');scene=out/(name+'-scene.json')
        p=subprocess.run([*command,'--smoke','--world',str(path),'--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene),*(['--replay',str(replay)] if orders else []),*extra],capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        return (json.loads(state.read_text()),json.loads(scene.read_text())) if expected==0 else (None,None)
    for side,home,faction in [('virus',0,1),('immune',1,2),('fungus',0,3)]:
        data=stage_world(root/'assets/levels'/(side+'-player-level.json'),out)
        for building in data['content']['buildings']:
            building.update(establishment_seconds=4,growth_seconds=.1,sprout_seconds=.1)
        data['rules']['cell_infection'].update(tint_seconds=12,morph_seconds=12)
        data['rules']['ai']['expansion_delay']=1 if side=='fungus' else 300
        data['rules']['economy']['enemy_attack_seconds']=300;data['rules']['economy']['manufacture_seconds']=1
        orders='' if side=='fungus' else f'0 select all\n0 order {home}\n'
        initial,initial_scene=run(side+'-neutral',data)
        middle,mid_scene=run(side+'-partial',data,600,orders)
        mature,scene=run(side+'-mature',data,1800,orders)
        a=initial['colonies'][home]['appearance'];b=middle['colonies'][home]['appearance'];c=mature['colonies'][home]['appearance']
        assert a['factions'][faction]==0 and 0<b['factions'][faction]<1 and c['factions'][faction]==1,(side,a,b,c)
        body_slot=mature['unit_capacity']+home
        style=next(v['style'] for v in scene['instances'] if v['slot']==body_slot)
        profile=next(f for f in data['content']['factions'] if f['id']==faction)
        assert abs(style[5]-profile['cell_appearance']['roughness'])<1e-5 and style[6]==1
        base=initial_scene['meshes'][home]['deformation'];partial=mid_scene['meshes'][home]['deformation'];full=scene['meshes'][home]['deformation']
        assert base['indices']==partial['indices']==full['indices'],'Cell topology changed'
        assert len(base['vertices'])==len(partial['vertices'])==len(full['vertices'])
        wall={i for triangle in base['indices'] if triangle[3]==3 for i in triangle[:3]}
        moved=0
        for i,(v0,v1,v2) in enumerate(zip(base['vertices'],partial['vertices'],full['vertices'])):
            if i not in wall:
                assert v0==v1==v2,'An organelle moved with the wall'
                continue
            distance=math.dist(v0[:3],v2[:3]);moved+=distance>1e-5
            for axis in range(3):assert abs(v1[axis]-(v0[axis]+b['factions'][faction]*(v2[axis]-v0[axis])))<1e-5,'Wall positions did not lerp'
            assert abs(math.dist(v2[3:],[0,0,0])-1)<1e-5
            body=data['bodies'][home];appearance=profile['cell_appearance']
            limit=body['radius']*appearance.get('displacement',{}).get('height',0)
            limit+=abs(math.dist(v0[:3],body['position'])-body['radius'])*appearance.get('roundness',0)
            assert distance<=limit+1e-5
        assert moved>100
        if side=='immune':
            center=data['bodies'][home]['position']
            radii=lambda mesh:[math.dist(mesh['vertices'][i][:3],center) for i in wall]
            original=radii(base);rounded=radii(full)
            before=max(original)-min(original);after=max(rounded)-min(rounded)
            assert .1*before<after<.25*before,(before,after)
            unchanged=deepcopy(data)
            next(f for f in unchanged['content']['factions'] if f['name']=='immune')['cell_appearance']['roundness']=0
            plain,_=run('immune-roundness-off',unchanged,1800,orders)
            assert plain==mature,'Wall roundness changed the simulation'
            print(f'Immune radial variation: {before:.6f} before, {after:.6f} after; some variation remains.')
        colors=base['wall_colors'];wall_colors=[colors[i] for i in wall]
        assert len({tuple(c) for c in wall_colors})>100
        assert max(c[0]/max(c[2],1e-6) for c in wall_colors)-min(c[0]/max(c[2],1e-6) for c in wall_colors)>.08,'Wall pigment only changed brightness'
        def face_normal(vertices,triangle):
            a,b,c=(vertices[i][:3] for i in triangle[:3]);u=[y-x for x,y in zip(a,b)];v=[y-x for x,y in zip(a,c)]
            return [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
        for triangle in base['indices']:
            if triangle[3]!=3:continue
            original=face_normal(base['vertices'],triangle)
            for deformation in (partial,full):
                normal=face_normal(deformation['vertices'],triangle)
                assert sum(x*y for x,y in zip(original,normal))>0,'A wall triangle collapsed or flipped'
        assert mature==run(side+'-repeat',data,1800,orders)[0]
        unlit=deepcopy(data);unlit['appearance']['ambient']=[0,0,0]
        dark,_=run(side+'-render-independent',unlit,1800,orders,('--samples','1'))
        assert all(dark[k]==mature[k] for k in ('colonies','units','outcome','births'))
        reset,reset_scene=run(side+'-restart',data,1800,orders,('--restart',))
        assert not any(reset['colonies'][home]['appearance']['factions']) and not reset['colonies'][home]['active']
        assert reset_scene['meshes'][home]['deformation']['vertices']==base['vertices'],'Restart did not restore the original wall'
        if side=='virus':virus=data
    # Let the opponent capture an established cell through normal combat and settlement.
    changeover=deepcopy(virus)
    for start in changeover['match'].pop('starts'):changeover['bodies'][start['cell']]['owner']=start['faction']
    changeover['bodies']=changeover['bodies'][:2]
    changeover['bodies'][0]['position']=[-8,0,0];changeover['bodies'][1]['position']=[8,0,0]
    changeover['spawns']=[dict(type='virus_fighter',position=[-100,0,8]),dict(type='immune_bomber',position=[8,3,6])]
    changeover['rules']['ai']['expansion_delay']=10;changeover['rules']['economy'].update(enemy_attack_seconds=10,manufacture_seconds=1000)
    changeover['rules'].pop('exploration',None)
    for building in changeover['content']['buildings']:
        building['hits']=1;building['branches'].update(seconds=300,regrow_seconds=300)
    for unit in changeover['content']['units']:
        if unit['faction']=='virus':unit['speed']=.01
        if unit['name']=='immune_bomber':unit.update(speed=2.8,shot_interval=.25)
    before,_=run('ownership-before',changeover,600)
    after,after_scene=run('ownership-after',changeover,4800)
    assert before['colonies'][0]['faction']==1 and before['colonies'][0]['appearance']['factions'][1]>0
    assert after['colonies'][0]['faction']==2 and after['colonies'][0]['appearance']['factions'][1]==0
    assert after['colonies'][0]['appearance']['factions'][2]==1
    _,neutral_scene=run('ownership-neutral-wall',changeover,1,extra=('--restart',))
    regular=neutral_scene['meshes'][0]['deformation'];rounded=after_scene['meshes'][0]['deformation']
    wall={i for triangle in regular['indices'] if triangle[3]==3 for i in triangle[:3]}
    center=changeover['bodies'][0]['position']
    span=lambda mesh:max(math.dist(mesh['vertices'][i][:3],center) for i in wall)-min(math.dist(mesh['vertices'][i][:3],center) for i in wall)
    assert .1*span(regular)<span(rounded)<.25*span(regular),'Immune capture did not round the wall'
    assert all(a==b for i,(a,b) in enumerate(zip(regular['vertices'],rounded['vertices'])) if i not in wall),'Immune capture moved an organelle'
    for field,value in [('children',1),('levels',0),('length_scale',1.1),('width_scale',0),('angle_degrees',0),('bend',0)]:
        run('bad-tree-'+field,change(virus,'content.buildings.0.branches.tree.'+field,value),expected=1)
    for field,value in [('roughness',-1),('roundness',-.01),('roundness',1.01),('displacement.features',257),('displacement.features',0),('displacement.width_degrees',5),('displacement.height',0),('displacement.profile','missing')]:
        run('bad-surface-'+field.replace('.','-'),change(virus,'content.factions.0.cell_appearance.'+field,value),expected=1)
print(f'Colony forms E2E passed: {count} application runs; fixed wall topology, vertex interpolation, unchanged organelles, valid normals, immune roundness and capture, faction materials, restart, deterministic results and invalid assets.')
