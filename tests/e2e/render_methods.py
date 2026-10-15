#!/usr/bin/env python3
"""Bounded Windows/Proton E2E checks for independent-frame sampling methods."""
from array import array
import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import struct
import subprocess
import sys
import time

from render_frames import check_gpu_environment, require, write_setting, read_ppm


def hdr(path):
    with path.open('rb') as file:
        require(file.readline() == b'PF\n', 'Expected linear PFM capture')
        width, height = map(int, file.readline().split())
        require(file.readline() == b'-1.0\n', 'Expected little-endian PFM')
        raw = file.read()
    require(len(raw) == width*height*12, 'Invalid capture payload')
    values = array('f')
    values.frombytes(raw)
    if sys.byteorder != 'little': values.byteswap()
    require(all(math.isfinite(x) and x >= 0 for x in values), 'Invalid radiance')
    stride = width*3
    pixels = array('f')
    for y in range(height-1,-1,-1): pixels.extend(values[y*stride:(y+1)*stride])
    return width, height, pixels


def plane_asset(folder):
    # Analytic diffuse scene loaded by the real application, not a kernel test.
    positions = [(-1., -0.75, 0.), (1., -0.75, 0.), (1., 0.75, 0.), (-1., 0.75, 0.)]
    mesh = folder/'plane.fmesh'
    with mesh.open('wb') as file:
        file.write(struct.pack('<8sII', b'FLMESH01', 4, 2))
        for x, y, z in positions:
            color = 0.525+0.475*x
            file.write(struct.pack('<12f', x,y,z,1., 0.,0.,1.,0., color,color,color,1.))
        for triangle in ((0,1,2,0),(0,2,3,0)):
            file.write(struct.pack('<4I', *triangle))
    world = folder/'plane.world'
    world.write_text('star 0 0 100000 10000 1 1 1\nplanet player 0 0 0 4 1 1 1\nmodel 0 plane.fmesh\n')
    return world


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('config', type=Path)
    parser.add_argument('world', type=Path)
    parser.add_argument('--part', choices=('invariants', 'energy', 'timing', 'display', 'terminator'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--progress', action='store_true')
    args = parser.parse_args()
    check_gpu_environment()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    command = [str(binary)] if sys.platform == 'win32' else [sys.executable,
        str(Path(__file__).resolve().parents[2]/'scripts/proton_run.py'), str(binary)]
    config = args.config.read_text()
    config = write_setting(config, 'spatial_denoise', 0)
    # SPMIS adds one fresh source candidate and one primary ray per pixel.
    for key, value in (('width',1920),('height',1080),('samples_per_pixel',4),
                       ('volume_samples_per_pixel',3),('seed',41)):
        config = write_setting(config,key,value)
    deadline = time.monotonic()+30
    results = {}

    def render(name, settings=config, scene=None, frames=1, extra=(), display=False):
        remaining = deadline-time.monotonic()
        require(remaining > 0, 'Reached batch limit; stopped GPU work')
        extent = [int(re.search(r'(?m)^'+key+r'\s+(\d+)',settings)[1]) for key in ('width','height')]
        surface, volume, reuse = [int(re.search(r'(?m)^'+key+r'\s+(\d+)',settings)[1]) for key in
                                 ('samples_per_pixel','volume_samples_per_pixel','spatial_sun_reuse')]
        require(extent == [1920,1080] and surface+volume+reuse <= 8 and frames <= 2,
                'Case exceeds approved workload')
        path = output/(name+'.render')
        path.write_text(settings)
        capture = output/(name+('.ppm' if display else '.pfm'))
        process = subprocess.Popen([*command,'--headless','--frames',str(frames),'--config',str(path),
            '--world',str(scene or args.world.resolve()),'--capture',str(capture),*extra],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=(sys.platform != 'win32'))
        try:
            stdout, stderr = process.communicate(timeout=min(10,remaining))
        except subprocess.TimeoutExpired as expired:
            if sys.platform == 'win32': process.kill()
            else:
                try: os.killpg(process.pid,signal.SIGKILL)
                except ProcessLookupError: pass
            process.wait(timeout=1)
            partial = expired.output or b''
            if isinstance(partial,bytes): partial = partial.decode(errors='replace')
            (output/(name+'.txt')).write_text(partial+'\nHost timeout; diagnostics stopped.\n')
            raise RuntimeError('Host timeout; no subsequent GPU runs launched')
        log = stdout+stderr
        (output/(name+'.txt')).write_text(log)
        require(process.returncode == 0,'Application failed: '+log)
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION') == '1':
            require('Vulkan validation: enabled.' in stdout,'Validation not active')
        results[name] = {key: [float(x) for x in re.findall(pattern,log)] for key,pattern in (
            ('trace_ms',r'GPU trace: ([\d.]+) ms'),('surface_ms',r'GPU surface: ([\d.]+) ms'),
            ('sources_ms',r'GPU sun sources: ([\d.]+) ms'),('volume_ms',r'GPU volumes: ([\d.]+) ms'),
            ('application_ms',r'Application frame: ([\d.]+) ms'))}
        print(name+': application passed',flush=True)
        if display:
            require(read_ppm(capture)[0:2] == tuple(extent),'Display capture changed native resolution')
            return capture
        data = hdr(capture)
        require(data[0:2] == tuple(extent),'HDR capture changed native resolution')
        return data

    spatial = write_setting(config,'spatial_sun_reuse',1)
    if args.part == 'invariants':
        one = render('spatial-one',spatial,extra=('--time','6'))
        two = render('spatial-after-other-sun',spatial,frames=2,extra=('--step-seconds','6'))
        require(one == two,'SPMIS reads prior-frame state')
        camera = render('spatial-camera',spatial,extra=('--time','6','--camera-drag','8','-4','--camera-zoom','2'))
        require(camera != one,'Camera movement did not change the render')
        # Compare the complete application using author matrix decomposition and
        # baked columns. Both surface and volume draws must match bit for bit.
        current = render('matrix-columns',config,extra=('--time','6'))
        original = render('matrix-decomposition',config,extra=('--time','6','--shader',
            str(binary.parent/'matrix-assets/shaders/path_trace.comp.spv')))
        require(current[0:2] == original[0:2] and current[2].tobytes() == original[2].tobytes(),
                'Baked SZ columns differ from author decomposition')
        if args.progress:
            sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
            from save_progress import archive_capture
            archive_capture(render('nile-display',config,extra=('--time','6'),display=True),'nile-visible-sun')
            archive_capture(render('spatial-display',spatial,extra=('--time','6'),display=True),'spatial-sun-visible-sun')
    elif args.part == 'energy':
        world = plane_asset(output)
        plane = config
        for key,value in (('volume_samples_per_pixel',0),('max_bounces',1),('sun_radiance',100),
                          ('camera_min_distance',1),('camera_position','0 0 5'),('camera_target','0 0 0'),('background','0 0 0')):
            plane = write_setting(plane,key,value)
        for seed in (3,37):
            for method in ('pcg','nile','spatial'):
                settings = write_setting(write_setting(plane,'seed',seed),'spatial_sun_reuse',int(method == 'spatial'))
                extra = ('--shader',str(binary.parent/'pcg-assets/shaders/path_trace.comp.spv')) if method == 'pcg' else ()
                width,height,values = render(f'plane-{seed}-{method}',settings,world,extra=extra)
                # Lo = albedo * L * sin(theta)^2 = albedo, since L=100,
                # r/d=0.1. Finite plane offset is negligible at d=100000.
                samples = []
                film_scale = math.tan(math.pi/8)*(width/height)*5
                for y in range(8,height-8,4):
                    for x in range(8,width-8,4):
                        world_x = (2*(x+0.5)/width-1)*film_scale
                        expected = 0.525+0.11875*world_x
                        samples.append(values[(y*width+x)*3]-expected)
                mean, rmse = math.fsum(samples)/len(samples), math.sqrt(math.fsum(x*x for x in samples)/len(samples))
                require(abs(mean)<0.003 and rmse<0.012,'Diffuse sun energy/gradient changed')
                results[f'plane-{seed}-{method}'].update(mean_error=mean,rmse=rmse)
                print(f'  analytic mean error {mean:.6g}; RMSE {rmse:.6g}',flush=True)
    elif args.part == 'terminator':
        world = plane_asset(output)
        elevation = -0.09
        world.write_text(f'star {math.sqrt(1-elevation*elevation)*100000} 0 {elevation*100000} 10000 1 1 1\nplanet player 0 0 0 4 1 1 1\nmodel 0 plane.fmesh\n')
        plane = config
        for key,value in (('volume_samples_per_pixel',0),('max_bounces',1),('sun_radiance',100),
                          ('camera_min_distance',1),('camera_position','0 0 5'),('camera_target','0 0 0'),('background','0 0 0')):
            plane = write_setting(plane,key,value)
        # Integrate max(n.dot(w),0) over the finite cone, analytically in azimuth
        # and with midpoint quadrature in cos(theta). No renderer/kernel is mocked.
        cosine_min = math.sqrt(0.99)
        step = (1-cosine_min)/20000
        integral = 0.0
        for index in range(20000):
            cosine = cosine_min+(index+0.5)*step
            a = elevation*cosine
            b = math.sqrt((1-elevation*elevation)*(1-cosine*cosine))
            if a > -b:
                angle = math.acos(-a/b)
                integral += (a*angle+b*math.sin(angle))/math.pi*step
        unit_radiance = 200*integral
        for method in ('nile','spatial'):
            settings = write_setting(plane,'spatial_sun_reuse',int(method == 'spatial'))
            width,height,values = render('grazing-'+method,settings,world)
            measured, expected = [], []
            scale = math.tan(math.pi/8)*(width/height)*5
            for y in range(8,height-8,4):
                for x in range(8,width-8,4):
                    measured.append(values[(y*width+x)*3])
                    expected.append(unit_radiance*(0.525+0.11875*(2*(x+0.5)/width-1)*scale))
            relative = math.fsum(measured)/math.fsum(expected)-1
            require(abs(relative)<0.035,'Grazing-sun energy indicates MIS bias')
            results['grazing-'+method]['relative_energy_error'] = relative
            print(f'  grazing-sun relative energy error {relative:.6g}',flush=True)
    elif args.part == 'display':
        captures = [render('nile-front',config,extra=('--time','60'),display=True),
                    render('spatial-front',spatial,extra=('--time','60'),display=True)]
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
        from save_progress import archive_capture
        for capture in captures: archive_capture(capture,capture.stem+'-lit')
    else:
        require(os.environ.get('MICROSHIP_VULKAN_VALIDATION') != '1','Timing requires validation disabled')
        settings = write_setting(config,'volume_samples_per_pixel',0)
        settings = write_setting(settings,'samples_per_pixel',7)
        # Seven surface samples plus one source candidate for SPMIS. Two rounds
        # reverse order; all variants use identical resolution and transport.
        for round_index,order in enumerate((('pcg','columns','nile','spatial'),('spatial','nile','columns','pcg'))):
            for method in order:
                case = write_setting(settings,'spatial_sun_reuse',int(method == 'spatial'))
                extra = ('--time','60')
                if method in ('pcg','columns'):
                    extra += ('--shader',str(binary.parent/(method+'-assets/shaders/path_trace.comp.spv')))
                render(f'{method}-{round_index}',case,frames=2,extra=extra)
        for method in ('pcg','columns','nile','spatial'):
            timings = [x for i in range(2) for x in results[f'{method}-{i}']['trace_ms']]
            print(f'{method}: mean trace {statistics.mean(timings):.6g} ms',flush=True)
    (output/'results.json').write_text(json.dumps(results,indent=2))
    print(args.part+': passed',flush=True)


if __name__ == '__main__':
    try: main()
    except (RuntimeError,ValueError,OSError) as error:
        print(str(error),file=sys.stderr)
        sys.exit(1)
