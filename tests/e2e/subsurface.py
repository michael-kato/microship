#!/usr/bin/env python3
"""Check scattering materials and validation in the real Windows application."""
from copy import deepcopy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-subsurface-') as directory:
    out=Path(directory);data=stage_world(root/'assets/levels/virus-player-level.json',out)
    settings=json.loads((root/'assets/rendering/renderer.json').read_text())
    def run(name,world=data,config=settings,expected=0,ticks=1):
        global count
        source=out/(name+'-world.json');write_world(source,world);render=out/(name+'-render.json');render.write_text(json.dumps(config))
        state=out/(name+'-state.json');scene=out/(name+'-scene.json')
        p=subprocess.run([*command,'--smoke','--world',str(source),'--config',str(render),'--ticks',str(ticks),'--world-seed','1234','--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        if expected:assert p.stderr and not state.exists();return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    initial,scene=run('materials')
    assert all(m['subsurface_weight'][0]>0 for m in scene['meshes'][:len(data['bodies'])]),'A cell lost its scattering material'
    assert scene['meshes'][-1]['subsurface_weight']==[0,0],'Backdrop was given SSS'
    # Loading scales optical distance with the cell mesh, while instance scaling happens in shading.
    bodies={b['name']:b for b in data['content']['bodies']}
    for body,m in zip(data['bodies'],scene['meshes']):
        raw=(out/bodies[body['type']]['model']).resolve().read_bytes();n=struct.unpack_from('<I',raw,8)[0]
        radius=max(max(struct.unpack_from('<4f',raw,16+i*64+48)[:3]) for i in range(n))
        assert abs(m['subsurface_radius'][1]-body['radius']*radius)<.00001
    baseline,view=run('grown',ticks=4800)
    assert sum(m['subsurface_weight']==[0,0] for m in view['meshes'])>=10,'A factory retained scattering'
    for producer in data['content']['buildings']:
        for path in [producer['model'],*producer.get('growth_meshes',{}).values()]:
            raw=(out/path).resolve().read_bytes();n=struct.unpack_from('<I',raw,8)[0]
            if raw[:8]==b'FLMESH02':assert all(struct.unpack_from('<f',raw,16+i*64+60)[0]==0 for i in range(n)),path
    disabled=deepcopy(settings);disabled['subsurface']['strength']=0
    no_sss,no_view=run('disabled',config=disabled,ticks=4800)
    assert baseline==no_sss and view==no_view,'SSS changed simulation or scene geometry'
    missing=deepcopy(settings);missing.pop('subsurface')
    assert run('optional',config=missing,ticks=4800)==(baseline,view)
    for field,value in [('strength',-1),('strength',1.1),('wrap',1.1),('transmission',-1),('membrane_thickness',0),('samples_per_pixel',0),('samples_per_pixel',7),('extra',1)]:
        bad=deepcopy(settings);bad['subsurface'][field]=value;run('bad-setting-'+str(count),config=bad,expected=1)
    # Malformed V2 materials must fail startup, even with the render effect disabled.
    original=(root/'assets/models/virus_virion.fmesh').read_bytes()
    def mesh_world(raw,name):
        path=out/(name+'.fmesh');path.write_bytes(raw);d=deepcopy(data)
        d['content']['units'][0]['model']=path.name;return d
    for offset,value in [(48,-.1),(48,float('nan')),(48,0),(60,1.1)]:
        raw=bytearray(original);struct.pack_into('<f',raw,16+offset,value)
        run('bad-material-'+str(count),world=mesh_world(raw,'bad-'+str(count)),config=disabled,expected=1)
    run('truncated',world=mesh_world(original[:-1],'truncated'),expected=1)
    # V1 remains an explicit zero-scattering material, with the same geometry.
    n,t=struct.unpack_from('<II',original,8);legacy=b'FLMESH01'+original[8:16]+b''.join(original[16+i*64:16+i*64+48] for i in range(n))+original[16+n*64:]
    old,old_view=run('legacy',world=mesh_world(legacy,'legacy'))
    assert old==initial and old_view['meshes'][len(data['bodies'])]['subsurface_weight']==[0,0]
print(f'Subsurface E2E passed: {count} application runs; authored materials, optical scale, unchanged state, optional controls, V1 compatibility and rejected material data.')
