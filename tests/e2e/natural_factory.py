#!/usr/bin/env python3
"""Check large-organelle roots, seeded branching, and regrowth in the application."""
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
with tempfile.TemporaryDirectory(prefix='nanova-natural-factory-') as directory:
    out=Path(directory);data=stage_world(root/'assets/levels/virus-player-level.json',out)
    for building in data['content']['buildings']:
        building.pop('repair',None)
        building.update(growth_seconds=.01,establishment_seconds=.01);building['branches']['seconds']=.01
    data['rules']['ai']['expansion_delay']=300;data['rules']['economy'].update(enemy_attack_seconds=300,manufacture_seconds=1000)
    def check_leaf_caps(game,scene):
        instances={item['slot']:item for item in scene['instances']}
        factory_base=game['unit_capacity']+len(game['colonies'])
        stem_base=factory_base+len(game['colonies'])*128
        for p,colony in enumerate(game['colonies']):
            for k,limb in enumerate(colony['structures']):
                leaf=instances.get(factory_base+p*128+k)
                stems=[instances[slot] for slot in range(stem_base+(p*128+k)*4,stem_base+(p*128+k+1)*4) if slot in instances]
                visible=colony['active'] and colony['visible'] and limb['health']>0 and bool(stems) and limb['visible_growth']>=limb['growth']
                assert bool(leaf)==visible,(p,k,'An intact branch has no leaf, or a hidden or cut branch has a leaf')
                if not visible:continue
                # Check actual rendered poses, including partial growth, wounds and tree sway.
                tip=[stems[-1]['transform'][j+1]+stems[-1]['transform'][j+3] for j in (0,4,8)]
                position=[leaf['transform'][j] for j in (3,7,11)]
                assert math.dist(tip,position)<.001,(p,k,'A leaf detached from its stem',tip,position)
                assert leaf['mesh']!=stems[-1]['mesh'],'A stem was used in place of a leaf'
                assert int(leaf['style'][7])&8 and all(int(stem['style'][7])&8 for stem in stems),'A factory part lost its emission mask'
    def run(name,world=data,ticks=600,seed=1234,expected=0):
        global count
        source=out/(name+'_world.json');write_world(source,world);state=out/(name+'_state.json');scene=out/(name+'_scene.json')
        result=subprocess.run([*command,'--smoke','--world',str(source),'--ticks',str(ticks),'--world-seed',str(seed),'--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:assert result.stderr and not state.exists();return None,None
        game=json.loads(state.read_text());render=json.loads(scene.read_text())
        check_leaf_caps(game,render)
        return game,render
    def check_roots(colony,branch):
        radii=colony['attachment_radii'];indices=colony['root_attachments']
        eligible=sorted((j for j,radius in enumerate(radii) if radius>=branch['root_minimum_radius']),key=lambda j:-radii[j])[:branch['root_limit']]
        assert indices==eligible,(indices,eligible,radii)
        assert colony['root_count']==len(indices) and 1<=len(indices)<=3
        for k in range(colony['root_count']):assert colony['structures'][k]['stem'][0]==colony['attachments'][indices[k]],'A factory missed its large organelle'
    game,scene=run('default');colony=game['colonies'][0];profile=data['content']['buildings'][0];branch=profile['branches'];tree=branch['tree']
    check_roots(colony,branch);assert 9<=len(colony['structures'])<=21<70
    assert colony['fighter_percent']==80 and colony['ratio_enabled'],'New colonies do not use the 80/20 mix'
    # Disable the layout rotation with the same seed. Anchors must move without
    # changing organ sizes or the branch-to-organ connection.
    aligned=deepcopy(data)
    for body in aligned['content']['bodies']:body['variation']['layout_rotation_degrees']=0
    old,_=run('aligned_organs',aligned);aligned_home=old['colonies'][0]
    assert aligned_home['attachment_radii']==colony['attachment_radii']
    assert max(math.dist(a,b) for a,b in zip(aligned_home['attachments'],colony['attachments']))>.1
    check_roots(aligned_home,branch)
    invalid=deepcopy(data);invalid['content']['bodies'][0]['variation']['layout_rotation_degrees']=181
    run('invalid_rotation',invalid,expected=1)
    def check_stems(colony):
        for k,limb in enumerate(colony['structures']):
            depth=0;parent=limb['parent']
            if parent>=0:assert limb['stem'][0]==colony['structures'][parent]['stem'][3],'A branch joint opened'
            while parent>=0:depth+=1;parent=colony['structures'][parent]['parent']
            children=sum(child['parent']==k for child in colony['structures'])
            low=k==colony['root_count']  # The first child of the first root stays a low leaf.
            assert children==(0 if depth==2 or low else max(1,children)) and children<=2
            assert limb['terminal']==(children==0)
            length=sum(math.dist(a,b) for a,b in zip(limb['stem'],limb['stem'][1:]));nominal=profile['height']*tree['length_scale']**depth
            assert nominal*(1-tree['length_jitter'])-1e-4<=length<=nominal*(1+tree['length_jitter'])+1e-4
            width=tree['width_scale']**depth
            assert width*(1-tree['width_jitter'])-1e-5<=limb['scale']<=width*(1+tree['width_jitter'])+1e-5
    check_stems(colony)
    assert run('repeat')==(game,scene),'A fixed seed changed the factory'
    later,_=run('later',ticks=660);assert later['colonies'][0]['structures']==colony['structures'],'Completed branches changed shape'
    counts={len(colony['structures'])}
    for seed in (1235,1236):
        other,_=run('seed_'+str(seed),seed=seed);c=other['colonies'][0];check_roots(c,branch);check_stems(c)
        counts.add(len(c['structures']));assert c['structures']!=colony['structures']
    assert len(counts)>1,'Branch count did not vary with the seed'
    changed=deepcopy(data);changed['content']['buildings'][0]['branches']['seed']+=1
    other,_=run('branch_seed',changed);assert other['colonies'][0]['attachments']==colony['attachments']
    assert other['colonies'][0]['structures']!=colony['structures'],'The branch seed was ignored'
    sparse=change(data,'content.buildings.0.branches.tree.branch_probability',0);c=run('single_children',sparse)[0]['colonies'][0]
    assert len(c['structures'])==c['root_count']*3-1
    full=change(data,'content.buildings.0.branches.tree.branch_probability',1);c=run('full_children',full)[0]['colonies'][0]
    assert len(c['structures'])==c['root_count']*7-2
    small=change(data,'bodies.0.radius',2.1);c=run('small_cell',small)[0]['colonies'][0];check_roots(c,branch);assert c['root_count']==2
    for side,faction in [('immune',2),('fungus',3)]:
        d=stage_world(root/'assets/levels'/(side+'-player-level.json'),out)
        for start in d['match'].pop('starts'):d['bodies'][start['cell']]['owner']=start['faction']
        d['spawns']=[];d['rules']['ai']['expansion_delay']=300;d['rules']['economy'].update(enemy_attack_seconds=300,manufacture_seconds=1000)
        state,_=run(side,d,ticks=120);home=1 if side=='immune' else 0;c=state['colonies'][home]
        definition=next(b for b in d['content']['buildings'] if b['faction']==side)
        check_roots(c,definition['branches']);assert len(c['structures'])==9
    # One expendable attacker removes one root, then leaves each faction to regrow.
    for side in ('virus','immune','fungus'):
        regrow=deepcopy(data);enemy='virus' if side=='fungus' else 'fungus'
        regrow['match'].pop('starts');regrow['match'].update(player=side,opponent=enemy)
        regrow['bodies'][0]['owner']=side;regrow['bodies'][1]['owner']=enemy
        regrow['spawns']=[];regrow['rules'].pop('exploration',None)
        for b in regrow['content']['buildings']:
            b.pop('repair',None);b['hits']=1;b['branches']['regrow_seconds']=.5
        definition=next(b for b in regrow['content']['buildings'] if b['faction']==side)
        growing=deepcopy(regrow)
        for building in growing['content']['buildings']:building['growth_seconds']=3
        partial,_=run(side+'_growing_leaves',growing,ticks=60)
        assert any(limb['health']>0 and 0<limb['visible_growth']<1 for limb in partial['colonies'][0]['structures']),side
        initial,_=run(side+'_regrow_probe',regrow,ticks=120)
        original=initial['colonies'][0]['structures'];tip=original[0]['position']
        attacker=deepcopy(next(u for u in regrow['content']['units'] if u['name']=='fungal_spore'))
        attacker.update(name='root_breaker',faction=enemy,range=.4,speed=.01)
        attacker['attack'].update(damage=1000,lifetime=.4,cloud=dict(radius=.25,seconds=.1,damage=.001))
        regrow['content']['units'].append(attacker)
        regrow['spawns']=[dict(type='root_breaker',position=[tip[0],tip[1],tip[2]+.1])]
        damaged=deepcopy(regrow)
        for building in damaged['content']['buildings']:building['hits']=4
        damaged['content']['units'][-1]['attack']['damage']=definition['health']/4+.01
        wounded,_=run(side+'_damaged_leaf',damaged,ticks=10)
        root=wounded['colonies'][0]['structures'][0]
        assert root['health']>0 and root['wounds']==1 and root['visible_growth']==.75,side
        lost,_=run(side+'_lost_root',regrow,ticks=10)
        assert lost['colonies'][0]['health']==0 and lost['colonies'][0]['active'],side
        rebuilt,_=run(side+'_regrown',regrow,ticks=120);c=rebuilt['colonies'][0]
        check_roots(c,definition['branches'])
        if side=='virus':check_stems(c)
        for limb in c['structures']:
            if limb['parent']>=0:assert limb['stem'][0]==c['structures'][limb['parent']]['stem'][3],side
        assert c['structures'][0]['generation']==2 and c['structures'][0]['health']==definition['health'],side
        assert c['structures'][0]['stem']!=original[0]['stem'],(side,'Regrowth reused the dead branch shape')
        for k in range(1,c['root_count']):
            assert c['structures'][k]['stem']==original[k]['stem'] and c['structures'][k]['generation']==1,(side,'An unrelated root moved')
        stable,_=run(side+'_regrown_stable',regrow,ticks=180)
        assert stable['colonies'][0]['structures']==c['structures'],(side,'Regrown branches did not remain stable')
        assert run(side+'_regrown_repeat',regrow,ticks=120)[0]==rebuilt,side
    for path,value in [('root_limit',0),('root_limit',129),('root_minimum_radius',1.4),('tree.branch_probability',1.1),('tree.angle_jitter_degrees',31),('tree.length_jitter',.41),('tree.width_jitter',.31)]:
        run('invalid_'+str(count),change(data,'content.buildings.0.branches.'+path,value),expected=1)
print(f'Natural factory E2E passed: {count} application runs; largest roots, fewer seeded branches, stable mature shapes, new regrowth shapes, connected joints, scale bounds, faction roots, attached leaves on every intact stem, and rejected settings.')
