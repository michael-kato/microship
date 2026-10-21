#!/usr/bin/env python3
"""Check the F and B keys with real OS input in bounded game windows."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from gpu_environment import gpu_process_timeout_seconds
import argparse,json,os,time
from pathlib import Path
from control_window import run_window
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=root/'build/screen-selection-window');p.add_argument('--run',action='store_true');a=p.parse_args()
if not a.run:print('Prepared two two-frame 1920x1080 F/B input checks. No GPU work started.');raise SystemExit(0)
os.environ['NANOVA_VULKAN_VALIDATION']='1';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
report={};started=time.monotonic()
for key in ('F','B'):
 remaining=gpu_process_timeout_seconds()
 report[key]=run_window(a.binary.resolve(),out/key,remaining,(),(0,0),False,key)
report['batch_seconds']=time.monotonic()-started
from sys import path
path.insert(0,str(root/'scripts'));from save_progress import archive_capture
for key in ('F','B'):archive_capture(out/key/'controls.ppm','screen-select-'+key.lower())
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
