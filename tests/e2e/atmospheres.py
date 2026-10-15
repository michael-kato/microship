#!/usr/bin/env python3
"""Atmosphere and fog checks through complete Windows application runs."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import time
from render_frames import check_gpu_environment,require,write_setting,read_ppm
from render_sampling import read_hdr
from save_progress import archive_capture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('config',type=Path)
    parser.add_argument('world',type=Path)
    parser.add_argument('--part',choices=('checks','front-light','fog-edge','timing','fog-timing'),required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--production',action='store_true',help='8 surface + 2 volume: requires explicit higher-budget GPU authorization')
    parser.add_argument('--reference-shader',type=Path,help='Optional frozen pre-fade shader for planet-only comparison')
    args=parser.parse_args()
    check_gpu_environment()
    binary=args.binary.resolve();require(binary.suffix.lower()=='.exe','Windows executable required')
    command=[str(binary)] if sys.platform=='win32' else [sys.executable,
        str(Path(__file__).resolve().parents[2]/'scripts/proton_run.py'),str(binary)]
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    source_world=args.world.resolve()
    records=[]
    for line in source_world.read_text().splitlines():
        fields=line.split()
        if fields and fields[0] in ('model','ship'):
            i=2 if fields[0]=='model' else 3
            fields[i]=Path(os.path.relpath((source_world.parent/fields[i]).resolve(),output)).as_posix()
            line=' '.join(fields)
        records.append(line)
    live=output/'live.world';live.write_text('\n'.join(records)+'\n')
    vacuum=output/'vacuum.world'
    vacuum.write_text(re.sub(r'(?m)^(atmosphere|haze|haze_edge_fade|global_atmosphere|global_atmosphere_edge_fade)\s+.*$','',live.read_text()))
    require(len(re.findall(r'(?m)^atmosphere\s+',live.read_text()))==2,'Expected two planet shells')
    fog_records=re.findall(r'(?m)^(haze|global_atmosphere)\s+',live.read_text())
    require(len(fog_records)==1,'Expected one fog volume separate from the planet shells')
    fog_kind=fog_records[0];fade_kind=fog_kind+'_edge_fade'
    label='global-fog' if fog_kind=='global_atmosphere' else 'restored-atmospheres'
    config=args.config.read_text()
    surface=8 if args.production else 6
    for key,value in (('width',1920),('height',1080),('samples_per_pixel',surface),
                      ('volume_samples_per_pixel',2),('spatial_sun_reuse',0),('spatial_denoise',1),('seed',41)):
        config=write_setting(config,key,value)
    config_path=output/'native.render';config_path.write_text(config)
    zero_path=output/'extinction-only.render';zero_path.write_text(write_setting(config,'volume_samples_per_pixel',0))
    deadline=time.monotonic()+30
    results={'settings':{'width':1920,'height':1080,'surface_spp':surface,'volume_spp':2,
                         'production_budget':args.production,'denoise':'indirect only'}}
    def run(name,scene=live,settings=config_path,extra=(),frames=1,display=False,capture=True,description=None):
        remaining=deadline-time.monotonic();require(remaining>0,'Batch deadline reached')
        require(frames<=2,'Frame cap exceeded')
        image=output/(name+('.ppm' if display else '.pfm'))
        arguments=[*command,'--headless','--no-stats','--config',str(settings),'--world',str(scene),'--frames',str(frames),*extra]
        if capture:arguments+=['--capture',str(image)]
        process=subprocess.Popen(arguments,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                                 start_new_session=sys.platform!='win32')
        try:stdout,stderr=process.communicate(timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            if sys.platform=='win32':process.kill()
            else:
                try:os.killpg(process.pid,signal.SIGKILL)
                except ProcessLookupError:pass
            process.communicate();raise RuntimeError('Application timeout; no further GPU work launched')
        (output/(name+'.txt')).write_text(stdout+stderr)
        require(process.returncode==0,name+' application failed: '+stderr)
        validated='Vulkan validation: enabled.' in stdout
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION')=='1':require(validated,'Requested validation unavailable')
        results[name]={'frames':frames,'validation':validated,
            'trace_ms':[float(x) for x in re.findall(r'GPU trace: ([\d.]+) ms',stdout)],
            'surface_ms':[float(x) for x in re.findall(r'GPU surface: ([\d.]+) ms',stdout)],
            'volume_ms':[float(x) for x in re.findall(r'GPU volumes: ([\d.]+) ms',stdout)],
            'filter_ms':[float(x) for x in re.findall(r'GPU spatial denoise: ([\d.]+) ms',stdout)],
            'render_ms':[float(x) for x in re.findall(r'total render: ([\d.]+) ms',stdout)],
            'application_ms':[float(x) for x in re.findall(r'Application frame: ([\d.]+) ms',stdout)]}
        if not capture:return None
        if display:
            width,height,pixels=read_ppm(image);require((width,height)==(1920,1080),'Incorrect native output dimensions')
            archive_capture(image,description or label+'-'+name+('-lit' if name in ('front','back') else ''));return pixels
        return read_hdr(image)
    if args.part=='checks':
        first=run('visible',extra=('--time','6'))
        returned=run('after-other-sun',extra=('--time','0','--step-seconds','6'),frames=2)
        require(first.tobytes()==returned.tobytes(),'Atmospheres retained light from an earlier frame')
        extinction=run('extinction-only',settings=zero_path,extra=('--time','6'))
        energy=sum(a-b for a,b in zip(first,extinction))
        require(energy>0,'In-scattering was absent')
        # Medium samples and their visibility must not alter far vacuum pixels.
        for y in (0,1,2,1077,1078,1079):
            for x in (0,1,2,1917,1918,1919):
                i=(y*1920+x)*3
                require(first[i:i+3]==extinction[i:i+3],'Atmosphere leaked into far empty space')
                require(all(abs(first[i+c]-expected)<1e-6 for c,expected in enumerate((.001,.002,.004))),
                        'Far empty space contains scattering or extinction')
        results['in_scattering_sum']=energy
        for phase,seconds in (('visible-sun',6),('front',60),('back',0)):
            run(phase,extra=('--time',str(seconds)),display=True)
    elif args.part=='front-light':
        # Hold density, geometry, extinction and sample dimensions fixed; vary only HG anisotropy.
        forward=output/'strong-forward-fog.world'
        def forward_phase(match):
            fields=match[0].split();fields[-1]='0.78';return ' '.join(fields)
        forward.write_text(re.sub(r'(?m)^'+fog_kind+r'\s+[^\n]+',forward_phase,live.read_text()))
        front=run('front-scattering',extra=('--time','60'))
        extinction=run('front-extinction',settings=zero_path,extra=('--time','60'))
        previous=run('forward-only-front',scene=forward,extra=('--time','60'))
        energy=sum(a-b for a,b in zip(front,extinction))
        old_energy=sum(a-b for a,b in zip(previous,extinction))
        require(energy>0 and old_energy>0 and energy/old_energy>3,'Fog remained strongly suppressed under front lighting')
        results['front_light']={'scattering_sum':energy,'forward_only_sum':old_energy,'ratio':energy/old_energy}
        run('forward-only-front-display',scene=forward,extra=('--time','60'),display=True,
            description=label+'-strong-forward-front-lit')
        run('front',extra=('--time','60'),display=True)
        run('visible-sun',extra=('--time','6'),display=True)
        run('back',extra=('--time','0'),display=True)
    elif args.part=='fog-edge':
        # Compare actual application output with only the fog density profile changed.
        hard=output/'hard-fog.world'
        hard.write_text(re.sub(r'(?m)^'+fade_kind+r'\s+.*$',fade_kind+' 0',live.read_text()))
        run('hard-edge',scene=hard,extra=('--time','6'),display=True,description=label+'-hard-edge')
        run('soft-edge',extra=('--time','6'),display=True,description=label+'-soft-edge')
        fog_text=re.sub(r'(?m)^atmosphere\s+.*$','',live.read_text())
        soft_only=output/'soft-fog-only.world';soft_only.write_text(fog_text)
        hard_only=output/'hard-fog-only.world'
        hard_only.write_text(re.sub(r'(?m)^'+fade_kind+r'\s+.*$',fade_kind+' 0',fog_text))
        hard_on=run('hard-scattering',scene=hard_only,extra=('--time','6'))
        hard_off=run('hard-extinction',scene=hard_only,settings=zero_path,extra=('--time','6'))
        soft_on=run('soft-scattering',scene=soft_only,extra=('--time','6'))
        soft_off=run('soft-extinction',scene=soft_only,settings=zero_path,extra=('--time','6'))
        def vector(key):
            return tuple(map(float,re.search(r'(?m)^'+key+r'\s+([^\n]+)',config)[1].split()))
        def unit(v):
            scale=math.sqrt(sum(x*x for x in v));return tuple(x/scale for x in v)
        def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
        eye=vector('camera_position');forward=unit(tuple(b-a for a,b in zip(eye,vector('camera_target'))))
        right=unit(cross(forward,vector('camera_up')));up=cross(right,forward)
        haze=tuple(map(float,re.search(r'(?m)^'+fog_kind+r'\s+([^\n]+)',fog_text)[1].split()))
        offset=tuple(haze[i]-eye[i] for i in range(3));offset2=sum(x*x for x in offset)
        scale=math.tan(math.radians(float(re.search(r'(?m)^vertical_fov_degrees\s+(\S+)',config)[1]))*.5)
        hard_edge=soft_edge=0.0;count=0
        for y in range(0,1080,3):
            film_y=(1-2*(y+.5)/1080)*scale
            for x in range(0,1920,3):
                film_x=(2*(x+.5)/1920-1)*scale*(1920/1080)
                direction=unit(tuple(forward[i]+right[i]*film_x+up[i]*film_y for i in range(3)))
                distance=sum(a*b for a,b in zip(offset,direction))
                impact2=max(0.0,offset2-distance*distance)/(haze[3]*haze[3])
                if distance<=0 or not .9*.9<impact2<.985*.985:continue
                i=(y*1920+x)*3
                hard_edge+=sum(max(0.0,hard_on[i+c]-hard_off[i+c]) for c in range(3))
                soft_edge+=sum(max(0.0,soft_on[i+c]-soft_off[i+c]) for c in range(3))
                count+=1
        require(count>100 and hard_edge>0,'No visible fog-boundary samples')
        require(0<soft_edge/hard_edge<.3,'Fog did not fade strongly near its outer edge')
        results['edge']={'pixels':count,'hard_scattering_sum':hard_edge,'soft_scattering_sum':soft_edge,
                         'soft_to_hard_ratio':soft_edge/hard_edge,'radius_fraction':[.9,.985]}
        if args.reference_shader:
            planet_only=output/'planet-atmospheres-only.world'
            planet_only.write_text(re.sub(r'(?m)^(?:haze|global_atmosphere)(?:_edge_fade)?\s+.*$','',live.read_text()))
            raw=output/'planet-reference.render';raw.write_text(write_setting(config,'spatial_denoise',0))
            current=run('planet-current',scene=planet_only,settings=raw,extra=('--time','6'),display=True,
                        description='planet-atmospheres-fog-fade-check')
            previous=run('planet-previous',scene=planet_only,settings=raw,
                         extra=('--time','6','--shader',str(args.reference_shader.resolve())),display=True,
                         description='planet-atmospheres-before-fog-fade')
            difference=sum((a-b)**2 for a,b in zip(current,previous))/len(current)
            require(difference<.0025,'Fog fade changed the separate planet atmosphere image')
            results['planet_atmospheres']={'identical':current==previous,'rmse_rgb8':math.sqrt(difference)}
    else:
        require(os.environ.get('MICROSHIP_VULKAN_VALIDATION')!='1','Disable validation for timings')
        cases=(('vacuum',vacuum),('atmospheres',live))
        if args.part=='fog-timing':
            hard=output/'hard-fog.world'
            hard.write_text(re.sub(r'(?m)^'+fade_kind+r'\s+.*$',fade_kind+' 0',live.read_text()))
            cases=(('hard-fog',hard),('soft-fog',live))
        for round in range(2):
            for mode,scene in (cases if round==0 else reversed(cases)):
                run(f'{mode}-{round}',scene=scene,extra=('--time','6'),frames=2,capture=False)
        summary={}
        for mode,_ in cases:
            rows=[v for k,v in results.items() if k.startswith(mode+'-')]
            summary[mode]={key:statistics.mean(x for row in rows for x in row[key]) for key in
                           ('trace_ms','surface_ms','volume_ms','filter_ms','render_ms','application_ms')}
            summary[mode]['application_fps']=1000/summary[mode]['application_ms']
        results['summary']=summary
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Atmospheres '+args.part+' E2E passed: native 1080p; '+str(surface)+' surface + 2 volume samples; no history.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Atmospheres E2E failed: '+str(error),file=sys.stderr);sys.exit(1)
