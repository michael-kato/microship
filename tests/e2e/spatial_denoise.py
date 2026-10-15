#!/usr/bin/env python3
"""Current-frame reconstruction checks through the real Windows application."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from render_frames import check_gpu_environment, require, write_setting, read_ppm
from render_sampling import read_hdr
from render_methods import plane_asset
from save_progress import archive_capture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('config',type=Path)
    parser.add_argument('world',type=Path)
    parser.add_argument('--part',choices=('scene','energy','timing'),required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    check_gpu_environment()
    binary=args.binary.resolve()
    require(binary.suffix.lower()=='.exe','Windows application required')
    command=[str(binary)] if sys.platform=='win32' else [sys.executable,
        str(Path(__file__).resolve().parents[2]/'scripts/proton_run.py'),str(binary)]
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    config=args.config.read_text()
    for key,value in (('width',1920),('height',1080),('samples_per_pixel',8),
                      ('volume_samples_per_pixel',0),('spatial_sun_reuse',0),('seed',41)):
        config=write_setting(config,key,value)
    deadline=time.monotonic()+30
    results={}

    def run(name,settings,world=args.world.resolve(),extra=(),frames=1,display=False,capture=True):
        remaining=deadline-time.monotonic()
        require(remaining>0,'Reached 30-second GPU batch limit')
        require(frames<=2,'Frame cap exceeded')
        budgets=[int(re.search(r'(?m)^'+key+r'\s+(\d+)',settings)[1]) for key in
                 ('samples_per_pixel','volume_samples_per_pixel','spatial_sun_reuse')]
        require(sum(budgets)<=8,'Fresh-sample cap exceeded')
        extent=[int(re.search(r'(?m)^'+key+r'\s+(\d+)',settings)[1]) for key in ('width','height')]
        require(extent==[1920,1080],'Native-resolution check required')
        path=output/(name+'.render');path.write_text(settings)
        image=output/(name+('.ppm' if display else '.pfm'))
        arguments=[*command,'--headless','--no-stats','--config',str(path),'--world',str(world),'--frames',str(frames),*extra]
        if capture: arguments+=['--capture',str(image)]
        process=subprocess.Popen(arguments,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                                 start_new_session=sys.platform!='win32')
        try: stdout,stderr=process.communicate(timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            if sys.platform=='win32': process.kill()
            else:
                try: os.killpg(process.pid,signal.SIGKILL)
                except ProcessLookupError: pass
            process.communicate()
            raise RuntimeError('Host timeout; batch stopped')
        (output/(name+'.txt')).write_text(stdout+stderr)
        require(process.returncode==0,name+' application failed: '+stderr)
        validated='Vulkan validation: enabled.' in stdout
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION')=='1': require(validated,'Validation was not active')
        results[name]={'validation':validated,'frames':frames,
            'trace_ms':[float(x) for x in re.findall(r'GPU trace: ([\d.]+) ms',stdout)],
            'filter_ms':[float(x) for x in re.findall(r'GPU spatial denoise: ([\d.]+) ms',stdout)],
            'render_ms':[float(x) for x in re.findall(r'total render: ([\d.]+) ms',stdout)],
            'application_ms':[float(x) for x in re.findall(r'Application frame: ([\d.]+) ms',stdout)]}
        if not capture: return None
        return read_ppm(image)[2] if display else read_hdr(image)

    raw=write_setting(config,'spatial_denoise',0)
    filtered=write_setting(config,'spatial_denoise',1)
    if args.part=='scene':
        original=run('raw',raw,extra=('--time','60'))
        reconstructed=run('filtered',filtered,extra=('--time','60'))
        returned=run('after-other-sun',filtered,extra=('--time','0','--step-seconds','60'),frames=2)
        require(reconstructed.tobytes()==returned.tobytes(),'Reconstruction retained data from an earlier frame')
        require(original.tobytes()!=reconstructed.tobytes(),'Spatial reconstruction did not affect surface lighting')
        # The scene's upper sky remains exact; no spatial leakage into vacuum.
        for y in range(0,250,4):
            for x in range(0,1920,4):
                i=(y*1920+x)*3
                require(original[i:i+3]==reconstructed[i:i+3],'Filter altered the clear background')
        changed=run('camera',filtered,extra=('--time','60','--camera-drag','180','-60'))
        require(changed.tobytes()!=reconstructed.tobytes(),'Camera input did not update reconstructed image')
        for name,settings,description in (('raw-display',raw,'raw-front-lit'),
                                          ('filtered-display',filtered,'indirect-denoise-front-lit')):
            run(name,settings,extra=('--time','60'),display=True)
            archive_capture(output/(name+'.ppm'),description)
    elif args.part=='energy':
        world=plane_asset(output)
        for key,value in (('max_bounces',1),('sun_radiance',100),('camera_min_distance',1),
                          ('camera_position','0 0 5'),('camera_target','0 0 0'),('background','0 0 0')):
            config=write_setting(config,key,value)
        errors={}
        for enabled in (0,1):
            name='plane-'+str(enabled)
            values=run(name,write_setting(config,'spatial_denoise',enabled),world)
            scale=math.tan(math.pi/8)*(1920/1080)*5
            samples=[values[(y*1920+x)*3]-(0.525+0.11875*((2*(x+0.5)/1920-1)*scale))
                     for y in range(32,1048,4) for x in range(32,1888,4)]
            mean=math.fsum(samples)/len(samples)
            rmse=math.sqrt(math.fsum(x*x for x in samples)/len(samples))
            require(abs(mean)<0.003,'Diffuse energy/vertex gradient bias exceeded tolerance')
            results[name].update(mean_error=mean,rmse=rmse);errors[enabled]=rmse
        require(errors[1]<=errors[0]*1.05,'Indirect reconstruction altered direct-light fixture quality')
    else:
        require(os.environ.get('MICROSHIP_VULKAN_VALIDATION')!='1','Disable validation for timings')
        for round in range(2):
            for enabled in ((0,1) if round==0 else (1,0)):
                run(f'timing-{round}-{enabled}',write_setting(config,'spatial_denoise',enabled),
                    extra=('--time','60'),frames=2,capture=False)
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Spatial denoise '+args.part+' E2E passed: native 1080p, eight fresh samples, no history.')

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Spatial denoise E2E failed: '+str(error),file=sys.stderr);sys.exit(1)
