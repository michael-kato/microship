#!/usr/bin/env python3
"""Bounded native-resolution biological scene and ambient-light E2E checks."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import struct
import subprocess
import sys
import time
from render_frames import check_gpu_environment,write_setting,read_ppm,require
from render_sampling import read_hdr
from save_progress import archive_capture
from debug_views import overlay_text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--part',choices=('probe','views','conquest','invariants','lighting','transmission','combat','buffers','background','isolation','timing','window','polish','battle-timing','spike-combat','cell-interiors','shadow-equivalence'),required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--before',type=Path,help='Previous complete Windows runtime bundle for paired timing')
    args=parser.parse_args();check_gpu_environment()
    root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    config=(root/'assets/rendering/biology.render').read_text()
    for key,value in (('width',1920),('height',1080),('samples_per_pixel',1 if args.part=='probe' else 7),('volume_samples_per_pixel',0),('spatial_sun_reuse',0),('seed',41)):
        config=write_setting(config,key,value)
    settings=out/'native.render';settings.write_text(config)
    default_command=[str(args.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(args.binary.resolve())]
    deadline=time.monotonic()+30;results={}
    def run(name,side='virus',ticks=0,orders='',frames=1,world=None,config=settings,capture=True,hdr=False,window=False,extra=(),binary=None):
        remaining=deadline-time.monotonic();require(remaining>0 and frames<=2,'Batch/frame limit')
        if binary:
            command=[str(binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary.resolve())]
            world=world or binary.resolve().parent/'assets/levels'/('biology-'+side+'.world')
        else:command=default_command
        world=world or root/'assets/levels'/('biology-'+side+'.world')
        image=out/(name+('.pfm' if hdr else '.ppm'));state=out/(name+'.json')
        replay=out/(name+'.orders');replay.write_text(orders)
        arguments=[*command,'--world',str(world),'--config',str(config),'--frames',str(frames),'--state',str(state),'--no-stats',*extra]
        arguments+=['--no-activate'] if window else ['--headless']
        if ticks:arguments+=['--ticks',str(ticks)]
        if orders:arguments+=['--replay',str(replay)]
        if capture:arguments+=['--capture',str(image)]
        p=subprocess.Popen(arguments,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=sys.platform!='win32')
        try:stdout,stderr=p.communicate(timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            if sys.platform=='win32':p.kill()
            else:
                try:os.killpg(p.pid,signal.SIGKILL)
                except ProcessLookupError:pass
            p.communicate();raise RuntimeError('GPU process timeout; no later runs launched')
        (out/(name+'.log')).write_text(stdout+stderr);require(p.returncode==0,name+': '+stdout+stderr)
        validation='Vulkan validation: enabled.' in stdout
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION')=='1':require(validation,'Requested validation unavailable')
        data=json.loads(state.read_text());require(data['sun_radius']==0,'Unexpected sun emitter')
        results[name]={'frames':frames,'validation':validation,'state':data,
            'render_ms':[float(v) for v in re.findall(r'total render: ([\d.]+) ms',stdout)],
            'application_ms':[float(v) for v in re.findall(r'Application frame: ([\d.]+) ms',stdout)],
            'background_ms':[float(v) for v in re.findall(r'GPU backdrop: ([\d.]+) ms',stdout)],
            'surface_ms':[float(v) for v in re.findall(r'GPU surface: ([\d.]+) ms',stdout)],
            'denoise_ms':[float(v) for v in re.findall(r'GPU spatial denoise: ([\d.]+) ms',stdout)],
            'effects_ms':[float(v) for v in re.findall(r'GPU effects: ([\d.]+) ms',stdout)],
            'acceleration_ms':[float(v) for v in re.findall(r'GPU acceleration update: ([\d.]+) ms',stdout)],
            'spike_instances':[[int(a),int(b),int(c)] for a,b,c in re.findall(r'Spike instances: attached (\d+); flying (\d+); embedded (\d+)',stdout)],
            'work_ms':[float(v) for v in re.findall(r'Application work: ([\d.]+) ms',stdout)],
            'presentation':re.findall(r'Presentation: ([^;]+)',stdout),
            'waits':[[float(a),float(b)] for a,b in re.findall(r'Presentation waits: acquire ([\d.]+) ms; present ([\d.]+) ms',stdout)]}
        if not capture:return data,None
        if hdr:return data,read_hdr(image)
        w,h,pixels=read_ppm(image);require((w,h)==(1920,1080),'Non-native screenshot')
        archive_capture(image,'biology-'+name);return data,pixels
    def plant(home):return f'0 select seedships\n0 order {home}\n'
    def assault(home,enemy):return f'0 select fighters\n0 order {enemy}\n'+plant(home)+f'300 cell {home}\n300 priority seedship\n1200 select seedships\n1200 order {enemy}\n'
    def cell_pointer(index):
        values={line.split()[0]:list(map(float,line.split()[1:])) for line in config.splitlines() if line and not line.startswith('#') and line.split()[0] in ('camera_position','camera_target','vertical_fov_degrees')}
        eye=values['camera_position'];target=values['camera_target']
        def unit(v):return [x/math.sqrt(sum(c*c for c in v)) for x in v]
        forward=unit([b-a for a,b in zip(eye,target)]);right=unit([-forward[2],0,forward[0]])
        up=[right[1]*forward[2]-right[2]*forward[1],right[2]*forward[0]-right[0]*forward[2],right[0]*forward[1]-right[1]*forward[0]]
        cells=[line.split() for line in (root/'assets/levels/biology-virus.world').read_text().splitlines() if line.startswith('cell ')]
        point=list(map(float,cells[index][2:5]));offset=[p-e for p,e in zip(point,eye)];depth=sum(a*b for a,b in zip(offset,forward))
        focal=1080/(2*math.tan(math.radians(values['vertical_fov_degrees'][0]/2)))
        return str(round(960+sum(a*b for a,b in zip(offset,right))*focal/depth)),str(round(540-sum(a*b for a,b in zip(offset,up))*focal/depth))
    def stage(source):
        text=source.read_text()
        for raw in source.read_text().splitlines():
            f=raw.split()
            if f and f[0] in ('model','ship','stalk_model','backdrop','spike_weapon'):
                i=2 if f[0]=='model' else 3 if f[0]=='ship' else 1
                text=text.replace(f[i],Path(os.path.relpath((source.parent/f[i]).resolve(),out)).as_posix())
        return text
    def cpu(name,scene,ticks,orders):
        replay=out/(name+'.orders');replay.write_text('\n'.join(line for line in orders.splitlines() if int(line.split()[0])<ticks)+'\n')
        state=out/(name+'.json');remaining=deadline-time.monotonic();require(remaining>0,'CPU preparation exceeded batch deadline')
        p=subprocess.run([*default_command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--replay',str(replay),'--state',str(state)],
            capture_output=True,text=True,timeout=min(10,remaining))
        require(p.returncode==0,p.stdout+p.stderr);return json.loads(state.read_text())
    if args.part=='probe':
        run('virus-in-tissue-initial')
    elif args.part=='views':
        for side,home in [('virus',0),('immune',1)]:
            run(side+'-cell-nodes-growing',side,300,plant(home))
            d,_=run(side+'-nutrient-production',side,900,plant(home),extra=('--camera-zoom','1'))
            require(d['births']>11,'No nutrient-funded units')
    elif args.part=='polish':
        for side,home,enemy in [('virus',0,1),('immune',1,0)]:
            d,_=run(side+'-polished-nodes-and-orders',side,900,plant(home),
                extra=('--select-group','1','--command',*cell_pointer(enemy)))
            require(d['order']['count']>0 and d['order']['cell']==enemy,'Missing visible order confirmation')
            run(side+'-fleet-approaching-enemy-cell',side,240,assault(home,enemy).split('300 cell')[0],extra=('--select-group','1'))
    elif args.part=='spike-combat':
        text=stage(root/'assets/levels/biology-virus.world');lines=[];small=set()
        for raw in text.splitlines():
            f=raw.split()
            if f and f[0]=='ship' and f[2]=='small':
                if f[1] in small:continue
                small.add(f[1])
            lines.append(raw)
        duel=out/'duel.world';duel.write_text('\n'.join(lines)+'\n')
        orders='0 select fighters\n0 order 1\n0 select seedships\n0 order 0\n'
        final=cpu('duel-events',duel,600,orders);events=final['effects']
        require(events['last_low_tick']>0 and events['last_death_tick']>0,'Duel lacked visible three-hit stages')
        flight=critical=None
        for tick in range(max(1,events['last_low_tick']-36),min(599,events['last_death_tick']+16),3):
            data=cpu('duel-'+str(tick),duel,tick,orders)
            if flight is None and data['effects']['projectiles']>0:flight=tick
            if critical is None and any(u['wounds']==2 and u['flash']==0 for u in data['units']):critical=tick
            if flight is not None and critical is not None:break
        require(flight is not None and critical is not None,'Missing flying spike or unflashed two-hit survivor')
        def inspect(tick):return orders+f'{tick-1} cell 1\n'
        def portrait(tick,label,critical=False,generation=None):
            data=cpu('portrait-'+label,duel,tick,orders)
            fighters=[u for u in data['units'] if u['kind']==0]
            if generation is not None:hero=next(u for u in fighters if u['generation']==generation)
            elif critical:hero=next(u for u in fighters if u['wounds']==2 and u['flash']==0)
            else:hero=next(u for u in fighters if u['faction']==1)
            other=next((u for u in fighters if u['faction']!=hero['faction']),None)
            direction=[other['position'][i]-hero['position'][i] for i in range(3)] if other else [0,0,1]
            length=math.sqrt(sum(x*x for x in direction));direction=[x/length for x in direction];direction[1]+=.65
            length=math.sqrt(sum(x*x for x in direction));direction=[x/length for x in direction]
            target=hero['position'];eye=[target[i]+direction[i]*2 for i in range(3)]
            cfg=settings.read_text()
            for key,value in [('camera_min_distance','1'),('camera_position',' '.join(map(str,eye))),('camera_target',' '.join(map(str,target))),
                ('camera_up','0 0 1' if abs(direction[1])>.97 else '0 1 0')]:cfg=write_setting(cfg,key,value)
            path=out/(label+'.render');path.write_text(cfg);return path
        flight_camera=portrait(flight,'flight-portrait');critical_camera=portrait(critical,'embedded-portrait',critical=True)
        extra=('--select-group','1')
        d,_=run('detached-flying-spike-closeup',ticks=flight,orders=inspect(flight),world=duel,config=flight_camera,extra=extra)
        require(d['effects']['projectiles']>0 and results['detached-flying-spike-closeup']['spike_instances'][0][1]>0,'Projectile mesh not submitted')
        a,visible=run('two-embedded-spikes-brown-black-damage',ticks=critical,orders=inspect(critical),world=duel,config=critical_camera,extra=extra)
        require(any(u['wounds']==2 and u['flash']==0 for u in a['units']),'Two-hit damage state missing')
        require(results['two-embedded-spikes-brown-black-damage']['spike_instances'][0][2]>=2,'Embedded meshes not submitted')
        # Real-application image A/B: same combat state, near-invisible shared spike mesh.
        tiny=bytearray((root/'assets/models/virus_spike.fmesh').read_bytes());_,vertices,_=struct.unpack_from('<8sII',tiny)
        for v in range(vertices):
            for axis in range(3):
                offset=16+v*48+axis*4;struct.pack_into('<f',tiny,offset,struct.unpack_from('<f',tiny,offset)[0]*.001)
        (out/'tiny-spike.fmesh').write_bytes(tiny);hidden=out/'tiny-spikes.world'
        hidden.write_text(re.sub(r'(?m)^spike_weapon \S+', 'spike_weapon tiny-spike.fmesh',duel.read_text()))
        b,absent=run('spike-geometry-visibility-comparison',ticks=critical,orders=inspect(critical),world=hidden,config=critical_camera,extra=extra)
        require(a==b and sum(x!=y for x,y in zip(visible,absent))>100,'Spike geometry failed image/state A/B')
        tick=events['last_death_tick']+1
        before=cpu('before-death',duel,tick-1,orders);after=cpu('after-death',duel,tick,orders)
        alive={u['generation'] for u in after['units']}
        dead=next(u['generation'] for u in before['units'] if u['kind']==0 and u['generation'] not in alive)
        death_camera=portrait(tick-1,'death-portrait',generation=dead)
        d,_=run('third-hit-death-burst',ticks=tick,orders=inspect(tick),world=duel,config=death_camera,extra=extra)
        require(d['effects']['pops']>0,'Third hit did not remove the unit and create a pop')
        results['duel_event_ticks']={'flight':flight,'two_hit_brown':critical,'death':tick}
    elif args.part=='shadow-equivalence':
        require(args.before and args.before.is_file(),'Need the complete pre-optimization bundle')
        for side,home in [('virus',0),('immune',1)]:
            a,first=run(side+'-before-zero-shadow-skip',side,900,plant(home),binary=args.before)
            b,second=run(side+'-after-zero-shadow-skip',side,900,plant(home))
            require(a==b and first==second,'Zero-BSDF shadow skip changed gameplay or display pixels')
        results['display_pixels_identical']=True
    elif args.part=='cell-interiors':
        source=root/'assets/levels/biology-virus.world';text=stage(source)
        full=out/'full-interiors.world';full.write_text(text)
        extra=('--focus','--camera-zoom','7')
        a,full_image=run('clear-cell-with-nucleus-and-organelles',world=full,extra=extra)
        diffuse=out/'diffuse-membrane.world';diffuse.write_text(re.sub(r'(?m)^cell_clarity.*$','cell_clarity 0',text))
        b,diffuse_image=run('diffuse-membrane-visibility-comparison',world=diffuse,extra=extra)
        data=(root/'assets/models/cell_infected.fmesh').read_bytes();magic,vertices,triangles=struct.unpack_from('<8sII',data)
        start=16+vertices*48;faces=[data[start+i*16:start+(i+1)*16] for i in range(triangles) if struct.unpack_from('<4I',data,start+i*16)[3]==3]
        (out/'shell-only.fmesh').write_bytes(struct.pack('<8sII',magic,vertices,len(faces))+data[16:start]+b''.join(faces))
        shell=out/'shell-only.world';shell.write_text(re.sub(r'(?m)^model 0 \S+','model 0 shell-only.fmesh',text))
        c,shell_image=run('clear-cell-without-internal-geometry',world=shell,extra=extra)
        require(a==b==c,'Cell rendering changed gameplay state')
        def difference(other):
            return statistics.mean(abs(full_image[(y*1920+x)*3+k]-other[(y*1920+x)*3+k])
                for y in range(420,660) for x in range(840,1080) for k in range(3))
        deltas=[difference(diffuse_image),difference(shell_image)]
        require(min(deltas)>2,'Clarity or internal structures did not visibly affect the cell interior')
        results['cell_interior_rgb8_mean_absolute_difference']=deltas
    elif args.part=='conquest':
        for side,home,enemy in [('virus',0,1),('immune',1,0)]:
            d,_=run(side+'-both-home-cells-controlled',side,3600,assault(home,enemy))
            require(d['outcome']==1,'Biological conquest failed')
    elif args.part=='invariants':
        for side,home in [('virus',0),('immune',1)]:
            a,pixels=run(side+'-one-frame',side,900,plant(home))
            b,again=run(side+'-two-frames',side,900,plant(home),frames=2)
            require(a==b and pixels==again,'Image history or rendering changed gameplay')
    elif args.part=='lighting':
        raw=out/'raw.render';raw.write_text(write_setting(config,'spatial_denoise',0))
        furnace=out/'ambient-sphere.world';furnace.write_text('ambient .3 .4 .5\ncell neutral 0 0 0 3 .5 .5 .5\n')
        _,hdr=run('uniform-ambient-lambert-sphere',world=furnace,config=raw,hdr=True)
        for y in range(520,560,4):
            for x in range(940,980,4):
                i=(y*1920+x)*3
                require(all(abs(hdr[i+c]-expected)<.0001 for c,expected in enumerate((.15,.20,.25))),
                        'Uniform environment failed diffuse energy check')
        # With environment and decorative background glow off, HDR must be black.
        source=root/'assets/levels/biology-virus.world';text=source.read_text()
        for line in source.read_text().splitlines():
            fields=line.split()
            if fields and fields[0] in ('model','ship','stalk_model','backdrop','spike_weapon'):
                i=2 if fields[0]=='model' else 3 if fields[0]=='ship' else 1
                text=text.replace(fields[i],Path(os.path.relpath((source.parent/fields[i]).resolve(),out)).as_posix())
        dark=out/'dark.world';text=re.sub(r'(?m)^backdrop_soft\s+.*$','backdrop_soft 6 0 .65 .008 7',text)
        text=re.sub(r'(?m)^studio_light.*$','',text)
        dark.write_text(re.sub(r'(?m)^ambient\s+.*$','ambient 0 0 0',text))
        _,black=run('no-environment-no-light',world=dark,config=raw,hdr=True)
        require(max(black)==0,'Unwanted light source in biological scene')
    elif args.part=='combat':
        legacy=root/'tests/e2e/scenes/biology-two-virus.world'
        replay=out/'battle.orders';replay.write_text(assault(0,1));state=out/'battle-final.json'
        prep=subprocess.run([*default_command,'--smoke','--world',str(legacy),
            '--ticks','3600','--replay',str(replay),'--state',str(state)],capture_output=True,text=True,timeout=10)
        require(prep.returncode==0,prep.stdout+prep.stderr);events=json.loads(state.read_text())['effects']
        for name,tick,field in [('traveling-projectiles-and-contrails',240,'projectiles'),
            ('low-health-hit-flash',events['last_low_tick']+1,'low_health'),
            ('red-critical-health',events['last_death_tick'],'low_health'),('unit-death-pop',events['last_death_tick']+1,'pops')]:
            orders='\n'.join(line for line in assault(0,1).splitlines() if int(line.split()[0])<tick)+'\n'
            data,_=run('legacy-'+name,ticks=tick,orders=orders,world=legacy)
            require(data['effects'][field]>0,'Missing rendered '+field)
    elif args.part=='transmission':
        source=root/'assets/levels/biology-virus.world';text=source.read_text()
        for line in source.read_text().splitlines():
            fields=line.split()
            if fields and fields[0] in ('model','ship','stalk_model','backdrop','spike_weapon'):
                i=2 if fields[0]=='model' else 3 if fields[0]=='ship' else 1
                text=text.replace(fields[i],Path(os.path.relpath((source.parent/fields[i]).resolve(),out)).as_posix())
        text=re.sub(r'(?m)^ambient .*$', 'ambient 0 0 0',text)
        text=re.sub(r'(?m)^studio_light key.*$', 'studio_light key -.55 .7 .6 1 .76 .68 0',text)
        text=re.sub(r'(?m)^studio_light fill.*$', 'studio_light fill .65 .2 .75 .68 .8 1 0',text)
        text=re.sub(r'(?m)^backdrop_soft.*$', 'backdrop_soft 6 0 .65 .008 7',text)
        images=[]
        for value,name in [('0','opaque-cells-rim-only'),('.38','translucent-cells-rim-only')]:
            scene=out/(name+'.world');scene.write_text(re.sub(r'(?m)^cell_translucency.*$','cell_translucency '+value+' .12',text))
            _,pixels=run(name,world=scene)
            images.append(sum(pixels[(y*1920+x)*3+c] for y in range(520,560) for x in range(720,750) for c in range(3)))
        require(images[1]>images[0]*1.1,'Rim light did not transmit through the cell membrane')
        results['rim_patch_display_sum']=images
    elif args.part=='buffers':
        run('translucent-cell-buffer-views',ticks=900,orders=plant(0),frames=2,extra=('--debug-views',))
    elif args.part=='background':
        for side,home in [('virus',0),('immune',1)]:
            a,first=run(side+'-soft-glowing-membranes',side,900,plant(home),extra=('--time','0'))
            b,changed=run(side+'-membranes-pulse-peak',side,900,plant(home),extra=('--time','1.75'))
            require(first!=changed,'Background pulse did not change the image')
            require(a['colonies']==b['colonies'] and a['units']==b['units'],'Background animation changed gameplay')
            _,again=run(side+'-membranes-no-history',side,900,plant(home),frames=2,extra=('--time','1.75','--step-seconds','-1.75'))
            require(first==again,'Background retained image history')
    elif args.part=='isolation':
        text=stage(root/'assets/levels/biology-virus.world')
        text=re.sub(r'(?m)^cell_translucency.*$','cell_translucency 0 .025',text)
        text=re.sub(r'(?m)^cell_clarity.*$','cell_clarity 0',text)
        first_world=out/'opaque-baseline.world';first_world.write_text(text)
        a,first=run('opaque-foreground-background-baseline',ticks=900,orders=plant(0),world=first_world,extra=('--time','0'))
        changed_world=out/'strong-unblurred.world';changed_world.write_text(re.sub(r'(?m)^backdrop_soft.*$','backdrop_soft 1 4 .12 .045 7',text))
        b,changed=run('opaque-foreground-independent-background',ticks=900,orders=plant(0),world=changed_world,extra=('--time','0'))
        require(a==b and first!=changed,'Background did not change independently of gameplay')
        for center in (cell_pointer(0),cell_pointer(1)):
            x,y=map(int,center)
            for row in range(y-20,y+20):
                start=(row*1920+x-10)*3;end=(row*1920+x+10)*3
                require(first[start:end]==changed[start:end],'Backdrop changed opaque foreground')
        results['opaque_foreground_interiors_identical']=True
    elif args.part in ('timing','battle-timing'):
        require(os.environ.get('MICROSHIP_VULKAN_VALIDATION')!='1','Disable validation for timings')
        require(args.before and args.before.is_file(),'Need previous complete Windows runtime bundle')
        cases=[(side+'-'+mode,side,home,args.before if mode=='before' else None)
            for side,home in [('virus',0),('immune',1)] for mode in ('before','after')]
        for round in range(2):
            for name,side,home,binary in (cases if round==0 else reversed(cases)):
                battle=args.part=='battle-timing'
                orders=assault(home,1-home).split('300 cell')[0] if battle else plant(home)
                run(name+'-'+str(round),side,360 if battle else 900,orders,frames=2,capture=False,binary=binary)
        results['summary']={name:{key:statistics.mean(values) if values else None
            for key in ('render_ms','application_ms','background_ms','surface_ms','denoise_ms','effects_ms','acceleration_ms','work_ms')
            for values in [[v for run_name,row in results.items() if run_name in (name+'-0',name+'-1') for v in row[key]]]}
            for name,_,_,_ in cases}
    else:
        _,pixels=run('immune-inactive-window','immune',900,plant(1),frames=2,window=True,extra=('--stats',))
        text=overlay_text(pixels);match=re.fullmatch(r'ACQUIRE ([\d.]+) PRESENT ([\d.]+) MS',text[2])
        require(match is not None,'Presentation wait text was unreadable')
        expected=results['immune-inactive-window']['waits'][0]
        require(all(abs(float(x)-y)<.011 for x,y in zip(match.groups(),expected)),
                'Overlay did not show previous completed presentation waits')
        results['immune-inactive-window']['text']=text
        coefficients=[int(v[:-1],16) for v in re.findall(r'0x[0-9a-f]+u',(root/'source/debug_font.h').read_text())]
        lookup={value:chr(i) for i,value in enumerate(coefficients) if value}
        card=[]
        for row in (0,3):
            line=''
            for column in range(41):
                glyph=0
                for y in range(7):
                    for x in range(4):
                        offset=((1080-164+12+row*24+y*2)*1920+1920-464+14+column*10+x*2)*3
                        if max(pixels[offset:offset+3])>180: glyph|=1<<(y*4+x)
                line+=lookup.get(glyph,'?' if glyph else ' ')
            card.append(line.rstrip())
        require('%' in card[0] and '*' in card[1],'Build percentage or production-priority glyph missing')
        results['immune-inactive-window']['card_text']=card
    (out/'results.json').write_text(json.dumps({'part':args.part,'resolution':[1920,1080],
        'budget':[1 if args.part=='probe' else 7,0],'background_visibility_queries_per_pixel':1,'runs':results},indent=2)+'\n')
    print('Biology '+args.part+' GPU E2E passed; native 1080p, three-point/ambient lighting, current-frame rendering.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Biology GPU E2E failed: '+str(error),file=sys.stderr);sys.exit(1)
