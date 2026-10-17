#!/usr/bin/env python3
"""Check single-wall membrane and interior medium settings through the Windows app."""
from copy import deepcopy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world,change
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='microship-medium-') as directory:
    out=Path(directory)
    def run(name,data,ticks=1,orders='',config=None,expected=0):
        global count
        world=out/(name+'-world.json');write_world(world,data);state=out/(name+'.json');scene=out/(name+'-scene.json')
        args=[*command,'--smoke','--world',str(world),'--ticks',str(ticks),'--world-seed','1234','--state',str(state),'--scene-state',str(scene)]
        if orders:
            replay=out/(name+'.orders');replay.write_text(orders);args+=['--replay',str(replay)]
        if config:
            settings=out/(name+'-render.json');settings.write_text(json.dumps(config));args+=['--config',str(settings)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        if expected:
            assert p.stderr and not state.exists();return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    data=stage_world(root/'assets/levels/biology-virus.json',out)
    initial,scene=run('neutral',data)
    models={b['name']:b for b in data['content']['bodies']}
    for body,mesh in zip(data['bodies'],scene['meshes']):
        raw=(out/models[body['type']]['model']).resolve().read_bytes();vertices,triangles=struct.unpack_from('<II',raw,8)
        authored=sum(struct.unpack_from('<I',raw,16+vertices*48+t*16+12)[0]==3 for t in range(triangles))
        drawn=sum(t[3]==3 for t in mesh['deformation']['indices'])
        assert authored==drawn>0,'An extra membrane wall was added'
    assert scene['membrane_fade']==[1,2] and scene['wall_blur_radius']==12
    assert all(v==[3,.6,18] for v in scene['medium_blur'][:-1]) and scene['medium_blur'][-1]==[0,0,0]
    orders='0 select all\n0 order 0\n600 ratio 65\n'
    baseline,view=run('grown',data,4800,orders)
    disabled=deepcopy(data);disabled['appearance']['thin_surface'].pop('edge_fade');disabled['appearance']['thin_surface'].pop('blur_radius')
    for body in disabled['bodies']:body['medium'].pop('depth_blur')
    reference,clear=run('effects-off',disabled,4800,orders)
    assert baseline==reference,'Membrane and medium presentation changed game state'
    for a,b in zip(view['meshes'],clear['meshes']):assert a==b,'Presentation settings changed geometry'
    assert clear['wall_blur_radius']==0 and clear['membrane_fade']==[0,0] and all(v==[0,0,0] for v in clear['medium_blur'])
    settings=json.loads((root/'assets/rendering/biology.json').read_text())
    for n in (0,8,64):
        config=deepcopy(settings);config['lighting']['shadow_grid_resolution']=n
        s,_=run('grid-'+str(n),data,4800,orders,config)
        assert s==baseline,'Shadow quality changed the simulation'
    # Optional focus settings must not affect simulation or geometry.
    config=deepcopy(settings);config.pop('tilt_shift')
    s,_=run('focus-off',data,4800,orders,config);assert s==baseline
    for field,value in [('focus_center',1.1),('focus_half_width',-.1),('falloff',0),('max_radius',65),('unknown',1)]:
        config=deepcopy(settings);config['tilt_shift'][field]=value
        run('bad-focus-'+field,data,config=config,expected=1)
    for n in (1,7,65):
        config=deepcopy(settings);config['lighting']['shadow_grid_resolution']=n
        run('bad-grid-'+str(n),data,config=config,expected=1)
    for path,value in [('appearance.thin_surface.blur_radius',33),('appearance.thin_surface.edge_fade.strength',1.1),('appearance.thin_surface.edge_fade.power',0),
                       ('appearance.thin_surface.edge_fade.extra',True),('bodies.0.medium.depth_blur.pixels_per_unit',-1),
                       ('bodies.0.medium.depth_blur.max_radius',33),('bodies.0.medium.depth_blur.sharp_distance',-1),
                       ('appearance.membrane',dict(inner_radius_fraction=.965))]:
        run('bad-'+str(count),change(data,path,value),expected=1)
print(f'Cell medium E2E passed: {count} application runs; one authored wall, fixed topology, angle fade, medium-depth blur, quality-independent simulation and rejected settings.')
