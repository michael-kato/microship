# Build and run

The target is Windows. Linux cross-compiles with MinGW and `glslc`. Application checks run the Windows executable through Steam Proton 11. An explicit headless Linux target is available for performance diagnosis.

A clean build needs the compiler, Vulkan headers and import library, `glslc`, and an initialized Proton prefix. Local tools are in `build/toolchains/`. Set `NANOVA_PROTON` and `STEAM_COMPAT_DATA_PATH` to use another Proton installation and prefix. The default prefix is `build/proton11-prefix`; it must contain `pfx/system.reg`. A normal Proton `run` command initializes it.

```sh
bash scripts/check.sh
python3 scripts/proton_run.py build/windows-cross/nanova.exe
```

In VS Code, open the `nanova` project folder. Press F5 and select
**nanova: run (Proton)**. This entry builds the Windows executable, then
starts it through Proton in a terminal. It uses the JavaScript Debugger
that ships with VS Code. It starts the game; C++ breakpoints are not supported
by this entry. The CMake extension's Run button is a separate command.
Use **Terminal > Run Task > nanova: run (Proton)** to launch with the task
system. The faction tasks start the generated 128-cell map with `--faction`.

`check.sh` defaults to an incremental build and short CPU application checks.
It starts no GPU work. Use `--quick fungal` or `--quick repair` to check one
changed area. Use `--quick smoke` for build, asset loading, render-input
creation and shutdown only. These short checks cover selected scenarios;
they do not replace the full suite. Run `bash scripts/check.sh --cpu` for
all CPU checks. Keep the full suite out of each edit cycle. Use the existing
bounded GPU checks when a visual change requires them.

On this host, an unchanged incremental build plus the 13 focused application
runs took 7.44 seconds. The full 30-group CPU suite took 525 seconds. The
short path checks factory repair, fungal growth, production, contact damage,
haze range and healing. It excludes capacity, broad combat, other controls
and renderer regressions covered by the full suite. These are different
coverage levels. Timing records are in `build/iteration_review/`.

Shipping builds configure with `-DDEVELOPMENT=OFF`. That removes the F3 wireframe view, F2 buffer views, the F1/F5–F10 hotkeys and the `--wireframe`/`--debug-views` flags. Pause-menu settings still control frame statistics and render features. The default `DEVELOPMENT=ON` build keeps them; `--wireframe` starts in wireframe.

Build assets are copied beside the executable. Keep `build/` ignored. Remove `build/windows-cross/` for a clean game build; keep local tools in `build/toolchains/`.

Select a level with `--world`:

```sh
python3 scripts/proton_run.py build/windows-cross/nanova.exe --world assets/levels/immune-player-level.json
```

`large_level.json` is the normal 128-cell map with all three factions; `--faction NAME` picks the player's side. The world seed generates the layout and home assignment. Older two-side levels remain diagnostic fixtures. `three_faction_stress.json` is the primary stress scene: 15 developed cells, 60 seconds of actual production per faction, and a shared battle point. `--world-seed N` fixes the variation seed. `--smoke` loads assets, runs the simulation, builds render input, and shuts down without a GPU. `--state PATH.json` and `--scene-state PATH.json` export diagnostics.

Per-frame diagnostic lines (`Scene time`, GPU stage times, `Application frame`) print only in bounded `--frames` runs. Interactive play writes startup lines only, so console output stays out of the frame.

All checks use the real application. Short GPU checks keep native 1920×1080, default renderer sampling. Each ordinary process uses at most two frames and ten seconds, with thirty seconds per sequential batch. Longer startup and performance limits are described below. See [PROFILING.md](PROFILING.md) for commands and validation limits.

To record flight paths, run `python3 tests/e2e/navigation.py build/windows-cross/nanova.exe --output build/navigation`. With Matplotlib installed, run `python3 scripts/plot_flight.py build/navigation/paths.json --output build/navigation/flight-paths.svg`. The plot joins measured half-second positions.

For the frame profiler checks, build the small Win32 input driver with `cmake --build build/windows-cross --target nanova_input_probe`. Run `python3 tests/e2e/profiler_window.py build/windows-cross/nanova.exe --run` for F1 messages and OS middle-button input. Run `python3 tests/e2e/profiler_render.py build/windows-cross/nanova.exe --run` for normal zoom, close zoom, and F2 compatibility. Without `--run`, these scripts start no GPU work. The input check uses `--frame-delay-ms 250` between its two frames to allow input delivery. This diagnostic delay requires a bounded `--frames` run, has a 250 ms limit, and is outside measured CPU work. It changes frame cadence and must not be used for performance measurements.

Factory roots use the transformed nucleus center and each organelle center
for all factions. The nucleus part has `anchor_origin: true` in its mesh-part
metadata. Meshes, centers, and attachment points use the same startup transform.
When a factory belongs to the nucleus itself, its authored surface attachment
provides the outward direction. The trunk continues along that route until
it clears the cell wall. The former ring arrangement is removed.

Fungal `branches.chain_length: 1` places all side limbs directly on their
large trunk. `root_height: 1` and `spacing: 0.8` keep the exposed trunk and limbs
short. The existing fan control spreads siblings evenly. Structure count,
output limits, and nutrient collection remain controlled by their existing
fields. A fixed-seed mature 4.2-radius cell has six direct limbs per large
trunk, compared with two before. Maximum reach fell from 14.98 to 6.77 units;
its longest side limb fell from 3.96 to 1.53 units. The source generator exports
the nucleus marker for future mesh rebuilds. The Blender generator was not run
for this change; existing mesh vertices are unchanged.

CPU combat profiles run the real application without a GPU. Add
`--smoke --matches 1 --ticks 3600 --world-seed 1234 --cpu-benchmark` to a
Proton command. The output includes tick percentiles and mean phase times
for AI, visuals, recovery, movement, combat, growth, sight, appearance, and
fog. It also records the phase times of the slowest tick. Phase clocks run
only when the benchmark requests them. Setup time is separate from tick time.

To compare a saved build with the current build, use:

```sh
python3 tests/e2e/combat_perf.py build/windows-cross/nanova.exe \
  --baseline /path/to/baseline/nanova.exe --world /path/to/unchanged/world.json
```

Keep the baseline executable and its assets together. Both builds use the
same selected world and its referenced assets. The check compares complete
exported game states after 1,200 ticks, then alternates the build order for
three 3,600-tick timing pairs. Match summaries must also match. Logs and
`report.json` go to `build/combat_profile/`. Add `--dense` for 90 seconds of
factory production, the Fungal player, and 18-unit sight. This prepares
more units through normal production. It does not inject units into the
simulation. The dense case also exercises fog during combat.

On Oct 21, combat optimization commit `c0f4b82`, before the later factory-shape
update, had these results in the Windows Release build through Proton 11 on
the Ryzen 9 9900X. The ordinary case uses three alternating pairs; the
dense case uses one pair. Each pair uses identical scene data and seed 1234.

| Battle | Peak units | Mean tick before / after | p99 tick before / after |
| --- | ---: | ---: | ---: |
| Original stress scene, 3,600 ticks | 2,563 | 3.153 / 0.821 ms | 8.267 / 3.615 ms |
| Dense scene with sight, 1,800 ticks | 3,743 | 9.446 / 4.370 ms | 18.205 / 10.893 ms |

The optimization calculates live factory-head positions once per phase,
rejects colonies outside each query range, and skips obstacle queries for
units that already ignore all factory obstacles. Query order, exact range
checks, and game outcomes stay the same. Complete exported states matched
before and after both battles. Rebuilds occur at movement and combat phase
boundaries, so later growth and damage cannot leave stale target positions.

