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
        # Retain the full, regular-tree fixture; natural_factory checks the new defaults.
        for building in data['content']['buildings']:
            building.update(establishment_seconds=4,growth_seconds=.1)
            branch=building['branches'];branch.pop('root_limit',None);branch.pop('root_minimum_radius',None)
            if branch['grammar']=='tree':
                branch['count']=70
                for key in ('branch_probability','angle_jitter_degrees','length_jitter','width_jitter'):branch['tree'].pop(key,None)
                branch['tree']['bend']=.2
            else:branch['count']=16
        for profile in data['content']['factions']:
            if 'cell_appearance' in profile:profile['cell_appearance'].update(seconds=12,morph_seconds=12)
        data['rules']['ai']['expansion_delay']=1 if side=='fungus' else 300
        data['rules']['economy']['enemy_attack_seconds']=300;data['rules']['economy']['manufacture_seconds']=1
        orders='' if side=='fungus' else f'0 select all\n0 order {home}\n'
        initial,initial_scene=run(side+'-neutral',data)
        middle,mid_scene=run(side+'-partial',data,600,orders)
        mature,scene=run(side+'-mature',data,1800,orders)
        a=initial['colonies'][home]['appearance'];b=middle['colonies'][home]['appearance'];c=mature['colonies'][home]['appearance']
        assert a['factions'][faction]==0 and 0<b['factions'][faction]<1 and c['factions'][faction]==1,(side,a,b,c)
        body_slot=256+home
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
            assert distance<=data['bodies'][home]['radius']*profile['cell_appearance'].get('displacement',{}).get('height',0)+1e-5
        assert moved>100 if side!='immune' else moved==0
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
        assert len(scene['instances'])>len(mid_scene['instances']) or side=='fungus'
        assert mature==run(side+'-repeat',data,1800,orders)[0]
        unlit=deepcopy(data);unlit['appearance']['ambient']=[0,0,0]
        dark,_=run(side+'-render-independent',unlit,1800,orders,('--samples','1'))
        assert all(dark[k]==mature[k] for k in ('colonies','units','outcome','births'))
        reset,reset_scene=run(side+'-restart',data,1800,orders,('--restart',))
        assert not any(reset['colonies'][home]['appearance']['factions']) and not reset['colonies'][home]['active']
        assert reset_scene['meshes'][home]['deformation']['vertices']==base['vertices'],'Restart did not restore the original wall'
        if side=='virus':
            coarse=deepcopy(data)
            coarse['content']['factions'][0]['cell_appearance']['displacement'].update(features=64,width_degrees=12)
            coarse_game,coarse_scene=run('virus-coarse-wall',coarse,1800,orders)
            assert coarse_game==mature,'Spike frequency changed the game'
            old=coarse_scene['meshes'][home]['deformation']
            assert old['indices']==full['indices'] and len(old['vertices'])==len(full['vertices'])
            neighbors={i:set() for i in wall}
            for triangle in base['indices']:
                if triangle[3]!=3:continue
                for i in triangle[:3]:neighbors[i].update(j for j in triangle[:3] if j!=i)
            def peaks(deformation):
                heights={i:math.dist(base['vertices'][i][:3],deformation['vertices'][i][:3]) for i in wall}
                return sum(heights[i]>1e-5 and all(heights[i]>heights[j]+1e-6 for j in neighbors[i]) for i in wall)
            coarse_peaks=peaks(old);fine_peaks=peaks(full)
            assert fine_peaks>=3*coarse_peaks,(coarse_peaks,fine_peaks)
            print(f'Virus wall peaks measured in application geometry: {coarse_peaks} before, {fine_peaks} after; unchanged topology.')
            colony=mature['colonies'][home];structures=colony['structures'];roots=colony['root_count']
            assert len(structures)==roots*7==70
            depths=[]
            for k,branch in enumerate(structures):
                depth=0;parent=branch['parent']
                while parent>=0:depth+=1;parent=structures[parent]['parent']
                depths.append(depth)
                children=sum(v['parent']==k for v in structures)
                assert children==(0 if depth==2 else 2)
                assert branch['terminal']==(depth==2)
                assert abs(branch['scale']-.62**depth)<1e-5
                length=sum(math.dist(a,b) for a,b in zip(branch['stem'],branch['stem'][1:]))
                assert abs(length-3.5*.62**depth)<1e-4
            assert [depths.count(i) for i in range(3)]==[10,20,40]
            assert colony['produced'][0]>0 and structures[colony['production_tip']]['terminal']
            trunk=structures[0]['stem'];vectors=[[b-a for a,b in zip(p,q)] for p,q in zip(trunk,trunk[1:])]
            cos=sum(a*b for a,b in zip(vectors[0],vectors[-1]))/(math.dist(vectors[0],[0,0,0])*math.dist(vectors[-1],[0,0,0]))
            assert cos<.999,'The main trunk is straight'
            virus=data
    # Break one real tree support. Other organelle roots must remain active.
    damaged=deepcopy(virus)
    for start in damaged['match'].pop('starts'):damaged['bodies'][start['cell']]['owner']=start['faction']
    damaged['spawns']=[];damaged['rules'].pop('exploration',None)
    for building in damaged['content']['buildings']:
        building['growth_seconds']=.1;building['branches'].update(seconds=.1,regrow_seconds=300)
    probe,_=run('tree-damage-probe',damaged,120)
    point=probe['colonies'][0]['structures'][0]['position']
    damaged['spawns']=[dict(type='immune_bomber',position=[point[0],point[1],point[2]+.1])]
    for unit in damaged['content']['units']:
        if unit['name']=='immune_bomber':unit.update(speed=.01,shot_interval=.1,range=1)
    broken,_=run('tree-support-loss',damaged,30)
    tree=broken['colonies'][0]['structures']
    assert tree[0]['wounds']>=3 and all(tree[k]['health']==0 for k in (10,11,30,31,32,33))
    assert all(tree[k]['health']==240 for k in range(1,10)) and broken['colonies'][0]['active']
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
    assert after_scene['meshes'][0]['deformation']['vertices']==neutral_scene['meshes'][0]['deformation']['vertices'],'Immune control did not restore the neutral wall'
    for field,value in [('children',1),('levels',0),('length_scale',1.1),('width_scale',0),('angle_degrees',0),('bend',0)]:
        run('bad-tree-'+field,change(virus,'content.buildings.0.branches.tree.'+field,value),expected=1)
    for field,value in [('roughness',-1),('displacement.features',257),('displacement.features',0),('displacement.width_degrees',5),('displacement.height',0),('displacement.profile','missing')]:
        run('bad-surface-'+field.replace('.','-'),change(virus,'content.factions.0.cell_appearance.'+field,value),expected=1)
    run('tree-capacity',change(virus,'content.buildings.0.branches.count',12),expected=1)
    run('old-surface-pieces',change(virus,'content.factions.0.cell_appearance.surface',dict(model='missing.fmesh')),expected=1)
print(f'Colony forms E2E passed: {count} application runs; fixed wall topology, vertex interpolation, unchanged organelles, valid normals, immune restoration, faction materials, curved forked trees, terminal production, restart, deterministic results and invalid assets.')
