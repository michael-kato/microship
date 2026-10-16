# microship

A C++/Vulkan strategy game inside a body. Virus units use spike projectiles. Larger immune units attach to targets, digest them, and heal while feeding. The fungal trial launches destructible seeking spores from home cells. Its blasts can damage nearby friendly units and structures. Fungal balance remains under evaluation in [the outline](ONE_PAGE_OUTLINE.md).

Game assets use JSON with named fields and local editor schemas. `assets/gameplay/biology.json` links separate body, faction, group, unit, and building files. Gameplay rules, level setup, presentation, and render settings use separate files. `assets/startup.json` selects the default assets. See the [asset guide](assets/README.md) for the file layout and editing rules.

Game presentation converts the current simulation into a plain CPU `RenderScene`. The Vulkan renderer reads meshes, transforms, materials, lights, media, and effects. It has no cell, faction, mobile type, or factory dependencies. `--smoke` builds and checks this render input without starting the GPU. `--scene-state PATH.json` exports it.

The map has two home cells and three neutral cells. The homes are about 102 world units apart. The camera starts at your home. The full map cannot fit within the normal zoom range. Scout to find cells. Camera movement does not reveal them. A discovered cell stays on the map. Enemy units and factories are visible only within sight range.

Fighters attack units first. Bombers attack factories first. Both have a secondary attack at one eighth of their normal rate. After defenders are gone, either type can colonize a cell. One unit is consumed to make the new factory. Destroy all enemy factories to win. Fungal spores act without orders and cannot colonize.

Idle craft use their own task ranges. A threat near a cell no longer sends every local craft after it. Fighters notice units within 5.5 world units; bombers notice units within 3.5 and structures within 5. Decisions are staggered. Nearby idle craft can also settle known, undefended cells. Explicit move and cell orders take priority. A craft resumes local tasks when its move ends.

Flight top speeds are 20 percent lower. Idle patrols use 65 percent of top speed. Acceleration is 3.2 world units per second squared, and orbit phase speed is 0.24 radians per second. Patrols vary their radius, height, direction and pace. They steer around current factory stems and heads. Feeding units can reach the structure they attack; newly produced units have two seconds to leave their growth tip. Fungal spores retain their seeking behaviour.

To record and plot real patrol paths, run `python3 tests/e2e/navigation.py build/windows-cross/microship.exe --output build/navigation`, then run `python3 scripts/plot_flight.py build/navigation/paths.json --output build/navigation/flight-paths.svg` with a Python environment that has Matplotlib. The plot joins measured half-second positions. It is not a prediction of future paths.

Each cell has a seeded membrane shape and internal layout. Each discrete organelle has a factory attachment. Small cells retain fewer organelles: the default radii 2.1, 2.7, and 4.2 have four, six, and ten attachments. Each attachment has an independent root. Stems extend away from the cell center. Virus factories have faceted heads, ribbed tails, and six fibres. Immune factories have rounded lobes and paired fans. Fungal factories have branching stalks and spore caps. These forms use separate meshes and authored growth rules.

A newly colonized cell grows its factory structures one at a time over 90 seconds. Production increases with visible growth. The first structure can produce units before the cell reaches full capacity. Damage can delay full capacity. Home cells retain their starting roots.

Base unit manufacture takes 6 seconds, up from 2.5 seconds. Roots share the base collection and manufacture rate. Child limbs add to that rate. Units form at mature tips. Roots have 240 health and take 12 hits; child limbs have 150 health and take 8 hits. Damage removes one of four stem sections after each three root hits or two child hits. Heads and detached sections drift away. Loss of a supporting section removes its child limbs. Other organelle factories keep working. Lost roots and limbs wait 24 seconds before regrowth. The cell is lost only when all its factories are destroyed. Attacks can reach structures through their own cell membrane; other cells still block the shot.

Normal window starts choose a new variation seed. `--world-seed N` selects a repeatable layout. CPU and headless runs use authored seeds unless this option is supplied. R restores the same layout. Virus and fungal cell tints change over 180 seconds; immune control returns cells toward their neutral appearance.