After the factory-shape update, a separate 1,800-tick dense run reached
3,801 units. Mean tick time was 3.941 ms, p99 was 10.461 ms, and maximum was
11.178 ms. This run used the new factory shapes and was not an equivalence
comparison with the old shapes. Its report is in
`build/fungal_bush_combat_profile/`.

Evidence for the earlier code comparison is in `build/combat_profile_final/`
and `build/dense_combat_profile_final/`. These are tick measurements from the
real application. They do not establish performance for every match or
native Windows. The reported 200 ms simulation frame was not reproduced.

## Static lighting and frame history

Persistent static lighting and scattering bakes are permitted. Compute them
from scene inputs and reuse them while their dependencies remain valid.
Static baking is not a temporal image effect. Rebuild or bypass affected
bakes before drawing when relevant geometry, poses, materials, lights, or
media change. Dynamic contributions and camera-dependent terms use current
input. Camera movement alone does not invalidate view-independent bakes.

Radiance cascades may retain their own lighting state between frames. Final pixels from different lighting histories need not match. Scene inputs and game state must still match.

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

Invalidation is targeted. A changed stationary blocker (its old and new
world bounds), a deformed wall, or a changed medium invalidates only the
interior grids and thin-surface instances whose rays toward an active
light can cross the change: the change's bounds swept away from each light
must meet the target's bounds. Light directions, light activation and
thin-surface transport settings still reset everything. While a wall morphs,
the close view rebakes about 0.5 thin surfaces per frame instead of 2.35;
mean bake time fell from 0.163 to 0.070 ms per frame.

A morphing wall refits its BLAS. Refits keep the tree built for the earlier
shape, so the BLAS is fully rebuilt after 60 refits (about a second of
morph) and once the wall has been unchanged for 30 rendered frames.

Set `lighting.static_lighting_cache` to `false` for the unbaked reference.
Set `lighting.force_lighting_rebuild` to `true` to rebuild the same bake
each frame for comparison. This retains the bake's spatial approximation.
The GPU log separates static bake time from dynamic shadow work. F1 shows
both in the `LIGHTING FIELDS` span and retains full GPU time accounting.

The live invalidation check is
`python3 tests/e2e/static_lighting_live.py build/windows-cross/nanova.exe --run`.
It checks bake invalidation and compares scene and simulation state at the same tick. Radiance-cascade image history can differ between the two runs.
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

With spatial volume filtering disabled, one film sample and one volume
ray let the bundled shader integrate
surface and volume light in the same camera traversal. Other sample
settings and external shaders retain separate passes. The volume hotkey
still removes scattering while it keeps attenuation. Near, inside and
overlapping-medium application checks compare the two paths at identical
sampling. In a 120-frame close-view check, the last 100 frames changed
from 10.983 to 10.473 ms median application time and from 11.514 to
10.614 ms at the 95th percentile. This result used the former six-film quality config, which remains available.

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
settings remain in `assets/rendering/renderer-quality.json`.

The authored four-pixel backdrop blur uses five linear texture fetches
per axis. The weights combine the original 49 fractional-position reads
into nine texel weights, then pair adjacent weights. Other radii retain
the original filter. A host-validated close-view capture differed in
2,445 color channels by one RGB8 level at most. All three backdrop lights
remain active. An image check caught an accidental light-loop edit during
the trial; it was corrected before this result.

`--frame-ticks 1..4` advances that many normal simulation ticks before
each bounded diagnostic frame. It requires `--frames` and rejects a
`--time` override. Normal play retains wall-clock simulation. Use this
mode for live geometry, growth and bake comparisons. The benchmark's
`--live` option uses one tick per frame and checks the final tick count.
Two host-validated game windows ended at tick 602 with identical state
and images. Earlier wall-clock window comparisons ended at different
ticks and are not controlled before/after results.

The normal guide shader writes only first-hit color and normal/depth.
F2 keeps its full diagnostic light buffers. A live grown-factory image
check found three color channels different by one level. The measured
GPU change was small (9.110 to 9.053 ms median), and it does not meet the
frame target by itself. The F1 label identifies combined surface, SSS and
volume work. Diagnostic output also reports start-to-start frame cadence.
An extra one-film constant-folding trial did not improve timing and was
removed. Reusable command lists also showed no useful median gain and
were removed.

## Spatial volume integration

`volume.spatial_filter` evaluates scattering on a grid within the current
frame. `volume.spatial_stride` accepts two or four native pixels. It needs
one volume ray per grid point. Surface shading and final output stay at
1920×1080. Attenuation stays in the native surface path. The filter checks
terminal geometry depth. If no nearby sample matches within about two
percent, the pixel uses native camera integration. Total and sharp light
are filtered separately so front-cell detail retains its blur boundary.

The grid, depth guides and scattering buffers are overwritten before use
in each frame. No rendered data is reused from an earlier frame. F2 uses
full-resolution volume integration so its light buffers remain direct
diagnostics. F6 skips scattering and retains attenuation. Two-pixel
filtering is the default only when this optional mode is enabled and the
stride is absent.

Four-pixel volume filtering against native integration at the same
three-node sampling produced mean RGB8 error 0.0202, RMS 0.2076 and maximum
16 in the close view. Three repeated frozen comparisons were identical
within each variant. Interior, overlap and far views had mean error at
most 0.0188 and maximum 13. Host validation passed. The separate fallback
pass trial was slower and was removed.

At matching three-node settings, three alternating live grown-factory
comparisons measured 9.293 ms median application time and 9.448 ms at the
95th percentile with four-pixel filtering. Start-to-start cadence was
9.309 ms median and 9.468 ms at the 95th percentile. GPU time was 7.792 ms
median. These use 300 measured frames per variant after 20 warm-up frames
per process, Proton 11, immediate presentation, native 1080p, one film
sample, spatial edge filtering and a 16-cell shadow field. All major
effects stayed enabled. The largest measured frame was 11.623 ms, so
these results do not establish a hard ten-millisecond bound.

## Default performance preset

`assets/rendering/renderer.json` now uses one native film sample, current-frame
directional edge filtering, three Gauss-Legendre volume points, a four-pixel
volume grid and a 16-cell shadow field. SSS, scattering, cell blur, tilt shift,
fog, atmosphere and backdrop highlights stay enabled. Static lighting bakes
keep their dependency checks. There is no temporal reconstruction.

Use the former six-film/eight-step/32-cell settings with:

```sh
python3 scripts/proton_run.py build/windows-cross/nanova.exe --config assets/rendering/renderer-quality.json
```

The fast preset trades subpixel coverage and scattering/shadow detail for
time. Its one ray cannot recover every feature covered by six film rays.
The edge filter improves the measured silhouette error, but cannot replace
all multisample detail. Use the quality config for visual comparisons.

## Configuration file names

Edit source settings under `assets/`, then build to copy them into the game
bundle. Restart the application to reload them. Filenames identify their purpose:

| File | Purpose |
| --- | --- |
| `startup.json` | Default level, renderer, shader, sound, and menu paths |
| `renderer.json` | Normal rendering quality, camera, SSS, and bloom |
| `renderer-quality.json` | Higher-sample rendering preset |
| `renderer-reference.json` | Reference renderer configuration |
| `scene.json` | Lighting, membrane opacity, atmosphere, fog, and self-glow |
| `gameplay.json` | Links cell, faction, unit, and factory definitions |
| `gameplay-rules.json` | Simulation, exploration, and AI settings |
| `combat-effects.json` | Projectile, trail, damage, and injury presentation |
| `audio.json` | Sound files, levels, cooldowns, and playback distance |
| `sound-synthesis.json` | Procedural sound generation |
| `start-menu.json` | Faction menu presentation and level paths |

