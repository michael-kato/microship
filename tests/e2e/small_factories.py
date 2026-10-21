#!/usr/bin/env python3
"""Check productive miniature factories in complete application scenarios."""
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world

root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-small-factories-') as directory:
    out=Path(directory)
    def run(name,data,ticks=600,expected=0,seed=1234):
        global count
        world=out/(name+'_world.json');state=out/(name+'_state.json');scene=out/(name+'_scene.json');write_world(world,data)
        result=subprocess.run([*command,'--smoke','--world',str(world),'--world-seed',str(seed),'--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        return (json.loads(state.read_text()),json.loads(scene.read_text())) if not expected else (None,None)
    def check_directions(colony,center,side,radius):
        origin=colony['factory_origin'];centers=colony['attachment_centers']
        assert math.dist(origin,center)>.01
        for k in range(colony['root_count']):
            organ=colony['root_attachments'][k];aim=[a-b for a,b in zip(centers[organ],origin)]
            if math.dist(aim,[0,0,0])<.0001:
                aim=[a-b for a,b in zip(colony['attachments'][organ],origin)]
            stem=colony['structures'][k]['stem'];direction=[a-b for a,b in zip(stem[-1],stem[0])]
            cosine=sum(a*b for a,b in zip(aim,direction))/(math.dist(aim,[0,0,0])*math.dist(direction,[0,0,0]))
            assert cosine>math.cos(math.radians(30)),(side,radius,k,'root left the nucleus-to-organelle route',cosine)
            assert all(sum((b-a)*v for a,b,v in zip(start,end,aim))>0 for start,end in zip(stem,stem[1:])),(side,k,'root turned inward')
    for side,home in [('virus',0),('immune',1),('fungus',0)]:
        data=stage_world(root/'assets/levels'/(side+'-player-level.json'),out)
        for start in data['match'].pop('starts'):data['bodies'][start['cell']]['owner']=start['faction']
        data['spawns']=[];data['rules'].pop('exploration',None)
        data['rules']['cell_infection'].update(morph_seconds=.01,tint_seconds=.01)
        data['rules']['ai']['expansion_delay']=300;data['rules']['economy']['enemy_attack_seconds']=300
        for building in data['content']['buildings']:
            building.update(growth_seconds=.01,sprout_seconds=.01,establishment_seconds=.01)
            building['branches'].update(seconds=.01,root_offset_seconds=0)
        for unit in data['content']['units']:unit.update(manufacture_seconds=.005,cost=.001,speed=.01,colonizes=False)
        profile=next(b for b in data['content']['buildings'] if b['faction']==side);branch=profile['branches']
        for radius,expected_roots in [(2.1,4),(2.7,6),(4.2,10)]:
            case=deepcopy(data);case['bodies'][home]['radius']=radius
            game,scene=run(side+'_'+str(radius),case);colony=game['colonies'][home]
            roots=colony['root_count'];large=colony['large_root_count'];indices=colony['root_attachments'];radii=colony['attachment_radii']
            assert roots==len(colony['attachments'])==expected_roots and 0<large<roots
            assert sorted(indices)==list(range(roots)),'A visible organelle has no factory'
            assert all(colony['leaf_births'][k]>0 for k in range(large,roots)),(side,radius,colony['leaf_births'])
            assert all(colony['leaf_births'][k]>0 for k,s in enumerate(colony['structures']) if s['health']>0 and s['visible_growth']==1),(side,radius,colony['leaf_births'])
            check_directions(colony,case['bodies'][home]['position'],side,radius)
            for k in range(large,roots):
                children=[i for i,v in enumerate(colony['structures']) if v['parent']==k]
                assert len(children)==1,(side,radius,k,'A small factory has no child branch')
                child=colony['structures'][children[0]];trunk=colony['structures'][k]
                assert child['stem'][0]==trunk['stem'][3],'A small branch joint opened'
                assert child['scale']<trunk['scale'],'A small branch did not taper'
                assert math.isclose(trunk['scale']/trunk['head_scale'],branch['additional_root_width_scale'],abs_tol=2e-5),(side,k,'Small trunk width was not applied')
                assert math.dist(child['position'],trunk['position'])>.03,'A small fork is hidden by its trunk'
            for k in range(large):
                children=[i for i,v in enumerate(colony['structures']) if v['parent']==k]
                assert len(children)==2,(side,radius,k,'A large trunk did not split')
                assert any(sum(v['parent']==i for v in colony['structures'])>=1 for i in children),'A large fork did not repeat'
            for k,limb in enumerate(colony['structures']):
                if limb['parent']<0:continue
                parent=colony['structures'][limb['parent']]
                assert limb['stem'][0]==parent['stem'][3],'A fork joint opened'
                assert limb['scale']<parent['scale'],'A fork did not taper'
            membrane=scene['meshes'][home]['deformation'];wall={i for t in membrane['indices'] if t[3]==3 for i in t[:3]}
            actual_radius=max(math.dist(membrane['vertices'][i][:3],case['bodies'][home]['position']) for i in wall)
            assert all(math.dist(v['position'],case['bodies'][home]['position'])>actual_radius for v in colony['structures'] if v['health']>0)
            instances={v['slot']:v for v in scene['instances']};head_base=game['unit_capacity']+len(game['colonies']);stem_base=head_base+len(game['colonies'])*128
            for k in range(roots):
                limb=colony['structures'][k];scale=limb['factory_scale']
                expected_scale=1 if k<large else branch['additional_root_scale']*min(1,radii[indices[k]]/min(radii[j] for j in indices[:large]))
                assert math.isclose(scale,expected_scale,abs_tol=1e-6),(side,k,scale,expected_scale)
                assert limb['parent']==-1 and limb['stem'][0]==colony['attachments'][indices[k]]
                length=sum(math.dist(a,b) for a,b in zip(limb['stem'],limb['stem'][1:]));nominal=branch.get('root_height',profile['height'])*scale
                jitter=branch.get('tree',{}).get('length_jitter',0)
                if not branch.get('spread_all_roots'):assert length>=nominal*(1-jitter)-1e-4
                assert math.dist(limb['position'],case['bodies'][home]['position'])>=colony['surface_radius']+branch['surface_clearance']-1e-4
                assert math.isclose(limb['health'],profile['health']*scale*scale,rel_tol=2e-5)
                head=instances[head_base+home*128+k];stem=instances[stem_base+(home*128+k)*4+3]
                tip=[stem['transform'][j+1]+stem['transform'][j+3] for j in (0,4,8)]
                assert math.dist(tip,[head['transform'][j] for j in (3,7,11)])<.001,'A rendered factory head detached'
                head_width=math.sqrt(sum(head['transform'][j]**2 for j in (0,4,8)))
                assert math.isclose(head_width,profile['width']*limb['head_scale'],rel_tol=2e-5)
                obstacles=[o for o in game['flight']['obstacles'] if o['cell']==home and o['structure']==k]
                has_head=True
                assert len(obstacles)==4+has_head,(side,k,obstacles)
                if has_head:assert math.isclose(obstacles[-1]['radius'],head_width*profile['navigation']['head_radius'],rel_tol=2e-5)
                stem_width=math.sqrt(sum(stem['transform'][j]**2 for j in (0,4,8)))
                assert math.isclose(obstacles[3]['radius'],stem_width*profile['navigation']['stem_radius'],rel_tol=2e-5),'Navigation missed the wider trunk'
            if radius==4.2:
                # A spore removes one small root. Other roots must survive; the small root must regrow.
                target=max(range(large,roots),key=lambda k:math.dist(colony['structures'][k]['position'],case['bodies'][home]['position']));original=colony['structures'][target];tip=original['position'];damaged=deepcopy(case)
                for b in damaged['content']['buildings']:
                    b.pop('repair',None);b.pop('haze',None);b['branches']['regrow_seconds']=.5
                for u in damaged['content']['units']:u['manufacture_seconds']=10000
                enemy=damaged['match']['opponent']
                attacker=deepcopy(next(u for u in damaged['content']['units'] if u['name']=='fungal_spore'))
                attacker.update(name='small_root_breaker',faction=enemy,range=2,speed=.01,targets=['structures'])
                attacker['attack'].update(damage=original['health']+.01,lifetime=.2,cloud=dict(radius=.01,seconds=.05,damage=.001))
                damaged['content']['units'].append(attacker);damaged['spawns']=[dict(type=attacker['name'],position=[tip[0],tip[1],tip[2]+.01])]
                lost,_=run(side+'_small_lost',damaged,ticks=10);rebuilt,_=run(side+'_small_regrown',damaged,ticks=120)
                a=lost['colonies'][home];b=rebuilt['colonies'][home]
                assert a['active'] and a['structures'][target]['health']==0,(side,a['structures'][target])
                child=next(k for k,v in enumerate(colony['structures']) if v['parent']==target)
                assert a['structures'][child]['health']==0,'A small branch survived a lost support'
                assert b['structures'][child]['health']>0 and b['structures'][child]['generation']==2,'A small branch failed to regrow'
                assert b['structures'][target]['generation']==2 and b['structures'][target]['health']>0
                assert b['structures'][target]['factory_scale']==original['factory_scale']
                assert b['structures'][target]['stem'][0]==colony['attachments'][indices[target]]
                assert all(v['generation']==1 and v['health']>0 for k,v in enumerate(b['structures'][:roots]) if k!=target)
        for seed in (1235,1236):
            varied,draw=run(side+'_surface_seed_'+str(seed),case,seed=seed);c=varied['colonies'][home]
            check_directions(c,case['bodies'][home]['position'],side,4.2)
            membrane=draw['meshes'][home]['deformation'];wall={i for t in membrane['indices'] if t[3]==3 for i in t[:3]}
            surface=max(math.dist(membrane['vertices'][i][:3],case['bodies'][home]['position']) for i in wall)
            assert all(math.dist(v['position'],case['bodies'][home]['position'])>surface for v in c['structures'])
            assert all(c['leaf_births'][k]>0 for k in range(len(c['structures'])))
            assert all(c['structures'][k]['stem'][0]==c['attachments'][c['root_attachments'][k]] for k in range(c['root_count']))
        # Force a large root along the branch-plane normal. A local split must
        # still separate its two children instead of rotating them onto one line.
        parallel=deepcopy(case);organ=colony['root_attachments'][0]
        aim=[a-b for a,b in zip(colony['attachment_centers'][organ],colony['factory_origin'])]
        if math.dist(aim,[0,0,0])<.0001:aim=[a-b for a,b in zip(colony['attachments'][organ],colony['factory_origin'])]
        aim=[v/math.dist(aim,[0,0,0]) for v in aim]
        for building in parallel['content']['buildings']:
            building['growth_plane']['normal']=aim
            building['branches']['tree'].update(bend=.05,angle_jitter_degrees=0)
        axis,_=run(side+'_parallel_fork',parallel);c=axis['colonies'][home]
        children=[v for v in c['structures'] if v['parent']==0]
        directions=[[b-a for a,b in zip(v['stem'][0],v['stem'][-1])] for v in children]
        cosine=sum(a*b for a,b in zip(*directions))/(math.dist(directions[0],[0,0,0])*math.dist(directions[1],[0,0,0]))
        assert cosine<math.cos(math.radians(20)),(side,'Parallel-axis forks collapsed',cosine)
        assert all(math.isfinite(x) for v in c['structures'] for point in v['stem'] for x in point)
        for value in (.99,4.01):
            bad=deepcopy(data)
            for b in bad['content']['buildings']:b['branches']['additional_root_width_scale']=value
            run(side+'_invalid_width_'+str(value),bad,expected=1)
        bad=deepcopy(data)
        for b in bad['content']['buildings']:b['branches'].pop('additional_root_scale')
        run(side+'_missing_small_scale',bad,expected=1)
        bad=deepcopy(data)
        for b in bad['content']['buildings']:b['branches']['additional_root_scale']=.51
        run(side+'_invalid_scale',bad,expected=1)
        for b in bad['content']['buildings']:b['branches'].update(additional_root_scale=.35,count=128,grammar='fan');b['branches'].pop('tree',None)
        run(side+'_overflow',bad,expected=1)
print(f'Small factories E2E passed: {count} application runs; all factions, all active organelles, scaled meshes and navigation, production, damage, regrowth, and capacity rejection.')
