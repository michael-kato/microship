#!/usr/bin/env python3
"""Select factions and close the launch menu with real OS mouse and key input."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from window_input import Desktop
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path);parser.add_argument('--output',type=Path,default=root/'build/start-menu-review');parser.add_argument('--gpu',action='store_true')
a=parser.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
desktop=Desktop();started=time.monotonic()
for choice,faction in ([(1,1)] if a.gpu else [(0,0),(1,1),(2,2),(3,3)]):
    existing=desktop.windows('microship - Choose faction');state=out/(str(choice)+'-state.json')
    args=['--menu','--frames','1','--samples','3','--state',str(state),'--capture',str(out/'menu-game.ppm')] if a.gpu else ([] if choice==0 else ['--smoke','--menu','--state',str(state)])
    p=subprocess.Popen([*command,*args],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        deadline=time.monotonic()+(1.4 if a.gpu else 4);handle=None
        while time.monotonic()<deadline and p.poll() is None:
            found=desktop.windows('microship - Choose faction')-existing
            if found:handle=next(iter(found));break
            time.sleep(.005)
        assert handle,'The launch menu did not appear'
        x,y=desktop.focus_point(handle);x-=80;y-=80
        print('Menu input',choice,'client origin',x,y,flush=True)
        if choice==0:
            from PIL import ImageGrab
            time.sleep(.05)
            ImageGrab.grab(bbox=(x,y,x+1920,y+1080)).save(out/'start-menu.png')
            desktop.escape()
        else:
            desktop.move(x+352+(choice-1)*608,y+570);time.sleep(.02);desktop.button(True,1);time.sleep(.02);desktop.button(False,1)
        output,errors=p.communicate(timeout=max(.1,2-(time.monotonic()-started)) if a.gpu else 5)
        (out/(str(choice)+'.log')).write_text(output+errors)
        assert p.returncode==0,(choice,output,errors)
        assert 'Start menu ready.' in output
        if choice:
            result=json.loads(state.read_text());assert result['player_faction']==faction,(choice,result['player_faction'])
            assert not any(c['active'] for c in result['colonies'])
        else:assert 'Loaded world' not in output,'Closing the menu started a game'
        if a.gpu:
            assert 'Vulkan validation: enabled.' in output
            sys.path.insert(0,str(root/'scripts'));from save_progress import archive_capture
            print(archive_capture(out/'menu-game.ppm','start-menu-virus-launch'))
    finally:
        if p.poll() is None:
            p.terminate()
            try:p.communicate(timeout=.5)
            except subprocess.TimeoutExpired:p.kill();p.communicate()
print('Start menu OS input passed: '+('one native-1080p validated game frame.' if a.gpu else 'default launch, close, and all three faction choices; no GPU work.'))
