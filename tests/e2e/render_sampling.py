#!/usr/bin/env python3
"""Bounded E2E sampling checks: only complete Windows application executions."""

from array import array
import argparse
from pathlib import Path
import math
import os
import signal
import struct
import subprocess
import sys
import time
from render_frames import check_gpu_environment, require, write_setting


def read_hdr(path):
    with path.open('rb') as file:
        require(file.readline() == b'PF\n', 'Expected linear RGB PFM')
        width, height = map(int, file.readline().split())
        require(file.readline() == b'-1.0\n', 'Expected little-endian PFM')
        data = file.read()
    require((width, height) == (1920, 1080) and len(data) == width*height*12, 'Invalid HDR extent/payload')
    values = array('f')
    values.frombytes(data)
    if sys.byteorder != 'little': values.byteswap()
    require(all(math.isfinite(value) and value >= 0 for value in values), 'Nonfinite/negative HDR radiance')
    stride = width*3
    pixels = array('f')
    for row in range(height-1,-1,-1): pixels.extend(values[row*stride:(row+1)*stride])
    return pixels


def furnace_mesh(path, roughness):
    # Test-scene asset generation, not an isolated renderer/kernel invocation.
    segments, rings = 32, 16
    normals = [(0., 1., 0.)]
    for ring in range(1, rings):
        theta = math.pi*ring/rings
        for segment in range(segments):
            phi = 2*math.pi*segment/segments
            normals.append((math.sin(theta)*math.cos(phi), math.cos(theta), math.sin(theta)*math.sin(phi)))
    normals.append((0., -1., 0.))
    triangles = []
    for i in range(segments):
        triangles.append((0, 1+(i+1)%segments, 1+i, 2))
    for ring in range(rings-2):
        upper, lower = 1+ring*segments, 1+(ring+1)*segments
        for i in range(segments):
            j = (i+1)%segments
            triangles.extend(((upper+i, upper+j, lower+i, 2), (upper+j, lower+j, lower+i, 2)))
    lower = 1+(rings-2)*segments
    for i in range(segments):
        triangles.append((len(normals)-1, lower+i, lower+(i+1)%segments, 2))
    with path.open('wb') as file:
        file.write(struct.pack('<8sII', b'FLMESH01', len(normals), len(triangles)))
        for normal in normals:
            file.write(struct.pack('<12f', *normal, 1.5, *normal, 0., 1., 1., 1., roughness))
        for triangle in triangles:
            file.write(struct.pack('<4I', *triangle))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('config', type=Path)
    parser.add_argument('world', type=Path)
    parser.add_argument('--part', choices=('invariants', 'furnace'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    check_gpu_environment()
    binary, world = args.binary.resolve(), args.world.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    command = [str(binary)]
    if sys.platform != 'win32':
        command = [sys.executable, str(Path(__file__).resolve().parents[2]/'scripts/proton_run.py'), str(binary)]
    config = args.config.read_text()
    config = write_setting(config, 'spatial_denoise', 0)
    for name, value in (('width', 1920), ('height', 1080), ('samples_per_pixel', 4),
                        ('volume_samples_per_pixel', 4), ('seed', 41)):
        config = write_setting(config, name, value)
    deadline = time.monotonic()+30

    def render(name, settings=config, scene=world, frames=1, extra=()):
        remaining = deadline-time.monotonic()
        require(remaining > 0, 'Reached 30-second batch limit; stopped GPU work')
        config_path = output/(name+'.render')
        config_path.write_text(settings)
        capture = output/(name+'.pfm')
        process = subprocess.Popen([*command, '--headless', '--frames', str(frames),
            '--world', str(scene), '--config', str(config_path), '--capture', str(capture), *extra],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=(sys.platform != 'win32'))
        try:
            stdout, stderr = process.communicate(timeout=min(10, remaining))
        except subprocess.TimeoutExpired:
            if sys.platform == 'win32':
                process.kill()
            else:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            raise RuntimeError('Application timed out; no subsequent GPU cases launched')
        (output/(name+'.txt')).write_text(stdout+stderr)
        result = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        require(result.returncode == 0, 'Sampling application failed', result)
        if os.environ.get('MICROSHIP_VULKAN_VALIDATION') == '1':
            require('Vulkan validation: enabled.' in stdout, 'Validation was not active', result)
        values = read_hdr(capture)
        print(name + ': passed application/HDR checks', flush=True)
        return values

    if args.part == 'invariants':
        first = render('visible', extra=('--time', '6'))
        returned = render('after-earlier-sun', frames=2, extra=('--time', '0', '--step-seconds', '6'))
        require(first == returned, 'Earlier logical frame altered current HDR radiance')
        moved = render('camera', extra=('--time', '6', '--camera-drag', '8', '-4', '--camera-zoom', '2'))
        require(first != moved, 'Camera movement did not change ray-traced output')
        clear = render('no-inscattering', write_setting(config, 'volume_samples_per_pixel', 0), extra=('--time', '6'))
        uniform = render('uniform-volume-proposal', write_setting(config, 'volume_local_bounds', 0), extra=('--time', '6'))
        bounded_energy = sum(a-b for a,b in zip(first,clear))
        uniform_energy = sum(a-b for a,b in zip(uniform,clear))
        require(bounded_energy > 0 and uniform_energy > 0, 'Volume lighting missing')
        require(0.65 < bounded_energy/uniform_energy < 1.5, 'Volume proposal changed aggregate lighting excessively')
        for y in range(8):
            for x in range(8):
                index = (y*1920+x)*3
                require(all(abs(first[index+c]-value) < 1e-6
                            for c,value in enumerate((.001,.002,.004))), 'Far empty space contains atmospheric radiance')
        print(f'Volume radiance sums: bounded {bounded_energy:.6f}; uniform {uniform_energy:.6f}.')
    else:
        world = output/'furnace.world'
        world.write_text((Path(__file__).parent/'scenes/furnace.world').read_text())
        config = write_setting(config, 'samples_per_pixel', 8)
        for name,value in (('volume_samples_per_pixel', 0), ('background', '1 1 1'),
                           ('camera_position', '0 0 12'), ('camera_target', '0 0 0')):
            config = write_setting(config,name,value)
        furnace_mesh(output/'furnace.fmesh',1.)
        compensated = render('rough-compensated', config, world)
        plain = render('rough-uncompensated', write_setting(config,'microfacet_energy_compensation',0), world)
        furnace_mesh(output/'furnace.fmesh',.3)
        smooth = render('smooth-compensated', config, world)
        def center_mean(values):
            return sum(values[(y*1920+x)*3] for y in range(532,548) for x in range(952,968))/256
        rough_mean, plain_mean, smooth_mean = map(center_mean,(compensated,plain,smooth))
        require(.85 < rough_mean < 1.15 and .85 < smooth_mean < 1.15, 'White-furnace radiance is not near unity')
        require(plain_mean < .7 and rough_mean > plain_mean*1.6, 'Conductor compensation did not restore missing energy')
        print(f'Furnace linear means: rough {rough_mean:.6f}; uncompensated {plain_mean:.6f}; smooth {smooth_mean:.6f}.')
    print(f'Sampling {args.part} E2E passed; all processes 1920x1080, <=8 total surface/volume samples, <=2 frames.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Sampling E2E failed: {error}', file=sys.stderr)
        sys.exit(1)
