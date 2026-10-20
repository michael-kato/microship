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
    for building in data['content']['buildings']:building.update(growth_seconds=.01,establishment_seconds=.01);building['branches']['seconds']=.01
    data['rules']['ai']['expansion_delay']=300;data['rules']['economy'].update(enemy_attack_seconds=300,manufacture_seconds=1000)
    def run(name,world=data,ticks=600,seed=1234,expected=0):
        global count
        source=out/(name+'_world.json');write_world(source,world);state=out/(name+'_state.json');scene=out/(name+'_scene.json')
        result=subprocess.run([*command,'--smoke','--world',str(source),'--ticks',str(ticks),'--world-seed',str(seed),'--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:assert result.stderr and not state.exists();return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    def check_roots(colony,branch):
        radii=colony['attachment_radii'];indices=colony['root_attachments']
        eligible=sorted((j for j,radius in enumerate(radii) if radius>=branch['root_minimum_radius']),key=lambda j:-radii[j])[:branch['root_limit']]
        assert indices==eligible,(indices,eligible,radii)
        assert colony['root_count']==len(indices) and 1<=len(indices)<=3
        for k in range(colony['root_count']):assert colony['structures'][k]['stem'][0]==colony['attachments'][indices[k]],'A factory missed its large organelle'
    game,scene=run('default');colony=game['colonies'][0];profile=data['content']['buildings'][0];branch=profile['branches'];tree=branch['tree']
    check_roots(colony,branch);assert 9<=len(colony['structures'])<=21<70
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
    # One expendable spore removes one root, then leaves the factory alone to regrow.
    regrow=deepcopy(data)
    for start in regrow['match'].pop('starts'):regrow['bodies'][start['cell']]['owner']=start['faction']
    regrow['match']['opponent']='fungus';regrow['bodies'][1]['owner']='fungus';regrow['spawns']=[];regrow['rules'].pop('exploration',None)
    for b in regrow['content']['buildings']:b['hits']=1;b['branches']['regrow_seconds']=.5
    initial,_=run('regrow_probe',regrow,ticks=120);original=initial['colonies'][0]['structures'];tip=original[0]['position']
    regrow['spawns']=[dict(type='fungal_spore',position=[tip[0],tip[1],tip[2]+.1])]
    for unit in regrow['content']['units']:
        if unit['name']=='fungal_spore':unit.update(range=.4,speed=.01);unit['attack'].update(damage=1000,lifetime=.4,cloud=dict(radius=.25,seconds=.1,damage=.001))
    lost,_=run('lost_root',regrow,ticks=10);assert lost['colonies'][0]['health']==0 and lost['colonies'][0]['active']
    rebuilt,_=run('regrown',regrow,ticks=120);c=rebuilt['colonies'][0];check_roots(c,branch);check_stems(c)
    assert c['structures'][0]['generation']==2 and c['structures'][0]['health']==profile['health']
    assert c['structures'][0]['stem']!=original[0]['stem'],'Regrowth reused the dead branch shape'
    assert c['structures'][1]['stem']==original[1]['stem'] and c['structures'][1]['generation']==1,'An unrelated tree moved'
    stable,_=run('regrown_stable',regrow,ticks=180)
    assert stable['colonies'][0]['structures']==c['structures'],'Regrown branches did not remain stable'
    assert run('regrown_repeat',regrow,ticks=120)[0]==rebuilt
    for path,value in [('root_limit',0),('root_limit',129),('root_minimum_radius',1.4),('tree.branch_probability',1.1),('tree.angle_jitter_degrees',31),('tree.length_jitter',.41),('tree.width_jitter',.31)]:
        run('invalid_'+str(count),change(data,'content.buildings.0.branches.'+path,value),expected=1)
print(f'Natural factory E2E passed: {count} application runs; largest roots, fewer seeded branches, stable mature shapes, new regrowth shapes, connected joints, scale bounds, faction roots, and rejected settings.')
