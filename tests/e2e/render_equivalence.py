#!/usr/bin/env python3
"""Compare culling/evaluation shortcuts with the same uncullled transport in the real app."""

from pathlib import Path
import argparse
import os
import signal
import subprocess
import sys
import tempfile
import time
from render_frames import check_gpu_environment, read_ppm, require, write_setting
from save_progress import archive_capture


def main():
    check_gpu_environment()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'reference_shader', 'config', 'world'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--progress', action='store_true', help='Save passing display screenshots in screenshots/')
    args = parser.parse_args()
    binary, reference_shader, config_path, world = (
        path.resolve() for path in (args.binary, args.reference_shader, args.config, args.world))
    command = [str(binary)]
    if binary.suffix.lower() == '.exe' and sys.platform != 'win32':
        command = [sys.executable, str(Path(__file__).resolve().parents[2] / 'scripts/proton_run.py'), str(binary)]
    config_text = config_path.read_text()
    config_text = write_setting(config_text, 'spatial_denoise', 0)
    for name, value in (('width', 1920), ('height', 1080), ('samples_per_pixel', 4),
                        ('volume_samples_per_pixel', 4)):
        config_text = write_setting(config_text, name, value)
    deadline = time.monotonic() + 30
    validation_runs = 0
    progress = []
    with tempfile.TemporaryDirectory(prefix='microship-equivalence-e2e-') as directory:
        directory = Path(directory)
        config = directory / 'native.render'
        config.write_text(config_text)
        # Sun phases and camera movement exercise pixel-cone and geometry bounds,
        # visibility masks and evaluation order with identical current-frame RNG.
        for index, extra in enumerate((('--time', '0'), ('--time', '6'), ('--time', '60'),
                                       ('--time', '6', '--camera-drag', '8', '-4', '--camera-zoom', '2'))):
            captures = []
            for reference in (True, False):
                remaining = deadline - time.monotonic()
                require(remaining > 0, 'Comparison reached its host time limit')
                capture = directory / f'{index}-{reference}.ppm'
                shader = ['--shader', str(reference_shader)] if reference else []
                arguments = ['--headless', '--frames', '1', '--seed', '41',
                             '--config', str(config), '--world', str(world),
                             '--capture', str(capture), *extra, *shader]
                process = subprocess.Popen([*command, *arguments],
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                           start_new_session=(sys.platform != 'win32'))
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
                    try:
                        process.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        pass
                    raise RuntimeError('Lighting comparison timed out; no subsequent runs launched')
                result = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
                require(result.returncode == 0, 'Lighting comparison application failed', result)
                if os.environ.get('MICROSHIP_VULKAN_VALIDATION') == '1':
                    require('Vulkan validation: enabled.' in stdout, 'Requested validation is not active', result)
                validation_runs += 'Vulkan validation: enabled.' in stdout
                captures.append(read_ppm(capture))
                if not reference:
                    progress.append(capture)
            require(captures[0] == captures[1],
                    f'Optimized lighting changed the reference display image in case {index}')
        # Archive only after the entire real-app comparison succeeds, before cleanup.
        if args.progress:
            descriptions = ('lighting-back-lit', 'lighting-visible-sun',
                            'lighting-front-lit', 'lighting-camera-view')
            for capture,description in zip(progress,descriptions):
                archive_capture(capture,description)
    print('Lighting-equivalence E2E passed: 8 complete 1920x1080 application runs; '
          'identical display images across sun phases and camera movement. '
          f'Vulkan validation enabled in {validation_runs}/8 runs.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Lighting-equivalence E2E failed: {error}', file=sys.stderr)
        sys.exit(1)
