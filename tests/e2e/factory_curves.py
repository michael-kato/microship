#!/usr/bin/env python3
"""Measure factory curves and their rendered and navigation paths in the app."""
from copy import deepcopy
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
from asset_data import stage_world, write_world

root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0

def delta(a,b):return [x-y for x,y in zip(a,b)]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def norm(v):return math.sqrt(dot(v,v))
def bend_ratio(points):
    chord=delta(points[-1],points[0]);squared=dot(chord,chord)
    assert squared>1e-8,'A curve has no extent'
    return max(norm([v-t*c for v,c in zip(delta(p,points[0]),chord)]) for p in points for t in [dot(delta(p,points[0]),chord)/squared])/math.sqrt(squared)

def start(instance):return [instance['transform'][j] for j in (3,7,11)]
def end(instance):return [instance['transform'][j]+instance['transform'][j-2] for j in (3,7,11)]

with tempfile.TemporaryDirectory(prefix='nanova-factory-curves-') as directory:
    out=Path(directory);base=stage_world(root/'assets/levels/virus_large_level.json',out)
    for placement in base['match'].pop('starts'):base['bodies'][placement['cell']]['owner']=placement['faction']
    base['match']['fully_established']=True;base['spawns']=[];base['rules'].pop('exploration',None)
    base['rules']['ai']['expansion_delay']=300;base['rules']['economy']['enemy_attack_seconds']=300
    base['appearance']['animation']['factories']['sway_degrees']=0
    for unit in base['content']['units']:unit['manufacture_seconds']=10000
    def run(name,data,seed=1234,expected=0,ticks=2):
        global count
        world=out/(name+'_world.json');write_world(world,data)
        state=out/(name+'_state.json');scene=out/(name+'_scene.json')
        result=subprocess.run([*command,'--smoke','--world',str(world),'--world-seed',str(seed),'--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:assert not state.exists();return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    scores={side:[] for side in ('virus','immune','fungus')}
    for radius in (2.1,2.7,4.2):
        data=deepcopy(base)
        for cell in (0,5,10):data['bodies'][cell]['radius']=radius
        for seed in (1234,1235,1236):
            game,scene=run('curve_'+str(radius)+'_'+str(seed),data,seed)
            spans=game['factory_path_segments'];assert spans>=12 and game['factory_growth_segments']==4
            instances={v['slot']:v for v in scene['instances']}
            heads=game['unit_capacity']+len(game['colonies']);stems=heads+len(game['colonies'])*128
            for side,cell in (('virus',0),('immune',5),('fungus',10)):
                colony=game['colonies'][cell]
                for k,limb in enumerate(colony['structures']):
                    points=limb['path'];assert len(points)==spans+1
                    assert points[0]==limb['stem'][0] and math.dist(points[-1],limb['position'])<1e-5
                    if limb['parent']>=0:assert math.dist(points[0],colony['structures'][limb['parent']]['stem'][3])<1e-5
                    score=bend_ratio(points);scores[side].append(score)
                    vectors=[delta(b,a) for a,b in zip(points,points[1:])]
                    assert all(dot(v,delta(points[-1],points[0]))>0 for v in vectors),'A curve doubled back'
                    turns=[math.acos(max(-1,min(1,dot(a,b)/(norm(a)*norm(b))))) for a,b in zip(vectors,vectors[1:])]
                    assert max(turns)<math.radians(16),(side,'A curve has a sharp corner',turns)
                    obstacles=[v for v in game['flight']['obstacles'] if v['cell']==cell and v['structure']==k]
                    assert len(obstacles)==spans+1
                    profile=next(v for v in data['content']['buildings'] if v['faction']==side)
                    for j in range(spans):
                        instance=instances[stems+(cell*128+k)*spans+j]
                        assert math.dist(start(instance),points[j])<3e-5 and math.dist(end(instance),points[j+1])<3e-5,'Render path differs from growth path'
                        assert math.dist(obstacles[j]['start'],points[j])<3e-5 and math.dist(obstacles[j]['end'],points[j+1])<3e-5,'Navigation differs from growth path'
                        width=norm([instance['transform'][n] for n in (0,4,8)])
                        assert math.isclose(obstacles[j]['radius'],width*profile['navigation']['stem_radius'],rel_tol=3e-5),'Curve collision width differs'
                    head=instances[heads+cell*128+k];direction=[head['transform'][n] for n in (1,5,9)]
                    assert dot(direction,vectors[-1])/(norm(direction)*norm(vectors[-1]))>.99999,'A head left the curve tangent'
            if radius==4.2 and seed==1234:
                assert run('repeat',data)==(game,scene),'Curves changed with the same seed'
    bands={'virus':(.02,.05),'immune':(.1,.22),'fungus':(.25,.45)}
    for side,values in scores.items():
        low,high=bands[side];assert low<min(values)<=max(values)<high,(side,min(values),max(values))
        print(side,'median bow/chord',round(statistics.median(values),4))
    def check_prefix(game,scene,cell,k,head_present):
        limb=game['colonies'][cell]['structures'][k];spans=game['factory_path_segments']
        heads=game['unit_capacity']+len(game['colonies']);stems=heads+len(game['colonies'])*128
        instances={v['slot']:v for v in scene['instances']}
        shown=[instances[stems+(cell*128+k)*spans+j] for j in range(spans) if stems+(cell*128+k)*spans+j in instances]
        assert len(shown)==math.ceil(limb['visible_growth']*spans-1e-4),'Curve growth used the wrong number of spans'
        assert math.dist(end(shown[-1]),limb['position'])<3e-5,'A partial curve left its growth point'
        assert (heads+cell*128+k in instances)==head_present,'A cut curve kept its head'
        obstacles=[v for v in game['flight']['obstacles'] if v['cell']==cell and v['structure']==k]
        assert len(obstacles)==len(shown)+int(head_present),'Navigation retained an invisible curve span'
        for instance,obstacle in zip(shown,obstacles):
            assert math.dist(start(instance),obstacle['start'])<3e-5 and math.dist(end(instance),obstacle['end'])<3e-5
    partial=deepcopy(base);partial['match']['fully_established']=False
    for building in partial['content']['buildings']:building['branches']['seconds']=.1
    growing,scene=run('partial_growth',partial,ticks=120)
    for cell in (0,5,10):
        children=[k for k,v in enumerate(growing['colonies'][cell]['structures']) if v['health']>0 and 0<v['visible_growth']<1]
        assert children,'The partial-growth case has no growing limb'
        for k in children:check_prefix(growing,scene,cell,k,True)
    # One contact burst cuts the final quarter of each faction's curved root.
    intact,_=run('damage_probe',base);damaged=deepcopy(base)
    for building in damaged['content']['buildings']:
        building['hits']=4;building.pop('repair',None);building.pop('haze',None)
    for side,cell in (('virus',0),('immune',5),('fungus',10)):
        root_limb=intact['colonies'][cell]['structures'][0];tip=root_limb['position']
        attacker=deepcopy(next(v for v in damaged['content']['units'] if v['name']=='fungal_spore'))
        attacker.update(name='curve_cutter_'+side,faction='virus' if side=='fungus' else 'fungus',range=.4,speed=.01,targets=['structures'])
        attacker['attack'].update(damage=root_limb['health']/4+.01,lifetime=.4,cloud=dict(radius=.01,seconds=.05,damage=.001))
        damaged['content']['units'].append(attacker)
        damaged['spawns'].append(dict(type=attacker['name'],position=[tip[0],tip[1],tip[2]+.1]))
    cut,scene=run('cut_quarter',damaged,ticks=10)
    for cell in (0,5,10):
        limb=cut['colonies'][cell]['structures'][0]
        assert limb['health']>0 and limb['wounds']==1 and limb['visible_growth']==.75,'Damage no longer removes one quarter'
        assert math.dist(limb['position'],limb['stem'][3])<2e-5
        check_prefix(cut,scene,cell,0,False)
    maximum=deepcopy(base)
    for building in maximum['content']['buildings']:building['branches']['tree']['curvature']=2
    extreme,_=run('maximum_curve',maximum)
    assert all(math.isfinite(x) for c in extreme['colonies'] for v in c['structures'] for point in v['path'] for x in point)
    straight=deepcopy(base)
    for building in straight['content']['buildings']:building['branches']['tree']['curvature']=0
    game,_=run('straight',straight)
    assert all(bend_ratio(v['path'])<1e-4 for c in game['colonies'] for v in c['structures']),'Zero curvature did not make a straight path'
    for value in (-.01,2.01):
        invalid=deepcopy(base);invalid['content']['buildings'][0]['branches']['tree']['curvature']=value
        run('invalid_'+str(value),invalid,expected=1)
print(f'Factory curves E2E passed: {count} application runs; faction curvature order, smooth turns, outward travel, connected forks, shared render/navigation paths, tip tangents, partial growth, quarter damage, repeatability, and rejected settings.')
