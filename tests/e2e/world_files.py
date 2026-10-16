#!/usr/bin/env python3
"""Check structured assets through complete application runs."""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from asset_data import established_world, write_world, change, remove

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
count=0


def run(executable,directory,arguments=()):
    global count
    command=[str(executable)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(executable)]
    result=subprocess.run([*command,'--smoke',*arguments],cwd=directory,capture_output=True,text=True,timeout=10)
    count+=1
    return result


def success(result,cells=None):
    assert result.returncode==0,result.stdout+result.stderr
    assert 'Application lifecycle smoke passed' in result.stdout,result.stdout
    assert 'Render configuration: direct ray tracing;' in result.stdout,result.stdout
    if cells is not None:assert f': {cells} bodies.' in result.stdout,result.stdout


def failure(result,path):
    details=f'{path}\n{result.stdout}{result.stderr}'
    assert result.returncode==1,details
    assert str(path) in result.stderr,details
    assert 'Application lifecycle smoke passed' not in result.stdout,details


with tempfile.TemporaryDirectory(prefix='microship-json-e2e-') as directory:
    out=Path(directory);scene=out/'editable.json'
    base=established_world(root/'assets/levels/biology-virus.json',out)
    minimal=dict(content=dict(bodies=[dict(name='fixture',label='BODY',geometry='sphere',hosts_site=False)],
                factions=[dict(id=1,name='virus',label='VIRUS',color=[.45,.9,.75]),dict(id=2,name='immune',label='IMMUNE',color=[1,.55,.65])],
                groups=[],units=[],buildings=[]),appearance=dict(ambient=[.07,.055,.065]),
                match=dict(player='virus',opponent='immune'),
                bodies=[dict(type='fixture',owner='neutral',position=[9,8,7],radius=2,tint=[.2,.3,.4])],spawns=[])
    success(run(binary,out),5)
    success(run(binary,out,['--config',str(binary.parent/'assets/rendering/reference.json'),'--stats','--debug-views','--no-activate']))
    bundle=out/'relocated game';bundle.mkdir();relocated=bundle/binary.name
    shutil.copy2(binary,relocated);shutil.copytree(binary.parent/'assets',bundle/'assets')
    success(run(relocated,out),5)
    manifest=bundle/'assets/startup.json';startup=json.loads(manifest.read_text())
    manifest.write_text(json.dumps(change(startup,'world','levels/biology-immune.json')))
    state=out/'manifest-state.json';success(run(relocated,out,['--state',str(state)]),5)
    assert json.loads(state.read_text())['player_faction']==2
    for bad in [change(startup,'unknown','anything'),change(startup,'world','/tmp/absent.json'),
                remove(startup,'shader'),change(startup,'world',12),
                '{"world":"levels/biology-virus.json","world":"levels/biology-immune.json"}']:
        manifest.write_text(bad if isinstance(bad,str) else json.dumps(bad));failure(run(relocated,out),manifest)
    manifest.write_text(json.dumps(startup))

    # Exercise mesh errors after the JSON declarations pass validation.
    mesh=out/'ship.fmesh';data=bytearray((bundle/'assets/models/virus_virion.fmesh').read_bytes())
    vertices=struct.unpack_from('<I',data,8)[0];tag_offset=16+vertices*48+12
    struct.pack_into('<I',data,tag_offset,2);mesh.write_bytes(data)
    unit=deepcopy(base['content']['units'][0]);unit.update(name='sample',model='ship.fmesh',scale=1,yaw_degrees=45)
    mobile=change(minimal,'bodies',[]);mobile['content']['groups']=base['content']['groups'];mobile['content']['units']=[unit]
    mobile['spawns']=[dict(type='sample',position=[0,0,0])];write_world(scene,mobile)
    success(run(binary,out,['--world',str(scene)]),0)
    struct.pack_into('<I',data,tag_offset,7);mesh.write_bytes(data)
    failure(run(binary,out,['--world',str(scene)]),mesh)
    mesh.unlink();failure(run(binary,out,['--world',str(scene)]),mesh)

    config=bundle/'assets/rendering/biology.json';render=json.loads(config.read_text())
    for path,value in [('volume.rays_per_pixel',4097),('volume.integration_steps',1),
                       ('sampling.max_bounces',4),('lighting.microfacet_energy_compensation',2),
                       ('sampling.seed',4294967296),('output.width',1920.5),('output.width','1920'),
                       ('camera.position',[0,0]),('camera.up',[0,0,0]),('sampling.max_transmission_layers',0),
                       ('output.exposure',0),('bloom.radius',65),('camera.max_distance',1),
                       ('output.unknown',1),('unknown',{}),('reconstruction',{}),('lighting.volume_local_bounds',True),('sampling.temporal',True)]:
        config.write_text(json.dumps(change(render,path,value)));failure(run(relocated,out),config)
    for path in ('output','camera.up','sampling.seed','volume.integration_steps'):
        config.write_text(json.dumps(remove(render,path)));failure(run(relocated,out),config)
    raw=json.dumps(render)
    config.write_text(raw.replace('"width": 1920','"width": 1920, "width": 1920'))
    failure(run(relocated,out),config)
    config.unlink();failure(run(relocated,out),config)

    for flag,value in [('--samples','0'),('--samples','4097'),('--seed','-1'),('--seed','4294967296'),('--frames','0'),('--frames','1junk')]:
        result=run(binary,out,[flag,value]);assert result.returncode==2 and flag in result.stderr,result.stdout+result.stderr

    variants=[minimal,change(minimal,'bodies',minimal['bodies']*16),
              change(minimal,'bodies.0.medium',dict(radius_fraction=.94,edge_fade=.28,scattering=[.035,.045,.04],absorption=.012,anisotropy=.2)),
              change(minimal,'content.bodies.0.label','BODY with spaces \u03bc')]
    for value in variants:
        write_world(scene,value);success(run(binary,out,['--world',str(scene)]),len(value['bodies']))
    # JSON whitespace, escaping, and exponent numbers must load through the application.
    write_world(scene,minimal);raw=scene.read_text()
    for valid in [raw.rstrip(),raw.replace('\n','\r\n'),raw.replace('9,','9e0,'),raw.replace('"neutral"','"neutr\\u0061l"')]:
        scene.write_text(valid);success(run(binary,out,['--world',str(scene)]),1)

    invalid=[change(minimal,'bodies',[]),change(minimal,'bodies',minimal['bodies']*17),
             change(mobile,'spawns',mobile['spawns']*65),change(minimal,'bodies.0.owner','other'),
             change(minimal,'bodies.0.radius',0),change(minimal,'bodies.0.tint',[-.2,.3,.4]),
             change(minimal,'bodies.0.tint',[1.2,.3,.4]),change(minimal,'bodies.0.position',['nine',8,7]),
             change(minimal,'bodies.0.position',[float('nan'),8,7]),change(minimal,'bodies.0.position',[1e100,8,7]),
             change(minimal,'bodies.0.extra',1),remove(minimal,'match'),remove(minimal,'appearance'),
             change(minimal,'content','absent.json'),change(minimal,'match.opponent','virus'),
             change(minimal,'content.bodies.0.hosts_site',1)]
    for value in invalid:
        write_world(scene,value);result=run(binary,out,['--world',str(scene)])
        assert result.returncode==1 and result.stderr and 'Application lifecycle smoke passed' not in result.stdout,result.stdout+result.stderr
    write_world(scene,minimal);raw=scene.read_text()
    for bad in ['',raw+'\0',raw+' trailing',raw[:-2]+',}',raw.replace('"radius": 2','"radius": 02'),
                raw.replace('"radius": 2','"radius": 2.'),raw.replace('"radius": 2','"radius": 0x2'),
                raw.replace('"radius": 2','"radius": 1e999'),raw.replace('"radius": 2','"radius": null'),
                raw.replace('"radius": 2','"radius": true'),raw.replace('"radius": 2','"radius": 2, "radius": 3'),
                raw.replace('"neutral"','"neutr\\u0000al"'),raw.replace('"neutral"','"neutr\\q"'),
                '['*33+'0'+']'*33,' '*1048577,'# comment\n'+raw,
                json.dumps({str(i):i for i in range(65)}),
                raw.replace('"radius": 2','"radius": 2, "ra\\u0064ius": 3'),
                raw.replace('"match"','"ma\\u0000tch"')]:
        scene.write_text(bad);failure(run(binary,out,['--world',str(scene)]),scene)
    scene.write_bytes(raw.encode().replace(b'neutral',b'neutr\xffal'));failure(run(binary,out,['--world',str(scene)]),scene)
    missing=out/'missing.json';failure(run(binary,out,['--world',str(missing)]),missing)
    result=run(binary,out,['--world']);assert result.returncode==2 and result.stderr.strip()
print(f'Structured-file E2E passed: {count} complete application runs; relocated bundle, render settings, types, syntax, references and capacities.')
