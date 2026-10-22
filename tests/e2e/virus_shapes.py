#!/usr/bin/env python3
"""Check pointed Virus ships and attached spikes through the real application."""
from copy import deepcopy
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from asset_data import stage_world, write_world

root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]

def dot(a,b):return sum(x*y for x,y in zip(a,b))
def origin(instance):return [instance['transform'][k] for k in (3,7,11)]
def loaded_mesh(path,scene,triangles=None):
    raw=path.read_bytes();magic,count,total=struct.unpack_from('<8sII',raw)
    stride=64 if magic==b'FLMESH02' else 48
    rows=[struct.unpack_from('<12f',raw,16+i*stride) for i in range(count)]
    assert total<=100 and any(m['triangles']==total for m in scene['meshes']),path
    if triangles is not None:assert total==triangles,path
    for i in range(total):
        a,b,c,_=struct.unpack_from('<4I',raw,16+count*stride+16*i)
        assert dot(rows[a][4:7],rows[b][4:7])>.9999 and dot(rows[a][4:7],rows[c][4:7])>.9999,'A pointed mesh has smooth normals'
    return [v[:3] for v in rows]

with tempfile.TemporaryDirectory(prefix='nanova_virus_shapes_') as directory:
    out=Path(directory);data=stage_world(binary.parent/'assets/levels/virus-player-level.json',out)
    data['match'].pop('starts');data['match'].update(auto_colonize=False,fully_established=True)
    for i,b in enumerate(data['bodies']):b.update(position=[80+i*20,40,0],owner='virus' if i==0 else 'immune' if i==1 else 'neutral')
    data['rules'].pop('exploration',None)
    for u in data['content']['units']:u.update(manufacture_seconds=10000,colonizes=False)
    data['spawns']=[dict(type='virus_fighter',position=[-2,0,0]),dict(type='virus_bomber',position=[2,0,0])]
    replay=out/'flight.orders';replay.write_text('0 select all\n0 move 10 4 3\n')
    def run(name,world,expected=0):
        wp=out/(name+'_world.json');write_world(wp,world);state=out/(name+'_state.json');scene=out/(name+'_scene.json')
        result=subprocess.run([*command,'--smoke','--world',str(wp),'--world-seed','1234','--ticks','120','--replay',str(replay),'--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10)
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:
            assert result.stderr.strip() and not state.exists();return
        return json.loads(state.read_text()),json.loads(scene.read_text())
    state,scene=run('pointed',data)
    legacy=deepcopy(data);legacy['effects']['spikes'].pop('attached_scale')
    old,old_scene=run('default_envelope',legacy)
    assert old==state,'Spike appearance changed game state'
    assert state['units'][0]['position']!=data['spawns'][0]['position'],'The flight scenario did not move'
    units={u['name']:u for u in data['content']['units']}
    for name in ('virus_fighter','virus_bomber'):
        path=(out/units[name]['model']).resolve();points=loaded_mesh(path,scene)
        spans=[max(v[k] for v in points)-min(v[k] for v in points) for k in range(3)]
        assert spans[2]>1.6*spans[0] and spans[2]>2.5*spans[1],(name,spans)
    payload=loaded_mesh((out/units['virus_bomber']['payload']['model']).resolve(),scene,20)
    assert max(v[2] for v in payload)-min(v[2] for v in payload)>2*(max(v[0] for v in payload)-min(v[0] for v in payload))
    spike=loaded_mesh((out/data['effects']['spikes']['model']).resolve(),scene,48)
    assert max(math.hypot(v[0],v[1]) for v in spike if v[2]>.75)<.026,'The spike has a wide head'
    spike_mesh=next(i for i,m in enumerate(scene['meshes']) if m['triangles']==48)
    attached=[v for v in scene['instances'] if v['mesh']==spike_mesh]
    fighter=next(u for u in state['units'] if u['type']=='virus_fighter')
    craft=next(v for v in scene['instances'] if v['slot']==fighter['slot']);m=craft['transform']
    axes=[[m[row*4+col] for row in range(3)] for col in range(3)]
    envelope=data['effects']['spikes']['attached_scale'];radius=data['effects']['spikes']['root_radius']
    assert len(attached)==fighter['ready_spikes']==data['rules']['spikes']['count']
    for instance in attached:
        offset=[a-b for a,b in zip(origin(instance),origin(craft))]
        local=[dot(offset,axis)/dot(axis,axis) for axis in axes]
        assert abs(math.sqrt(sum((local[k]/envelope[k])**2 for k in range(3)))-radius)<3e-5,'A spike left its hull envelope'
    assert [origin(v) for v in attached]!=[origin(v) for v in old_scene['instances'] if v['mesh']==spike_mesh]
    for i,value in enumerate(([0,1,1],[1,2.1,1],[1,1],[1,1,1,1],'thin')):
        bad=deepcopy(data);bad['effects']['spikes']['attached_scale']=value;run('invalid_'+str(i),bad,1)
print('Virus shapes E2E passed: 7 application runs; pointed loaded meshes, moving hull attachments, unchanged game state, and invalid data.')
