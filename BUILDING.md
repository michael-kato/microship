# Build and run

The target is Windows. Linux cross-compiles with MinGW and `glslc`. Application checks run the Windows executable through Steam Proton 10. There is no native Linux target.

A clean build needs the compiler, Vulkan headers and import library, `glslc`, and an initialized Proton prefix. Local tools are in `build/toolchains/`. Set `NANOVA_PROTON` and `STEAM_COMPAT_DATA_PATH` to use another Proton installation and prefix. The prefix must contain `pfx/system.reg`. A normal Proton `run` command initializes it.

```sh
bash scripts/check.sh --cpu
python3 scripts/proton_run.py build/windows-cross/nanova.exe
```

Build assets are copied beside the executable. Keep `build/` ignored. Remove `build/windows-cross/` for a clean game build; keep local tools in `build/toolchains/`.

Select a level with `--world`:

```sh
python3 scripts/proton_run.py build/windows-cross/nanova.exe --world assets/levels/biology-immune.json
```

`biology-fungus.json` and `biology-swarm.json` are faction trials. `--world-seed N` fixes the variation seed. `--smoke` loads assets, runs the simulation, builds render input, and shuts down without a GPU. `--state PATH.json` and `--scene-state PATH.json` export diagnostics.

All checks use the real application. Keep native 1920×1080, three film samples, one volume film ray, and one backdrop query. Each GPU process can use at most two frames and 2 seconds; a sequential batch can use at most 5 seconds. See [PROFILING.md](PROFILING.md) for commands and validation limits.

To record flight paths, run `python3 tests/e2e/navigation.py build/windows-cross/nanova.exe --output build/navigation`. With Matplotlib installed, run `python3 scripts/plot_flight.py build/navigation/paths.json --output build/navigation/flight-paths.svg`. The plot joins measured half-second positions.
 s
For the frame profiler checks, build the small Win32 input driver with `cmake --build build/windows-cross --target nanova_input_probe`. Run `python3 tests/e2e/profiler_window.py build/windows-cross/nanova.exe --run` for F1 messages and OS middle-button input. Run `python3 tests/e2e/profiler_render.py build/windows-cross/nanova.exe --run` for normal zoom, close zoom, and F2 compatibility. Without `--run`, these scripts start no GPU work. The input check uses `--frame-delay-ms 250` between its two frames to allow input delivery. This diagnostic delay requires a bounded `--frames` run, has a 250 ms limit, and is outside measured CPU work. It changes frame cadence and must not be used for performance measurements.

To compare SSS and volume changes, preserve the old executable and its `assets/` folder before rebuilding. Then run `python3 tests/e2e/scattering_perf.py build/windows-cross/nanova.exe --baseline build/scattering-baseline/nanova.exe --cases near grown --run`. Without `--run`, the script prepares inputs only. Add `--timing` to disable host validation for timing comparisons. Use `--diagnose` instead of `--baseline` to measure SSS and volume controls. The script keeps the GPU limits above and checks full-image differences and simulation state for comparisons.

## Static lighting and frame history

Persistent static lighting and scattering bakes are permitted. Compute them
from scene inputs and reuse them while their dependencies remain valid.
Static baking is not a temporal image effect. Rebuild or bypass affected
bakes before drawing when relevant geometry, poses, materials, lights, or
media change. Dynamic contributions and camera-dependent terms use current
input. Camera movement alone does not invalidate view-independent bakes.

Do not reconstruct a frame from previous frame images or samples. TAA,
reprojection, temporal denoising, and progressive accumulation across game
frames remain excluded. For each cache, check dependency changes against
fresh application renders; report image error, GPU time, bake time, and
memory.

The biology config enables `lighting.static_lighting_cache`. The renderer
bakes incoming light visibility and medium attenuation at thin cell
surfaces and in interior shadow grids. It calculates material shading,
view-dependent highlights, SSS thickness, and camera-path scattering from
current input. Moving blockers use a separate current-frame shadow field.
Static transport changes cause a rebuild before the next draw. These
bakes stay in GPU memory; they are not saved as authored image maps.

Set `lighting.static_lighting_cache` to `false` for the unbaked reference.
Set `lighting.force_lighting_rebuild` to `true` to rebuild the same bake
each frame for comparison. This retains the bake's spatial approximation.
The GPU log separates static bake time from dynamic shadow work. F1 shows
both in the `LIGHTING FIELDS` span and retains full GPU time accounting.

To compare reuse with a fresh bake, run
`python3 tests/e2e/static_lighting.py build/windows-cross/nanova.exe --run`.
Add `--baseline build/static-lighting-baseline/nanova.exe --variants baseline cached refresh`
for the saved pre-change executable and assets. For three alternating-order
timing pairs, run
`python3 tests/e2e/static_lighting_timing.py build/windows-cross/nanova.exe --baseline build/static-lighting-baseline/nanova.exe --run`.
These scripts retain the GPU limits above. Without `--run`, they prepare
inputs only. The older scattering and volume comparison scripts use the
unbaked path for strict image-equivalence checks.

The live invalidation check is
`python3 tests/e2e/static_lighting_live.py build/windows-cross/nanova.exe --run`.
It compares a changing cell with a fresh application at the same tick.
The camera check is
`python3 tests/e2e/static_lighting_camera.py build/windows-cross/nanova.exe --run`;
first build `nanova_input_probe`. It sends Win32 pan messages to the real
window. On Proton it disables host audio for this input-only check. Its
250 ms pause permits input delivery and must not be used for timings.

Three short close-view timing pairs at native 1080p and three film samples
measured median GPU render time of 35.974 ms before and 26.864 ms after.
All pairs saved 8.324 to 9.181 ms. Surface/SSS time fell from 18.472 to
10.191 ms; volume time stayed near 13.3 ms. Median application time,
including the final capture, fell from 43.144 to 35.155 ms. The bake adds
about 34.1 MiB for the default world. Surface interpolation and lower
resolution moving shadows change the image slightly: close-view mean
channel error was 0.225 RGB8 levels, with a maximum of 8. Reused and fresh
bakes matched exactly. These checks do not establish steady gameplay
performance, shipping six-film performance, or native Windows results.

## Optical-depth textures

`volume.optical_depth_lut` enables generated R32F lookup textures for the
radial density integral. The biology config enables it. Missing or false
uses the original midpoint calculation. Film samples and scattering
integration points are unchanged. No previous-frame image data is used.

Each medium has a 256 by 256 layer. Tables depend only on density fade.
A fade change rebuilds the table before use. Radius, position and RGB
extinction use current input and do not require new table values.
`lighting.force_lighting_rebuild` also rebuilds these tables for checks.
The default world uses 1.5 MiB of texel data plus 1.5 MiB of upload data;
actual Vulkan allocation sizes can be larger.

Run `python3 tests/e2e/optical_depth.py build/windows-cross/nanova.exe --run`
for the bounded application comparison. Add `--pairs 3 --timing` for three
alternating timing pairs. `--variants lookup refresh` checks reuse against
a new bake. `--case` selects near, normal, grown, inside, far, overlap,
boundary or opaque. All retain the GPU limits above.

Three close-view pairs measured median volume time of 13.207 ms with the
midpoint path and 10.999 ms with the lookup. Median GPU render time was
26.888 ms and 24.555 ms. Close-view differences were at most one RGB8 level;
the mean was 0.00985. These short three-film checks do not establish
shipping six-film or sustained frame time. The lookup alone does not meet
the 10 ms target.
