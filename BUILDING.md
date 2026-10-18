# Build and run

The target is Windows. Linux cross-compiles with MinGW and `glslc`. Application checks run the Windows executable through Steam Proton 11. An explicit headless Linux target is available for performance diagnosis.

A clean build needs the compiler, Vulkan headers and import library, `glslc`, and an initialized Proton prefix. Local tools are in `build/toolchains/`. Set `NANOVA_PROTON` and `STEAM_COMPAT_DATA_PATH` to use another Proton installation and prefix. The default prefix is `build/proton11-prefix`; it must contain `pfx/system.reg`. A normal Proton `run` command initializes it.

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
view-dependent highlights and camera-path scattering from current input.
SSS thickness uses live queries unless the optional thickness bake is on. Moving blockers use a separate current-frame shadow field.
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


## Volume lighting textures and static thickness

The biology config enables `volume.lighting_grid` and
`lighting.texture_grid`. The first adds a 32 by 32 by 32 incoming-light
field for the unshadowed atmosphere. It stores light-path attenuation;
it does not add geometry shadows to the background. Cell fields retain
static and current moving shadows. The second copies current volume
lighting into a filtered RGBA32F 3D texture. Surface shadows keep buffer
interpolation. Phase evaluation and camera-ray integration still use the
current camera at full resolution. This is not a lower-resolution final
image or a camera-aligned volume integration pass.

Missing controls default to false. Set `volume.optical_depth_lut`,
`volume.lighting_grid` and `lighting.texture_grid` to false for the
original volume path. Set only `lighting.texture_grid` to false to compare
hardware texture filtering with buffer interpolation. The default texture
uses 9 MiB of texel data. With the optical-depth tables and upload buffer,
the added storage is about 12 MiB, before allocation padding.

`lighting.organelle_cache` is optional and defaults to false. With static
lighting enabled, it bakes solid thickness and light-entry positions at
triangle corners. Missing valid corner exits use live queries. Moving
shadows and material response remain current. The bake reuses the
existing surface atlas. Close-view reuse saved about 0.3 ms, but its added
rebuild cost was about 0.5 ms. It remains off in the biology config.

For three alternating volume timing pairs, run:

```sh
python3 tests/e2e/optical_depth.py build/windows-cross/nanova.exe --variants off texture --pairs 3 --timing --run
```

Use `--variants texture texture-refresh` for cache reuse, or
`--variants organelle organelle-refresh` for the optional thickness bake.
`--shadow-grid 0` checks the live fallback. `--no-static-cache` checks the
unbaked path. The live and camera checks above accept `--thickness-cache`.
All retain the normal bounded GPU limits.

Three close-view, three-film pairs measured median volume time of
13.226 ms before these volume changes and 6.842 ms after them. Paired
volume savings were 6.362 to 6.710 ms. Median GPU render time was 27.769 ms
and 20.708 ms. One optimized surface measurement was 13.612 ms; the other
two were about 10.2 ms. Close-view mean image error was 0.111 RGB8 levels,
with a maximum of 2. These short checks do not establish steady gameplay
performance or a total frame time below 10 ms.

## RenderDoc diagnosis

The local capture tool uses RenderDoc 1.44 from `/opt/renderdoc_1.44`.
It needs its SDK header, `cc`, Vulkan headers, the existing Proton setup,
and an available GPU. The small native Vulkan layer is a diagnostic tool;
the game remains a Windows target. The tool creates a local layer manifest
and does not change system Vulkan registration.

Prepare the close-view capture with:

```sh
python3 scripts/renderdoc_profile.py build/windows-cross/nanova.exe --output build/renderdoc-on
```

Add `--run` to start GPU work. The default uses native 1920 by 1080,
six film samples, one volume ray and one backdrop query. It runs two game
frames, captures the second headless frame, then runs one bounded replay
process. Each process has a two-second limit; the batch has a five-second
limit. Hardware tracing remains disabled. The user authorized RenderDoc
for this diagnosis. Replay uses Khronos host validation; capture does not.

Use `--sss off --output build/renderdoc-off --run` for the matching SSS-off
case. When `subsurface.strength` is zero, built-in shaders now compile out
SSS. The default keeps SSS enabled. External shaders keep their own
specialization contract.

The output contains an `.rdc` capture, `report.json`, pipeline compiler
reports, logs and the frozen scene inputs. Display images are also saved
in `screenshots/`. The analysis identifies shader programs by captured
SPIR-V hashes. It reports event duration and compute invocations, plus
register counts and code statistics from
`KHR_pipeline_executable_properties`. Compiler subgroup limits are not
measured occupancy. This setup exposes no AMD hardware stall counters.

