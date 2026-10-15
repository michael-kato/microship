#!/usr/bin/env python3
"""Opt-in high-sample comparisons through complete Windows application runs.

The October 14 approval covers two 256-spp captures and four 256-spp timing
runs. Do not run this script without explicit authorization for those budgets.
"""
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
from save_progress import archive_capture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('config',type=Path)
    parser.add_argument('world',type=Path)
    parser.add_argument('--part',choices=('captures','recheck','timing','low-timing'),required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reference-captures',type=Path)
    parser.add_argument('--reference-timing',type=Path)
    args=parser.parse_args()
    check_gpu_environment()
    binary=args.binary.resolve();require(binary.suffix.lower()=='.exe','Windows executable required')
    command=[str(binary)] if sys.platform=='win32' else [sys.executable,
        str(Path(__file__).resolve().parents[2]/'scripts/proton_run.py'),str(binary)]
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    config=args.config.read_text()
    for key,value in (('width',1920),('height',1080),('volume_samples_per_pixel',0),
                      ('spatial_sun_reuse',0),('max_bounces',4),('seed',41)):
        config=write_setting(config,key,value)
    deadline=time.monotonic()+30
    results={}
    high_runs=0
    def run(name,samples,denoise,scene_time=60,capture=False):
        nonlocal high_runs
        require(samples in (8,256) and denoise in (0,1),'Invalid comparison budget')
        require(samples!=256 or denoise==0,'Reference must be raw')
        if samples==256:
            high_runs+=1
            require(high_runs <= (2 if args.part=='captures' else 4),'Approved reference count exceeded')
        remaining=deadline-time.monotonic();require(remaining>0,'Batch deadline reached')
        settings=write_setting(write_setting(config,'samples_per_pixel',samples),'spatial_denoise',denoise)
        path=output/(name+'.render');path.write_text(settings)
        image=output/(name+'.ppm')
        arguments=[*command,'--headless','--no-stats','--config',str(path),'--world',str(args.world.resolve()),
                   '--frames','1','--time',str(scene_time)]
        if capture:arguments+=['--capture',str(image)]
        process=subprocess.Popen(arguments,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                                 start_new_session=sys.platform!='win32')
        try:stdout,stderr=process.communicate(timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            if sys.platform=='win32':process.kill()
            else:
                try:os.killpg(process.pid,signal.SIGKILL)
                except ProcessLookupError:pass
            process.communicate();raise RuntimeError('Timeout: stop all further reference work')
        (output/(name+'.txt')).write_text(stdout+stderr)
        require(process.returncode==0,name+' failed: '+stderr)
        validated='Vulkan validation: enabled.' in stdout
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION')=='1':require(validated,'Requested validation not active')
        results[name]={'samples':samples,'denoise':denoise,'time':scene_time,'frames':1,'validation':validated,
            'trace_ms':[float(x) for x in re.findall(r'GPU trace: ([\d.]+) ms',stdout)],
            'filter_ms':[float(x) for x in re.findall(r'GPU spatial denoise: ([\d.]+) ms',stdout)],
            'render_ms':[float(x) for x in re.findall(r'total render: ([\d.]+) ms',stdout)],
            'application_ms':[float(x) for x in re.findall(r'Application frame: ([\d.]+) ms',stdout)]}
        if capture:
            width,height,pixels=read_ppm(image);require((width,height)==(1920,1080),'Incorrect native output dimensions')
            description = name.replace('filtered-', 'indirect-denoise-')+'-lit'
            if samples==256: description += '-256-spp'
            archive_capture(image,description);return pixels
    if args.part in ('captures','recheck'):
        if args.part=='captures':
            references={phase:run('reference-'+phase,256,0,t,True) for phase,t in (('front',60),('back',0))}
        else:
            require(args.reference_captures is not None,'Recheck requires saved references; it must not retrace them')
            references={phase:read_ppm(args.reference_captures/('reference-'+phase+'.ppm'))[2] for phase in ('front','back')}

        for phase,t in (('front',60),('back',0)):
            reference=references[phase];background=reference[:3]
            foreground=[i for i in range(0,len(reference),3)
                        if max(abs(reference[i+c]-background[c]) for c in range(3))>2]
            require(len(foreground)>1000,'Reference had no useful visible scene')
            for mode in ('raw','filtered'):
                name=mode+'-'+phase;pixels=run(name,8,int(mode=='filtered'),t,True)
                mse=sum((pixels[i+c]-reference[i+c])**2 for i in foreground for c in range(3))/(3*len(foreground))
                mean=sum(pixels[i+c]-reference[i+c] for i in foreground for c in range(3))/(3*len(foreground))
                results[name].update(foreground_pixels=len(foreground),foreground_rmse_rgb8=math.sqrt(mse),
                    foreground_mean_error_rgb8=mean,foreground_psnr_db=10*math.log10(255*255/max(mse,1e-20)))
        if args.part=='recheck':
            for phase in ('front','back'):
                require(results['filtered-'+phase]['foreground_rmse_rgb8']<=results['raw-'+phase]['foreground_rmse_rgb8']*1.02,
                        'Spatial reconstruction regressed '+phase+' reference quality')
    else:
        require(os.environ.get('MICROSHIP_VULKAN_VALIDATION')!='1','Disable validation for timing comparisons')
        for round in range(4):
            modes=(('reference','filtered') if round%2==0 else ('filtered','reference')) if args.part=='timing' else ('filtered',)
            for mode in modes:
                run(f'{mode}-{round}',256 if mode=='reference' else 8,int(mode=='filtered'))
        summary={}
        for mode in ('reference','filtered'):
            if mode=='reference' and args.part=='low-timing':
                require(args.reference_timing is not None,'Supply saved timing results; do not retrace reference frames')
                summary[mode]=json.loads(args.reference_timing.read_text())['summary']['reference']
                continue
            rows=[v for k,v in results.items() if k.startswith(mode+'-')]
            summary[mode]={key:statistics.mean(x for row in rows for x in row[key])
                           for key in ('trace_ms','filter_ms','render_ms','application_ms')}
        if args.part=='low-timing':
            summary['reference_source']=str(args.reference_timing)
            summary['comparison_note']='Reference frames belong to the preceding batch, not interleaved with this final filter.'
        summary['application_reduction_percent']=100*(1-summary['filtered']['application_ms']/summary['reference']['application_ms'])
        summary['gpu_render_reduction_percent']=100*(1-summary['filtered']['render_ms']/summary['reference']['render_ms'])
        results['summary']=summary
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Reference '+args.part+' E2E passed; native 1080p, sequential single-frame application runs.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Reference E2E failed: '+str(error),file=sys.stderr);sys.exit(1)
