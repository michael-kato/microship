#!/usr/bin/env python3
"""Exercise three-hit spike combat, recovery and the wider map in the real application."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='microship-spikes-') as directory:
    out=Path(directory)
    def stage(side):
        source=root/'assets/levels'/('biology-'+side+'.world');text=source.read_text()
        for raw in source.read_text().splitlines():
            f=raw.split()
            if f and f[0] in ('model','ship','stalk_model','backdrop','spike_weapon'):
                i=2 if f[0]=='model' else 3 if f[0]=='ship' else 1
                text=text.replace(f[i],Path(os.path.relpath((source.parent/f[i]).resolve(),out)).as_posix())
        return text
    def run(name,text,ticks=1,orders='',expected=0,extra=()):
        global count
        scene=out/(name+'.world');scene.write_text(text)
        replay=out/(name+'.orders');replay.write_text('\n'.join(line for line in orders.splitlines() if int(line.split()[0])<ticks)+'\n')
        state=out/(name+'.json');args=[*command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
        if orders:args+=['--replay',str(replay)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        if p.returncode!=expected:raise RuntimeError(name+': '+p.stdout+p.stderr)
        return json.loads(state.read_text()) if not expected else None
    def check_hits(state):
        for u in state['units']:
            maximum=36 if u['kind']==0 else 60
            assert u['wounds'] in (0,1,2)
            assert abs(u['health']-maximum*(3-u['wounds'])/3)<.00001,'Damage was not one of three hits'
        for c in state['colonies']:
            if c['health']>0:assert abs(c['health']-80*(3-c['wounds'])/3)<.00001
    for side,home,enemy,neutral,player in [('virus',0,1,2,1),('immune',1,0,4,2)]:
        text=stage(side);initial=run(side+'-initial',text)
        assert len(initial['colonies'])==5 and all(c['faction']==0 for c in initial['colonies'][2:])
        plant=f'0 select seedships\n0 order {home}\n'
        mature=run(side+'-slow-ai',text,1100,plant)
        fast=run(side+'-fast-ai',re.sub(r'(?m)^ai_production.*$','ai_production 1 22',text),1100,plant)
        own=lambda s:sum(u['faction']==player for u in s['units'])
        other=lambda s:sum(u['faction']!=player for u in s['units'])
        assert own(mature)==own(fast),'AI rate changed player production'
        assert other(mature)<other(fast),'AI production was not reduced'
        captured=run(side+'-neutral-capture',text,1200,f'0 select seedships\n0 order {neutral}\n')
        assert captured['colonies'][neutral]['faction']==player and captured['colonies'][neutral]['growth']==1
        assert captured['outcome']==0,'Neutral capture incorrectly won the game'
        assault=f'0 select fighters\n0 order {enemy}\n'+plant+f'300 cell {home}\n300 priority seedship\n1200 select seedships\n1200 order {enemy}\n'
        won=run(side+'-conquest',text,4200,assault);check_hits(won)
        assert won['outcome']==1 and all(won['colonies'][p]['faction']==player for p in (0,1))
        assert won['effects']['deaths']>0 and won['effects']['hits']>0
        assert won['recovery']['detached']>0,'No physical spike detachment'
        assert won==run(side+'-repeat',text,4200,assault),'Replay was not deterministic'
        tick=won['effects']['last_low_tick']+1
        critical=run(side+'-two-embedded',text,tick,assault);check_hits(critical)
        assert any(u['wounds']==2 for u in critical['units']),'No two-hit survivor'
        if side=='virus': assert any(u['ready_spikes']<22 for u in critical['units'] if u['faction']==1 and u['kind']==0),'Fired spike stayed attached'
        if side=='virus':
            assert won['recovery']['regrown']>0,'No spike regrowth'
            assert won['recovery']['healed']>0,'Friendly orbit did not heal survivors'
            assert all(u['ready_spikes']==22 for u in won['units'] if u['faction']==1 and u['kind']==0),'Spikes failed to regrow'
            # Damage is hit-based: changing historical DPS cannot change the new battle.
            low=re.sub(r'(?m)^fighter_rules 3.5 36 8 ', 'fighter_rules 3.5 36 .01 ',text)
            high=re.sub(r'(?m)^fighter_rules 3.5 36 8 ', 'fighter_rules 3.5 36 800 ',text)
            a=run('low-dps-three-hits',low,800,assault);b=run('high-dps-three-hits',high,800,assault)
            assert a==b,'DPS altered three-hit damage'
            dark=re.sub(r'(?m)^ambient .*$', 'ambient 0 0 0',text)
            unlit=run('dark-rendering-independent',dark,800,assault)
            assert all(unlit[k]==a[k] for k in ('colonies','units','effects','recovery','outcome')),'Rendering affected combat'
    expanded=run('ai-neutral-expansion',stage('virus'),4200)
    assert any(c['faction']==2 and c['growth']>0 for c in expanded['colonies'][2:]),'AI did not contest neutral cells'
    assert all(u['target']!=0 for u in expanded['units'] if u['faction']==2),'AI rushed home before the original assault delay'
    reset=run('restart-loaded-scene',stage('virus'),900,'0 select seedships\n0 order 0\n',extra=('--restart',))
    assert reset['tick']==0 and reset['births']==11 and reset['recovery']=={'healed':0,'regrown':0,'detached':0}
    assert all(c['health']==0 for c in reset['colonies']) and all(u['wounds']==0 and u['ready_spikes']==22 for u in reset['units'])
    text=stage('virus')
    for name,bad in [
        ('bad-count',re.sub(r'(?m)^spike_weapon (\S+) 22',r'spike_weapon \1 33',text)),
        ('duplicate-spikes',text+re.search(r'(?m)^spike_weapon.*$',text)[0]+'\n'),
        ('missing-recovery',re.sub(r'(?m)^recovery_rules.*$','',text)),
        ('bad-recovery',re.sub(r'(?m)^recovery_rules.*$','recovery_rules 0 3 1.6',text)),
        ('bad-ai-rate',re.sub(r'(?m)^ai_production.*$','ai_production 0 22',text)),
        ('bad-clarity',re.sub(r'(?m)^cell_clarity.*$','cell_clarity 1.1',text)),
        ('bad-injury',re.sub(r'(?m)^injury_colors.*$','injury_colors -1 0 0 0 0 0',text)),
        ('missing-spike-mesh',re.sub(r'(?m)^spike_weapon \S+', 'spike_weapon missing.fmesh',text)),
    ]:run(name,bad,expected=1)
print(f'Spike gameplay E2E passed: {count} real-application runs; three-hit combat, detachment/regrowth, friendly healing, both roles, neutral capture, AI pacing and invalid assets.')