In the earlier six-film close-view captures, the surface pass took
20.252 ms with SSS and 15.205 ms with SSS compiled out. Volume took
7.277 ms and 7.101 ms. The surface program used 216 and 168 vector
registers. An earlier runtime-only SSS-off test retained the 216-register
program and took 19.324 ms. The compiled-off image matches that earlier
SSS-off image exactly; the default SSS-on image also stays unchanged.
These are short instrumented captures, not steady frame-time statistics.
The report's event-duration sum excludes gaps and waits. It is not
application frame time or FPS. Even the SSS-off surface pass exceeds the
10 ms total target.

The window capture recorded, but replay failed with
`VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS`. The headless capture and replay
work. The failure does not establish a driver cause. Native Windows and
the reported 100 ms gameplay case remain unverified.


## Live render controls

F5 toggles SSS, F6 volume scattering, F7 cell blur, F8 tilt shift, and F9
the fog-of-war filter. The keys open F1 and clear its timing averages.
The normal window prepares SSS-on and SSS-off surface programs at startup;
key presses do not compile shaders or replace Vulkan resources.

Build `nanova_input_probe`, then prepare or run one check:

```sh
python3 tests/e2e/render_toggles.py build/windows-cross/nanova.exe --case off --run
```

Cases are `baseline`, `off`, `restore`, `repeat`, and `buffers`. Each uses
one native 1080p window, two frames, three film samples, one volume ray,
one backdrop query, Khronos host validation, and a two-second process
limit. There is no hardware tracing. The 250 ms input pause is outside CPU
work timing; these input runs are not performance benchmarks. The
resident Win32 input driver sends key presses, held repeats and releases.
The repeat case also checks focus loss and two presses before a poll.
These are posted Win32 messages, not a physical keyboard check.

Use separate output folders. After a baseline run, pass its folder with
`--reference` to the restore case. This checks identical simulation state
and exact display pixels below the profiler. The timing panel is excluded
because its measured numbers change between runs. The checks also verify
that skipped pass timings fall near zero, profiler means restart, and
valid static lighting is reused. See GAMEPLAY.md for the control details.


## Compute execution groups

`lighting.subgroup_size` accepts 0, 32 or 64. Zero or a missing field uses
the driver choice. The biology config selects 32, measured on the RX 7800
XT with RADV. This changes execution grouping, not resolution, sample
counts or the lighting model. The renderer checks the extension, feature,
stage, size range and group limit before creating the device. An explicit
unsupported size gives a startup error. Use zero for the driver default.
External shaders keep the driver choice.

Run the fixed-quality comparison with a saved executable and its assets:

```sh
python3 tests/e2e/render_perf.py build/windows-cross/nanova.exe --baseline build/render-opt-baseline/nanova.exe --pairs 3 --timing --run
```

The default uses six film samples at native 1080p. `--samples 3` selects the
reduced check. Each GPU process uses two frames and at most two seconds;
the sequential batch has a five-second limit. Omit `--timing` for host
validation. `--case` selects the existing near, normal, grown, inside,
far, overlap, boundary or opaque scene. `--subgroup-size 0` checks the
driver default. Results include image differences, simulation equality,
second-frame times and each alternating pair. These are short frozen-scene
checks; they do not establish sustained gameplay or percentile frame time.

The close-view six-film comparison measured median surface time of
19.865 ms before and 16.072 ms after. Paired savings were 3.728 to
4.083 ms. Median GPU render time was 31.162 ms and 26.742 ms. All compared
RGB8 pixels matched exactly. RenderDoc confirmed subgroup size 32 and a
surface event of 16.038 ms against 19.604 ms for the saved 64-lane build.
Volume time stayed near 7 ms. The 10 ms total target is still unmet.

RenderDoc accepts `--subgroup-size 0`, `32` or `64` for this comparison.
Zero omits the new field, so saved executables can use their original
config contract. See PROFILING.md for the reports and validation limits.


The volume pass computes the first blurred depth once per camera ray.
It uses that depth to divide sharp and blurred scattering contributions.
This replaces repeated blur-radius queries at each integration point;
the surface pass still computes the actual pixel radius. It supports both
fractional cell-depth and distance-based blur. The calculation uses the
current camera and media and adds no persistent data.

For an isolated comparison against the saved 32-lane build, add
`--baseline-subgroup-size 32`. `--blur-profile linear` checks the
supported distance-based profile; `--blur-profile off` checks media with
no depth blur. The default keeps the authored profile. Three alternating
close-view pairs measured volume time of 7.037 ms before and 5.298 ms
after, with paired savings of 1.697 to 1.808 ms. Near-view RGB8 error was
at most 2 in 12 color channels out of 6,220,800. Interior, overlap,
linear-blur and blur-disabled comparisons matched exactly. Differences
come from floating-point evaluation at the blur-start boundary.


