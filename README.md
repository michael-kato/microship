# microship

Microscopic strategy inside a body, inspired by Eufloria. Command craft, scout living cells, establish factories, and expand. Both sides start around neutral cells. Fighters prefer units; bombers prefer factories. A slider sets their production ratio. Destroy enemy factories to win.

Virus forces use large swarms of light craft and spike projectiles. Immune forces use fewer, tougher craft that attach, digest, and heal. The fungal trial uses seeking explosive spores.

Seeded membranes, organelles, and branching factories form the world. Units grow at factory tips. Damaged limbs break away. Fog strongly blurs areas outside current sight. Direct ray tracing provides light, shadows, and atmospheric scattering. Colors come from vertices; each frame uses current data only.

Simulation rules remain separate from rendering. The project uses direct functions and contiguous data, with real application checks.

C++ and Vulkan target Windows. Linux builds use MinGW, `glslc`, and an initialized Steam Proton prefix:

```sh
bash scripts/check.sh --cpu
python3 scripts/proton_run.py build/windows-cross/microship.exe
```