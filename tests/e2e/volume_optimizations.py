#!/usr/bin/env python3
"""Compare real application volume paths. Default prepares inputs without GPU work."""
import argparse
import json
from pathlib import Path
import os
import re
import statistics
import struct
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
from save_progress import archive_capture
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--case',choices=('near','normal','boundary','overlap','thin','opaque','solid','live','far'),default='near')
parser.add_argument('--baseline',type=Path,help='Executable with its bundled assets for a before/after comparison')
parser.add_argument('--variants',nargs='+',choices=('baseline','current'),default=('current',))
parser.add_argument('--output',type=Path,default=root/'build/volume-opt-review')
parser.add_argument('--frames',type=int,choices=(1,2),default=1)
parser.add_argument('--depth-blur',choices=('on','off'),help='Override the authored medium surface blur')
parser.add_argument('--edge-fade',choices=('on','off'),help='Override the membrane view-angle fade')
parser.add_argument('--wall-blur',choices=('on','off'),help='Override blur for all light transmitted through a wall')
parser.add_argument('--tilt-shift',choices=('on','off'),help='Override the miniature focus band')
parser.add_argument('--subsurface',choices=('on','off','wrap'),help='Compare SSS with no SSS or without thickness transmission')
parser.add_argument('--subsurface-samples',type=int,help='Number of existing film samples used for SSS thickness')
parser.add_argument('--weak-medium',action='store_true',help='Restore the previous cell scattering coefficients for comparison')
parser.add_argument('--shadow-grid',type=int,help='Override current-frame visibility grid resolution; zero traces every shadow')
parser.add_argument('--atmosphere-shadows',choices=('on','off'),help='Override geometry shadows for the global medium')
parser.add_argument('--timing',action='store_true',help='Disable host validation for GPU stage timings')
mode=parser.add_mutually_exclusive_group();mode.add_argument('--run',action='store_true');mode.add_argument('--cpu',action='store_true')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
if 'baseline' in args.variants and not args.baseline:parser.error('--variants baseline requires --baseline PATH')
binaries=dict(baseline=args.baseline.resolve() if args.baseline else None,current=args.binary.resolve())
data=stage_world(root/'assets/levels/biology-virus.json',out)
if args.edge_fade=='off':data['appearance']['thin_surface'].pop('edge_fade',None)
if args.wall_blur=='off':data['appearance']['thin_surface'].pop('blur_radius',None)
for body in data['bodies']:
    if 'medium' not in body:continue
    if args.depth_blur=='off':body['medium'].pop('depth_blur',None)
    if args.weak_medium:body['medium'].update(scattering=[.035,.045,.04],absorption=.012)
config=json.loads((root/'assets/rendering/biology.json').read_text());config['sampling']['samples_per_pixel']=3
# Keep strict legacy comparisons on the unbaked path.
config['lighting'].pop('static_lighting_cache',None);config['lighting'].pop('force_lighting_rebuild',None)
if args.tilt_shift=='off':config.pop('tilt_shift',None)
if args.subsurface=='off':config['subsurface']['strength']=0
if args.subsurface=='wrap':config['subsurface']['transmission']=0
if args.subsurface_samples is not None:config['subsurface']['samples_per_pixel']=args.subsurface_samples
if args.shadow_grid is not None:config['lighting']['shadow_grid_resolution']=args.shadow_grid
assert config['output']['width']==1920 and config['output']['height']==1080
assert config['volume']==dict(rays_per_pixel=1,integration_steps=8)
if args.atmosphere_shadows:data['appearance']['atmosphere']['shadows']=args.atmosphere_shadows=='on'
if args.case=='solid':
    data['appearance']['backdrop'].pop('soft');data['appearance']['backdrop'].pop('wave',None);data['appearance']['backdrop']['samples']=1;data['rules'].pop('exploration',None)
ticks=4800
if args.case=='boundary':
    data['appearance']['atmosphere'].update(center=data['bodies'][0]['position'],radius=26,edge_fade=.4)
if args.case=='overlap':
    data['bodies'][2]['position']=[a+b for a,b in zip(data['bodies'][0]['position'],(2,0,0))];ticks=1
if args.case=='opaque':data['appearance']['thin_surface']['transmission']=0
if args.case=='thin':
    unit=next(u for u in data['content']['units'] if u['name']=='virus_fighter')
    source=(out/unit['model']).resolve();mesh=bytearray(source.read_bytes());vertices,triangles=struct.unpack_from('<II',mesh,8)
    for t in range(triangles):struct.pack_into('<I',mesh,16+vertices*(64 if mesh[:8]==b'FLMESH02' else 48)+t*16+12,3)
    (out/'transmitting-craft.fmesh').write_bytes(mesh);unit['model']='transmitting-craft.fmesh'
world=out/(args.case+'-world.json');write_world(world,data)
settings=out/'render.json';settings.write_text(json.dumps(config,indent=2)+'\n')
orders=out/'settle.orders';orders.write_text('0 select all\n0 order 0\n600 ratio 65\n' if ticks>600 else '')
base=['--world',str(world),'--config',str(settings),'--world-seed','1234','--ticks',str(ticks),'--no-stats']
if ticks>600:base+=['--replay',str(orders)]
if args.case in ('near','thin','opaque'):base+=['--camera-zoom','1000']
if args.case=='far':base+=['--camera-zoom','-1000']
if args.case=='live':
    if args.variants!=['current'] or args.frames!=2:parser.error('Live refit uses --variants current --frames 2')