Cell, factory, faction, unit, level, and authoring settings also have explicit
filenames. Configuration basenames must be unique across the active assets.
The build removes only the listed obsolete names from its own asset bundle.

## JSON formatting

Use two spaces for indentation. Keep an array on one line when the complete
line fits within 100 characters, including indentation, the field name,
and the trailing comma. Longer arrays use multiple lines. Long strings
and other indivisible values can exceed the limit.

Run `python3 scripts/format_json.py` to format project JSON files, or add
`--check` to check without writing. Use `--line-width N` to select another
limit for a manual run; project checks use 100. Both build check scripts
check JSON formatting before they build. The command rejects duplicate
keys and non-JSON numbers, and preserves number precision and field order.

VS Code tasks provide **nanova: format JSON** and **nanova: check JSON formatting**.
The built-in JSON formatter keeps existing line breaks; use the format
task to apply the array width rule. No additional packages are needed.

## Angular virus factory check

The factory shape check used the same seed, game ticks, camera, and render
settings before and after the mesh change. Three native 1920 x 1080 captures
passed Khronos host validation through Proton. The wide game states matched.
The virus stem changed from 148 to 92 triangles per segment. The growing tip
changed from an eight-triangle leaf to a 92-triangle phage head. Each four-part
limb uses fewer triangles in total. Factory SSS weights are zero in every faction.

The single-frame GPU times were 8.079 ms before and 8.105 ms after in the wide
view, and 9.467 ms after in the close view. These are bounded capture results,
not a sustained performance test. Images are `000835` through `000837` in
`screenshots/`. CPU checks passed for data assets, colony forms, and SSS.

New screenshot names use underscores, such as `000845_cell_animation_pose.png`.
The archive tool counts both old hyphen names and new underscore names. It keeps
the earlier images and continues the same numeric sequence.

## Biological motion and organelle appearance

The `cell_animation` CPU check runs the application with motion on and off. It
checks game state, mesh topology, volume-preserving squash, fixed attachment
points, connected stem joints, repeatable poses, optional controls, and invalid
settings. It also rejects an active cell whose membrane is marked as movable
internal geometry. The full membrane group makes 66 application runs. Its CTest
limit is 90 seconds; the checked run took about 45 seconds.

Prepare the GPU check without starting the GPU:

```sh
python3 tests/e2e/cell_animation_render.py build/windows-cross/nanova.exe
```

Add `--run` to execute the prepared check within the approved ordinary limits.
It uses three sequential native 1920 x 1080 processes, at most two frames per
process, ten seconds per process, thirty seconds per batch, Khronos host
validation, and no hardware tracing. It uses the loaded shipping sample settings
and rejects settings above the ordinary cap. It compares time 14 from a fresh
process with time 14 after time 10 in the same process. Both scene exports and
output pixels must match exactly. Time 10 and time 14 must produce different
images. Logs and reports stay in `build/cell_animation_render/`.

The check passed through Proton. The final close views measured 8.374 and
8.449 ms after static bakes, with first frames from 9.886 to 9.993 ms. A prior
close capture reached 10.345 ms on its first frame. These captures used the
current local scene and renderer settings, including the user's membrane edits.
They do not establish sustained performance or native Windows behavior. The
six-case glow check also passed host validation and preserved all game states.
Images `000838` through `000849` show the motion, mutation, and glow checks.

Animation partitions the existing cell triangles into part meshes. It adds no
vertices or triangles. Organelle and factory motion uses instance transforms;
BLAS geometry stays fixed for this motion. Moving objects use current lighting
and shadows. Static membrane lighting remains cached while its inputs are valid.
Faction displacement remains a separate vertex morph with its existing refit
and cache invalidation. Membrane ripples change visible shading normals only.

## Finer virus wall spikes

The default virus displacement now requests 256 peaks with a six-degree width,
up from 64 peaks with a twelve-degree width. Height remains 0.19 of cell radius.
The field is prepared at startup. Per-frame interpolation uses the same vertices
and triangles. The loader and JSON schema accept up to 256 features and widths
from six degrees.

The colony-forms check measured 64 and 256 local peaks in the real application
exports. Its 38 application runs passed, including partial morph interpolation,
unit normals, unflipped wall triangles, restored neutral geometry, unchanged game
state, and rejected out-of-range settings. The cross-build and smoke check passed.

Four native 1920 x 1080 captures passed Khronos host validation through Proton.
All four game states matched. Total mesh storage remained 108,028 triangles.
The two-frame wide captures measured 8.237/6.054 ms before and 8.756/6.471 ms
after. Close captures measured 10.488/8.955 ms before and 12.048/9.715 ms after.
The first frame includes static bakes. These short captures do not establish
sustained performance; the denser shape had a higher measured rendering cost.

Images are `000850` through `000853`. The scene inputs, logs, measured report,
and before/after sliders are in `build/fine_spikes_review/`. The existing local
membrane settings were preserved and used for both sides of each comparison.

## Frame pipeline and GPU memory

Across the October 19 changes, median frame cadence at the grown close view
fell from 9.21 to 7.02 ms and at the wide view from 6.00 to 3.02 ms (tick
600; 120-frame windows, live simulation), including the cost of exact
receiver shadows and baked ambient visibility. See PROFILING.md for the table.

Vertices, triangles and BLAS indices live in device-local memory. Startup
uploads them once through a staging buffer and builds every BLAS in one
submission; static meshes share temporary scratch. Deformable walls copy
their changed vertex ranges from host staging into the device-local buffer
before the refit, so shading never reads geometry over the PCIe BAR.
Effect tile lists upload only the offsets and the references the frame
uses, not the 2.1 MB reference capacity.

The HDR beauty and scratch images are RGBA16F. Each frame writes them once
and every post pass reads them several times, so half floats halve that
traffic without accumulating error. Alpha holds blur radii in pixels, well
inside half-float range. `.pfm` captures convert the half floats to 32-bit
values. Against RGBA32F, close-view GPU median fell from 7.52 to 7.09 ms and
wide from 4.86 to 4.35 ms; mean relative HDR change was 0.08 percent.

Fog blurs at quarter resolution: a 4x4 box downsample, two separable
Gaussian passes in disjoint scratch tiles, then native composition only
where current sight hides the map. Sigma stays a third of the authored
radius; taps step one quarter-resolution pixel apart, so a 96-pixel radius
is fully sampled instead of read every fourth native pixel. Fog/bloom/display
time fell from 1.61 to 1.18 ms in the wide view and from 1.34 to 1.15 ms in
the close view; `fog_memory_render.py` still passes with similar falloff,
memory and decay changes.

Effects, tilt shift and the spatial volume filter read a compact guide
buffer: one vec4 per pixel (first-hit distance, hit code, log terminal
depth) followed by two vec4 per volume stride cell. Normal frames no longer
write or read the 96-byte F2 diagnostic record; only F2 fills it, and it is
allocated full size only when buffer views can open (windowed or
`--debug-views`). The guides use 37 MB at 1080p. GPU median fell from 6.90
to 5.88 ms in the close view and from 3.92 to 2.71 ms in the wide view;
captures are byte-identical.

