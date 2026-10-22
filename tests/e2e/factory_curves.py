#!/usr/bin/env python3
"""Measure factory curves and their rendered and navigation paths in the app."""
from copy import deepcopy
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import struct
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

def lobes(points):
    chord=delta(points[-1],points[0]);squared=dot(chord,chord)
    quarter=delta(points[len(points)//4],points[0]);along=dot(quarter,chord)/squared
    axis=[a-along*b for a,b in zip(quarter,chord)];size=norm(axis)
    assert size>1e-6,'An S-curve has no first lobe'
    axis=[v/size for v in axis]
    values=[dot(delta(point,points[0]),axis)/math.sqrt(squared) for point in points]
    return max(values),-min(values)

def start(instance):return [instance['transform'][j] for j in (3,7,11)]
def end(instance):return [instance['transform'][j]+instance['transform'][j-2] for j in (3,7,11)]

def check_pose(instance,a,b,rounded,vertices):
    if rounded:
        low=min(v[1] for v in vertices);high=max(v[1] for v in vertices);middle=(low+high)/2
        center=[x+(y-x)*middle for x,y in zip(start(instance),end(instance))]
        assert math.dist(center,[(x+y)/2 for x,y in zip(a,b)])<3e-5,'A rounded piece left its path center'
        axis=delta(end(instance),start(instance));direction=delta(b,a)
        assert dot(axis,direction)/(norm(axis)*norm(direction))>.99999
        width=norm([instance['transform'][n] for n in (0,4,8)])
        assert norm(axis)*(high-low)>=norm(direction)+.1*width-2e-5,'Rounded pieces leave gaps along the path'
    else:
        assert math.dist(start(instance),a)<3e-5 and math.dist(end(instance),b)<3e-5,'Render path differs from growth path'

def mesh_points(path):
    raw=Path(path).read_bytes();magic,vertices,triangles=struct.unpack_from('<8sII',raw)
    stride=64 if magic==b'FLMESH02' else 48
    return [struct.unpack_from('<3f',raw,16+i*stride) for i in range(vertices)]

def surface_brightness(path,style):
    raw=Path(path).read_bytes();magic,count,_=struct.unpack_from('<8sII',raw)
    stride=64 if magic==b'FLMESH02' else 48
    rgb=[sum(struct.unpack_from('<3f',raw,16+i*stride+32)[k] for i in range(count))/count for k in range(3)]
    tinted=[rgb[k]*(1-style[3])+style[k]*style[3] for k in range(3)]
    return dot(tinted,(.2126,.7152,.0722))

def check_bounds(instance,points,obstacle):
    a=obstacle['start'];direction=delta(obstacle['end'],a);squared=dot(direction,direction)
    matrix=instance['transform']
    for v in points:
        point=[sum(matrix[row*4+j]*v[j] for j in range(3))+matrix[row*4+3] for row in range(3)]
        t=max(0,min(1,dot(delta(point,a),direction)/squared)) if squared else 0
        assert math.dist(point,[x+t*y for x,y in zip(a,direction)])<=obstacle['radius']+3e-5,'Navigation missed visible factory geometry'

def check_tendril_sectors(path):
    # Inspect the actual exported mesh loaded by the successful game run.
    raw=Path(path).read_bytes();magic,count,triangles=struct.unpack_from('<8sII',raw)
    stride=64 if magic==b'FLMESH02' else 48;points=mesh_points(path)
    parent=list(range(count))
    def root(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    for i in range(triangles):
        a,b,c,_=struct.unpack_from('<4I',raw,16+count*stride+i*16)
        parent[root(b)]=root(a);parent[root(c)]=root(a)
    groups={}
    for i,v in enumerate(points):groups.setdefault(root(i),[]).append(v)
    caps=[v for v in groups.values() if min(p[1] for p in v)>.3]
    stalks=[v for v in groups.values() if min(p[1] for p in v)<.1]
    assert len(caps)==len(stalks)==3,'Fungal ends no longer have three separate tendrils'
    centers=[[sum(v[a] for v in group)/len(group) for a in range(3)] for group in caps]
    angles=sorted(math.atan2(v[2],v[0]) for v in centers)
    gaps=[(angles[(i+1)%3]-angles[i])%math.tau for i in range(3)]
    assert all(abs(v-math.tau/3)<.01 for v in gaps),'Tendril ends are unevenly spaced'
    for group in stalks:
        angle=math.atan2(sum(v[2] for v in group),sum(v[0] for v in group))
        # Each tube stays in its own 120 degree sector, including its thickness.
        assert all(abs(math.atan2(math.sin(math.atan2(v[2],v[0])-angle),math.cos(math.atan2(v[2],v[0])-angle)))<math.pi/3 for v in group),'A tendril crossed its neighbor sector'

with tempfile.TemporaryDirectory(prefix='nanova-factory-curves-') as directory:
    # The stress scene keeps authored cells; homes 0, 5 and 10 belong to the three factions.
    out=Path(directory);base=stage_world(root/'assets/levels/three_faction_stress.json',out)
    for key in ('production_seconds','formation_spacing','rally_point','player_ai'):base['match'].pop(key)
    base['spawns']=[];base['rules'].pop('exploration',None)
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
    scores={side:[] for side in ('virus','immune','fungus')};asymmetry={side:[] for side in scores}
    for radius in (2.1,2.7,4.2):
        data=deepcopy(base)
        for cell in (0,5,10):data['bodies'][cell]['radius']=radius
        for seed in (1234,1235,1236):
            game,scene=run('curve_'+str(radius)+'_'+str(seed),data,seed)
            spans=game['factory_path_segments'];assert spans>=32 and game['factory_growth_segments']==4
            instances={v['slot']:v for v in scene['instances']}
            heads=game['unit_capacity']+len(game['colonies']);stems=heads+len(game['colonies'])*128
            for side,cell in (('virus',0),('immune',5),('fungus',10)):
                colony=game['colonies'][cell]
                for k,limb in enumerate(colony['structures']):
                    points=limb['path'];assert len(points)==spans+1
                    assert points[0]==limb['stem'][0] and math.dist(points[-1],limb['position'])<1e-5
                    if limb['parent']>=0:assert math.dist(points[0],colony['structures'][limb['parent']]['stem'][3])<1e-5
                    score=bend_ratio(points);scores[side].append(score)
                    positive,negative=lobes(points)
                    assert min(positive,negative)>.003,(side,'A curve remained C-shaped',positive,negative)
                    asymmetry[side].append(abs(positive-negative)/max(positive,negative))
                    vectors=[delta(b,a) for a,b in zip(points,points[1:])]
                    assert all(dot(v,delta(points[-1],points[0]))>0 for v in vectors),'A curve doubled back'
                    turns=[math.acos(max(-1,min(1,dot(a,b)/(norm(a)*norm(b))))) for a,b in zip(vectors,vectors[1:])]
                    assert max(turns)<math.radians(35),(side,'A curve has a sharp corner',turns)
                    obstacles=[v for v in game['flight']['obstacles'] if v['cell']==cell and v['structure']==k]
                    assert len(obstacles)==spans+1
                    profile=next(v for v in data['content']['buildings'] if v['faction']==side)
                    for j in range(spans):
                        instance=instances[stems+(cell*128+k)*spans+j]
                        check_pose(instance,points[j],points[j+1],profile['growth_meshes'].get('rounded',False),mesh_points(out/profile['growth_meshes']['segment']))
                        if radius==4.2 and seed==1234 and k==0:check_bounds(instance,mesh_points(out/profile['growth_meshes']['segment']),obstacles[j])
                        assert math.dist(obstacles[j]['start'],points[j])<3e-5 and math.dist(obstacles[j]['end'],points[j+1])<3e-5,'Navigation differs from growth path'
                        width=norm([instance['transform'][n] for n in (0,4,8)])
                        assert math.isclose(obstacles[j]['radius'],width*profile['navigation']['stem_radius'],rel_tol=3e-5),'Curve collision width differs'
                    if profile['growth_meshes'].get('length_color'):
                        weights=[instances[stems+(cell*128+k)*spans+j]['style'][3] for j in range(spans)]
                        assert all(a<=b+2e-6 for a,b in zip(weights,weights[1:])),'Length tint reversed along a stem'
                        if limb['parent']>=0:
                            parent_span=stems+(cell*128+limb['parent'])*spans+spans-spans//4-1
                            assert weights[0]>=instances[parent_span]['style'][3]-2e-6,'A child reset the inherited length color'
                        if k==0:
                            assert weights[-1]>weights[0]+.1,'Long stems did not darken'
                            mesh=out/profile['growth_meshes']['segment']
                            near=surface_brightness(mesh,instances[stems+(cell*128+k)*spans]['style'])
                            far=surface_brightness(mesh,instances[stems+(cell*128+k)*spans+spans-1]['style'])
                            assert far<near,'The length tint made the surface lighter'
                            if radius==4.2:assert far<near*.85,'The large stem gradient is too weak'
                            if radius==4.2 and seed==1234:check_tendril_sectors(out/profile['growth_meshes']['tip'])
                    head=instances[heads+cell*128+k];direction=[head['transform'][n] for n in (1,5,9)]
                    assert dot(direction,vectors[-1])/(norm(direction)*norm(vectors[-1]))>.99999,'A head left the curve tangent'
                    if radius==4.2 and seed==1234 and k==0:check_bounds(head,mesh_points(out/profile['growth_meshes']['tip']),obstacles[-1])
            if radius==4.2 and seed==1234:
                assert run('repeat',data)==(game,scene),'Curves changed with the same seed'
    bands={'virus':(.006,.045),'immune':(.035,.23),'fungus':(.07,.45)}
    for side,values in scores.items():
        low,high=bands[side];assert low<min(values)<=max(values)<high,(side,min(values),max(values))
        assert statistics.median(asymmetry[side])>.01,(side,'Seeded curves remained symmetric')
        print(side,'median bow/chord',round(statistics.median(values),4),'median lobe asymmetry',round(statistics.median(asymmetry[side]),4))
    assert statistics.median(scores['virus'])<statistics.median(scores['immune'])<statistics.median(scores['fungus'])
    regular=deepcopy(base)
    for building in regular['content']['buildings']:building['branches']['tree']['curve_jitter']=0
    regular_game,_=run('no_jitter',regular)
    for colony in regular_game['colonies']:
        for limb in colony['structures']:
            positive,negative=lobes(limb['path'])
            assert abs(positive-negative)/max(positive,negative)<.003,'Zero jitter did not restore an even S-curve'
    def check_prefix(game,scene,cell,k,head_present):
        limb=game['colonies'][cell]['structures'][k];spans=game['factory_path_segments']
        heads=game['unit_capacity']+len(game['colonies']);stems=heads+len(game['colonies'])*128
        instances={v['slot']:v for v in scene['instances']}
        shown=[instances[stems+(cell*128+k)*spans+j] for j in range(spans) if stems+(cell*128+k)*spans+j in instances]
        assert len(shown)==math.ceil(limb['visible_growth']*spans-1e-4),'Curve growth used the wrong number of spans'
        assert (heads+cell*128+k in instances)==head_present,'A cut curve kept its head'
        obstacles=[v for v in game['flight']['obstacles'] if v['cell']==cell and v['structure']==k]
        assert len(obstacles)==len(shown)+int(head_present),'Navigation retained an invisible curve span'
        assert math.dist(obstacles[len(shown)-1]['end'],limb['position'])<3e-5,'A partial curve left its growth point'
        rounded=base['content']['buildings'][cell//5]['growth_meshes'].get('rounded',False)
        for instance,obstacle in zip(shown,obstacles):
            check_pose(instance,obstacle['start'],obstacle['end'],rounded,mesh_points(out/base['content']['buildings'][cell//5]['growth_meshes']['segment']))
    partial=deepcopy(base);partial['match']['fully_established']=False
    for building in partial['content']['buildings']:building['branches']['seconds']=.1
    growing,scene=run('partial_growth',partial,ticks=120)
    for cell in (0,5,10):
        children=[k for k,v in enumerate(growing['colonies'][cell]['structures']) if v['health']>0 and 0<v['visible_growth']<1]
        assert children,'The partial-growth case has no growing limb'
        for k in children:check_prefix(growing,scene,cell,k,True)
    # One contact burst cuts the final quarter of each faction's curved root.
    intact,_=run('damage_probe',base)
    no_color=deepcopy(base)
    for building in no_color['content']['buildings']:building['growth_meshes'].pop('length_color',None)
    assert run('color_does_not_change_simulation',no_color)[0]==intact,'Length coloring changed game outcomes'
    damaged=deepcopy(base)
    for building in damaged['content']['buildings']:
        building['hits']=4;building.pop('repair',None);building.pop('haze',None)
    for side,cell in (('virus',0),('immune',5),('fungus',10)):
        root_limb=intact['colonies'][cell]['structures'][0];tip=root_limb['position']
        attacker=deepcopy(next(v for v in damaged['content']['units'] if v['name']=='fungal_spore'))
        attacker.update(name='curve_cutter_'+side,faction='virus' if side=='fungus' else 'fungus',range=.4,speed=.01,targets=['structures'])
        attacker['attack'].update(damage=root_limb['health']/8+.01,lifetime=.4,cloud=dict(radius=.01,seconds=.05,damage=.001))
        damaged['content']['units'].append(attacker)
        damaged['spawns'].extend([dict(type=attacker['name'],position=[tip[0],tip[1],tip[2]+.1])]*2)
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
    for value in (-.01,1.01):
        invalid=deepcopy(base);invalid['content']['buildings'][0]['branches']['tree']['curve_jitter']=value
        run('invalid_jitter_'+str(value),invalid,expected=1)
    missing=deepcopy(base);missing['content']['buildings'][0]['branches']['tree'].pop('curvature')
    run('jitter_without_curve',missing,expected=1)
    for value in (-.01,2.01):
        invalid=deepcopy(base);invalid['content']['buildings'][0]['branches']['tree']['curvature']=value
        run('invalid_'+str(value),invalid,expected=1)
print(f'Factory curves E2E passed: {count} application runs; faction curvature order, opposing S lobes, seeded asymmetry, smooth turns, outward travel, connected forks, shared render/navigation paths, tip tangents, partial growth, quarter damage, repeatability, and rejected settings.')
