#!/usr/bin/env python3
"""Bounded 1080p gameplay renders of the actual Windows application."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from render_frames import check_gpu_environment,write_setting,read_ppm,require
from save_progress import archive_capture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--part',choices=('probe','growth','battle','invariants','static','timing','window'),required=True)
    parser.add_argument('--before',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();check_gpu_environment()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    config=(root/'assets/rendering/first.render').read_text()
    for key,value in (('width',1920),('height',1080),('samples_per_pixel',6),('volume_samples_per_pixel',2),('spatial_sun_reuse',0),('seed',41)):
        config=write_setting(config,key,value)
    settings=out/'bounded.render';settings.write_text(config)
    plant='0 select seedships\n0 order 0\n'
    priority=plant+'300 planet 0\n300 priority seedship\n'
    battle='0 select fighters\n0 order 1\n'+priority
    deadline=time.monotonic()+30;results={}
    def run(name,ticks=0,orders='',frames=1,binary=args.binary,extra=(),capture=True,window=False):
        remaining=deadline-time.monotonic();require(remaining>0 and frames<=2,'GPU batch/frame cap')
        replay=out/(name+'.orders');replay.write_text(orders)
        image=out/(name+'.ppm');state=out/(name+'.json')
        command=[str(binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary.resolve())]
        legacy=args.before and binary.resolve()==args.before.resolve()
        arguments=[*command,'--world',str(root/'assets/levels/prototype.world'),'--config',str(settings),'--frames',str(frames),'--no-stats']
        if not legacy:arguments+=['--state',str(state)]
        if not window:arguments+=['--headless']
        else:arguments+=['--no-activate']
        if capture:arguments+=['--capture',str(image)]
        if ticks:arguments+=['--ticks',str(ticks)]
        if orders:arguments+=['--replay',str(replay)]
        arguments+=list(extra)
        process=subprocess.Popen(arguments,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=sys.platform!='win32')
        try:stdout,stderr=process.communicate(timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            if sys.platform=='win32':process.kill()
            else:os.killpg(process.pid,signal.SIGKILL)
            process.communicate();raise RuntimeError('GPU process timed out; batch stopped')
        (out/(name+'.log')).write_text(stdout+stderr)
        require(process.returncode==0,name+': '+stdout+stderr)
        validation='Vulkan validation: enabled.' in stdout
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION')=='1':require(validation,'Requested Vulkan validation missing')
        data=None if legacy else json.loads(state.read_text())
        results[name]={'validation':validation,'frames':frames,'state':data,
            'render_ms':[float(v) for v in re.findall(r'total render: ([\d.]+) ms',stdout)],
            'application_ms':[float(v) for v in re.findall(r'Application frame: ([\d.]+) ms',stdout)]}
        if capture:
            width,height,pixels=read_ppm(image);require((width,height)==(1920,1080),'Non-native capture')
            archive_capture(image,'gameplay-'+name);return data,pixels
        return data,None
    if args.part=='probe':
        settings.write_text(write_setting(write_setting(config,'samples_per_pixel',1),'volume_samples_per_pixel',0))
        data,pixels=run('initial-instanced-fleet')
        require(len(data['units'])==11,'Initial fleet missing')
    elif args.part=='growth':
        growing,_=run('seed-rooting-stalk-growth',300,plant)
        require(0<growing['colonies'][0]['growth']<1,'No growing stalk')
        mature,_=run('sunlight-manufacturing-fighters',1200,plant)
        require(mature['births']>11,'No manufactured ships')
        seeds,_=run('manufactured-seedship',1800,priority)
        require(any(u['kind']==1 and u['faction']==1 for u in seeds['units']),'No new player seedship')
    elif args.part=='battle':
        run('fighters-clearing-defenders',360,battle)
        won,_=run('both-planets-colonized',3600,battle+'1800 select seedships\n1800 order 1\n')
        require(won['outcome']==1,'Conquest loop did not finish')
    elif args.part=='invariants':
        a,pixels=run('manufacturing-one-frame',1200,plant)
        b,again=run('manufacturing-two-frames',1200,plant,frames=2)
        require(a==b and pixels==again,'Rendering changed simulation or retained lighting history')
    elif args.part=='static':
        require(args.before and args.before.is_file(),'Need previous Windows executable')
        world=root/'assets/levels/first.world'
        extra=('--world',str(world),'--time','6')
        a,pixels=run('render-scene-before-instancing',binary=args.before,extra=extra)
        b,again=run('render-scene-after-instancing',extra=extra)
        require(pixels==again,'Static render scene regressed after instancing')
    elif args.part=='timing':
        require(os.environ.get('MICROSHIP_VULKAN_VALIDATION')!='1','Disable validation for timings')
        require(args.before and args.before.is_file(),'Need previous Windows executable')
        for round in range(2):
            cases=(('before',args.before,False),('after',args.binary,False),('prototype',args.binary,True))
            for name,binary,gameplay in (cases if round==0 else reversed(cases)):
                extra=() if gameplay else ('--world',str(root/'assets/levels/first.world'),'--time','6')
                run(name+'-'+str(round),ticks=1200 if gameplay else 0,orders=plant if gameplay else '',
                    frames=2,binary=binary,extra=extra,capture=False)
        import statistics
        results['summary']={name:{key:statistics.mean(v for k,row in results.items()
            if k.startswith(name+'-') for v in row[key]) for key in ('render_ms','application_ms')}
            for name in ('before','after','prototype')}
    else:
        data,_=run('inactive-window-colony-hud',1200,plant,frames=2,window=True)
        require(data['colonies'][0]['health']>0,'Window lost game state')
    (out/'results.json').write_text(json.dumps({'part':args.part,'resolution':[1920,1080],
        'budget': [1,0] if args.part=='probe' else [6,2],'runs':results},indent=2)+'\n')
    print('Gameplay '+args.part+' GPU E2E passed; native 1080p, bounded current-frame samples.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Gameplay GPU E2E failed: '+str(error),file=sys.stderr);sys.exit(1)
