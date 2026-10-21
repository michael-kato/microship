# Nanova

Microscopic strategy inside a body, inspired by Eufloria. Command craft, scout living cells, establish factories, and expand. All three factions start on a 15-cell map. Fighters prefer units; bombers prefer factories. A slider sets their production ratio. Destroy enemy factories to win.

Virus forces use large swarms of light craft and spike projectiles. Immune forces use fewer, tougher craft that stun a target with a blob, devour it, then heal nearby allies. The fungal trial fields swarms of melee spores that burst into toxic clouds.

Seeded membranes, organelles, branching factories, and wet anatomical backgrounds form the world. Units grow at factory tips. Damaged limbs break away. Fog returns gradually and strongly blurs unseen areas. Direct ray tracing provides light, shadows, and atmospheric scattering. Colors come from vertices; each frame uses current data only.

Each visible organelle supports a factory. Large organelles support branching factories. Smaller organelles support short stems and small heads that also produce units. `shared.branches.additional_root_scale` sets their maximum size relative to a large factory; organelle radius reduces it further. Navigation bounds follow the same scale. Small-factory health and manufacture rates scale with cross-sectional area. Fungal sources share the existing cell output limit.

Textures are permitted on any object when the game generates them. Noise and other procedural methods may generate color, normal, roughness, opacity, emission, and environment maps. Vertex colors remain supported.

Simulation rules remain separate from rendering. The project uses direct functions and contiguous data, with real application checks.

Normal faction choices use `virus_large_level.json`, `immune_large_level.json`, and `fungus_large_level.json`. Home cells form an equilateral triangle with 64-unit sides. All 15 cells use a triangular grid with 16 units between neighboring centers. Each side has the same cell sizes and spacing. Older two-side levels remain diagnostic fixtures. Every intact, mature factory head produces units, including heads below forks. Stems extend through the cell wall. All factions grow factory trunks roughly from the nucleus toward each organelle center. Fungal factories use short trunks with several short limbs that fan out from each trunk.

The primary stress scene is `assets/levels/three_faction_stress.json`: five fully developed cells and 60 seconds of actual production per faction, with all armies and reinforcements ordered to the center. Preparation runs 3,600 normal production steps with full initial nutrient storage and no combat or unit expiry. The produced armies assemble at their front cells. Counts follow faction production; unit storage holds 4,096. Run it with `python3 scripts/proton_run.py build/windows-cross/nanova.exe --world assets/levels/three_faction_stress.json`.

C++ and Vulkan target Windows. Linux builds use MinGW, `glslc`, and an initialized Steam Proton 11 prefix:

```sh
bash scripts/check.sh
python3 scripts/proton_run.py build/windows-cross/nanova.exe
```