## Native Linux comparison

The user authorized a native comparison. It uses the same C++ simulation,
scene build, Vulkan renderer and SPIR-V as the Windows game. It has no
native menu, window or live audio; use `--headless` or `--smoke`. The
Windows game remains the interactive target.

```sh
cmake -S . -B build/linux-diagnostic -G Ninja -DCMAKE_BUILD_TYPE=Release -DNANOVA_NATIVE_DIAGNOSTIC=ON
cmake --build build/linux-diagnostic --parallel
ctest --test-dir build/linux-diagnostic --output-on-failure
```

`tests/e2e/platform_perf.py --run` compares Proton 10, Proton 11 and native
Linux at the same native 1080p settings. It requires separate initialized
prefixes at `build/proton-prefix` and `build/proton11-prefix`. It does not
upgrade the old prefix. For uncaptured timings, add
`--pairs 3 --timing --no-capture`. Runs remain bounded to two frames and
two seconds per process, five seconds per sequential GPU batch. The
report keeps native state differences rather than assuming exact
cross-platform floating-point results. Native and Proton shader binaries
can be compared directly. RenderDoc accepts either executable.

The installed default is Proton 11.0-2e-x86_64. A copied-prefix CPU check,
headless GPU validation and a window control check passed. Proton 10 and
11 images were identical. The native image differed by at most one RGB8
level in 114 channels out of 6,220,800. Native state has four small unit
float differences after 600 ticks; native scene data also has small
floating-point differences. This is not cross-platform determinism proof.
Three uncaptured timing runs measured median GPU time of 25.133 ms under
Proton 10, 25.126 ms under Proton 11 and 25.793 ms natively. Median app time
was 29.537, 29.502 and 27.408 ms. These are short frozen-scene comparisons.
Native startup and live platform behavior are separate from steady GPU
cost. The 10 ms target is still unmet.

Diagnostic output is buffered in 64 KiB. The menu and scene-ready messages
flush at their input boundaries. Render timing includes its diagnostic
flushes. Three alternating close-view pairs without capture measured
29.379 to 26.996 ms median application time. GPU time changed from 25.120
to 25.625 ms, so this change reduces CPU output cost only. A validated
two-frame window check restored all feature controls and reproduced the
reference image below the timing panel.

For one film sample and one volume ray, the bundled shader integrates
surface and volume light in the same camera traversal. Other sample
settings and external shaders retain separate passes. The volume hotkey
still removes scattering while it keeps attenuation. Near, inside and
overlapping-medium application checks compare the two paths at identical
sampling. In a 120-frame close-view check, the last 100 frames changed
from 10.983 to 10.473 ms median application time and from 11.514 to
10.614 ms at the 95th percentile. This does not change the default six-film
quality setting.

Fog filtering now skips pixels with exactly zero fog. A CPU view check
can skip the full filter only when every covered visibility-field vertex
is fully revealed. Field edges and uncertain projections keep the filter.
The tilt filter copies its center when all rounded taps address that
pixel. Close and far captures differed by at most one RGB8 level. A
120-frame close-view comparison reduced median GPU time from 9.254 to
8.845 ms. Application time remained above ten milliseconds.

`volume.quadrature` accepts `midpoint` (default) or `gauss_legendre`.
Gauss-Legendre requires two or three integration steps. The optical-depth
table and static light fields are unchanged. At three nodes, native
close-view and grown-factory captures had mean RGB8 error 0.390 and maximum
8 against a 32-step midpoint volume reference at the same film sampling.
Two nodes had mean error 0.748 and maximum 10 in the close view. These are
volume approximations, not exact replacements or full-transport references.

`sampling.spatial_antialiasing` enables a current-frame directional edge
filter before the HUD. It does not use earlier frames or change output
resolution. Missing or false keeps the previous output. F2 diagnostic
panels bypass it. Broad bloom uses the center bloom contribution for
neighbor tests. The method cannot recover geometry missed by a single ray.

The one-film, three-node Gauss, 16-cell shadow-grid trial keeps all major
rendering features enabled. Its mean close-view error against the former
six-film/eight-step/32-cell setting was 0.860 RGB8 levels. The spatial
filter reduced measured edge RMS error from 7.068 to 6.078 RGB8 levels
against that reference (vertical contrast above eight levels, rows 1–849).
This is one frozen scene, not a general motion-quality result. The former
settings remain in `assets/rendering/biology-quality.json`.
