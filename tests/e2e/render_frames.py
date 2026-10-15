#!/usr/bin/env python3
"""Exercise Vulkan path tracing through complete Floria application runs."""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from gpu_environment import check_gpu_environment


def run(binary, directory, config, world, capture, frames=1, extra=()):
    command = [str(binary)]
    if binary.suffix.lower() == ".exe" and sys.platform != "win32":
        command = [sys.executable, str(Path(__file__).resolve().parents[2]
                                      / "scripts/proton_run.py"), str(binary)]
    result = subprocess.run(
        [*command, "--headless", "--frames", str(frames),
         "--capture", str(capture), "--config", str(config),
         "--world", str(world), "--seed", "41", "--samples", "8", *extra],
        cwd=directory, capture_output=True, text=True, timeout=60,
    )
    return result


def require(condition, name, result=None):
    if condition:
        return
    details = ""
    if result is not None:
        details = (f" (exit {result.returncode})\n"
                   f"{result.stdout}{result.stderr}")
    raise RuntimeError(name + details)


def read_ppm(path):
    data = path.read_bytes()
    cursor = 0

    def token():
        nonlocal cursor
        while cursor < len(data):
            if data[cursor] in b" \t\r\n":
                cursor += 1
            elif data[cursor] == ord("#"):
                newline = data.find(b"\n", cursor)
                cursor = len(data) if newline == -1 else newline + 1
            else:
                break
        begin = cursor
        while cursor < len(data) and data[cursor] not in b" \t\r\n#":
            cursor += 1
        require(cursor > begin, "Capture has an incomplete PPM header")
        return data[begin:cursor]

    kind = token()
    width, height, maximum = int(token()), int(token()), int(token())
    require(kind in (b"P6", b"P3") and width > 0 and height > 0
            and maximum == 255, "Capture is not an 8-bit RGB PPM")
    if kind == b"P6":
        require(cursor < len(data) and data[cursor] in b" \t\r\n",
                "Capture lacks a PPM header delimiter")
        cursor += 2 if data[cursor:cursor + 2] == b"\r\n" else 1
        pixels = data[cursor:]
    else:
        pixels = bytes(int(token()) for _ in range(width * height * 3))
        require(not data[cursor:].strip(), "Capture has extra PPM data")
    require(len(pixels) == width * height * 3, "Capture has wrong RGB payload size")
    return width, height, pixels


def write_setting(text, name, value):
    changed, count = re.subn(
        rf"(?m)^{re.escape(name)}\s+[^\n#]+", f"{name} {value}", text,
    )
    require(count == 1, f"Expected exactly one '{name}' render setting")
    return changed


def check_render(name, result, capture, frames=1):
    require(result.returncode == 0, name, result)
    device = re.search(r"(?m)^GPU: (.+)$", result.stdout)
    timings = re.findall(r"(?m)^GPU trace: ([0-9]+(?:\.[0-9]+)?) ms; 8 spp; ([1-9][0-9]*) triangles$",
                         result.stdout)
    require(device is not None and not any(
        word in device.group(1).lower() for word in ("llvmpipe", "lavapipe", "software")),
        name + " did not report a hardware GPU", result)
    require(len(timings) == frames and all(float(time) > 0 for time, _ in timings),
            name + " did not report measured GPU time for each frame", result)
    require(capture.is_file(), name + " did not produce a capture", result)
    width, height, pixels = read_ppm(capture)
    require((width, height) == (1920, 1080), name + " changed native output resolution", result)
    require(max(pixels) > 128 and max(pixels) - min(pixels) > 80
            and len(set(zip(pixels[0::3], pixels[1::3], pixels[2::3]))) > 64,
            name + " did not produce visible lighting and scene variation", result)
    return pixels


