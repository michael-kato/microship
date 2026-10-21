#!/usr/bin/env python3
"""Check full AI production against the previous handicap in complete matches."""
from copy import deepcopy
import json,subprocess,sys,tempfile
from pathlib import Path
from asset_data import established_world,write_world,branches,producers,units
root=Path(__file__).resolve().parents[2];binary=Path(sys.argv[1]).resolve();prefix=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
count=0
with tempfile.TemporaryDirectory(prefix='nanova-difficulty-') as directory:
 out=Path(directory)
 def run(name,data,expected=0):
  global count
  wp=out/(name+'-world.json');write_world(wp,data);state=out/(name+'.json')
  result=subprocess.run([*prefix,'--smoke','--world',str(wp),'--ticks','900','--world-seed','1234','--state',str(state)],capture_output=True,text=True,timeout=10);count+=1
  assert result.returncode==expected,(name,result.stdout,result.stderr)
  if not expected:return json.loads(state.read_text())
 for level in sorted((root/'assets/levels').glob('*.json')):
  base=established_world(level,out)
  if base['match'].get('player_ai'):continue  # The stress scene runs every faction under AI control.
  # Mature leaves and sufficient nutrients separate manufacture from deployment.
  fast=units(producers(branches(base,count=1),growth=.01,establishment_seconds=.01,collection=100),cost=.1)
  for body in fast['bodies']:body['position'][0]*=10
  for spawn in fast['spawns']:spawn['position'][0]*=10
  fast['rules']['ai']['expansion_delay']=300;fast['rules']['economy']['enemy_attack_seconds']=300
  current=run(level.stem+'-full',fast);slow=deepcopy(fast);slow['rules']['ai']['production_rate']=.6;previous=run(level.stem+'-handicap',slow)
  player=current['player_faction'];enemy=next(f['id'] for f in fast['content']['factions'] if f['name']==fast['match']['opponent'])
  produced=lambda state,faction:sum(sum(c['produced']) for c in state['colonies'] if c['faction']==faction)
  assert produced(current,enemy)>produced(previous,enemy),(level.name,produced(current,enemy),produced(previous,enemy))
  assert produced(current,player)==produced(previous,player),'AI difficulty changed player manufacture'
  assert current['effects']['hits']==previous['effects']['hits']==0,'Combat affected the rate comparison'
  print(level.name,'enemy products:',produced(previous,enemy),'->',produced(current,enemy))
 bad=deepcopy(fast);bad['rules']['ai']['production_rate']=1.01;run('above-maximum',bad,1)
print(f'AI difficulty E2E passed: {count} application runs; faster enemy production for every level, unchanged player production, maximum rate enforced.')