else:base+=['--time',str(ticks/60),'--step-seconds','0']
prepared=[]
for variant in args.variants:
    binary=binaries[variant]
    command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
    prefix=args.case+'-'+variant
    tail=['--state',str(out/(prefix+'-state.json')),'--scene-state',str(out/(prefix+'-scene.json'))]
    invocation=command+base+tail+(['--smoke'] if args.cpu else ['--frames',str(args.frames),'--capture',str(out/(prefix+'.ppm')),
        '--no-activate' if args.case=='live' else '--headless'])
    prepared.append((variant,prefix,invocation))
(out/'commands.json').write_text(json.dumps([v[2] for v in prepared],indent=2)+'\n')
if not args.run and not args.cpu:
    print(f'Prepared {len(prepared)} processes: native 1920x1080; 3 film + 1 volume + 1 backdrop; {args.frames} frames; 2 seconds per process; 5 seconds per GPU batch. No GPU work started.');sys.exit(0)
environment=os.environ.copy();environment['NANOVA_VULKAN_VALIDATION']='0' if args.timing else '1'
started=time.monotonic();report=[];reference_state=None
labels=dict(application='Application frame',surface='GPU surface',volume='GPU volumes',acceleration='GPU acceleration update',gpu_total='total render',shadow_grid='GPU shadow grid',medium_blur='GPU medium blur')
for variant,prefix,invocation in prepared:
    remaining=5-(time.monotonic()-started)
    if not args.cpu and remaining<=0:raise RuntimeError('The five-second GPU batch limit expired')
    process=subprocess.run(invocation,capture_output=True,text=True,env=environment,timeout=10 if args.cpu else min(2,remaining))
    (out/(prefix+'.log')).write_text(process.stdout+process.stderr)
    if process.returncode:raise RuntimeError(prefix+': '+process.stdout+process.stderr)
    if not args.cpu and not args.timing:assert 'Vulkan validation: enabled.' in process.stdout
    if not args.cpu and args.timing:assert 'Vulkan validation: not reported by loader/tooling.' in process.stdout
    state=json.loads((out/(prefix+'-state.json')).read_text())
    if reference_state is None:reference_state=state
    elif args.case!='live':assert state==reference_state,'The renderer changed simulation state'
    item=dict(variant=variant)
    if not args.cpu:
        item['capture']=str(archive_capture(out/(prefix+'.ppm'),'volume-'+prefix+('-timing' if args.timing else '')))
        values={key:[float(v) for v in re.findall(re.escape(label)+r': ([0-9.]+) ms',process.stdout)] for key,label in labels.items()}
        assert all(len(v)==args.frames for v in values.values()),'Missing frame timings'
        item.update(frames_ms=values,mean_ms={key:statistics.mean(v) for key,v in values.items()})
        if args.case=='live':assert re.search(r'Deformed mesh updates: [1-9]',process.stdout),'No live mesh refit'
    report.append(item);(out/'report.json').write_text(json.dumps(dict(elapsed=time.monotonic()-started,validation=not args.timing,cases=report),indent=2)+'\n')
    print(item,flush=True)
gpu_elapsed=time.monotonic()-started

def pixels(path):
    contents=path.read_bytes();header=re.match(rb'P6\s+1920\s+1080\s+255\s',contents)
    assert header,'The application did not capture native 1080p RGB'
    result=contents[header.end():];assert len(result)==1920*1080*3;return result

if args.run and args.case!='live' and len(prepared)>1:
    reference=pixels(out/(prepared[0][1]+'.ppm'));differences=[]
    for _,prefix,_ in prepared[1:]:
        image=pixels(out/(prefix+'.ppm'));maximum=total=changed=large=0
        for a,b in zip(reference,image):
            d=abs(a-b);maximum=max(maximum,d);total+=d;changed+=d!=0;large+=d>1
        differences.append(dict(case=prefix,max_channel_error=maximum,mean_channel_error=total/len(reference),changed_channels=changed,channels_over_one=large))
    (out/'image-differences.json').write_text(json.dumps(differences,indent=2)+'\n');print(differences,flush=True)
    if any(d['channels_over_one'] for d in differences):raise RuntimeError('Display difference exceeds one RGB8 level; inspect the comparison before accepting the change')
if args.case=='live' and args.run:
    reference=out/'live-fresh-scene.json'
    command=[str(args.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(args.binary.resolve())]
    p=subprocess.run(command+['--smoke','--world',str(world),'--config',str(settings),'--world-seed','1234','--ticks',str(state['tick']),
        '--replay',str(orders),'--scene-state',str(reference)],capture_output=True,text=True,timeout=10)
    (out/'live-cpu.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stdout+p.stderr
    live=json.loads((out/'live-current-scene.json').read_text());fresh=json.loads(reference.read_text())
    for a,b in zip(live['meshes'],fresh['meshes']):
        if 'deformation' in a:assert a['deformation']['vertices']==b['deformation']['vertices']
print(f'Application checks passed in {gpu_elapsed:.3f} seconds' + ('. Image analysis and CPU reference work are outside the GPU batch.' if args.run else '; no GPU work.'))