Twelve short effects are synthesized locally as PCM WAV files. The game has hooks for orders, growth, maturity, birth, firing, hits, death, colonization, healing, outcomes, and limb breaks. Playback uses a fixed 16-voice mixer. Off-screen enemy events do not produce sounds. `--smoke --ticks 1800 --audio-capture build/growth.wav` exports a 30-second mix without starting the GPU or an audio device. Audio settings and synthesis parameters are in `assets/audio/`.

Cells have branching internal geometry and a scattering medium within the membrane. Units and factories also have repeated branch forms. All color is in mesh vertices. Mesh vertices also carry roughness and optical index. Material profiles give organelles, fibres, capsids, and faction structures separate gloss and pigment ranges. The authoring generator bakes this variation without image maps. The direct ray tracer evaluates the studio lights and their shadows, follows clear membrane transmission, and integrates cell scattering. Ambient fill approximates indirect light. Each frame uses current scene data and fixed spatial samples. There is no temporal processing or denoising.

Controls:

| Input | Action |
| --- | --- |
| Left drag / wheel | Pan / zoom at the pointer |
| 1 / 2 / 3 | Select fighters / bombers / all units |
| Shift-click | Add or remove a unit from the selection |
| Right-click open space | Move selected units and scout |
| Right-click a known cell | Attack, orbit, or colonize |
| Click your cell, then drag its production slider | Set the fighter / bomber production ratio |
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

The production slider sets the unit mix for the selected friendly cell in one-percent steps. The left end makes fighters only; the right end makes bombers only. Each cell keeps its own setting. A unit already in production finishes before the new mix applies. The ratio controls future production, not the number of surviving units. F and S still select the two endpoints. Replay `ratio 75` requests 75 percent fighters and 25 percent bombers. Restart restores the startup setting. Automatic spore emitters have no ratio control.

Select the fungal trial with `--world assets/levels/biology-fungus.json`. The separate `biology-swarm.json` level tests a faster two-hit virus swarm against immune units. It does not replace the default three-hit virus balance.

GPU checks require a user request under [the project rules](AGENTS.md). `scripts/check.sh --gpu` adds three sequential native 1920×1080 captures, including a frozen-frame repeat with validation. The default render config uses six film samples, one volume film ray, and one background query per pixel. Each GPU process uses at most two frames, a 10-second process timeout, and a 30-second batch limit. See [PROFILING.md](PROFILING.md).

[Structured assets](assets/README.md), [engineering rules](ENGINEERING.md), and [research notes](RESEARCH_NOTES.md) contain the data contracts and rendering constraints. Two-cell worlds in `tests/e2e/scenes/` supply short CPU checks. The source, assets, parser, and shaders support the biological game. The old space game and its compiled applications were removed.

Keep `build/` ignored. It contains generated applications, shaders, copied assets, local tools, and check output. Commit source assets, source code, CMake files, and scripts. A clean game build needs the compiler, Vulkan headers/import library, `glslc`, and an initialized Proton prefix. Remove `build/windows-cross/` to rebuild the game from source; keep local tools in `build/toolchains/`.

The October 16 direct-ray GPU check passed through Proton with host validation active. Native 1080p captures are in `screenshots/000240-direct-rays-virus-initial.png` through `000242-direct-rays-immune-growing-factory.png`. The frozen repeat is identical. See [PROFILING.md](PROFILING.md) for the four observed frame times and verification limits.


The October 16 gameplay pass passed 266 application scenarios across eight CPU groups through Proton. New native 1080p captures show [organelle growth](screenshots/000243-organelle-growth.png), [immune limb damage](screenshots/000247-immune-breaks-factory-limbs.png), and [a fungal blast](screenshots/000248-fungal-blast.png). Khronos host validation passed. See [PROFILING.md](PROFILING.md) for frame observations, reproduction commands, and limits. The PCM mix was checked as an application export; live audio and native Windows remain unverified.

The organelle factory check saved native 1080p captures 000249–000253. It checked virus, immune, fungal, root damage, and small-cell views with Khronos host validation active. See [PROFILING.md](PROFILING.md) for timings and limits.
