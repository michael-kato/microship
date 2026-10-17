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