The scene keeps about 11,000 fixed instance slots; most are empty. Each
frame compacts the slots with a nonzero mask and fully rebuilds the TLAS
from them. Instance IDs index the compacted style and lighting arrays in the
same order, so shaders are unchanged; slot-indexed data (bake keys,
surface-cache offsets) stays on the CPU. The previous design built the TLAS
once at startup and refit it every frame, so its tree kept the startup
layout while units spread out. At tick 4,800 the acceleration stage fell
from 0.29 to 0.25 ms; total GPU time changed by about 0.04 ms.

Each BLAS has two geometries: opaque triangles (geometry 0, `OPAQUE`) and
thin membranes (geometry 1, `NO_DUPLICATE_ANY_HIT`). The triangle buffer
orders each mesh the same way, and instance custom indices name meshes; a
small `mesh_ranges` table maps geometry and primitive back to a triangle.
Shadow rays use `TerminateOnFirstHit`: traversal commits opaque blockers in
hardware and stops, and only membranes reach the shader to multiply their
transmission, each exactly once. Stationary and moving blockers carry
instance mask bits 2 and 8, so the static and dynamic light bakes select
them during traversal instead of rejecting candidates in the shader.

Two frames are in flight. Each slot owns its command buffer, fence, acquire
semaphore, timestamp queries, descriptor sets and every buffer the CPU
writes per frame (uniform, TLAS instances, media, lighting instances and
bounds, sight, styles, effects, HUD text, wall staging). GPU-only images,
grids and caches are shared: one queue runs frames in order and each pass's
first barrier orders it after the previous frame's last use; the TLAS build
waits for the previous frame's compute stage. Present semaphores are one per
swapchain image. The CPU waits only for the slot it is about to reuse, so
simulation and recording overlap the previous frame's GPU work. Timestamps
are read when a slot is reused; captures and shutdown drain the queue, so
bounded runs still print one GPU report per frame. F1 names these CPU spans
`SLOT WAIT + QUERY` and `SUBMIT`.

At the grown close view (live simulation, IMMEDIATE present, alternating
pairs of 120-frame windows, first 20 frames discarded) median cadence fell
from 9.21 to 7.91 ms; the wide view fell from 6.02 to 5.10 ms. Application
time now tracks GPU time.

## Shading model updates

The display transform is AgX (`assets/shaders/display_transform.glsl`), used
by the display pass, its edge filter and F2 radiance panels. It replaces
per-channel Reinhard, which shifted saturated highlights in hue and mapped
middle grey 0.5 down to 0.33. AgX keeps middle grey near 0.18 and rolls bright
saturated light toward white. At the authored exposure of 1.6 the stable
close view's mean display value rose from 119 to 136 (RGB8); `output.exposure`
remains the brightness control.

Ambient specular uses pre-integrated GGX albedo, `F0*(E-B)+B`, from the
32x32 tables in `microfacet.cpp` (directional albedo E and the Schlick bias
integral B), instead of a mirror Schlick term at n.v. Rough surfaces lose the
bright white grazing rim and keep more diffuse color there. The environment
lookup follows the GGX dominant direction, which moves toward the normal as
roughness grows. Conductors keep their multiple-scattering gain.

Subsurface wrap is continuous for solid organelles as well as walls:
`max(0, cos + wrap) / (1 + wrap)^2`, gated by the smooth shading normal so it
falls to zero at `cos = -wrap`. Surface reflection still requires the
geometric front face. The previous solid form added a constant
`wrap / (1 + 2 wrap)` floor that switched off per triangle at the geometric
terminator, which drew faceted bands on organelles.

Roughness at or above 0.999, or IOR at or below 1, still authors a purely
diffuse dielectric, but specular reflection and the interface Fresnel split
now fade out over roughness 0.95–0.999 and IOR 1.05–1.0 instead of switching
off, so vertex materials interpolated across those limits shade without a
seam. No current material is inside either band.

Opaque surfaces inside cells trace exact current shadow rays for their
surface reflection. They previously read the 16^3 interior visibility grid,
offset one voxel (about 0.56 units in a radius-4.2 cell) along the normal,
which removed contact shadows, leaked light and blurred moving units' shadows.
The grid still lights the volume, and light diffused under organelle surfaces
(the SSS wrap) keeps the smooth field, since scattering blurs it anyway. Thin
walls keep their baked per-vertex samples. This costs about 0.75 ms of surface
time in the close view and 0.17 ms in the wide view.

Ambient fill inside cells is scaled by a baked ambient visibility stored in
the spare alpha channel of each interior grid point's first light entry. The
static bake traces 32 fixed Fibonacci directions per grid point through
stationary blockers, wall transmission and medium attenuation, weighted by
the environment gradient's luminance. Surface ambient (diffuse, specular and
light from behind thin walls) and ambient in-scattering in cell volumes read
it with the same one-voxel normal offset as the light grids. Moving units do
not occlude ambient light. Previously enclosed organelles received the full
open environment. The close view darkens by about half a display level on
average, mostly in crevices; GPU cost is about 0.08 ms.

## Audio output

Live audio mixes on its own thread. WinMM signals an event whenever one of
four 512-sample buffers (about 43 ms queued in total, down from 85 ms)
completes; the thread starts queued voices, mixes and resubmits. The game
thread only computes each sound's distance gain and pushes it into a
64-entry single-producer, single-consumer ring, so a slow frame no longer
starves the device. A full ring drops the sound, as a full voice set does.
`--audio-capture` still mixes synchronously per simulation tick for
deterministic WAV output. Audible output has not been verified on Proton or
native Windows.

## Motion between simulation ticks

The simulation stays a fixed 60 Hz. Interactive play renders faster, so
before each scene build the game blends unit, projectile and limb-debris
positions from the previous tick toward the current one by the leftover
accumulator fraction, then restores the current positions; simulation
results never see the blend. Motion therefore advances on every frame, at
up to one tick (16.7 ms) of display delay. This is current simulation state,
not image history. Bounded `--frames`, headless and capture runs draw the
exact current tick, so diagnostics and references are unchanged.

## Trials not kept

Reusing per-segment medium intervals in volume quadrature gave no gain at
default settings and slowed the fused quality path's surface stage from 8.90
to 10.65 ms. Replacing duplicate rounded taps in the cell-blur and tilt-shift
filters with whole-pixel taps slowed the cell blur from 0.261 to 0.312 ms;
the repeated taps were cache hits. Both were reverted.

## Faction balance runs

`python3 tests/e2e/faction_balance.py build/windows-cross/nanova.exe` plays
every faction pairing headless with the built-in AI on both sides
(`match.player_ai`). Fog is off, each side starts with its faction's
player-level roster, and every pairing runs with both roles and both start
cells for each world seed (`--seeds`, default 10; `--minutes`, default 15
game minutes; `--jobs`, default half the CPU threads). Each pairing, role and
cell is one process: `nanova --smoke --ticks N --world-seed S --matches M`
plays M seeded matches back to back without audio or rendering, stops each at
its outcome or N ticks, and prints one JSON summary line per match. The three
factions (120 matches) take about 2.3 minutes on a 24-thread CPU, mostly in
matches with fungal swarms of several hundred spores. It writes `results.json` and `report.txt` under `build/faction-balance/`
and prints wins, decision times, units built and lost, peak army and
flagged results: one-sided pairings, stalemates, role or start-cell bias,
idle factions and unit-storage saturation while a match is still open.
State exports and match summaries carry `outcome_tick` and per-faction
`faction_stats` (current colonies and units, then match totals).
Matches with heavy parallel CPU load can exceed the per-process timeout of
the ordinary checks; run those with lower `ctest -j`. Under Proton, a check
can also fail with "ShellExecuteEx failed" or a 10 s timeout from a stuck
Wine launch; the same check passes when rerun, and stale `nanova.exe` Wine
processes from the failed run should be stopped first.

