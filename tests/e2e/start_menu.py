#!/usr/bin/env python3
"""Select factions and close the launch menu with real OS mouse and key input."""
import argparse
import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import time
from window_input import Desktop
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path);parser.add_argument('--output',type=Path,default=root/'build/start-menu-review');parser.add_argument('--gpu',action='store_true')
parser.add_argument('--samples',type=int,choices=(1,3,6),default=1)
parser.add_argument('--cold-start',action='store_true')
parser.add_argument('--queued-input',action='store_true',help='Use the real Win32 message queue; does not verify physical desktop input')
a=parser.parse_args();process_limit=10 if a.cold_start else 2;out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(a.binary.resolve())] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(a.binary.resolve())]
# Bounded rendering checks do not open a physical audio device. The normal
# application keeps live audio; its device startup is outside this input check.
application_env=os.environ.copy()
if a.gpu and sys.platform!='win32':
    application_env['WINEDLLOVERRIDES']=application_env.get('WINEDLLOVERRIDES','')+';winepulse.drv,winealsa.drv=d'
desktop=None if a.queued_input else Desktop();started=time.monotonic()
helper=a.binary.resolve().parent/'nanova_input_probe.exe'
if a.queued_input:assert helper.is_file(),'Build nanova_input_probe first'
helper_prefix=[str(helper)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(helper)]
for choice,faction in ([(1,1)] if a.gpu else [(0,0),(1,1),(2,2),(3,3)]):
    existing=desktop.windows('nanova - Choose faction') if desktop else set();state=out/(str(choice)+'-state.json')
    args=['--menu','--no-activate','--frames','1','--samples',str(a.samples),'--state',str(state),'--capture',str(out/'menu-game.ppm')] if a.gpu else ([] if choice==0 else ['--smoke','--menu','--state',str(state)])
    p=subprocess.Popen([*command,*args],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True,env=application_env)
    try:
        if a.queued_input:
            args=['--menu-close'] if choice==0 else ['--menu-click',str(352+(choice-1)*608),'570']
            env=os.environ.copy();env['NANOVA_VULKAN_VALIDATION']='0'
            result=subprocess.run([*helper_prefix,*args],capture_output=True,text=True,env=env,timeout=2)
            assert result.returncode==0,(result.stdout,result.stderr)
        else:
            deadline=time.monotonic()+(process_limit-.6 if a.gpu else 4);handle=None
            while time.monotonic()<deadline and p.poll() is None:
                found=desktop.windows('nanova - Choose faction')-existing
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
        output,errors=p.communicate(timeout=max(.1,process_limit-(time.monotonic()-started)) if a.gpu else 5)
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
    except subprocess.TimeoutExpired as error:
        def text(value):return value.decode(errors='replace') if isinstance(value,bytes) else value or ''
        (out/(str(choice)+'.log')).write_text(text(error.stdout)+text(error.stderr))
        raise
    finally:
        if p.poll() is None:
            if sys.platform=='win32':p.terminate()
            else:os.killpg(p.pid,signal.SIGTERM)
            try:p.communicate(timeout=.5)
            except subprocess.TimeoutExpired:
                if sys.platform=='win32':p.kill()
                else:os.killpg(p.pid,signal.SIGKILL)
                p.communicate()
print(('Start menu Win32 queue passed: ' if a.queued_input else 'Start menu OS input passed: ')+('one native-1080p validated game frame.' if a.gpu else 'default launch, close, and all three faction choices; no GPU work.'))
