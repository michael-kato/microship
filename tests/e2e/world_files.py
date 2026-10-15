#!/usr/bin/env python3
"""Exercise world-file loading through complete Floria application runs."""

from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile


def fail(name, result):
    print(f"FAIL: {name} (exit {result.returncode})", file=sys.stderr)
    print(result.stdout, end="", file=sys.stderr)
    print(result.stderr, end="", file=sys.stderr)
    sys.exit(1)


def run(binary, directory, arguments):
    command = [str(binary)]
    if binary.suffix.lower() == ".exe" and sys.platform != "win32":
        command = [sys.executable, str(Path(__file__).resolve().parents[2]
                                      / "scripts/proton_run.py"), str(binary)]
    return subprocess.run(
        [*command, "--smoke", *arguments], cwd=directory,
        capture_output=True, text=True, timeout=10,
    )


def check_success(name, result, expected_records=()):
    if (result.returncode != 0
            or "Application lifecycle smoke passed" not in result.stdout
            or not re.search(r"Loaded world .*: [1-9][0-9]* bodies\.", result.stdout)
            or any(record not in result.stdout.splitlines() for record in expected_records)):
        fail(name, result)


def check_failure(name, result, path):
    explanation = result.stderr.replace(str(path), "")
    if (result.returncode != 1 or str(path) not in result.stderr
            or not any(character.isalpha() for character in explanation)
            or "Renderer stub:" in result.stdout
            or "Application lifecycle smoke passed" in result.stdout):
        fail(name, result)