Results on Oct 20 (commit b0062ab, 40 matches per pairing): immune beats
virus 40-0, virus beats fungus 27-13 (1 undecided), immune beats fungus
40-0. Before the white blood cell blob, virus and immune were 22-18. The
built-in AI sends each unit to its target as it is produced, so armies
arrive piecemeal and virus peaks at 9-24 craft alive; these runs measure
trickled engagements more than massed swarms. In massed equal-cost battles
(virus ordered onto immune defenders, no production) immune won 40 against 17
at about 480 nutrients each, and virus won two of three 28 against 12 at 336.

## Unit storage and swarm budget

Unit storage holds 4096 units (`MAX_UNITS`; state exports report
`unit_capacity`). The simulation finds neighbors through two grids rebuilt
each phase: a clamped world grid keyed by cell and faction, for targets,
site defense and sight, and a hashed grid as wide as the largest
separation spacing, for separation. Factory avoidance skips cells and limb
capsules that cannot come within reach. Unit spikes, payload sacs and
embedded spikes draw from fixed per-frame instance pools (16384 attached,
2048 embedded); parts past a full pool are not drawn and gameplay is
unaffected. Fog memory stamps one disc per occupied sight-field cell.

Measured Oct 20 with 2048 units (1024 per side, packed around the two
homes): native diagnostic simulation about 2 ms per tick idle and 2.2 ms per
tick in a full fight. Bounded 1080p GPU frames through Proton with the
20-triangle `fungal_swarm_spore` mesh: 5.9 ms at a home view against 4.7 ms
with no units, 10.5 ms close up. Virus fighters (1256 triangles, eleven
spikes each) cost 6.8 and 13.9 ms in the same views.


## Factory shape checks

Run `ctest --test-dir build/windows-cross --output-on-failure -R '^(small_factories|factory_curves)$'`
after a build. This starts the real Windows application through Proton. It checks
large-organelle root selection, seeded branch counts and dimensions, connected
joints, mature shape stability, and rejected settings. Damage scenarios check
new regrowth shapes for virus, immune and fungal factories. The check also verifies
that every intact stem ends in an attached leaf, including stems with children.
Cut tips can remain bare. Other roots must
keep their shapes, and a repeated seed must produce the same result.

On Oct 20, the Windows cross-build and four application check groups passed:
`natural_factory`, `growth_factions`, `colony_forms`, and `cell_animation`.
Two native 1920×1080 captures through Proton passed with Khronos host validation
and no tracing. Each used two frozen frames with default sampling. The grown
virus cell had three roots and fifteen structures at seed 1234. GPU times were
6.572/4.166 ms in the full-factory view and 9.356/7.297 ms in the close view.
The first frame includes initial bake work. These are short appearance checks,
not sustained performance or native Windows results. Screenshots are
`000927_natural_factory_current_wide.png` and
`000928_natural_factory_current_near.png` in `screenshots/`.

The intact-leaf check passed after the leaf-placement change on Oct 20. Frozen
1080p captures `000929_all_branches_leaf_wide.png` and
`000930_all_branches_leaf_near.png` show fifteen leaves on fifteen intact
branches, up from eight. Simulation exports, mesh geometry and existing poses
matched the prior captures. The checks used two frames per process, default
sampling and Khronos host validation. They do not measure sustained play.

## Factory emission mask

Run `ctest --test-dir build/windows-cross --output-on-failure -R '^factory_emission$'`
after a build. Nineteen real-application runs check the color and rim controls, optional and
disabled settings, all three factions, instance scope, unchanged game results,
and invalid settings.

On Oct 20, four CPU application check groups passed: `data_assets`,
`cell_animation`, `natural_factory` and `factory_emission`. The control check
passed again with the final default power of 0.5. Frozen native 1920×1080
captures passed Khronos host validation at default sampling, with two frames per
process and no tracing. Disabled output matched the prior image pixel for pixel.
Enabling the mask preserved simulation exports, mesh geometry and poses. The
final close view measured 9.584/7.563 ms GPU time for its two frames; the disabled
view measured 9.221/7.249 ms. These short checks do not establish sustained cost
or native Windows behavior.

Screenshots `000931_factory_fresnel_wide_off.png` and
`000935_factory_fresnel_wide_on.png` show the full factory. The close views are
`000933_factory_fresnel_near_off.png` and `000936_factory_fresnel_near_on.png`.
Prepared inputs, logs, report and comparison viewers are in
`build/factory_fresnel_review/`. Its `run_review.py` prepares inputs by default;
`--run` starts the bounded GPU batch. `wide_comparison.html` and
`near_comparison.html` compare the disabled and enabled images.

The warm factory color update passed `data_assets` and `factory_emission` on
Oct 20. Three native 1080p two-frame captures passed host validation at default
sampling, with no tracing. With the color override omitted, the wide image
matched its prior capture pixel for pixel. The warm color preserved the game
state, geometry, poses and Fresnel settings. The warm close view measured
10.927/7.378 ms GPU time; these short checks do not establish sustained cost.
Screenshots `000938_warm_wide_factory_emission.png` and
`000939_warm_near_factory_emission.png` show the final color. Inputs, logs, report
and before/after viewers are in `build/warm_factory_review/`. Its `run_review.py`
prepares inputs by default; `--run` starts the bounded GPU batch.


## Pause menu checks

Build the input driver with `cmake --build build/windows-cross --target nanova_input_probe`. Run `python3 tests/e2e/pause_menu.py build/windows-cross/nanova.exe` for CPU application checks of the production menu, settings files, faction changes and quit. These runs load the game, open its real Win32 menu and use its controls. They start no GPU work. The input driver sends Win32 messages to the selected application process. This does not establish physical input or native Windows behavior.

`--smoke --pause-on-start --settings PATH.json` opens the production pause menu after game loading and before the smoke check continues. The explicit player file keeps the diagnostic separate from normal preferences. The smoke path does not check graphics resource changes. `--pause-on-start` also works with bounded window runs.

Add `--gpu` to the pause-menu check for native 1920x1080 captures with host
Vulkan validation. It checks the frozen scene before Resume, repeated Apply,
feature changes without a renderer rebuild, Restore Defaults, and recovery
after a settings save failure. Apply redraws the scene before the settings
menu opens again. The match stays paused. The check compares captured pixels,
checks input isolation, then resumes for two frames. It uses one film sample,
a 10-second process limit and no hardware tracing. It does not measure sustained
performance or cover every graphics-setting combination.


Run `python3 tests/e2e/pause_menu.py build/windows-cross/nanova.exe --gpu --os-input` to check OS Esc input, mouse navigation, Esc back and Enter resume. This captures only the menu window's native client pixels. Game captures remain native 1920x1080.

Checked Oct 20: 24 CPU menu and audio application runs passed. Captured PCM confirmed volume gain and mute. The smoke, camera, screen selection and polish checks passed. All three faction choices passed through the main menu. Bounded GPU runs checked graphics Apply, renderer cleanup during a faction change, recovery after a settings save failure, input isolation and resume with the host Vulkan validation layer. OS menu input also passed.

The broader `world_files` check failed because its empty building fixture has no `shared` field. The existing content loader requires that field. The loader and fixture helper were not changed for this feature. The complete suite, all graphics combinations, native Windows and audible device output remain unverified.


## Repeated factory forks (Oct 21)

All factions use repeated tree forks. Large roots split twice. Child limbs
get shorter and thinner. Seeded angles and bends change the shape within
the authored bounds. Each small root has one child. Its stem uses a 2.5 width
multiplier; its head, health, and per-source manufacture scale stay separate.
Navigation and debris use the visible stem scale. Roots keep the direction
from the nucleus through the organelle center.

