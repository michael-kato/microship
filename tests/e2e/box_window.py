#!/usr/bin/env python3
"""Run one native game window and select the starting craft with OS pointer events."""
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from window_input import Desktop

root=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
command=[str(binary)] if sys.platform=='win32' else [sys.executable,str(root/'scripts/proton_run.py'),str(binary)]
desktop=Desktop();existing=desktop.windows();state=out/'box-window-state.json';capture=out/'box-window.ppm'
process=subprocess.Popen([*command,'--frames','2','--world-seed','1234','--no-stats','--capture',str(capture),'--state',str(state)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    deadline=time.monotonic()+8;handle=None
    while time.monotonic()<deadline and process.poll() is None:
        found=desktop.windows()-existing
        if found:handle=next(iter(found));break
        time.sleep(.005)
    if handle is None:
        if process.poll() is None:process.terminate()
        output,errors=process.communicate(timeout=1)
        print(output,end='');print(errors,file=sys.stderr,end='')
        raise RuntimeError('The input check did not find the game window before it closed')
    x,y=desktop.focus_point(handle);x-=80;y-=80
    desktop.move(x+600,y+250);desktop.button(True,1)
    desktop.move(x+1320,y+850);desktop.button(False,1)
    output,errors=process.communicate(timeout=8)
    print(output,end='');print(errors,file=sys.stderr,end='')
    assert process.returncode==0,'The native application failed'
    result=json.loads(state.read_text())
    selected=[u for u in result['units'] if u['selected']]
    assert len(selected)==7 and all(u['faction']==result['player_faction'] for u in selected),('Native drag selection',selected)
    camera_drags=re.findall(r'Camera input: drag ([0-9.eE+-]+) ([0-9.eE+-]+);',output)
    assert all(float(x)==0 and float(y)==0 for x,y in camera_drags),'Left drag panned the camera'
    assert 'Vulkan validation: enabled.' in output,'Host validation was not active'
    assert len(re.findall(r'Application frame:',output))<=2
    print('Native box selection passed: actual OS press, drag, release, seven selected craft, capture and shutdown.')
finally:
    if process.poll() is None:
        process.terminate()
        try:process.communicate(timeout=1)
        except subprocess.TimeoutExpired:process.kill();process.communicate()
