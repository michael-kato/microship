#!/usr/bin/env python3
"""Run bounded real-keyboard and mouse checks in native game windows."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
import argparse,json,os,time
from pathlib import Path
from control_window import run_window
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/keyboard-window');p.add_argument('--run',action='store_true');a=p.parse_args()
if not a.run:print('Prepared five two-frame 1920x1080 input checks. No GPU work started.');raise SystemExit(0)
os.environ['NANOVORE_VULKAN_VALIDATION']='1';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
cases=[('wasd-up-right',('W','D'),(1,1),True),('wasd-down-left',('S','A'),(-1,-1),True),('arrows-up-right',('UP','RIGHT'),(1,1),True),('arrows-down-left',('DOWN','LEFT'),(-1,-1),True),('opposed-middle',('W','S','A','D','UP','DOWN','LEFT','RIGHT'),(0,0),False)]
report={};start=time.monotonic()
for name,keys,axes,order in cases:
 remaining=gpu_process_timeout_seconds()
 report[name]=run_window(a.binary.resolve(),out/name,remaining,keys,axes,order)
report['batch_seconds']=time.monotonic()-start
from sys import path
path.insert(0,str(root/'scripts'));from save_progress import archive_capture
for name,_,_,_ in cases:archive_capture(out/name/'controls.ppm','keyboard-'+name)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
