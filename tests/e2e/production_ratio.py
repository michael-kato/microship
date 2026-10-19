#!/usr/bin/env python3
"""Check production ratios through the Windows application and its input controls."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import established_world, write_world, branches, producers, units

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-ratio-') as directory:
    out=Path(directory)
    def run(name,text,ticks=1,orders='',extra=(),expected=0):
        global count
        scene=out/(name+'-world.json');write_world(scene,text)
        replay=out/(name+'.orders');replay.write_text(orders)
        state=out/(name+'.json')
        args=[*command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
        if orders:args+=['--replay',str(replay)]
        process=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert process.returncode==expected,(name,process.stdout,process.stderr)
        return (json.loads(state.read_text()) if expected==0 else None),process.stdout
    for side,home in [('virus',0),('immune',1)]:
        base=established_world(root/'assets/levels'/(side+'-player-level.json'),out)
        fast=units(producers(branches(base,count=1),growth=.01,establishment_seconds=.01,collection=100),cost=.1)
        fast['rules']['economy']['manufacture_seconds']=.05
        fast['rules']['ai']['expansion_delay']=300
        for percent in (0,1,25,50,73,99,100):
            replay=f'0 cell {home}\n0 ratio {percent}\n'
            state,_=run(side+'-'+str(percent),fast,450,replay)
            colony=state['colonies'][home];fighters,bombers=colony['produced'];total=fighters+bombers
            assert total>=100,(side,percent,total)
            assert abs(fighters-total*percent/100)<=.5+1e-6,(side,percent,colony)
            assert colony['fighter_percent']==percent and colony['ratio_enabled']
            if percent==50:
                repeated,_=run(side+'-repeat',fast,450,replay)
                assert repeated==state,'Ratio replay is not deterministic'
                held,_=run(side+'-held',fast,450,replay+''.join(f'{tick} ratio 50\n' for tick in range(1,450)))
                assert held==state,'Repeated pointer values changed production'
        # Ratio changes must not change a unit already paid for and in production.
        slow=deepcopy(fast);slow['rules']['economy']['manufacture_seconds']=6
        before,_=run(side+'-building-before',slow,120,f'0 cell {home}\n0 ratio 100\n')
        changed,_=run(side+'-building-after',slow,121,f'0 cell {home}\n0 ratio 100\n120 ratio 0\n')
        a=before['colonies'][home];b=changed['colonies'][home]
        assert a['building'] and b['building'] and a['building_type']==b['building_type']
        assert b['manufacturing']>a['manufacturing'] and abs(b['energy']-a['energy'])<2
        reset,_=run(side+'-restart',fast,450,f'0 cell {home}\n0 ratio 25\n',('--restart',))
        assert reset['tick']==0 and reset['colonies'][home]['fighter_percent']==100
        assert reset['colonies'][home]['produced']==[0,0] and not reset['colonies'][home]['ratio_enabled']
        # A different cell cannot inherit the selected cell's ratio.
        known=deepcopy(fast);known['rules'].pop('exploration',None)
        isolated,_=run(side+'-cell-scope',known,1,f'0 cell {home}\n0 ratio 25\n0 cell {1-home}\n0 ratio 75\n')
        assert isolated['colonies'][home]['fighter_percent']==25
        assert not isolated['colonies'][1-home]['ratio_enabled']
        # Exercise clicks and a complete drag through the same input path as the window.
        baseline,log=run(side+'-pointer-baseline',base,orders=f'0 cell {home}\n')
        camera=next(line for line in log.splitlines() if line.startswith('Initial camera:'))
        for x,expected in [(1472,100),(1574,75),(1676,50),(1778,25),(1880,0)]:
            state,_=run(side+'-click-'+str(x),base,orders=f'0 cell {home}\n',extra=('--click',str(x),'1030'))
            assert state['colonies'][home]['fighter_percent']==expected,(side,x,expected,state['colonies'][home]['fighter_percent'])
        for x,expected in [(1200,100),(1676,50),(1910,0)]:
            state,log=run(side+'-drag-'+str(x),base,orders=f'0 cell {home}\n',extra=('--pointer-drag','1676','1030',str(x),'600'))
            assert state['colonies'][home]['fighter_percent']==expected,(side,x,expected,state['colonies'][home]['fighter_percent'])
            assert camera in log,'Slider drag moved the camera'
        # Insufficient nutrients must not advance the ratio sequence while waiting.
        poor=producers(fast,collection=.01)
        poor['bodies'][home]['site']['availability']=.01
        poor['rules']['economy']['manufacture_seconds']=.01
        first,_=run(side+'-poor-early',poor,2,f'0 cell {home}\n0 ratio 25\n')
        last,_=run(side+'-poor-later',poor,450,f'0 cell {home}\n0 ratio 25\n')
        assert first['colonies'][home]['produced']==last['colonies'][home]['produced']==[0,0]
        assert not last['colonies'][home]['building']
        for invalid in ('101','-1','nan','12.5'):
            run(side+'-invalid-'+invalid,base,orders=f'0 ratio {invalid}\n',expected=1)
    fungus=established_world(root/'assets/levels/fungus-player-level.json',out)
    original,_=run('automatic-original',fungus)
    changed,_=run('automatic-ratio',fungus,orders='0 cell 0\n0 ratio 50\n',extra=('--click','1676','1030'))
    assert original==changed,'Ratio controls changed automatic production'
print(f'Production ratio E2E passed: {count} real-application runs; mixes, endpoints, input, waiting, ownership, restart and invalid orders.')
