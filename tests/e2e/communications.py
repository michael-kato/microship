#!/usr/bin/env python3
"""Check faction contacts, territory milestones, alert clicks and sound routing in the app."""
from copy import deepcopy
import argparse
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import wave
from asset_data import stage_world,write_world

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path);parser.add_argument('--output',type=Path)
args=parser.parse_args();binary=args.binary.resolve()
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova_communications_') as temporary:
 out=args.output.resolve() if args.output else Path(temporary);out.mkdir(parents=True,exist_ok=True)
 def run(name,data,ticks=1,extra=(),expected=0):
  global count
  world=out/(name+'_world.json');write_world(world,data);state=out/(name+'_state.json')
  result=subprocess.run([*command,'--smoke','--world',str(world),'--world-seed','1234','--ticks',str(ticks),'--state',str(state),*extra],capture_output=True,text=True,timeout=25);count+=1
  (out/(name+'.log')).write_text(result.stdout+result.stderr)
  assert result.returncode==expected,(name,result.stdout,result.stderr)
  if expected:assert not state.exists();return
  return json.loads(state.read_text()),result.stdout
 base=stage_world(binary.parent/'assets/levels/virus-player-level.json',out)
 base['match'].pop('starts');base['match'].update(auto_colonize=False,fully_established=True)
 base['rules']['ai']['expansion_delay']=300;base['rules']['economy']['enemy_attack_seconds']=10000
 for u in base['content']['units']:
  u.update(speed=.01,range=.01,shot_interval=1000,colonizes=False,manufacture_seconds=10000);u.pop('colonization',None);u.pop('carrier',None)
  if 'blob' in u.get('attack',{}):u['attack']['blob']['range']=.01
 template=deepcopy(base['bodies'][0]);template['radius']=4.2
 def contact(faction):
  d=deepcopy(base);player='immune' if faction=='virus' else 'virus';d['match'].update(player=player,opponent=faction)
  d['bodies']=[dict(deepcopy(template),position=[0,0,0],owner=player),dict(deepcopy(template),position=[80,0,0],owner=faction),dict(deepcopy(template),position=[40,0,0],owner=player)]
  unit=lambda f:next(x['production']['default_unit'] for x in d['content']['factions'] if x['name']==f)
  d['spawns']=[dict(type=unit(player),position=[39,0,7]),dict(type=unit(faction),position=[41,0,7])]
  return d
 for faction,fid in [('virus',1),('immune',2),('fungus',3)]:
  d=contact(faction);first,_=run(faction+'_contact',d)
  alerts=first['communications']['alerts'];assert len(alerts)==1 and alerts[0]['cell']==2 and alerts[0]['pending']
  events=first['communications']['dialogue'];assert [(e['faction'],e['trigger']) for e in events]==[(fid,1),(fid,2)],events
  profile=next(f['personality'] for f in d['content']['factions'] if f['name']==faction);voice=json.loads((out/profile).resolve().read_text())['dialogue']
  assert [e['line'] for e in events]==[voice['ships'],voice['cells']]
  later,_=run(faction+'_stays',d,120)
  assert later['communications']['alerts_emitted']==1 and len(later['communications']['dialogue'])==2,'A continuing contact repeated'
  assert later['communications']['alerts'][0]['pending'],'An unread alert expired'
  reset,_=run(faction+'_restart',d,120,extra=('--restart',))
  assert reset['tick']==0 and not reset['communications']['dialogue'] and not reset['communications']['alerts']
 # A normal click focuses and acknowledges the remote outpost; X only dismisses it.
 d=contact('immune');focused,log=run('focus_alert',d,extra=('--click','1650','50'))
 assert not focused['communications']['alerts'][0]['pending']
 camera=next(x for x in log.splitlines() if x.startswith('Initial camera:'));target=list(map(float,camera.split('target:')[1].rstrip('.').split()))
 assert math.dist(target,[40,0,0])<1e-5,(camera,target)
 closed,log=run('close_alert',d,extra=('--click','1880','40'))
 assert not closed['communications']['alerts'][0]['pending'] and 'target: 0 0 0.' in log
 # A visible enemy near a neutral cell must not create a territory alert.
 neutral=contact('immune');neutral['bodies'][2]['owner']='neutral'
 quiet,_=run('neutral_contact',neutral)
 assert not quiet['communications']['alerts']
 # Keep all contacts when more than three tiles are waiting.
 crowded=contact('immune');crowded['bodies']=crowded['bodies'][:2];crowded['bodies'][1]['position']=[240,0,0]
 crowded['spawns']=[dict(type='virus_fighter',position=[0,0,7])]
 for x in (40,80,120,160):
  crowded['bodies'].append(dict(deepcopy(template),position=[x,0,0],owner='virus'))
  crowded['spawns'].append(dict(type='immune_fighter',position=[x,0,7]))
 all_alerts,_=run('four_alerts',crowded)
 assert {a['cell'] for a in all_alerts['communications']['alerts'] if a['pending']}=={2,3,4,5}
 consumed,log=run('consume_oldest_alert',crowded,extra=('--click','1650','50'))
 assert {a['cell'] for a in consumed['communications']['alerts'] if a['pending']}=={3,4,5}
 # Build the milestone cases from the game's actual seeded 128-cell layout.
 layout=stage_world(binary.parent/'assets/levels/large_level.json',out);seeded,_=run('layout_128',layout)
 assert len(seeded['colonies'])==128
 for faction,fid,controlled in [('virus',1,90),('immune',2,9),('immune',2,10),('immune',2,90),('fungus',3,90)]:
  d=deepcopy(base);player='immune' if faction=='virus' else 'virus';d['match'].update(player=player,opponent=faction);d['spawns']=[];d['bodies']=[]
  for i,c in enumerate(seeded['colonies']):
   d['bodies'].append(dict(deepcopy(template),position=c['position'],radius=c['radius'],owner=player if i==0 else faction if i<=controlled else 'neutral'))
  state,_=run(faction+'_owns_'+str(controlled),d)
  events=[e for e in state['communications']['dialogue'] if e['trigger']==3]
  assert [e['cells'] for e in events]==list(range(10,min(90,controlled)+1,10))
  assert all(e['faction']==fid for e in events)
  if controlled==10:
   again,_=run('threshold_stays_10',d,60)
   assert len([e for e in again['communications']['dialogue'] if e['trigger']==3])==1
 # Mute every clip except one faction's colonization. Nonzero captured PCM proves routing.
 captures=[];bank_source=binary.parent/'assets/audio/audio.json'
 for faction in ('virus','immune','fungus'):
  d=contact('immune' if faction!='immune' else 'virus');d['match'].update(player=faction,opponent='immune' if faction!='immune' else 'virus')
  d['bodies'][0]['owner']=faction;d['bodies'][2]['owner']=faction;d['appearance'].pop('communications',None)
  unit=next(f['production']['default_unit'] for f in d['content']['factions'] if f['name']==faction)
  d['spawns']=[dict(type=unit,position=[0,0,7])]
  bank=json.loads(bank_source.read_text())
  for clip in bank['events'].values():clip.update(file=os.path.relpath((bank_source.parent/clip['file']).resolve(),out),gain=0)
  for f in bank['banks']:
   for name,clip in f['events'].items():clip.update(file=os.path.relpath((bank_source.parent/clip['file']).resolve(),out),gain=.8 if f['name']==faction and name=='colonize' else 0)
  audio=out/(faction+'_bank.json');audio.write_text(json.dumps(bank));capture=out/(faction+'_colonize.wav')
  run(faction+'_audio',d,60,extra=('--audio',str(audio),'--audio-capture',str(capture)))
  with wave.open(str(capture)) as w:
   assert (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes())==(1,2,48000,48000)
   raw=w.readframes(w.getnframes());samples=struct.unpack('<'+'h'*(len(raw)//2),raw)
  assert max(map(abs,samples))>50 and max(map(abs,samples))<30000,(faction,'missing or clipped bank')
  captures.append(raw)
 assert len(set(captures))==3,'Faction sound banks produced identical audio'
 # Invalid external profiles and communication settings fail at startup.
 d=contact('immune');entry=next(f for f in d['content']['factions'] if f['name']=='immune');profile=json.loads((out/entry['personality']).resolve().read_text())
 for name,edit in [('zero_period',lambda p:p['motion'].update(period=0)),('duplicate_milestone',lambda p:p['dialogue']['milestones'][1].update(cells=10)),('missing_milestone',lambda p:p['dialogue']['milestones'].pop())]:
  bad=deepcopy(d);copy=deepcopy(profile);edit(copy);path=out/(name+'_personality.json');path.write_text(json.dumps(copy));next(f for f in bad['content']['factions'] if f['name']=='immune')['personality']=path.name
  run(name,bad,expected=1)
 (out/'test_result.json').write_text(json.dumps(dict(application_runs=count,checks=['all faction first contacts','all nine milestones','no repeat during contact','restart','camera focus','dismiss','faction PCM routing','invalid profiles']),indent=2)+'\n')
 print(f'Communications E2E passed: {count} application runs; three faction voices, nine milestones, first intrusions, tile focus, dismissal, PCM routing and invalid assets.')
