#!/usr/bin/env python3
"""Run scouting, bomber and factory scenarios in the Windows application."""
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from asset_data import established_world,units,producers,branches,change,remove,write_world

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='microship-gameplay-') as directory:
    out=Path(directory)
    def stage(side):
        return established_world(root/'assets/levels'/('biology-'+side+'.json'),out)
    def run(name,text,ticks=1,orders='',extra=(),expected=0):
        global count
        scene=out/(name+'-world.json');write_world(scene,text)
        replay=out/(name+'.orders');replay.write_text(orders)
        state=out/(name+'.json')
        args=[*command,'--smoke','--world',str(scene),'--ticks',str(ticks),'--state',str(state),*extra]
        if orders:args+=['--replay',str(replay)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=10);count+=1
        assert p.returncode==expected,(name,p.stdout,p.stderr)
        if expected:return None,p.stdout
        d=json.loads(state.read_text())
        definitions={u['name']:u for u in text['content']['units']}
        for u in d['units']:
            assert all(math.isfinite(x) for x in u['position']),name+': invalid position'
            v=definitions[u['type']];maximum=v['health'];hits=v['hits']
            assert 0<=u['wounds']<hits and abs(u['health']-maximum*(hits-u['wounds'])/hits)<.00001,name+': invalid hit count'
        return d,p.stdout
    def active(c):return sum(s['health']>0 for s in c['structures'])
    for side,player,home,enemy,neutral in [('virus',1,0,1,2),('immune',2,1,0,4)]:
        text=stage(side)
        initial,log=run(side+'-initial',text)
        assert initial['player_faction']==player and initial['outcome']==0
        assert active(initial['colonies'][home])==10 and active(initial['colonies'][enemy])==10
        assert not initial['colonies'][enemy]['discovered']
        assert not any(u['visible'] for u in initial['units'] if u['faction']!=player)
        target='-52 -4 0' if side=='virus' else '48 14 0'
        assert '; target: '+target+'.' in log,'Startup camera did not focus home'
        pan,_=run(side+'-camera-keeps-enemy-hidden',text,extra=('--camera-drag','-6000','0','--camera-zoom','-1000'))
        assert pan==initial,'Camera movement changed discovery or simulation'
        _,zoom_log=run(side+'-zoom-limit',text,extra=('--camera-zoom','-1000',))
        line=next(line for line in zoom_log.splitlines() if line.startswith('Initial camera: '))
        eye,target_text=line[len('Initial camera: '):-1].split('; target: ')
        eye=list(map(float,eye.split()));center=list(map(float,target_text.split()))
        distance=math.sqrt(sum((a-b)**2 for a,b in zip(eye,center)))
        assert abs(distance-38)<.001,'Zoom exceeded the map scale limit'
        visible_width=2*distance*math.tan(math.radians(22.5))*1920/1080
        assert visible_width<100,'The map fits within one view at maximum zoom'
        hidden,_=run(side+'-unknown-order',text,orders=f'0 select all\n0 order {enemy}\n')
        assert hidden['order']['count']==0,'An order exposed an unknown cell'
        assert all(u['target']==home for u in hidden['units'] if u['selected'])
        for group in ('fighters','bombers'):
            captured,_=run(side+'-colonize-'+group,text,1800,f'0 select {group}\n0 move '+('-30 0 6.4' if side=='virus' else '24 9 6.4')+f'\n240 order {neutral}\n')
            assert captured['colonies'][neutral]['faction']==player and captured['colonies'][neutral]['active'] and 0<captured['colonies'][neutral]['growth']<=1,(side,group,'colonization failed')
            original={u['generation'] for u in initial['units'] if u['faction']==player}
            remaining={u['generation'] for u in captured['units']}
            assert len(original-remaining)==1,'More than one player unit was consumed for one cell'
            assert captured['outcome']==0,'A neutral capture ended the match'
        early,_=run(side+'-one-structure',text,900)
        grown,_=run(side+'-branch-growth',text,3000)
        single=branches(text,count=1)
        baseline,_=run(side+'-single-production',single,3000)
        assert active(early['colonies'][home])==10 and active(grown['colonies'][home])==11
        own=lambda s:sum(u['faction']==player for u in s['units'])
        assert grown['colonies'][home]['production']>baseline['colonies'][home]['production'],'New structures did not increase production rate'
        priority,_=run(side+'-produce-bombers',text,2400,f'0 cell {home}\n0 priority bomber\n')
        assert sum(u['faction']==player and u['kind']==1 for u in priority['units'])>1
        enemy_pos='48 14 7' if side=='virus' else '-52 -4 7'
        scout=f'0 select fighters\n0 move {enemy_pos}\n'
        scouting_ticks=3000
        found,_=run(side+'-scout',text,scouting_ticks,scout)
        assert found['colonies'][enemy]['discovered'],'Scouts did not discover the enemy'
        assert found==run(side+'-repeat',text,scouting_ticks,scout)[0],'Replay was not deterministic'
        reset,_=run(side+'-restart',text,1800,scout,extra=('--restart',))
        assert reset['tick']==0 and not reset['colonies'][enemy]['discovered']
        assert all(active(c)==c['root_count'] for c in reset['colonies']),'Restart retained branches'
        dark=change(text,'appearance.ambient',[0,0,0])
        unlit,_=run(side+'-lighting-independent',dark,scouting_ticks,scout,extra=('--samples','1'))
        assert all(unlit[k]==found[k] for k in ('colonies','units','effects','recovery','outcome')),'Light changed the game'
    # Short combat scene: ordinary startup metadata, closer homes and no AI expansion.
    compact=stage('virus')
    for body in compact['bodies']:
        if body['owner']=='virus':body['position'][:2]=[-8,0]
        elif body['owner']=='immune':body['position'][:2]=[8,0]
        else:body['position'][0]+=300
    for spawn in compact['spawns']:
        dx,dy=(44,4) if spawn['type'].startswith('virus_') else (-40,-14)
        spawn['position'][0]+=dx;spawn['position'][1]+=dy
    compact=remove(compact,'rules.exploration')
    compact=change(compact,'rules.ai.expansion_delay',300)
    compact=change(compact,'rules.economy',dict(storage=180,manufacture_seconds=1000,landing_seconds=1.5,enemy_attack_seconds=300))
    compact=branches(compact,count=1)
    # This fixture checks explicit primary target masks. Default units also have a slow secondary attack.
    for unit in compact['content']['units']:unit.pop('secondary_attack',None)
    fighters,_=run('fighters-cannot-bomb',compact,2400,'0 select fighters\n0 order 1\n')
    assert fighters['effects']['hits']>0 and fighters['effects']['deaths']>0,'No unit combat'
    assert fighters['colonies'][1]['health']==480 and fighters['colonies'][1]['wounds']==0,'Fighters damaged a factory'
    # Move defenders away while keeping both reusable templates in the scene.
    no_defenders=change(compact,'spawns',compact['spawns'])
    for spawn in no_defenders['spawns']:
        if spawn['type'].startswith('immune_'):spawn['position'][0]=1000
    bombers,_=run('bombers-destroy-factory',no_defenders,5400,'0 select bombers\n0 order 1\n')
    assert bombers['outcome']==1 and bombers['sound_events']['win']==1,'Bomber did not destroy the enemy factory'
    assert bombers['colonies'][1]['health']==0 or bombers['colonies'][1]['faction']==bombers['player_faction'],'The target still has enemy production'
    expanded_target=branches(no_defenders,seconds=1,count=9)
    for building in expanded_target['content']['buildings']:
        building['health']=80;building['hits']=3;building['branches'].update(health=45,hits=2)
    destroyed,_=run('destroy-grown-factory',expanded_target,5400,'0 select bombers\n0 order 1\n')
    assert destroyed['outcome']==1 and destroyed['sound_events']['win']==1
    # Continued play can establish a new friendly factory after enemy supports are lost.
    assert destroyed['colonies'][1]['faction']==destroyed['player_faction'] or all(s['health']==0 for s in destroyed['colonies'][1]['structures']),'Root death left live enemy branches'
    text=stage('virus')
    capacity=units(text,group='fighter',cost=.01)
    capacity=change(capacity,'rules.economy',dict(storage=10000,manufacture_seconds=.001,landing_seconds=1.5,enemy_attack_seconds=300))
    capacity=producers(capacity,growth=1,collection=100)
    capacity=change(capacity,'rules.ai.expansion_delay',300)
    full,_=run('capacity-holds-production',capacity,1500)
    assert len(full['units'])==256 and any(c['capacity_blocked'] for c in full['colonies'])
    for name,bad in [
        ('unsupported-bio-owner',change(text,'bodies.1.owner','robotic')),
        ('missing-factory-side',remove(text,'content.buildings.1')),
        ('invalid-branch-count',branches(text,count=129)),
        ('zero-plane',branches(text,normal=[0,0,0])),
        ('invalid-sight',change(text,'rules.exploration.sight_radius',float('nan'))),
        ('missing-factory-mesh',change(text,'content.buildings.0.model','missing.fmesh')),
        ('invalid-interior-radius',change(text,'bodies.0.medium.radius_fraction',2)),
        ('invalid-interior-coefficient',change(text,'bodies.0.medium.scattering',[-1,.1,.1])),
    ]:run(name,bad,expected=1)
    for name,orders in [('bad-move','0 move nan 0 0\n'),('extra-move','0 move 0 0 0 1\n'),('unsorted','5 select all\n0 move 0 0 0\n')]:
        run(name,text,10,orders,expected=1)
print(f'Gameplay E2E passed: {count} application runs. Scouting, both unit roles, sacrifice, factories, production, combat, restart and input checks.')
