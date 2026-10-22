#!/usr/bin/env python3
"""Check distributed fungal sources and their shared output in the application."""
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
with tempfile.TemporaryDirectory(prefix='nanovore-fungal-sources-') as directory:
    out=Path(directory);base=stage_world(root/'assets/levels/fungus-player-level.json',out)
    for start in base['match'].pop('starts'):base['bodies'][start['cell']]['owner']=start['faction']
    base['spawns']=[];base['rules'].pop('exploration',None)
    base['rules']['ai']['expansion_delay']=300;base['rules']['economy']['enemy_attack_seconds']=300
    for building in base['content']['buildings']:
        building.update(growth_seconds=.01,sprout_seconds=.01,establishment_seconds=.01)
        building['branches'].update(seconds=.01,root_offset_seconds=0)
    for unit in base['content']['units']:
        unit.update(manufacture_seconds=10000,colonizes=False);unit.pop('colonization',None);unit.pop('carrier',None)
        if unit['name']=='fungal_spore':unit['manufacture_seconds']=.5;unit['attack']['lifetime']=.2
    def fungus(data):return next(b for b in data['content']['buildings'] if b['faction']=='fungus')
    def run(name,data,ticks=120,expected=0):
        global count
        world=out/(name+'_world.json');write_world(world,data);state=out/(name+'_state.json')
        result=subprocess.run([*command,'--smoke','--world',str(world),'--world-seed','1234','--ticks',str(ticks),'--state',str(state)],capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        return json.loads(state.read_text()) if not expected else None
    for radius in (.6,2.1,4.2):
        data=deepcopy(base);data['bodies'][0]['radius']=radius
        early=run('start_'+str(radius),data);later=run('minute_'+str(radius),data,18120)
        a=early['colonies'][0];b=later['colonies'][0];roots=b['large_root_count']
        leaves=[k for k,s in enumerate(b['structures']) if s['health']>0 and s['visible_growth']==1]
        budget=next(v['leaves'] for v in fungus(data)['branches']['production_leaf_limits'] if v['roots']==roots)
        assert len(leaves)>budget and all(b['leaf_births'][k]>a['leaf_births'][k] for k in leaves),(radius,roots,leaves,b['leaf_births'])
        actual=sum(b['produced'])-sum(a['produced']);expected=budget*600
        # A job completes on a simulation tick. Fractional factory rates can
        # lose less than one tick per completion, plus one partial job per tip.
        rounding=math.ceil(expected/(.5*60))+len(leaves)
        assert expected-rounding<=actual<=expected+len(leaves),(radius,actual,expected,rounding)
        assert not a['capacity_blocked'] and not b['capacity_blocked']
        assert math.isclose(b['production'],1+.12*(9-roots),abs_tol=.00001),(radius,b['production'])
        chains=[sum(s['parent']==k for s in b['structures']) for k in range(roots)]
        assert b['root_count']==len(b['attachments'])
        assert all(b['leaf_births'][k]>a['leaf_births'][k] for k in range(roots,b['root_count']))
        assert min(chains)>0 and max(chains)-min(chains)<=1,(radius,chains)
        assert chains==[2]*roots,(radius,'A large trunk did not fork',chains)
        for k in range(roots):
            children=[i for i,v in enumerate(b['structures']) if v['parent']==k]
            assert all(sum(v['parent']==i for v in b['structures'])==2 for i in children),'A fork did not repeat'
        for k in range(roots,b['root_count']):
            assert sum(v['parent']==k for v in b['structures'])==1,'A small root has no fork'
        lengths=[sum(math.dist(a,b) for a,b in zip(v['stem'],v['stem'][1:])) for v in b['structures'][b['root_count']:]]
        assert max(lengths)<3,(radius,'Fungal side branches are too long',lengths)

        assert later['faction_stats'][2]['built']==sum(b['produced'])
        print(f'Radius {radius}: {roots} roots, {len(leaves)} producing tips, {actual} spores/5 minutes, expected {expected}.')
    # Measure sibling fan width against the earlier spread setting in the application.
    wide=run('fan_wide',base)['colonies'][0];narrow_data=deepcopy(base)
    fungus(narrow_data)['branches']['tree']['angle_degrees']=20
    narrow=run('fan_narrow',narrow_data)['colonies'][0]
    def fan_width(colony):
        total=0
        for k in range(colony['large_root_count']):
            tips=[v['position'] for v in colony['structures'] if v['parent']==k]
            total+=max((math.dist(a,b) for a in tips for b in tips),default=0)
        return total
    assert fan_width(wide)>1.2*fan_width(narrow),(fan_width(wide),fan_width(narrow))
    for field,value in [('chain_length',0),('chain_length',129),('chain_length',1.5),('spread_scale',0),('spread_scale',3.01),('production_reference_count',0),('production_leaf_limits',[dict(roots=3,leaves=0)]),('production_leaf_limits',[dict(roots=3,leaves=4)]*2),('production_leaf_limits',[dict(roots=3,leaves=4,establishing_leaves=0)])]:
        bad=deepcopy(base);fungus(bad)['branches'][field]=value
        run('invalid_'+str(count),bad,expected=1)
print(f'Fungal sources E2E passed: {count} application runs; distributed roots, more producing tips, retained mature output and nutrient income, and rejected settings.')
