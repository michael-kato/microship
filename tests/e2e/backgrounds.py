#!/usr/bin/env python3
"""Check backdrop selection, materials, and game-state isolation in the real app."""
from copy import deepcopy
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
from authored_assets import stage_world,write_world
binary=Path(sys.argv[1]).resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='microship-backgrounds-') as directory:
    out=Path(directory);data=stage_world(root/'assets/levels/biology-virus.json',out)
    bank=[data['appearance']['backdrop']['model'],*data['appearance']['backdrop']['variants']]
    def run(name,world,seed=0,expected=0,restart=False):
        global count
        path=out/(name+'-world.json');write_world(path,world);state=out/(name+'-state.json');scene=out/(name+'-scene.json')
        command=[*prefix,'--smoke','--world',str(path),'--world-seed',str(seed),'--ticks','600','--state',str(state),'--scene-state',str(scene)]
        if restart:command+=['--restart']
        result=subprocess.run(command,capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:
            assert result.stderr and not state.exists();return
        selected=re.search(r'Backdrop: (.+)\.\n',result.stdout).group(1).replace('\\','/')
        return json.loads(state.read_text()),json.loads(scene.read_text()),Path(selected).name
    # Live starts use odd seeds. All eight choices must remain reachable.
    seen=set()
    for seed in range(1,128,2):
        state,scene,selected=run('bank-'+str(seed),data,seed)
        choices=[Path(path).name for path in bank];assert selected in choices
        i=choices.index(selected)
        if i in seen:continue
        seen.add(i)
        fixed=deepcopy(data);fixed['appearance']['backdrop'].pop('variants');fixed['appearance']['backdrop']['model']=bank[i]
        fixed_state,fixed_scene,_=run('fixed-'+str(i),fixed,seed)
        assert state==fixed_state and scene==fixed_scene,'Backdrop selection changed simulation or other render input'
        background=next(v for v in scene['instances'] if v['mask']==4)
        mesh=scene['meshes'][background['mesh']]
        assert mesh['triangles']<=35000 and .18<=mesh['roughness'][0]<=mesh['roughness'][1]<.4
        assert mesh['subsurface_weight']==[0,0]
        if len(seen)==8:break
    assert len(seen)==8,'Some background choices were not reachable from live seeds'
    first=run('repeat-a',data,1234);second=run('repeat-b',data,1234)
    assert first==second,'Fixed-seed backdrop is not repeatable'
    reset=run('restart',data,1234,restart=True);initial=run('initial',data,1234)
    assert reset[2]==initial[2] and reset[1]['meshes']==initial[1]['meshes'],'Restart changed backdrop geometry'
    # Render presentation cannot change combat or production results.
    a=deepcopy(data);a['appearance']['backdrop'].pop('variants')
    reference=run('simulation-flesh',a,1234)[0];a['appearance']['backdrop']['model']=bank[-1]
    assert run('simulation-villi',a,1234)[0]==reference
    # Unselected alternatives use the normal mesh validator at startup too.
    (out/'broken.fmesh').write_bytes(b'FLMESH01')
    bad=deepcopy(data);bad['appearance']['backdrop']['variants']=['broken.fmesh'];run('bad-unused-model',bad,expected=1)
    for name,value in [('empty',[]),('duplicate',[bank[0]]),('missing',['absent.fmesh']),('capacity',bank*2),('type',4),('item',[5])]:
        bad=deepcopy(data);bad['appearance']['backdrop']['variants']=value;run('bad-'+name,bad,expected=1)
print(f'Backdrop E2E passed: {count} application runs; eight choices, odd seeds, deterministic layout, restart, independent game state, wet materials, invalid banks rejected.')