def main():
    check_gpu_environment()
    if len(sys.argv) != 4:
        print("Usage: render_frames.py PATH_TO_MICROSHIP RENDER_CONFIG WORLD", file=sys.stderr)
        return 2
    binary, default_config, default_world = (Path(arg).resolve() for arg in sys.argv[1:])
    config_text = default_config.read_text(encoding="utf-8")
    for name, value in (("width", 1920), ("height", 1080), ("samples_per_pixel", 8),
                        ("max_bounces", 4), ("seed", 41), ("bloom_radius", 4)):
        config_text = write_setting(config_text, name, value)

    with tempfile.TemporaryDirectory(prefix="microship-render-e2e-") as directory:
        directory = Path(directory)
        config = directory / "native.render"
        config.write_text(config_text, encoding="utf-8")
        first = directory / "first.ppm"
        result = run(binary, directory, config, default_world, first)
        first_pixels = check_render("first independent GPU frame", result, first)

        third = directory / "third.ppm"
        result = run(binary, directory, config, default_world, third, frames=3)
        third_pixels = check_render("third independent GPU frame", result, third, frames=3)
        require(first_pixels == third_pixels,
                "Static frame and fixed seed changed after prior rendered frames", result)

        (directory / 'levels').mkdir()
        shutil.copytree(default_world.parent.parent / 'models', directory / 'models')
        changed_world = directory / "levels/changed.world"
        world_text = default_world.read_text(encoding="utf-8")
        world_text, changes = re.subn(
            r"(?m)^(planet\s+player\s+\S+\s+\S+\s+\S+\s+\S+)\s+\S+\s+\S+\s+\S+\s*$",
            r"\1 0.05 0.05 0.95", world_text,
        )
        require(changes == 1, "Expected one player planet in the default world")
        changed_world.write_text(world_text, encoding="utf-8")
        changed = directory / "changed.ppm"
        result = run(binary, directory, config, changed_world, changed)
        changed_pixels = check_render("edited world rendered without rebuilding", result, changed)
        require(sum(a != b for a, b in zip(first_pixels, changed_pixels)) > 48,
                "Edited vertex-color world data did not affect the image", result)

        front = directory / 'front.ppm'
        result = run(binary, directory, config, default_world, front, extra=('--time', '45'))
        front_pixels = check_render('orbit front-lighting phase', result, front)
        require(sum(a != b for a, b in zip(first_pixels, front_pixels)) > 256,
                'Moving sun did not change current-frame illumination', result)
        # Return to the identical timestamp after different TLAS transforms.
        returned = directory / 'returned.ppm'
        result = run(binary, directory, config, default_world, returned, frames=3,
                     extra=('--time', '90', '--step-seconds', '-45'))
        returned_pixels = check_render('same scene after moving-sun history', result, returned, frames=3)
        require(returned_pixels == first_pixels, 'Earlier moving-sun frames affected the final image', result)

        camera = directory / 'camera.ppm'
        result = run(binary, directory, config, default_world, camera,
                     extra=('--camera-drag', '8', '-4', '--camera-zoom', '2'))
        camera_pixels = check_render('production camera pan and zoom', result, camera)
        require(sum(a != b for a,b in zip(first_pixels,camera_pixels)) > 256,
                'Pan/zoom input did not change the rendered view', result)

        atmospheric_world = directory / 'levels/atmospheric.world'
        fixture = (Path(__file__).resolve().parent/'scenes/atmospheres.world').read_text()
        media = re.findall(r'(?m)^(?:atmosphere|haze)\s+.*$', fixture)
        atmospheric_world.write_text(re.sub(r'(?m)^(atmosphere|haze|haze_edge_fade|global_atmosphere|global_atmosphere_edge_fade)\s+.*$', '',
                                           default_world.read_text())+'\n'+'\n'.join(media)+'\n', encoding='utf-8')
        atmospheric = directory / 'atmospheric.ppm'
        result = run(binary, directory, config, atmospheric_world, atmospheric)
        atmospheric_pixels = check_render('explicit planet atmosphere and dust fixture', result, atmospheric)
        require(sum(a != b for a,b in zip(first_pixels,atmospheric_pixels)) > 128,
                'Authored medium attenuation did not change visible lighting', result)

        global_clear = directory / 'levels/global-clear.world'
        global_clear.write_text(re.sub(r'(?m)^global_atmosphere\s+.*$', '',
                                       default_world.read_text()) +
                               '\nglobal_atmosphere 0 0 0 22 .00015 .00019 .00026 .65\n', encoding='utf-8')
        global_capture = directory / 'global-clear.ppm'
        result = run(binary, directory, config, global_clear, global_capture)
        global_pixels = check_render('explicit bounded atmospheric fixture', result, global_capture)
        require(sum(a != b for a,b in zip(first_pixels,global_pixels)) > 64,
                'The global atmosphere did not change the rendered scene', result)

        sun = directory / 'sun-glow.ppm'
        result = run(binary, directory, config, default_world, sun, extra=('--time', '6'))
        sun_pixels = check_render('visible sun with spatial glow', result, sun)
        config.write_text(write_setting(config_text, 'bloom_strength', 0), encoding='utf-8')
        unfiltered = directory / 'sun-no-glow.ppm'
        result = run(binary, directory, config, default_world, unfiltered, extra=('--time', '6'))
        raw_pixels = check_render('same sun with glow disabled', result, unfiltered)
        halo_pixels = sum(min(raw_pixels[i:i+3]) < 230
                          and sum(sun_pixels[i:i+3]) > sum(raw_pixels[i:i+3]) + 8
                          for i in range(0, len(raw_pixels), 3))
        require(halo_pixels > 12, 'Current-frame bloom did not extend outside bright cores', result)

        gallery_world = default_world.with_name('factions.world')
        gallery_text = default_config.with_name('factions.render').read_text(encoding='utf-8')
        for name, value in (('width', 1920), ('height', 1080), ('samples_per_pixel', 8),
                            ('seed', 41), ('bloom_radius', 4)):
            gallery_text = write_setting(gallery_text, name, value)
        config.write_text(gallery_text, encoding='utf-8')
        gallery = directory / 'gallery.ppm'
        result = run(binary, directory, config, gallery_world, gallery)
        gallery_pixels = check_render('all twelve faction ships', result, gallery)
        require('Ships: 12' in result.stdout, 'Gallery did not load twelve ships', result)
        # Remove the authored ships through the actual world loader, preserving
        # lighting and the reflection planet. Both gallery rows must change.
        no_ships = directory / 'levels/no-ships.world'
        no_ships.write_text(re.sub(r'(?m)^ship\s+.*$', '', gallery_world.read_text()), encoding='utf-8')
        empty = directory / 'no-ships.ppm'
        result = run(binary, directory, config, no_ships, empty)
        empty_pixels = check_render('gallery without ships', result, empty)
        require(sum(a != b for a,b in zip(gallery_pixels,empty_pixels)) > 256,
                'Ship geometry and materials did not contribute to the image', result)

        config.write_text(write_setting(config_text, "width", 0), encoding="utf-8")
        invalid = directory / "invalid.ppm"
        result = run(binary, directory, config, default_world, invalid)
        require(result.returncode == 1 and str(config) in result.stderr
                and not invalid.exists() and "assert" not in result.stderr.lower(),
                "Invalid render config must fail with an ordinary asset error", result)

    print("GPU-render E2E passed: 13 complete application runs; native captures, "
          "independent moving-sun frames, pan/zoom, global atmosphere, spatial sun glow, "
          "twelve faction meshes, editable vertex colors, invalid config.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"GPU-render E2E failed: {error}", file=sys.stderr)
        sys.exit(1)
