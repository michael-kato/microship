#!/usr/bin/env python3
"""Check biological motion and appearance through complete application runs."""
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from asset_data import stage_world,write_world,change
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-animation-') as directory:
    out=Path(directory);data=stage_world(root/'assets/levels/virus-player-level.json',out)
    for b in data['content']['buildings']:b.update(growth_seconds=.01,establishment_seconds=.01);b['branches']['seconds']=.01
    def run(name,world=data,ticks=600,expected=0):
        global count
        source=out/(name+'_world.json');write_world(source,world);state=out/(name+'_state.json');scene=out/(name+'_scene.json')
        result=subprocess.run([*command,'--smoke','--world',str(source),'--ticks',str(ticks),'--world-seed','1234','--state',str(state),'--scene-state',str(scene)],capture_output=True,text=True,timeout=10);count+=1
        assert result.returncode==expected,(name,result.stdout,result.stderr)
        if expected:assert result.stderr and not state.exists();return None,None
        return json.loads(state.read_text()),json.loads(scene.read_text())
    game,scene=run('moving')
    still=deepcopy(data);a=still['appearance']['animation'];a['organelles']['squash']=a['factories']['sway_degrees']=a['membrane']['ripple_strength']=0
    same,fixed=run('fixed',still)
    assert same==game,'Appearance motion changed the game'
    assert fixed['meshes']==scene['meshes'],'Motion changed mesh vertices or topology'
    assert scene['organelle_style']==[data['appearance']['organelle_appearance'][key] for key in ('edge_saturation','edge_width','center_glow','center_power')]
    assert scene['membrane_ripple'][0]==.05 and fixed['membrane_ripple'][0]==0
    organs=[i for i in scene['instances'] if int(i['style'][7])&4]
    assert len(organs)==44, len(organs)  # 34 rooted organs and ten internal networks.
    instances={i['slot']:i for i in scene['instances']};stationary={i['slot']:i for i in fixed['instances']}
    roots=[point for c in game['colonies'] for point in c['attachments']]
    def point(matrix,p):return [matrix[row*4+3]+sum(matrix[row*4+k]*p[k] for k in range(3)) for row in range(3)]
    anchored=0
    for organ in organs:
        matrix=organ['transform'];diagonal=[matrix[0],matrix[5],matrix[10]]
        assert abs(math.prod(diagonal)-1)<3e-6,'Squash changed organelle volume'
        anchored+=min(math.dist(point(matrix,p),p) for p in roots)<1e-4
        assert not organ['static_lighting'] and stationary[organ['slot']]['static_lighting']
        assert stationary[organ['slot']]['transform']==[1,0,0,0,0,1,0,0,0,0,1,0]
        assert matrix!=stationary[organ['slot']]['transform']
    assert anchored==len(roots)==34,'An organelle moved its factory attachment'
    body_base=256;factory_base=body_base+len(data['bodies']);stem_base=factory_base+len(data['bodies'])*128
    for body,colony in enumerate(game['colonies']):
        assert instances[body_base+body]['transform']==stationary[body_base+body]['transform']
        if not colony['active'] or not colony['visible']:continue
        for k,structure in enumerate(colony['structures']):
            slots=[stem_base+(body*128+k)*4+j for j in range(4)]
            if not all(slot in instances for slot in slots):continue
            for left,right in zip(slots,slots[1:]):
                assert math.dist(point(instances[left]['transform'],[0,1,0]),point(instances[right]['transform'],[0,0,0]))<3e-5,'A swaying stem joint opened'
            assert instances[slots[0]]['transform']!=stationary[slots[0]]['transform']
            if structure['parent']<0:
                assert math.dist(point(instances[slots[0]]['transform'],[0,0,0]),structure['stem'][0])<2e-5,'A factory root moved'
    repeat,repeated=run('repeat');assert repeat==game and repeated==scene
    # Different animation rates change poses, while the same game still runs.
    faster=deepcopy(data);faster['appearance']['animation']['organelles']['period_seconds']=7
    other,changed=run('other-period',faster);assert other==game and changed['meshes']==scene['meshes']
    assert any(a['transform']!=b['transform'] for a,b in zip(scene['instances'],changed['instances']))
    absent=deepcopy(data);absent['appearance'].pop('animation');absent['appearance'].pop('organelle_appearance')
    no_motion,optional=run('optional',absent);assert no_motion==game and optional['membrane_ripple'][0]==0
    assert optional['organelle_style']==[1,.5,1,0]
    bad=deepcopy(data);definition=next(d for d in bad['content']['bodies'] if d['name']==bad['bodies'][0]['type'])
    parts=json.loads((out/definition['variation']['parts']).resolve().read_text());parts['parts'][0]['movable']=True
    source=out/'movable_wall.parts.json';source.write_text(json.dumps(parts));definition['variation']['parts']=source.name
    run('movable_wall',bad,expected=1)
    for field,value in [('organelle_appearance.edge_width',0),('organelle_appearance.center_power',17),('animation.organelles.squash',.11),('animation.organelles.period_seconds',0),('animation.membrane.wavelength',0),('animation.membrane.ripple_strength',-.1),('animation.factories.sway_degrees',6),('animation.factories.extra',1)]:
        run('bad_'+str(count),change(data,'appearance.'+field,value),expected=1)
print(f'Cell animation E2E passed: {count} application runs; unchanged game and topology, volume preservation, fixed attachments, connected stems, repeatable poses and rejected settings.')
