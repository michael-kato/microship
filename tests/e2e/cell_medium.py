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
with tempfile.TemporaryDirectory(prefix='nanovore-medium-') as directory:
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
    data=stage_world(root/'assets/levels/virus-player-level.json',out)
    initial,scene=run('neutral',data)
    models={b['name']:b for b in data['content']['bodies']}
    for body,mesh in zip(data['bodies'],scene['meshes']):
        raw=(out/models[body['type']]['model']).resolve().read_bytes();vertices,triangles=struct.unpack_from('<II',raw,8)
        authored=sum(struct.unpack_from('<I',raw,16+vertices*(64 if raw[:8]==b'FLMESH02' else 48)+t*16+12)[0]==3 for t in range(triangles))
        drawn=sum(t[3]==3 for t in mesh['deformation']['indices'])
        assert authored==drawn>0,'An extra membrane wall was added'
    assert scene['wall_blur_radius']==0
    assert all(abs(v[0]-1/3)<1e-6 and abs(v[1]-2/3)<1e-6 and v[2]==18 for v in scene['medium_blur'][:-1]) and scene['medium_blur'][-1]==[0,0,0]
    assert scene['medium_blur_fractional']==[True]*len(data['bodies'])+[False]
    orders='0 select all\n0 order 0\n600 ratio 65\n'
    baseline,view=run('grown',data,4800,orders)
    disabled=deepcopy(data);disabled['appearance']['thin_surface'].pop('blur_radius',None)
    disabled['appearance']['cell_medium'].pop('depth_blur')
    reference,clear=run('effects-off',disabled,4800,orders)
    assert baseline==reference,'Membrane and medium presentation changed game state'
    for a,b in zip(view['meshes'],clear['meshes']):assert a==b,'Presentation settings changed geometry'
    assert clear['wall_blur_radius']==0 and all(v==[0,0,0] for v in clear['medium_blur'])
    settings=json.loads((root/'assets/rendering/renderer.json').read_text())
    for n in (0,8,64):
        config=deepcopy(settings);config['lighting']['shadow_grid_resolution']=n
        s,_=run('grid-'+str(n),data,4800,orders,config)
        assert s==baseline,'Shadow quality changed the simulation'
    for section,field in [('volume','optical_depth_lut'),('volume','lighting_grid'),('lighting','texture_grid'),('lighting','organelle_cache')]:
        for value in (False,True):
            config=deepcopy(settings);config[section][field]=value
            assert run('optical-'+section+'-'+field+str(value),data,4800,orders,config)[0]==baseline
        config=deepcopy(settings);config[section][field]=1
        run('bad-optical-'+field,data,config=config,expected=1)
    for value in (0,32,64):
        config=deepcopy(settings);config['lighting']['subgroup_size']=value
        assert run('subgroup-'+str(value),data,4800,orders,config)[0]==baseline
    for value in (1,16,31,33,65,True):
        config=deepcopy(settings);config['lighting']['subgroup_size']=value
        run('bad-subgroup-'+str(value),data,config=config,expected=1)
    for mode,n in [('midpoint',8),('gauss_legendre',2),('gauss_legendre',3)]:
        config=deepcopy(settings);config['volume'].update(quadrature=mode,integration_steps=n)
        assert run('quadrature-'+mode+str(n),data,4800,orders,config)[0]==baseline
    for stride in (2,4):
        config=deepcopy(settings);config['volume'].update(spatial_filter=True,spatial_stride=stride)
        assert run('spatial-volume-'+str(stride),data,4800,orders,config)[0]==baseline
    for section,field in [('sampling','spatial_antialiasing'),('volume','spatial_filter')]:
        for value in (False,True):
            config=deepcopy(settings);config[section][field]=value
            assert run('spatial-'+field+str(value),data,4800,orders,config)[0]==baseline
        config=deepcopy(settings);config[section][field]=1
        run('bad-spatial-'+field,data,config=config,expected=1)
    for field,value in [('spatial_stride',3),('spatial_stride',True),('spatial_stride',5),('rays_per_pixel',0),('rays_per_pixel',2),('quadrature','unknown'),('quadrature',True),('integration_steps',8)]:
        config=deepcopy(settings);config['volume'].update(quadrature='gauss_legendre',integration_steps=3,spatial_filter=True)
        config['volume'][field]=value;run('bad-volume-mode-'+str(count),data,config=config,expected=1)
    # Optional focus settings must not affect simulation or geometry.
    config=deepcopy(settings);config.pop('depth_of_field')
    s,_=run('focus-off',data,4800,orders,config);assert s==baseline
    for field,value in [('focus_range',1.1),('focus_range',-.1),('blur_scale',1001),('max_radius',65),('unknown',1)]:
        config=deepcopy(settings);config['depth_of_field'][field]=value
        run('bad-focus-'+field,data,config=config,expected=1)
    for n in (1,7,65):
        config=deepcopy(settings);config['lighting']['shadow_grid_resolution']=n
        run('bad-grid-'+str(n),data,config=config,expected=1)
    for path,value in [('appearance.thin_surface.blur_radius',33),('appearance.thin_surface.clarity',.2),
                       ('appearance.cell_medium.depth_blur.pixels_per_unit',-1),
                       ('appearance.cell_medium.depth_blur.max_radius',33),('appearance.cell_medium.depth_blur.sharp_distance',-1),
                       ('appearance.cell_medium.depth_blur.start_fraction',.8),('appearance.cell_medium.depth_blur.full_fraction',.2),('appearance.cell_medium.depth_blur.full_fraction',1.1),('appearance.membrane',dict(inner_radius_fraction=.965))]:
        run('bad-'+str(count),change(data,path,value),expected=1)
print(f'Cell medium E2E passed: {count} application runs; one authored wall, fixed topology, medium-depth blur, quality-independent simulation and rejected settings.')
