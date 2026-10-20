#!/usr/bin/env python3
"""Check neutral starts, settlement, sight input and authored changes in the Windows app."""
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world, write_world, change

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-startup-') as directory:
    out=Path(directory)
    def run(name,data,ticks=1,orders='',extra=(),expected=0):
        global count
        world=out/(name+'-world.json');write_world(world,data)
        replay=out/(name+'.orders');replay.write_text(orders)
        state=out/(name+'.json');scene=out/(name+'-scene.json')
        args=[*command,'--smoke','--world',str(world),'--ticks',str(ticks),'--state',str(state),'--scene-state',str(scene),*extra]
        if orders:args+=['--replay',str(replay)]
        result=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    for path in sorted((root/'assets/levels').glob('*.json')):
        data=stage_world(path,out);player=data['match']['player'];opponent=data['match']['opponent']
        factions={f['name']:f for f in data['content']['factions']}
        home=next(v['cell'] for v in data['match']['starts'] if v['faction']==player)
        enemy=next(v['cell'] for v in data['match']['starts'] if v['faction']==opponent)
        initial,drawing=run(path.stem+'-initial',data)
        assert not any(c['active'] or c['faction'] for c in initial['colonies'])
        assert initial['outcome']==0 and initial['landings']==0
        assert all(any(u['faction']==factions[f]['id'] for u in initial['units']) for f in (player,opponent))
        assert drawing['fog']['blur_radius']==96 and drawing['fog']['sight']
        assert any(m[3]==160 for m in drawing['media']),'Global atmosphere is missing'
        # The neutral cell shapes exist for blur, while discovery still limits orders.
        assert sum(i['mesh']<len(data['bodies']) for i in drawing['instances'])==len(data['bodies'])
        assert not initial['colonies'][enemy]['discovered']
        founders=[u for u in initial['units'] if u['faction']==factions[player]['id'] and u['commanded']]
        assert len(founders)==1 and founders[0]['kind']==0 and founders[0]['task']=='colonize'
        orbit,_=run(path.stem+'-orbit',data,600)
        assert orbit['colonies'][home]['active'] and not orbit['colonies'][enemy]['active'] and orbit['outcome']==0
        assert orbit['landings']==1 and orbit['units'][0]['position']!=initial['units'][0]['position']
        replay=f'0 select all\n0 order {home}\n' if not factions[player]['production'].get('mode')=='spores' else ''
        settled,_=run(path.stem+'-settle',data,2400,replay)
        assert settled['colonies'][home]['active'] and settled['colonies'][enemy]['active'],path.name
        assert settled['colonies'][home]['faction']==factions[player]['id']
        assert settled['colonies'][enemy]['faction']==factions[opponent]['id']
        assert settled['landings']>=2 and settled['outcome']==0
        for cell in (home,enemy):
            c=settled['colonies'][cell]
            assert 0<c['establishment_age']<180
            assert c['production']>0 and any(v['health']<=0 or v['growth']<1 for v in c['structures'])
        reset,_=run(path.stem+'-restart',data,2400,replay,('--restart',))
        assert reset['tick']==0 and reset['landings']==0 and not any(c['active'] for c in reset['colonies'])
    base=stage_world(root/'assets/levels/virus-player-level.json',out)
    early,_=run('no-early-victory',base,600,'0 select all\n0 order 0\n')
    assert early['colonies'][0]['active'] and not early['colonies'][1]['active'] and early['outcome']==0
    # Authored balance is inspected through produced application state.
    immune=stage_world(root/'assets/levels/immune-player-level.json',out)
    initial,_=run('immune-durability',immune)
    types={u['name']:u for u in immune['content']['units']}
    assert types['immune_fighter']['health']==432 and types['immune_fighter']['hits']==24
    assert types['immune_bomber']['health']==720 and types['immune_bomber']['hits']==24
    assert all(u['health']==types[u['type']]['health'] for u in initial['units'])
    for bad in [change(base,'match.starts.0.cell',16),change(base,'match.starts.1.cell',0),
                change(base,'match.starts.0.faction','neutral'),change(base,'match.starts',base['match']['starts'][:1]),
                change(base,'bodies.0.owner','virus'),change(base,'spawns',[]),change(base,'appearance.fog.blur_radius',0),
                change(base,'appearance.fog.sight_feather',0),change(base,'appearance.atmosphere.radius',0),
                change(base,'appearance.atmosphere.scattering',[-1,0,0]),
                change(base,'content.buildings.0.growth_plane.normal',[0,0,0]),
                change(base,'content.buildings.0.growth_plane.depth_ratio',2)]:
        run('invalid-'+str(count),bad,expected=1)
print(f'Startup E2E passed: {count} real-application runs; neutral starts, orbit, both settlements, durability, fog, atmosphere, restart and malformed assets.')
