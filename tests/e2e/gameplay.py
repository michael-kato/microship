#!/usr/bin/env python3
"""Gameplay scenarios through the complete Windows application; no GPU required."""
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    binary=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parents[2]
    source=root/'assets/levels/prototype.world';text=source.read_text()
    command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
    with tempfile.TemporaryDirectory(prefix='microship-gameplay-e2e-') as temporary:
        directory=Path(temporary)
        # The actual application loads every scenario through its ordinary asset path.
        for line in text.splitlines():
            fields=line.split()
            if fields and fields[0] in ('model','ship','stalk_model'):
                i=2 if fields[0]=='model' else 3 if fields[0]=='ship' else 1
                resolved=(source.parent/fields[i]).resolve()
                text=text.replace(fields[i],Path(os.path.relpath(resolved,directory)).as_posix())
        results={}
        def run(name,ticks,orders='',world=text,expected=0,extra=()):
            scene=directory/(name+'.world');scene.write_text(world)
            replay=directory/(name+'.orders');replay.write_text(orders)
            state=directory/(name+'.json')
            args=[*command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
            if orders:args+=['--replay',str(replay)]
            result=subprocess.run(args,capture_output=True,text=True,timeout=10)
            if result.returncode!=expected:raise RuntimeError(name+': '+result.stdout+result.stderr)
            if expected:return
            d=json.loads(state.read_text());results[name]=d
            for colony in d['colonies']:
                if not (0<=colony['growth']<=1 and 0<=colony['energy']<=10000):raise RuntimeError(name+': invalid economy state')
            for unit in d['units']:
                if not all(math.isfinite(x) for x in unit['position']) or unit['health']<=0:raise RuntimeError(name+': invalid live unit')
            return d
        plant='0 select seedships\n0 order 0\n'
        initial=run('initial',1)
        assert len(initial['units'])==11 and initial['landings']==0
        growing=run('growing',300,plant)
        assert growing['landings']==2 and 0<growing['colonies'][0]['growth']<1
        economy=run('manufacture-fighters',1200,plant)
        assert economy['colonies'][0]['growth']==1 and economy['births']>11
        assert sum(u['faction']==1 and u['kind']==0 for u in economy['units'])>6
        seed_orders=plant+'300 planet 0\n300 priority seedship\n'
        seeds=run('manufacture-seedship',1800,seed_orders)
        assert any(u['faction']==1 and u['kind']==1 for u in seeds['units'])
        blocked=run('blocked-landing',720,'0 select seedships\n0 order 1\n')
        assert blocked['blocked_landings']>0 and blocked['colonies'][1]['faction']==2
        assert blocked['colonies'][0]['health']==0 and blocked['outcome']==-1
        dark=re.sub(r'(?m)^star\s+[^\n]+','star 0 -40 -80 5 1 .85 .6',text)
        dark=re.sub(r'(?m)^sun_orbit\s+[^\n]+','',dark)
        night=run('night-no-production',1800,plant,dark)
        assert night['colonies'][0]['growth']==1 and night['colonies'][0]['sunlight']==0
        assert night['colonies'][0]['energy']==0 and night['births']==11
        assault='0 select fighters\n0 order 1\n'+seed_orders+'1800 select seedships\n1800 order 1\n'
        won=run('clear-land-and-win',3600,assault)
        assert won['outcome']==1 and won['losses']>=3
        assert all(c['health']>0 and c['faction']==1 for c in won['colonies'])
        repeated=run('deterministic-replay',3600,assault)
        assert repeated==won
        capacity=re.sub(r'(?m)^fighter_rules\s+[^\n]+','fighter_rules 3.5 36 8 3.2 .01',text)
        capacity=re.sub(r'(?m)^economy_rules\s+[^\n]+','economy_rules 10000 .001 1.5 90',capacity)
        capacity=re.sub(r'(?m)^stalk_rules\s+[^\n]+','stalk_rules 1 80 100 2 1.8',capacity)
        full=run('capacity-pauses-production',1500,plant,capacity)
        assert len(full['units'])==256 and any(c['capacity_blocked'] for c in full['colonies'])
        run('reject-unsorted-orders',10,'5 select all\n0 order 0\n',expected=1)
        run('reject-invalid-planet',10,'0 order 999\n',expected=1)
        run('reject-unknown-command',10,'0 teleport 1\n',expected=1)
        run('reject-nonfinite-rule',1,world=text.replace('fighter_rules 3.5','fighter_rules nan'),expected=1)
        run('reject-incomplete-rules',1,world=re.sub(r'(?m)^economy_rules.*$','',text),expected=1)
        run('reject-missing-site',1,world=re.sub(r'(?m)^colony_site 0.*$','',text),expected=1)
        run('reject-duplicate-rule',1,world=text+'fighter_rules 3.5 36 8 3.2 12\n',expected=1)
        run('reject-absolute-stalk-path',1,world=re.sub(r'(?m)^stalk_model.*$','stalk_model /missing.fmesh',text),expected=1)
        run('reject-invalid-ticks',1,expected=2,extra=('--ticks','-1'))
        print('Gameplay E2E passed: 18 complete application runs; commands, landing, growth, sunlight, production, combat, conquest, determinism, capacity and bad inputs.')

if __name__=='__main__':
    try:main()
    except (AssertionError,OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Gameplay E2E failed: '+str(error),file=sys.stderr);raise
