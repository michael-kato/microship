# microship

A C++/Vulkan strategy game inside a body. Play as viruses or white blood cells. Use nutrients from cells to grow factories and produce units. Fungus and other faction rules remain design proposals in [the outline](ONE_PAGE_OUTLINE.md).

Game assets use JSON with named fields and local editor schemas. `assets/gameplay/biology.json` links separate body, faction, group, unit, and building files. Gameplay rules, level setup, presentation, and render settings use separate files. `assets/startup.json` selects the default assets. See the [asset guide](assets/README.md) for the file layout and editing rules.

Game presentation converts the current simulation into a plain CPU `RenderScene`. The Vulkan renderer reads meshes, transforms, materials, lights, media, and effects. It has no cell, faction, mobile type, or factory dependencies. `--smoke` builds and checks this render input without starting the GPU. `--scene-state PATH.json` exports it.

The map has two home cells and three neutral cells. The homes are about 102 world units apart. The camera starts at your home. The full map cannot fit within the normal zoom range. Scout to find cells. Camera movement does not reveal them. A discovered cell stays on the map. Enemy units and factories are visible only within sight range.

Fighters attack units. Bombers attack factories. After defenders are gone, either type can colonize a cell. One unit is consumed to make the new factory. Destroy all enemy factories to win.

Each home starts with one factory structure. Virus factories have capsids and forked ribs. Immune factories have rounded lobes. Each factory grows an additional structure every 16 seconds, up to the count in `gameplay/buildings.json`. Each new structure grows for five seconds and increases nutrient collection and production. The structures lie in an authored plane near the camera plane. Their branches and offsets use a fixed seed.

Cells have branching internal geometry and a scattering medium within the membrane. Units and factories also have repeated branch forms. All color is in mesh vertices. The direct ray tracer evaluates the studio lights and their shadows, follows clear membrane transmission, and integrates cell scattering. Ambient fill approximates indirect light. Each frame uses current scene data and fixed spatial samples. There is no temporal processing or denoising.

Controls:

| Input | Action |
| --- | --- |
| Left drag / wheel | Pan / zoom at the pointer |
| 1 / 2 / 3 | Select fighters / bombers / all units |
| Shift-click | Add or remove a unit from the selection |
| Right-click open space | Move selected units and scout |
| Right-click a known cell | Attack, orbit, or colonize |
| Click a cell, then F / S | Produce fighters / bombers |
| Home | Focus the selected cell or your home |
| Space / R | Pause / restart |
| F1 / F2 | Timing data / current render buffers |

Build and run the CPU application checks on Linux:

```sh
bash scripts/check.sh --cpu
```

The target is Windows. The build uses MinGW and `glslc`. CPU checks run the Windows executable through Steam Proton 10. Set `MICROSHIP_PROTON` and `STEAM_COMPAT_DATA_PATH` to use another Proton installation and initialized prefix. The prefix must contain `pfx/system.reg`. A normal Proton `run` command initializes it. Build assets are copied beside the executable.

Start the game:

```sh
python3 scripts/proton_run.py build/windows-cross/microship.exe
```

Select the immune side:

```sh
python3 scripts/proton_run.py build/windows-cross/microship.exe --world assets/levels/biology-immune.json
```

GPU checks require a user request under [the project rules](AGENTS.md). `scripts/check.sh --gpu` adds three sequential native 1920×1080 captures, including a frozen-frame repeat with validation. The default render config uses six film samples, one volume film ray, and one background query per pixel. Each GPU process uses at most two frames, a 10-second process timeout, and a 30-second batch limit. See [PROFILING.md](PROFILING.md).

[Structured assets](assets/README.md), [engineering rules](ENGINEERING.md), and [research notes](RESEARCH_NOTES.md) contain the data contracts and rendering constraints. Two-cell worlds in `tests/e2e/scenes/` supply short CPU checks. The source, assets, parser, and shaders support the biological game. The old space game and its compiled applications were removed.

Keep `build/` ignored. It contains generated applications, shaders, copied assets, local tools, and check output. Commit source assets, source code, CMake files, and scripts. A clean game build needs the compiler, Vulkan headers/import library, `glslc`, and an initialized Proton prefix. Remove `build/windows-cross/` to rebuild the game from source; keep local tools in `build/toolchains/`.

The October 16 direct-ray GPU check passed through Proton with host validation active. Native 1080p captures are in `screenshots/000240-direct-rays-virus-initial.png` through `000242-direct-rays-immune-growing-factory.png`. The frozen repeat is identical. See [PROFILING.md](PROFILING.md) for the four observed frame times and verification limits.
