# Nanova

Microscopic strategy inside a body, inspired by Eufloria. Command craft, scout living cells, establish factories, and expand. Both sides start around neutral cells. Fighters prefer units; bombers prefer factories. A slider sets their production ratio. Destroy enemy factories to win.

Virus forces use large swarms of light craft and spike projectiles. Immune forces use fewer, tougher craft that stun a target with a blob, devour it, then heal nearby allies. The fungal trial fields swarms of melee spores that burst into toxic clouds.

Seeded membranes, organelles, branching factories, and wet anatomical backgrounds form the world. Units grow at factory tips. Damaged limbs break away. Fog returns gradually and strongly blurs unseen areas. Direct ray tracing provides light, shadows, and atmospheric scattering. Colors come from vertices; each frame uses current data only.

Textures are permitted on any object when the game generates them. Noise and other procedural methods may generate color, normal, roughness, opacity, emission, and environment maps. Vertex colors remain supported.

Simulation rules remain separate from rendering. The project uses direct functions and contiguous data, with real application checks.

C++ and Vulkan target Windows. Linux builds use MinGW, `glslc`, and an initialized Steam Proton 11 prefix:

```sh
bash scripts/check.sh
python3 scripts/proton_run.py build/windows-cross/nanova.exe
```