#!/usr/bin/env python3
"""Run biological game scenarios through the real Windows application, CPU only."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def require(condition,message):
    if not condition:raise RuntimeError(message)


def main():
    root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
    command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
    count=0
    with tempfile.TemporaryDirectory(prefix='microship-biology-e2e-') as directory:
        out=Path(directory)
        def stage(source):
            text=source.read_text()
            for raw in source.read_text().splitlines():
                fields=raw.split()
                if fields and fields[0] in ('model','ship','stalk_model','backdrop','spike_weapon'):
                    i=2 if fields[0]=='model' else 3 if fields[0]=='ship' else 1
                    text=text.replace(fields[i],Path(os.path.relpath((source.parent/fields[i]).resolve(),out)).as_posix())
            return text
        def run(name,text,ticks=1,orders='',expected=0,extra=()):
            nonlocal count
            scene=out/(name+'.world');scene.write_text(text)
            replay=out/(name+'.orders');replay.write_text(orders)
            state=out/(name+'.json')
            args=[*command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
            if orders:args+=['--replay',str(replay)]
            result=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
            require(result.returncode==expected,name+': '+result.stdout+result.stderr)
            if expected:return
            require('no sun.' in result.stdout,name+': missing ambient-only startup')
            data=json.loads(state.read_text());require(data['sun_radius']==0,name+': unexpected sun')
            require(all('cell' in c and 'nutrients' in c for c in data['colonies']),name+': wrong resource/body contract')
            return data
        for side,player,home,enemy in [('virus',1,0,1),('immune',2,1,0)]:
            text=stage(root/'tests/e2e/scenes'/('biology-two-'+side+'.world'))
            initial=run(side+'-initial',text)
            require(initial['player_faction']==player and len(initial['units'])==11,side+': wrong side')
            require(sum(u['faction']==player and u['kind']==0 for u in initial['units'])==6,side+': starting force')
            plant=f'0 select seedships\n0 order {home}\n'
            growing=run(side+'-growing',text,300,plant)
            require(0<growing['colonies'][home]['growth']<1,side+': node failed to establish')
            mature=run(side+'-production',text,1200,plant)
            require(mature['colonies'][home]['growth']==1 and mature['births']>11,side+': no nutrient-funded production')
            dark=re.sub(r'(?m)^ambient\s+.*$','ambient 0 0 0',text)
            dark_state=run(side+'-lighting-independent',dark,1200,plant)
            require(mature['colonies']==dark_state['colonies'] and mature['units']==dark_state['units'],side+': lighting changed gameplay')
            depleted=re.sub(r'(?m)^nutrients\s+(\d+)\s+.*$',r'nutrients \1 0',text)
            empty=run(side+'-no-nutrients',depleted,1200,plant)
            require(empty['births']==11 and empty['colonies'][home]['energy']==0,side+': unfunded units')
            assault=f'0 select fighters\n0 order {enemy}\n'+plant+f'300 cell {home}\n300 priority seedship\n1200 select seedships\n1200 order {enemy}\n'
            won=run(side+'-capture-both-cells',text,3600,assault)
            require(won['outcome']==1 and all(c['faction']==player and c['growth']==1 for c in won['colonies']),side+': conquest failed')
            require(won['effects']['shots']>0 and won['effects']['hits']>0 and won['effects']['deaths']>0,side+': no projectile combat events')
            again=run(side+'-deterministic',text,3600,assault)
            require(won==again,side+': non-deterministic replay')
            if side=='virus':
                for event,field in [('last_flash_tick','flashes'),('last_low_tick','low_health'),('last_death_tick','pops')]:
                    tick=won['effects'][event]+1
                    observed=run(side+'-'+field,text,tick,'\n'.join(line for line in assault.splitlines() if int(line.split()[0])<tick)+'\n')
                    require(observed['effects'][field]>0,side+': missing '+field)
                orbit=run('orbit-motion',text,1200,plant)
                earlier=run('orbit-earlier',text,1100,plant)
                left={u['generation']:u['position'] for u in earlier['units'] if u['kind']==0}
                require(any(u['generation'] in left and left[u['generation']]!=u['position'] for u in orbit['units'] if u['kind']==0),'Fighters did not orbit')
                require(orbit['effects']['trails']>0,'No fighter trails')

        text=stage(root/'tests/e2e/scenes/biology-two-virus.world')
        for name,bad in [
            ('star-and-ambient',text+'star 0 0 50 2 1 1 1\n'),
            ('ambient-orbit',text+'sun_orbit 0 0 0 1 0 0 120\n'),
            ('negative-ambient',re.sub(r'(?m)^ambient .*$', 'ambient -1 0 0',text)),
            ('invalid-side',text.replace('biology virus','biology alien')),
            ('missing-nutrients',re.sub(r'(?m)^nutrients 0.*$','',text)),
            ('duplicate-nutrients',text+'nutrients 0 1\n'),
            ('missing-backdrop',re.sub(r'(?m)^backdrop\s+\S+','backdrop missing.fmesh',text)),
            ('duplicate-backdrop',text+re.search(r'(?m)^backdrop.*$',text)[0]+'\n'),
            ('invalid-gradient',re.sub(r'(?m)^ambient_gradient.*$','ambient_gradient 1 1 1 -1 0 0',text)),
            ('duplicate-gradient',text+'ambient_gradient 1 1 1 1 1 1\n'),
            ('duplicate-background-budget',text+'backdrop_samples 1\nbackdrop_samples 1\n'),
            ('invalid-background-budget',text+'backdrop_samples 0\n'),
            ('fractional-background-budget',text+'backdrop_samples .5\n'),
            ('duplicate-soft-backdrop',text+'backdrop_soft 64 1 .1 .02 7\n'),
            ('invalid-soft-blur',re.sub(r'(?m)^backdrop_soft.*$','backdrop_soft 0 1 .1 .02 7',text)),
            ('invalid-soft-pulse',re.sub(r'(?m)^backdrop_soft.*$','backdrop_soft 64 1 .1 .5 7',text)),
            ('invalid-soft-period',re.sub(r'(?m)^backdrop_soft.*$','backdrop_soft 64 1 .1 .02 0',text)),
            ('soft-without-model',re.sub(r'(?m)^backdrop\s+.*$','',text)),
            ('incomplete-studio',re.sub(r'(?m)^studio_light rim.*$','',text)),
            ('duplicate-studio',text+'studio_light key 0 1 0 1 1 1 2\n'),
            ('zero-light-direction',re.sub(r'(?m)^studio_light key.*$','studio_light key 0 0 0 1 1 1 1',text)),
            ('invalid-transmission',re.sub(r'(?m)^cell_translucency.*$','cell_translucency 1.1 .1',text)),
            ('duplicate-transmission',text+'cell_translucency .3 .1\n'),
            ('solar-transmission-unsupported',re.sub(r'(?m)^(ambient(?:_gradient)?|studio_light|biology) .*$', '',text)+'star 0 0 40 2 1 1 1\n'),
            ('incomplete-combat',re.sub(r'(?m)^orbit_rules.*$','',text)),
            ('invalid-combat-palette',re.sub(r'(?m)^combat_color virus.*$','combat_color virus 2 1 1',text)),
            ('invalid-backdrop-wave',re.sub(r'(?m)^backdrop_wave.*$','backdrop_wave .5 9 .5',text))]:
            run(name,bad,expected=1)
        print(f'Biology E2E passed: {count} complete application runs; both sides, nodes, nutrient production, capture, lighting independence and bad inputs.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Biology E2E failed: '+str(error),file=sys.stderr);sys.exit(1)
