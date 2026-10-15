#!/usr/bin/env python3
"""Validate HUD and diagnostic views through bounded real Windows application runs."""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

from render_frames import check_gpu_environment, read_ppm, require, write_setting
from save_progress import archive_capture

def overlay_text(pixels):
    # Recognize the real application screenshot, including the numeric counter.
    font = Path(__file__).resolve().parents[2]/'source/debug_font.h'
    coefficients = [int(value,16) for value in re.findall(r'0x([0-9a-f]+)u',font.read_text())]
    characters = {value:chr(index) for index,value in enumerate(coefficients) if value}
    lines = []
    for line in range(3):
        text = []
        for column in range(32):
            bits = sum((pixels[((8+line*25+y*3+1)*1920+8+column*15+x*3+1)*3]>180) << (y*4+x)
                       for y in range(7) for x in range(4))
            text.append(characters.get(bits,' ' if bits==0 else '?'))
        lines.append(''.join(text).strip())
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('config',type=Path)
    parser.add_argument('world',type=Path)
    parser.add_argument('--part',choices=('surface','volume','window'),required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    check_gpu_environment()
    binary = args.binary.resolve()
    require(binary.suffix.lower()=='.exe','Only the Windows application is supported')
    command = [str(binary)] if sys.platform=='win32' else [sys.executable,
        str(Path(__file__).resolve().parents[2]/'scripts/proton_run.py'),str(binary)]
    output = args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    settings = args.config.read_text()
    for key,value in (('width',1920),('height',1080),('samples_per_pixel',4),
                      ('volume_samples_per_pixel',4 if args.part=='volume' else 0),
                      ('spatial_sun_reuse',0),('seed',41)):
        settings = write_setting(settings,key,value)
    config = output/'native.render'
    config.write_text(settings)
    world = (Path(__file__).resolve().parent/'scenes/atmospheres.world') if args.part=='volume' else args.world.resolve()
    deadline = time.monotonic()+30
    results = {}

    def run(name,extra=(),frames=1,window=False):
        remaining = deadline-time.monotonic()
        require(remaining>0,'Reached batch deadline; no more GPU runs launched')
        capture = output/(name+'.ppm')
        arguments = [*command,'--config',str(config),'--world',str(world),'--frames',str(frames),
            '--capture',str(capture),*extra]
        if not window: arguments.append('--headless')
        process = subprocess.Popen(arguments,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                                   start_new_session=sys.platform!='win32')
        try:
            stdout,stderr = process.communicate(timeout=min(10,remaining))
        except subprocess.TimeoutExpired as expired:
            if sys.platform=='win32': process.kill()
            else:
                try: os.killpg(process.pid,signal.SIGKILL)
                except ProcessLookupError: pass
            partial = expired.output or b''
            errors = expired.stderr or b''
            if isinstance(partial,bytes): partial = partial.decode(errors='replace')
            if isinstance(errors,bytes): errors = errors.decode(errors='replace')
            (output/(name+'.txt')).write_text(partial+errors+'\nHost timeout; remaining runs cancelled.\n')
            raise RuntimeError('Debug E2E timed out; remaining GPU runs cancelled')
        (output/(name+'.txt')).write_text(stdout+stderr)
        result = subprocess.CompletedProcess(arguments,process.returncode,stdout,stderr)
        require(result.returncode==0,'Debug application run failed',result)
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION')=='1':
            require('Vulkan validation: enabled.' in stdout,'Requested validation was inactive',result)
        require(len(re.findall(r'GPU trace: [\d.]+ ms',stdout))==frames,'Missing frame timings',result)
        width,height,pixels = read_ppm(capture)
        require((width,height)==(1920,1080),'Diagnostic rendering changed native resolution')
        results[name] = {'frames':frames,'validation':'Vulkan validation: enabled.' in stdout,
            'gpu_work':[float(x) for x in re.findall(r'GPU work:? ([\d.]+) ms',stdout)],
            'app_work':[float(x) for x in re.findall(r'Application work: ([\d.]+) ms',stdout)]}
        return pixels,capture

    if args.part=='surface':
        raw,_ = run('raw',('--time','60','--no-stats'))
        stats,stats_capture = run('stats',('--time','60','--stats'),frames=2)
        for y in range(1080):
            start=(y*1920+(512 if y<92 else 0))*3
            end=(y+1)*1920*3
            require(raw[start:end]==stats[start:end],'FPS overlay modified scene pixels outside its rectangle')
        bright=sum(value>180 for y in range(8,80) for value in stats[y*1920*3:(y*1920+512)*3])
        require(bright>500,'FPS text was not visible in the upper-left corner')
        text = overlay_text(stats)
        match = re.fullmatch(r'GPU ([\d.]+) MS WORK ([\d.]+) MS',text[0])
        require(match is not None,'GPU/application-work milliseconds were not legible')
        gpu_ms,work_ms=map(float,match.groups())
        require(abs(gpu_ms-results['stats']['gpu_work'][0])<.011 and abs(work_ms-results['stats']['app_work'][0])<.011,
                'Overlay did not report the actual previous completed GPU/application work')
        require(re.fullmatch(r'FPS [\d.]+ HEADLESS',text[1]) is not None,'FPS cadence was missing')
        results['stats']['text'] = text
        views,views_capture = run('views',('--time','60','--debug-views','--no-stats'))
        returned,_ = run('views-after-other-sun',('--time','0','--step-seconds','60','--debug-views','--no-stats'),frames=2)
        require(views==returned,'Buffer views retained data from an earlier logical frame')
        require(all(views[(y*1920+x)*3+c]==0 for y in range(790,1030,4)
                    for x in range(760,1140,4) for c in range(3)),
                'The volume buffer contains scattering in the atmosphere-free scene')
        for left in (640,1280):
            colors={views[(y*1920+x)*3:(y*1920+x)*3+3] for y in range(400,670,4)
                    for x in range(left+64,left+576,4)}
            require(len(colors)>64,'Vertex-color/normal views did not show scene geometry')
        fps_views,fps_capture = run('views-fps',('--time','60','--debug-views'),frames=2)
        require(fps_views!=views,'FPS/buffer composition did not change layout')
        require(re.fullmatch(r'GPU ([\d.]+) MS WORK ([\d.]+) MS',overlay_text(fps_views)[0]) is not None,
                'FPS text was not legible beside buffer views')
        for capture,description in ((stats_capture,'fps-overlay'),(views_capture,'render-buffer-views'),
                                    (fps_capture,'render-buffers-with-fps')):
            archive_capture(capture,description)
    elif args.part=='volume':
        views,capture = run('volume',('--time','6','--debug-views','--no-stats'))
        returned,_ = run('volume-after-other-sun',('--time','0','--step-seconds','6','--debug-views','--no-stats'),frames=2)
        require(views==returned,'Volume/debug buffers retained a previous frame')
        energy=sum(views[(y*1920+x)*3+c] for y in range(790,1030,4)
                   for x in range(700,1220,4) for c in range(3))
        require(energy>64,'The volume view omitted actual current-frame scattering')
        archive_capture(capture,'atmosphere-buffer-views')
    else:
        pixels,capture = run('window',('--time','60','--debug-views','--no-activate'),frames=2,window=True)
        results['window']['text'] = overlay_text(pixels)
        require(re.fullmatch(r'GPU ([\d.]+) MS WORK ([\d.]+) MS',results['window']['text'][0]) is not None,
                'Window FPS text was not legible')
        archive_capture(capture,'window-render-buffers')
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Debug '+args.part+' E2E passed: native 1080p, <=8 fresh samples, <=2 frames/process, 30-second batch.',flush=True)


if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print('Debug E2E failed: '+str(error),file=sys.stderr)
        sys.exit(1)
