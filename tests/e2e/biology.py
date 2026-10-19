#!/usr/bin/env python3
"""Run biological game scenarios through the real Windows application, CPU only."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from asset_data import established_world,change,remove,write_world


def require(condition,message):
    if not condition:raise RuntimeError(message)


def main():
    root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
    command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
    count=0
    with tempfile.TemporaryDirectory(prefix='nanova-biology-e2e-') as directory:
        out=Path(directory)
        def stage(source):
            return established_world(source,out)
        def run(name,text,ticks=1,orders='',expected=0,extra=()):
            nonlocal count
            scene=out/(name+'-world.json');write_world(scene,text)
            replay=out/(name+'.orders');replay.write_text(orders)
            state=out/(name+'.json')
            args=[*command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
            if orders:args+=['--replay',str(replay)]
            result=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
            require(result.returncode==expected,name+': '+result.stdout+result.stderr)
            if expected:return
            require('Ambient diffuse RGB:' in result.stdout,name+': missing ambient startup')
            data=json.loads(state.read_text())
            require(all('cell' in c and 'nutrients' in c for c in data['colonies']),name+': wrong resource/body contract')
            return data
        for side,player,home,enemy in [('virus',1,0,1),('immune',2,1,0)]:
            text=stage(root/'tests/e2e/scenes'/(side+'-two-cell-test-level.json'))
            initial=run(side+'-initial',text)
            require(initial['player_faction']==player and len(initial['units'])==11,side+': wrong side')
            require(sum(u['faction']==player and u['kind']==0 for u in initial['units'])==6,side+': starting force')
            require(initial['colonies'][home]['health']==(240 if player==1 else 480) and initial['colonies'][home]['growth']==1,side+': missing starting factory')
            mature=run(side+'-production',text,1800)
            require(mature['births']>11,side+': no nutrient-funded production')
            require(sum(s['health']>0 for s in mature['colonies'][home]['structures'])==6,side+': no branch growth')
            dark=change(text,'appearance.ambient',[0,0,0])
            dark_state=run(side+'-lighting-independent',dark,1800)
            require(mature['colonies']==dark_state['colonies'] and mature['units']==dark_state['units'],side+': lighting changed gameplay')
            depleted=change(text,'bodies',text['bodies'])
            for body in depleted['bodies']:body['site']['availability']=0
            empty=run(side+'-no-nutrients',depleted,1800)
            require(empty['births']==11 and empty['colonies'][home]['energy']==0,side+': unfunded units')
            require(mature==run(side+'-repeat',text,1800),side+': non-deterministic replay')
            earlier=run(side+'-orbit-earlier',text,1700)
            left={u['generation']:u['position'] for u in earlier['units'] if u['kind']==0}
            require(any(u['generation'] in left and left[u['generation']]!=u['position'] for u in mature['units'] if u['kind']==0),'Fighters did not orbit')
            require(mature['effects']['trails']>0,'No fighter trails')

        text=stage(root/'tests/e2e/scenes/virus-two-cell-test-level.json')
        for name,bad in [
            ('unknown-light',change(text,'appearance.sun',{})),
            ('negative-ambient',change(text,'appearance.ambient',[-1,0,0])),
            ('invalid-side',change(text,'match.player','alien')),
            ('missing-nutrients',remove(text,'bodies.0.site.availability')),
            ('missing-backdrop',change(text,'appearance.backdrop.model','missing.fmesh')),
            ('invalid-gradient',change(text,'appearance.gradient.bottom',[-1,0,0])),
            ('invalid-background-budget',change(text,'appearance.backdrop.samples',0)),
            ('fractional-background-budget',change(text,'appearance.backdrop.samples',.5)),
            ('invalid-soft-blur',change(text,'appearance.backdrop.soft.blur_radius',0)),
            ('invalid-soft-pulse',change(text,'appearance.backdrop.soft.pulse_amplitude',.5)),
            ('invalid-soft-period',change(text,'appearance.backdrop.soft.period_seconds',0)),
            ('soft-without-model',remove(text,'appearance.backdrop.model')),
            ('incomplete-studio',remove(text,'appearance.lights.rim')),
            ('zero-light-direction',change(text,'appearance.lights.key.direction',[0,0,0])),
            ('invalid-transmission',change(text,'appearance.thin_surface.transmission',1.1)),
            ('incomplete-combat',remove(text,'rules.orbit')),
            ('invalid-combat-palette',change(text,'content.factions.0.color',[2,1,1])),
            ('invalid-backdrop-wave',change(text,'appearance.backdrop.wave.amplitude',.5)),
        ]:run(name,bad,expected=1)
        print(f'Biology E2E passed: {count} complete application runs; both sides, factories, nutrient production, orbit movement, lighting independence and bad inputs.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Biology E2E failed: '+str(error),file=sys.stderr);sys.exit(1)