The method uses repeated branch relations from
[plant self-similarity research](https://algorithmicbotany.org/papers/fractals.tip2004.html)
and split-angle control from the
[dielectric breakdown model](https://arxiv.org/abs/cond-mat/0112218).
These are shape references. Growth remains the existing bounded game rule.

At radius 4.2 and seed 1234, each faction has ten roots: three large trees
with seven structures each, plus seven small trees with two structures each.
That is 35 structures per cell. Fungal source checks still enforce the
existing mature output and nutrient-income limits.

The Windows cross-build passed twelve real-application check groups through
Proton: smoke, natural_factory, small_factories, fungal_sources, data_assets,
growth_factions, three_factions, factory_repair, navigation, fungal_defence,
colony_forms, and cell_animation. Checks cover radii 2.1, 2.7, and 4.2,
three layout seeds, outward roots, taper, joints, wall clearance, navigation
widths, damage, and regrowth. A parallel-axis case checks that forks remain
separate and finite. Invalid widths, missing dependencies, and storage
overflow fail at startup. The bend value 0.05 now passes its documented
lower limit; tree decimal bounds use double precision during JSON validation.

The earlier balance fixtures retain their original faction grammars.
Current-content checks use the new defaults. Instrumented coverage, the
complete suite, and native Windows remain unverified.

### Bounded render cost

RX 7800 XT, RADV Mesa 25.2.8, Windows executable through Proton 11.
Each capture uses one frozen native 1920x1080 frame, default sampling,
one film sample, three volume steps, and Khronos host Vulkan validation.
Initial lighting bakes are included. Each process completed in about one
second. These short captures measure appearance and bounded cost.

| Faction | Layout | GPU trace ms | Total GPU ms | Application frame ms |
| --- | --- | ---: | ---: | ---: |
| fungus | Earlier | 9.802 | 11.812 | 28.749 |
| fungus | Current | 9.657 | 11.484 | 28.866 |
| immune | Earlier | 9.230 | 10.985 | 29.184 |
| immune | Current | 9.497 | 11.348 | 29.687 |
| virus | Earlier | 11.534 | 13.303 | 32.051 |
| virus | Current | 11.314 | 13.172 | 30.807 |

Final captures are screenshots 000986 (fungal), 000987 (immune), and 000988
(virus). Earlier captures are 000980, 000982, and 000984. Inputs, application
logs, captures, and the measurements are in `build/fractal_factory_review/`.

### Dense combat cost

Ryzen 9 9900X, Windows Release cross-build through Proton 11. The real
three-faction stress match uses seed 1234, fifteen mature cells, 90 seconds
of actual production, fungal sight radius 18, and 1,800 measured combat
ticks. Three runs per build ran with no other test active. Setup is separate
from the tick measurements. The table gives medians across the three runs.

| Layout | Peak units | Mean tick ms | p99 tick ms |
| --- | ---: | ---: | ---: |
| Earlier | 3801 | 3.954 | 10.429 |
| Current | 3721 | 4.236 | 11.403 |

The current mean is about 7 percent higher. The layouts produce different
unit positions and fight results, so these values measure content cost.
The largest observed current tick was 12.979 ms. Phase clocks show movement
and fog as the largest costs. Raw reports are in
`build/fractal_factory_dense_before/` and `build/fractal_factory_dense_after/`.
CPU tick times, GPU work, and application frame times are separate values.
Long gameplay and sustained GPU performance remain unverified.


## Curved factory paths (Oct 21)

All factions use quadratic Bezier paths. Virus curvature is 0.12, Immune
is 0.65, and Fungus is 1.4. `branches.tree.curvature` sets the bow relative
to the distance between endpoints. The maximum offset is about one quarter
of that setting: 3, 16.25, and 35 percent of endpoint distance. Zero makes
a straight path. The setting accepts values from zero through two.

Each limb has twelve path spans. Growth, rendering, navigation, head
direction, and detached pieces use those points. The four damage sections
remain separate: losing one section removes three path spans. Child limbs
join at the parent curve and use its local direction. A bound on outward
growth keeps curved children from crossing back through the cell.
Paths are built when a factory starts or regrows. Simulation ticks read
the fixed arrays. Curvature adds no per-unit allocation or synchronization.

Twelve real-application groups passed through Proton: smoke, factory_curves,
natural_factory, small_factories, cell_animation, factory_emission,
data_assets, colony_forms, navigation, factory_repair, fungal_sources,
and three_factions. The new curve check uses all factions, radii 2.1, 2.7,
and 4.2, and seeds 1234 through 1236. It measures bow, gradual turns,
outward travel, connected forks, head directions, and agreement between
rendered and navigation paths. It checks partial growth, a quarter-damage
cut, zero curvature, maximum curvature, repeatability, and invalid settings.
Existing checks cover regrowth and joints during animation.

### Native captures and bounded GPU cost

RX 7800 XT, RADV Mesa 25.2.8, Windows Release executable through Proton 11.
Each capture uses one frozen native 1920x1080 frame with default sampling
and the Khronos host Vulkan validation layer. All six captures passed.
Initial lighting bakes are included. Each process completed in about one
second. These values describe bounded captures; sustained play remains
unverified.

| Faction | Paths | GPU trace ms | Total GPU ms | Application ms | Application FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| virus | Earlier | 11.155 | 12.977 | 30.161 | 33.16 |
| virus | Curved | 11.157 | 13.165 | 33.414 | 29.93 |
| immune | Earlier | 9.515 | 11.505 | 29.155 | 34.30 |
| immune | Curved | 11.016 | 13.009 | 32.119 | 31.13 |
| fungus | Earlier | 9.471 | 10.934 | 27.400 | 36.50 |
| fungus | Curved | 9.946 | 11.927 | 30.095 | 33.23 |

Native captures: 000990 fungal, 000992 immune, and 000994 virus.
Earlier paths: 000989, 000991, and 000993. The input files, logs, capture
script, and render report are in `build/curved_factory_review/`.

### Dense combat cost

Ryzen 9 9900X, Windows Release cross-build through Proton 11. Each build
ran three real matches with fifteen mature cells, seed 1234, 90 seconds
of production, fungal sight radius 18, and 1,800 measured combat ticks.
No other test ran during measurement. Setup is separate. Values below
are medians across three runs.

| Paths | Peak units | Mean tick ms | p99 tick ms |
| --- | ---: | ---: | ---: |
| Earlier | 3721 | 4.251 | 11.389 |
| Curved | 3722 | 4.244 | 11.691 |

Curves change unit paths and fight results. These values measure content
cost across different match states. The largest observed curved-path tick
was 13.864 ms. Raw reports are in `build/curved_factory_dense_before/` and
`build/curved_factory_dense_after/`. CPU ticks, GPU work, application frames,
and sustained performance remain separate measurements. Native Windows,
the complete suite, and instrumented coverage remain unverified.


## Irregular S-bends (Oct 21)

The current factory paths use unequal opposing lobes and smaller bends at
three scales. The smaller bends have half the wavelength and one quarter
of the amplitude at each scale. Each seed varies their phase, the main
bend position, and lobe size. Geometry stays fixed between growth changes.

The design follows the use of related variation in
[plant geometry research](https://algorithmicbotany.org/papers/variation.eg2005.html).
This implementation controls curve shape directly. It retains the existing
branch hierarchy, taper, outward roots, production, and damage rules.

`branches.tree.curve_jitter` controls variation from zero through one.
Defaults are 0.2 for Virus, 0.4 for Immune, and 0.65 for Fungus. Curvature
remains 0.12, 0.65, and 1.4. Zero jitter produces an even S-curve; zero
curvature produces a straight path. Jitter requires a curvature setting.

Each limb has 32 shared path spans. Growth, rendering, navigation, and
breakage use the same points. Four damage sections remain, with eight
path spans per section. Navigation scratch now belongs to the simulation:
its bounded array exceeds the Windows executable's 2 MiB stack reserve.
The movement phase reuses that storage. Diagnostic state export allocates
its separate buffer at the export boundary. No tick allocates this buffer.

Twelve targeted application groups passed through Proton: smoke,
factory_curves, small_factories, natural_factory, cell_animation,
factory_emission, fungal_sources, navigation, factory_repair, data_assets,
colony_forms, and three_factions. Checks include all factions, three cell
sizes, three seeds, opposing lobes, asymmetry, repeatability, zero jitter,
smooth sampled turns, matching render and navigation paths, partial growth,
quarter cuts, regrowth, production limits, and invalid settings.

### Native captures

The RX 7800 XT used RADV Mesa 25.2.8 and the Windows Release application
through Proton 11. Each frozen capture used one native 1920x1080 frame,
default sampling, and the Khronos host Vulkan validation layer. All captures
passed. Initial lighting bakes are included. These short captures measure
bounded cost. Sustained GPU performance remains unverified.

| Faction | Path | Total GPU ms | Application ms | Application FPS |
| --- | --- | ---: | ---: | ---: |
| virus | Earlier C-bend | 10.551 | 28.158 | 35.51 |
| virus | Irregular S-bend | 11.418 | 32.578 | 30.70 |
| immune | Earlier C-bend | 9.793 | 27.872 | 35.88 |
| immune | Irregular S-bend | 12.506 | 32.705 | 30.58 |
| fungus | Earlier C-bend | 9.831 | 27.709 | 36.09 |
| fungus | Irregular S-bend | 11.186 | 30.904 | 32.36 |

Final native captures are 001001 fungal, 001002 immune, and 001003 virus.
Inputs, logs, the capture script, and measurements are in
`build/s_curve_factory_review/`. The earlier 24-span review remains saved.

### Dense combat cost

Ryzen 9 9900X, Windows Release build through Proton 11. Three real matches
per build used seed 1234, fifteen mature cells, 90 seconds of production,
fungal sight radius 18, and 1,800 measured combat ticks. No other test ran
during measurement. Setup is separate. Values below are run medians.

| Path | Peak units | Mean tick ms | p99 tick ms |
| --- | ---: | ---: | ---: |
| Earlier C-bend | 3722 | 4.194 | 11.379 |
| Irregular S-bend | 3723 | 5.201 | 12.950 |

The mean increased by about 1.01 ms in this case. Movement is the largest
added phase cost. Geometry changes also alter unit paths and fight results.
The largest observed S-path tick was 16.572 ms. Raw records are in
`build/s_curve_factory_dense_before/` and `build/s_curve_factory_dense_after/`.
CPU simulation, GPU stages, and application frames are separate measures.
The complete suite, native Windows, and long gameplay remain unverified.

## Immune production and factory health (Oct 21)

Immune fighters now take 4 seconds to manufacture, down from 8. Bombers
now take 5 seconds, down from 10. Factory trunks have 2,880 health and 48
hits, up from 1,440 and 24. Branches have 1,800 health and 32 hits, up from
900 and 16. Raising both health and hits preserves damage per wound and
also increases resistance to direct projectiles. Small structures retain
area scaling and round their hit counts up. Nutrient costs and collection
rates keep their existing values.

The factory loader and building schema now allow 64 hits. The unit limit
remains 32. A real application accepts the factory limit and rejects 65
root or branch hits and 33 unit hits. The natural-factory destruction check
uses four quarter-health bursts to stay within the damage asset limit.

Verification used the Windows application through Proton. Eight groups
passed: smoke, data_assets, production_ratio, natural_factory,
small_factories, factory_curves, three_factions, and factory_repair.
Controlled 60-second production runs used the same seed, one mature Immune
cell, sufficient nutrients, and separate fighter and bomber priorities.
Factory work includes completed units plus unfinished job progress. Each
of the 35 structures performed twice the prior work within 0.5%; total
work ratios were 1.999992 for fighters and 1.999996 for bombers. Completed
counts were 147 to 299 fighters and 106 to 236 bombers. Batch completion
changes count ratios at a fixed end time. All 35 structure health values
were twice their prior values, with the same paths and scales.

Three real dense combat matches per balance used seed 1234, 15 mature
cells, 90 seconds of production, and 1,800 measured ticks. No other check
ran during the measurements. Median CPU tick times:

| Balance | Peak units | Mean ms | p99 ms |
| --- | ---: | ---: | ---: |
| Before buff | 3725 | 5.162 | 12.742 |
| After buff | 3731 | 5.193 | 12.747 |

The largest observed tick after the buff was 13.638 ms. This is a bounded
CPU simulation measurement. It does not establish sustained GPU or native
Windows performance. Inputs, scripts, logs, states, and raw timing reports
are in `build/immune_buff_review/`.


## Faction factory setup rates (Oct 21)

Factory definitions can set `setup_speed` from 0.25 to 4. The default is
1. At asset loading, the loader divides growth, sprout, establishment,
branch emergence, root interval, and root offset durations by this value.
This adds no simulation field or per-tick rate calculation. Repair work
and regrowth wait durations retain their authored values; replacement
structures grow at the faction growth rate.

Immune uses speed 2: the first sprout takes 4.286 seconds and complete
establishment takes 257.143 seconds after settlement. Fungus uses speed
0.9: the first sprout takes 9.524 seconds and establishment takes 571.429
seconds. Virus uses speed 1: 8.571 and 514.286 seconds. These durations
exclude travel and landing. Unit manufacture has a separate rate.

The setup comparison ran 28 real Windows application scenarios through
Proton. Founders used normal flight and landing. Each faction was compared
with a speed-1 control at matching fractions of setup time. Early, middle,
and mature states retained the same 35 structure paths and parent links;
visible growth differed by less than 0.02, including tick rounding. Every
structure was complete in the final comparison. The game accepted both
rate limits and rejected zero, negative, out-of-range, and text rates.
Exact older timing fixtures remove `setup_speed` to retain their explicit
reference timings. Current-content checks use the shipping rates.

The controlled production checks count unfinished work as well as completed
units, as in manufacturing. Matched seeds and growth fractions provide
experimental controls. Area scaling checks cover small structures, as in
material testing. Invalid data and damage scenarios try to disprove the
result. The checks use the game executable, its state export, replayable
inputs, and CPU profiler. No separate component model supplies results.

Inputs, the comparison script, state exports, and logs are in
`build/factory_setup_review/`.


Nine affected E2E groups passed: smoke, data_assets, growth_factions,
factory_curves, three_factions, fungal_sources, small_factories,
factory_repair, and natural_factory. Two checks had Proton launch failures
during parallel execution; both passed when repeated sequentially.
The final balance also completed three dense combat matches with seed
1234, 15 mature cells, 90 seconds of production, and 1,800 measured ticks
per match. Peak population was 3731. The median mean CPU tick was
5.260 ms; median p99 was 12.882 ms; the largest observed tick was
13.440 ms. No other check ran during these measurements. This is
bounded CPU evidence, not a sustained GPU or native Windows result.


## Second Immune buff (Oct 21)

Immune manufacture times are now 2 seconds for fighters and 2.5 seconds
for bombers. Factory trunks have 5,760 health and 96 hits; branches have
3,600 health and 64 hits. These values double the previous rate and
durability. Setup speed remains 2. The building hit limit is now 128 in
both the loader and schema. Damage fixtures use eighth-health bursts
to remain within the damage asset limit.

Real Proton application checks passed for smoke, natural_factory,
factory_curves, and three_factions. Controlled production work ratios
were 1.999953 for fighters and 1.999975 for bombers. Every structure in
the 35-structure cell had twice its prior health. Inputs, states, and
logs are in `build/immune_second_buff_review/`.


## Faction factory shapes (Oct 21)

Immune stems and heads use overlapping rounded masses joined by voxel
remeshing in Blender. The runtime width profile adds three broad bulges
per limb. Virus factory tips use a slender body, 45 fine tapered spines,
and one axial spike. The phage cap and six tail fibres are removed from
factory tips. Fungal factories use brown stems and four curved tendrils
per tip, with rounded ends. Their stem mesh has no lateral needle branches.
The user supplied the Cordyceps photo used for the tendril reference.

The source profiles remain in `cell-unit-factory-shape.json`. The
`--factories-only` generator option updates only factory exports and saves
`assets/models/source/factory_shapes.blend`. Other mesh exports and their
source scenes are retained. Shape colors use vertex attributes.

Each rounded stem piece fits its actual mesh bounds. Its axial length
covers the path span plus rounded end margins. Its center stays on the
path. This prevents flattened disks and gaps between short pieces. The
same fitting applies to debris. Broad Immune widths are shared by
presentation, navigation, and breakage. Fungal navigation uses radius
0.2 for stems and 0.85 for the new tip clusters.

`growth_meshes` has optional named controls: `rounded` (boolean), `bulges`
(integer 0 through 8), `glow_scale`, and `tint_strength` (both 0 through 1).
The defaults retain the earlier straight-axis posing, no broad bulges,
full glow, and 0.25 faction tint. Immune uses rounded pieces, three bulges,
and glow 0.25. Fungus uses rounded pieces, zero bulges, glow 0.06, and tint
0.04 so the brown surface remains visible.

Per-mesh triangle counts:

| Faction | Stem before | Stem after | Tip before | Tip after |
| --- | ---: | ---: | ---: | ---: |
| Virus | 68 | 68 | 92 | 268 |
| Immune | 1096 | 756 | 480 | 960 |
| Fungus | 856 | 320 | 340 | 1096 |

Real Windows application checks through Proton passed for smoke,
factory_curves, small_factories, natural_factory, navigation, cell_animation,
data_assets, factory_repair, three_factions, fungal_sources, and
factory_emission. The curve check compares actual mesh vertices with
navigation capsules, checks axial coverage to prevent gaps, and checks
round piece centers, partial growth, cuts, and connected factory paths.
Older endpoint fixtures keep their explicit earlier posing mode. Invalid
rounding, bulge count, glow, and tint settings are rejected.

Native 1920x1080 captures used default sampling, one frozen frame per
process, and the Khronos host Vulkan validation layer. The close-view
comparison disables tilt-shift blur for shape inspection. It does not
change the shipping renderer. Final close views are screenshots 001017
(Immune), 001013 (Virus), and 001018 (Fungus). Before views are 001009,
001010, and 001011. These are bounded measurements:

| Faction | Before GPU ms | After GPU ms | Before app ms | After app ms |
| --- | ---: | ---: | ---: | ---: |
| Immune | 10.099 | 9.363 | 18.637 | 17.930 |
| Virus | 9.019 | 8.965 | 18.091 | 17.161 |
| Fungus | 11.501 | 9.750 | 20.144 | 18.178 |

GPU values are the total renderer query. App values are application frame
times. These single-frame results do not establish sustained frame rate
or native Windows behavior. Inputs, capture commands, states, and logs
are in `build/factory_shapes_review/`.


CPU comparison used the preserved build from commit 0b26e8e and the final
build. Each ran three real matches with seed 1234, 15 mature cells, 90
seconds of production, and 1,800 measured combat ticks. No other check ran
during these measurements. Median per-run results:

| Build | Peak units | Mean tick ms | p99 tick ms | Largest observed tick ms |
| --- | ---: | ---: | ---: | ---: |
| before | 3731 | 5.234 | 12.719 | 15.088 |
| after | 3740 | 5.069 | 12.861 | 14.418 |

Balance and navigation changes alter the battle, so these states differ.
This is bounded CPU simulation evidence. It does not establish sustained
GPU performance. Raw timing and phase records are in the `cpu_before` and
`cpu_after` folders under `build/factory_shapes_review/`.


## Fungal pigment and tip spacing (Oct 21)

Fungal factory color now becomes darker with cumulative distance along
its growth path. Child branches inherit distance at the parent junction.
This keeps the color continuous through forks. Visible growth controls
which part of the path supplies the end color. Existing injury tint takes
priority on wounded stems. The optional `growth_meshes.length_color`
object supplies a target RGB color, a positive distance, and strength from
0 through 1. This uses current geometry and instance tint; it stores no
frame history and allocates no per-frame heap memory.

The Fungal setting uses color [0.045, 0.012, 0.004], distance 8, and strength
0.65. In the captured large root, linear surface-color brightness changed
from 0.154 near the start to 0.101 near the end, about 35 percent lower.
This is an average mesh-color and instance-tint measurement before light
and display conversion. The previous stem had the same value at both ends.

Each tip now has three tendrils spaced at 120 degrees. Their bases are
separate, their lateral motion is smaller, and each tube stays inside its
own angular sector. Tendril height variation is 14 percent. The Fungal
fork angle is 44 degrees with 6 degrees of jitter. Length jitter is 0.16
and curve jitter is 0.5. Curvature remains 1.4. These changes retain the
wandering paths while reducing crowding. The tip mesh uses 930 triangles,
down from 1,096. Factory production head counts retain their values.

Seven real Windows application groups passed through Proton:
factory_curves, small_factories, fungal_sources, factory_emission,
data_assets, three_factions, and navigation. The added checks inspect the
exported mesh after a successful game run: three separate tendrils,
equal endpoint angles, and no tube crossing into the neighboring sector.
Color checks cover monotonic tint, inherited distance, actual surface
brightness, and identical simulation state when the length color is removed.
Malformed color settings are rejected. Existing checks cover three cell
sizes, three fixed seeds, partial growth, damage, and navigation bounds.

Host Vulkan validation passed for native 1920x1080 captures, one frozen
frame per process with default sampling. Tilt-shift blur was disabled for
inspection. Before and after close views are screenshots 001019 and
001020. Large and small cell views are 001021 and 001022; the latter uses
seed 1235. The close view measured GPU renderer time 9.673 to 9.540 ms
and application frame time 19.076 to 18.831 ms. These are single-frame
measurements, not a sustained frame-rate claim.

CPU comparison used three real matches per build, seed 1234, 15 mature
cells, 90 seconds of production, and 1,800 measured combat ticks. No other
check ran during these measurements. Median per-run results:

| Build | Peak units | Mean tick ms | p99 tick ms | Largest observed tick ms |
| --- | ---: | ---: | ---: | ---: |
| before | 3740 | 5.077 | 12.951 | 17.156 |
| after | 3750 | 5.088 | 13.143 | 17.870 |

Branch geometry changes the battle, so the states differ between builds.
Color alone leaves simulation state unchanged. Reports, worlds, state
exports, capture commands, and timing logs are in
`build/fungal_spacing_review/`. Native Windows and sustained GPU timing
remain unverified.