def main():
    if len(sys.argv) != 2:
        print("Usage: world_files.py PATH_TO_MICROSHIP", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1]).resolve()
    star = "star 1 2 3 1 0.5 0.6 0.7"
    planet = "planet neutral 9 8 7 2 0.2 0.3 0.4"
    valid = f"{star}\n{planet}\n"
    count = 0
    fog = "haze 0 0 0 6 .003 .0035 .004 .78\n"
    global_fog = "global_atmosphere 0 0 0 24 .0012 .0015 .0018 .2\n"

    with tempfile.TemporaryDirectory(prefix="microship-world-e2e-") as directory:
        world_path = Path(directory) / "editable.world"

        check_success("default world outside source directory", run(binary, directory, []))
        count += 1

        atmospheric = Path(__file__).resolve().parent / 'scenes/atmospheres.world'
        check_success('explicit atmospheric regression assets',
                      run(binary, directory, ['--world', str(atmospheric)]), ('Ships: 4',))
        count += 1

        check_success('raw reference config and diagnostic CLI controls',
                      run(binary,directory,['--config',str(binary.parent/'assets/rendering/reference.render'),
                                            '--stats','--debug-views','--no-activate']))
        count += 1

        # Exercise the distributable layout, independent of the original build tree.
        bundle = Path(directory) / "relocated game"
        bundle.mkdir()
        relocated = bundle / binary.name
        shutil.copy2(binary, relocated)
        shutil.copytree(binary.parent / "assets", bundle / "assets")
        check_success("relocated executable and assets", run(relocated, directory, []))
        count += 1

        gallery_world = bundle / 'assets/levels/factions.world'
        check_success('all faction exports in relocated bundle',
                      run(relocated, directory, ['--world', str(gallery_world)]), ('Ships: 12',))
        count += 1

        # A ship-only asset scene uses the same application geometry path.
        mesh_path = Path(directory) / 'ship.fmesh'
        source_mesh = bundle / 'assets/models/liquid_metal_small.fmesh'
        mesh_data = source_mesh.read_bytes()
        mesh_path.write_bytes(mesh_data)
        ship_only = star + '\nship liquid_metal small ship.fmesh 0 0 0 1 45 1 1 1\n'
        world_path.write_text(ship_only, encoding='utf-8')
        result = run(binary, directory, ['--world', str(world_path)])
        if result.returncode != 0 or 'Ships: 1' not in result.stdout or ': 0 bodies.' not in result.stdout:
            fail('ship-only scene with conductor mesh', result)
        count += 1
        vertex_count = struct.unpack_from('<I', mesh_data, 8)[0]
        broken = bytearray(mesh_data)
        struct.pack_into('<I', broken, 16 + vertex_count*48 + 12, 7)
        mesh_path.write_bytes(broken)
        check_failure('unsupported mesh surface tag',
                      run(binary, directory, ['--world', str(world_path)]), mesh_path)
        count += 1
        mesh_path.unlink()
        check_failure('missing ship mesh', run(binary, directory, ['--world', str(world_path)]), mesh_path)
        count += 1

        missing_config = bundle / "assets/rendering/biology.render"
        render_text = missing_config.read_text()
        for field, value in (('volume_samples_per_pixel', 4097), ('spatial_sun_reuse', 2), ('spatial_denoise', 2),
                             ('volume_local_bounds', 2), ('microfacet_energy_compensation', 2)):
            malformed = re.sub(rf'(?m)^{field}\s+[^\n]+', f'{field} {value}', render_text)
            missing_config.write_text(malformed)
            check_failure('invalid ' + field, run(relocated, directory, []), missing_config)
            count += 1
        missing_config.unlink()
        check_failure("missing bundled render settings", run(relocated, directory, []), missing_config)
        count += 1

        for flag, value in (("--samples", "0"), ("--samples", "4097"),
                            ("--seed", "-1"), ("--seed", "4294967296"),
                            ("--frames", "0"), ("--frames", "1junk")):
            result = run(binary, directory, [flag, value])
            if result.returncode != 2 or flag not in result.stderr:
                fail("invalid numeric CLI control " + flag, result)
            count += 1

        successes = [
            ("alternate world", valid, (star, planet)),
            ("final record without newline", valid.rstrip("\n"), (star, planet)),
            ("blank lines and final comment without newline", "\n# Example world\n\n" + valid + "\n# End", (star, planet)),
            ("maximum-length final comment", valid + "#" + "x" * 510, (star, planet)),
            ("planet and asteroid records", valid + "asteroid enemy -4 -5 -6 0.25 1 0 0\n",
             (star, planet, "asteroid enemy -4 -5 -6 0.25 1 0 0")),
            ("maximum body capacity", star + "\n" + (planet + "\n") * 16, (star, planet)),
        ]
        for value in ('0', '.5', '1'):
            successes.append(('fog fade '+value, valid+fog+'haze_edge_fade '+value+'\n', (star,planet)))
            successes.append(('global fog fade '+value, valid+global_fog+'global_atmosphere_edge_fade '+value+'\n', (star,planet)))
        successes.append(('independent fog fades',valid+fog+'haze_edge_fade .4\n'+global_fog+
                          'global_atmosphere_edge_fade .6\n',(star,planet)))
        for name, contents, records in successes:
            world_path.write_text(contents, encoding="utf-8")
            result = run(binary, directory, ["--world", str(world_path)])
            check_success(name, result, records)
            expected_count = contents.count("planet ") + contents.count("asteroid ")
            if f": {expected_count} bodies." not in result.stdout:
                fail(name + " body count", result)
            count += 1

        missing_path = Path(directory) / "missing.world"
        check_failure("missing file", run(binary, directory, ["--world", str(missing_path)]), missing_path)
        count += 1

        failures = [
            ("empty file", ""),
            ("missing star", planet + "\n"),
            ("missing bodies", star + "\n"),
            ("duplicate star", valid + star + "\n"),
            ("unknown record", valid + "moon neutral 0 0 0 1 1 1 1\n"),
            ("unknown ownership", valid.replace("neutral", "other")),
            ("missing value", valid.replace("0.2 0.3 0.4", "0.2 0.3")),
            ("invalid numeric value", valid.replace("9 8 7", "nine 8 7")),
            ("partial numeric value", valid.replace("9 8 7", "9junk 8 7")),
            ("NaN position", valid.replace("9 8 7", "nan 8 7")),
            ("infinite radius", valid.replace("7 2 0.2", "7 inf 0.2")),
            ("numeric overflow", valid.replace("9 8 7", "1e100 8 7")),
            ("zero radius", valid.replace("7 2 0.2", "7 0 0.2")),
            ("negative radius", valid.replace("7 2 0.2", "7 -2 0.2")),
            ("negative color", valid.replace("0.2 0.3 0.4", "-0.2 0.3 0.4")),
            ("color over one", valid.replace("0.2 0.3 0.4", "1.2 0.3 0.4")),
            ("trailing junk", valid.rstrip("\n") + " junk\n"),
            ("capacity overflow", star + "\n" + (planet + "\n") * 17),
            ("512-character final line", valid + "#" + "x" * 511),
            ("512-character newline-terminated line", valid + "#" + "x" * 511 + "\n"),
            ("zero solar orbit axis", valid + 'sun_orbit 0 0 0 0 0 0 120\n'),
            ("negative orbit period", valid + 'sun_orbit 0 0 0 1 0 0 -1\n'),
            ("duplicate orbit", valid + 'sun_orbit 0 0 0 1 0 0 120\n'*2),
            ("model references missing body", valid + 'model 5 example.fmesh\n'),
            ("absolute model path", valid + 'model 0 /tmp/example.fmesh\n'),
            ("negative atmosphere height", valid + 'atmosphere 0 -1 .02 .1 .2 .3 .1 .01 .7\n'),
            ("invalid scattering coefficient", valid + 'atmosphere 0 .1 .02 -1 .2 .3 .1 .01 .7\n'),
            ("fog fade without fog", valid+'haze_edge_fade .5\n'),
            ("negative fog fade", valid+fog+'haze_edge_fade -.1\n'),
            ("fog fade above one", valid+fog+'haze_edge_fade 1.1\n'),
            ("nonfinite fog fade", valid+fog+'haze_edge_fade nan\n'),
            ("missing fog fade", valid+fog+'haze_edge_fade\n'),
            ("extra fog fade field", valid+fog+'haze_edge_fade .5 .2\n'),
            ("duplicate fog fade", valid+fog+'haze_edge_fade 0\nhaze_edge_fade .5\n'),
            ("global fade without global fog", valid+fog+'global_atmosphere_edge_fade .5\n'),
            ("negative global fog fade", valid+global_fog+'global_atmosphere_edge_fade -.1\n'),
            ("global fog fade above one", valid+global_fog+'global_atmosphere_edge_fade 1.1\n'),
            ("nonfinite global fog fade", valid+global_fog+'global_atmosphere_edge_fade nan\n'),
            ("missing global fog fade", valid+global_fog+'global_atmosphere_edge_fade\n'),
            ("extra global fog fade field", valid+global_fog+'global_atmosphere_edge_fade .5 .2\n'),
            ("duplicate global fog fade", valid+global_fog+'global_atmosphere_edge_fade 0\nglobal_atmosphere_edge_fade .5\n'),
            ("invalid haze phase", valid + 'haze 0 0 0 20 .01 .01 .01 1\n'),
            ("duplicate global atmosphere", valid + 'global_atmosphere 0 0 0 180 .01 .01 .01 .7\n'*2),
            ("negative global scattering", valid + 'global_atmosphere 0 0 0 180 -.01 .01 .01 .7\n'),
            ("unknown ship faction", valid + 'ship unknown small sample.fmesh 0 0 0 1 0 1 1 1\n'),
            ("invalid ship size", valid + 'ship flowery giant sample.fmesh 0 0 0 1 0 1 1 1\n'),
            ("invalid ship scale", valid + 'ship flowery small sample.fmesh 0 0 0 0 0 1 1 1\n'),
            ("invalid ship tint", valid + 'ship robotic large sample.fmesh 0 0 0 1 0 2 1 1\n'),
            ("absolute ship mesh", valid + 'ship flowery small /tmp/sample.fmesh 0 0 0 1 0 1 1 1\n'),
            ("ship capacity overflow", valid + 'ship rocky small sample.fmesh 0 0 0 1 0 1 1 1\n'*65),
        ]
        for name, contents in failures:
            world_path.write_text(contents, encoding="utf-8")
            check_failure(name, run(binary, directory, ["--world", str(world_path)]), world_path)
            count += 1

        result = run(binary, directory, ["--world"])
        if result.returncode != 2 or not result.stderr.strip() or "Renderer stub:" in result.stdout:
            fail("missing --world argument", result)
        count += 1

    print(f"World-file E2E passed: {count} complete application runs.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"World-file E2E failed: {error}", file=sys.stderr)
        sys.exit(1)
