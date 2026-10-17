# Structured assets

Edit the JSON files and restart the application. Asset edits need no C++ rebuild. A build copies changed files beside the executable. Runtime meshes remain binary `.fmesh` files. Blender source files remain in `models/source/`.

## File layout

| File | Purpose |
| --- | --- |
| `startup.json` | Select the initial level, render settings, shader, and sound bank |
| `gameplay/biology.json` | List the five content definition files |
| `gameplay/bodies.json` | Define body geometry and production-site capability |
| `gameplay/factions.json` | Define faction IDs, labels, colors, and production links |
| `gameplay/groups.json` | Define control groups and selection labels |
| `gameplay/units.json` | Define mobile units, models, movement, combat, costs, and actions |
| `gameplay/buildings.json` | Define factories, collection, health, size, and branch growth |
| `gameplay/rules.json` | Define economy, AI, scouting, flight, orbit, ammunition, and recovery |
| `levels/biology-virus.json` | Set the match sides and starting body and unit placements |
| `levels/biology-immune.json` | Select immune control of the same starting map |
| `levels/biology-fungus.json` | Select the automatic fungal spore trial |
| `gameplay/faction-trials.json` | Select experimental faction production links with shared body, unit, group, and building definitions |
| `models/*.parts.json` | Define mesh part ranges, pivots, and factory attachment points |
| `audio/biology.json` | Map gameplay events to WAV files, gains, and cooldowns |
| `audio/synthesis.json` | Set the parameters used by `scripts/make_sfx.py` |
| `presentation/biology.json` | Set ambient light, studio lights, membranes, and backdrop |
| `presentation/combat.json` | Set projectile appearance, damage colors, and spike geometry |
| `rendering/biology.json` | Set the normal render budget, camera, geometry, and display settings |
| `rendering/reference.json` | Set the reference render budget; use requires GPU authorization |
| `schemas/*.schema.json` | Supply field names, types, and ranges for JSON editors |

The level contains explicit paths to content, rules, appearance, and effects. Content contains explicit paths to bodies, factions, groups, units, and buildings. Paths are relative to the file that contains them. They must not be absolute. There are no file searches, inherited settings, recursive includes, or merge rules. The application resolves `startup.json` from the executable directory, independent of the working directory.

The two-cell CPU fixtures use their own `biology-two-rules.json`. These rules retain the fixture's AI and attack delays. They do not change the normal game rules.

Playable levels declare `match.starts` as two objects with `faction` and `cell` fields. Cell is a zero-based placement index. The two factions and cells must be distinct. All cells start neutral, and each side needs a craft with `colonizes: true`. Starting-cell identity persists separately from current ownership. Existing-factory fixtures omit `match.starts` and use body ownership to define homes.

## Editing

JSON objects use named fields. Object field order does not affect loading. Arrays preserve placement order. Position and direction arrays use X, Y, Z order. Color arrays use linear R, G, B order. Action flags use `true` or `false`. Unit targets use names:

```json
{
  "targets": ["units"],
  "colonizes": true,
  "detaches": true,
  "trails": true
}
```

Use `[]` for no attack targets, `["structures"]` for structures, or `["units", "structures"]` for both. Do not use numeric masks.

Each body placement contains its own `site` and `medium` objects. They do not reference a separate body index:

```json
{
  "type": "unclaimed",
  "owner": "neutral",
  "position": [-30, 0, 0],
  "radius": 2.1,
  "tint": [1, 1, 1],
  "site": {
    "availability": 0.75,
    "normal": [0, 0.29, 0.957]
  },
  "medium": {
    "radius_fraction": 0.94,
    "edge_fade": 0.28,
    "scattering": [0.035, 0.045, 0.04],
    "absorption": 0.012,
    "anisotropy": 0.2
  }
}
```

Body types use `geometry: "mesh"` with a `model` path, or `geometry: "sphere"` without a model path. Only types with `hosts_site: true` can contain a production site. Gameplay hosts require a site normal and resource availability. Decorative bodies need neither. Spawns reference unit names and contain a position.

Faction IDs are in `[1,8]`. ID 0 means neutral. Group IDs are in `[0,7]`. Names have no compiled faction or unit meaning. Production links must refer to a building and units of the same faction. A product needs no starting placement. Hit counts are integers in `[1,32]`. Default virus units use three hits, immune units use fifteen, and fungal spores use two. At most two embedded spikes appear as geometry.

Building `establishment_seconds` sets the time from colonization to full factory growth without damage. The default is 180 seconds. Each structure gets an equal part of that time and starts in index order. A child still requires a mature support. Only emerged structures have health or contribute production. Playable levels start without factories. Existing-factory test fixtures can start with mature roots. Damage regrowth uses `growth_seconds` and the branch delays. If `establishment_seconds` is absent, roots start together and children use `branches.seconds`.

Building `branches` contains `seconds`, `count`, `spacing`, `production`, `seed`, `normal`, `health`, `hits`, and `regrow_seconds`. Optional `roots` selects `single` or `organelles`. `organelles` requires staged growth meshes and enough structure slots for every active attachment. Count is in `[1,128]`. Optional `grammar` selects `axial`, `fan`, `hypha`, or `tree`. Axial growth builds successive child levels from the roots. Fan growth places paired children on roots. Hypha growth makes chains with three limbs. Each rule also controls stem bending. Defaults for older authored files are `single` and `axial`.

The `tree` grammar requires a `tree` object with `children`, `levels`, `length_scale`, `width_scale`, `angle_degrees`, `bend`, and `root_share`. Each root uses the same repeated branching rule. The structure capacity must hold every level for every active root; startup rejects a smaller capacity. Only terminal leaves produce units. Initial tree deployment follows parent-first depth order. `root_share` sets the fraction of deployment time used by the trunks.

Faction `cell_appearance` can contain `roughness` and a `displacement` object. Displacement sets `features`, `height` as a fraction of cell radius, angular `width_degrees`, `profile` (`pointed` or `rounded`), and `seed`. Startup evaluates this scalar height field at existing wall vertices and stores faction blend-shape deltas. Ownership weights blend from the neutral positions. Duplicate positions share a normal group. Only the thin wall moves; internal organelles stay fixed. Current wall positions update normals, mesh bounds, and the ray-tracing BLAS. Triangle indices and all allocations remain fixed. `scripts/make_colony_forms.py` now exports only the factory leaf and its editable source scene. No image maps are used.

Optional building `growth_plane` has a nonzero `normal` and a `depth_ratio` in `[0,1]`. It controls stem depth relative to the authored plane. It does not use live camera input. The normal game aligns this plane with the startup view.

Optional appearance `atmosphere` defines `center`, `radius`, `edge_fade`, RGB `scattering`, scalar `absorption`, and `anisotropy`. It adds a finite world medium to the cell media. Optional appearance `fog` defines `blur_radius` in native pixels, `sight_feather` in world units, and map `plane_z`. Fog uses the current sight union and two full-resolution spatial passes. Unknown cell forms can supply the blurred image. Their presence does not reveal them to orders. Units and structures outside sight remain absent from render input.

Roots share one base production rate. Each live root contributes `visible_growth² / root_count`. Child limbs contribute `production × visible_growth²`. Collection and manufacture use the sum. The normal manufacture time is twelve seconds. Roots use building `health` and `hits`; child limbs use branch `health` and `hits`. Damage removes stem sections and heads before the full limb fails. A lost support also removes its descendants. Other roots remain active. The cell loses ownership only when no structure remains. `growth_meshes.segment` and `growth_meshes.tip` select the faction meshes. `presentation/combat.json` defines debris lifetime, speed, drag, spin, and shrink time.

Body `variation` references a mesh-parts file and sets `shape` and `interior` variation. Part ranges must cover every vertex in order. Rigid part transforms keep each organelle and its detail together. The same seeded transform moves its attachment point. Optional part `minimum_scale` omits that part and its attachment when the body radius is smaller than the threshold. Mesh triangles are removed at startup. The authored profile keeps the nucleus, one mitochondrion, and two vesicles in small cells. More mitochondria and vesicles become active at radii 2.5 and 3.4. Each placement can set `seed`; `--world-seed` changes the whole layout. Variation is generated and validated at startup. Growth and debris change preallocated instance poses. Cell-wall blend shapes also update existing vertex positions and normals during play.

Unit `autonomy` defines `unit_radius`, `structure_radius`, `colonize_radius`, `leash_radius`, `response_seconds`, and `cruise_fraction`. The first three fields set personal task ranges in world units. Zero disables acquisition of that task. `leash_radius` adds a pursuit margin outside the assigned cell radius. `response_seconds` sets the task decision interval; a generation-based phase staggers decisions. It does not change weapon cooldowns. `cruise_fraction` scales speed during patrol and launch. Weapon `range` remains separate. Existing content without `autonomy` uses weapon range for local attacks, no automatic settlement, and full cruise speed.

Idle tasks prefer the authored primary attack category, then the secondary category, then settlement. A valid target remains selected until it dies, changes generation, or leaves the personal range plus a 35 percent retention margin or the pursuit area. Direct orders take priority. Friendly cell orders resume patrol. After a move ends, the craft uses the nearest known cell as its local assignment. Settlement still consumes one unit and requires no defenders. A player craft cannot use this task to expose an unknown cell.

`rules.flight` can add `lookahead_seconds` and `obstacle_clearance`. With lookahead enabled, building `navigation.stem_radius` and `navigation.head_radius` define local collision bounds. The simulation scales these bounds with the current stem and head transforms. It rebuilds a fixed capsule array each movement phase. Predictive steering uses these capsules and current velocities. A final contact check prevents penetration. Missing lookahead disables this factory navigation path for older fixtures.

`rules.orbit` can add `weave_radius` and `weave_height`. These vary patrol radius and height in world units. Individual phase rates and directions are deterministic. Factory damage and growth update the navigation shapes on the next tick. No renderer object or render-quality setting affects navigation. `--state` exports each craft's task, attention handle, command status and current flight goal. Its `flight` object exports obstacle bounds and avoidance/contact counters.

Unit `attack.mode` selects `projectile`, `digest`, or `spore`. Spore mode requires `seek_radius`, `blast_radius`, `lifetime`, and `friendly_fire`. `secondary_attack` names distinct fallback targets and an `interval_factor`. Faction production mode `spores` requires the same non-colonizing spore product in both production links. It supports organelle roots and staged growth. These emitters do not consume nutrients or accept production orders. Faction `cell_appearance` sets a tint, maximum strength, and transformation time.

Sound banks require all named events. Each clip is canonical mono 48 kHz 16-bit PCM, with a maximum duration of two seconds. Clips load before ticks; the mixer uses fixed voice and output buffers. `scripts/make_sfx.py` regenerates the WAV files from their synthesis settings. `--audio PATH.json` selects another sound bank. `--audio-capture PATH.wav` requires `--smoke` and between 1 and 3600 ticks.

`rules.json` contains simulation parameters. `presentation/combat.json` contains visual parameters. Changing light or render settings must not change gameplay outcomes. The current level setup places bodies and units; it does not define a separate event scripting language. Replay `.orders` files feed the existing application controls.

Render files contain `output`, `sampling`, `volume`, `geometry`, `camera`, `lighting`, and `bloom` objects. All render fields are required. The normal budget remains native 1920×1080, six fixed film samples, one volume film ray, and one background query. Each logical frame uses only current inputs and fixed spatial samples. Zero volume rays remove in-scattering but retain attenuation. Remove body media and the global atmosphere to remove attenuation too.

JSON does not allow comments or trailing commas. Field descriptions are in this document and the local schemas. Each runtime JSON document has a `$schema` link for editor validation. The application does not fetch schemas or use the network.

## Validation

The application checks syntax, duplicate keys, unknown fields, types, ranges, names, links, and storage limits before simulation starts. It rejects missing files and invalid meshes. It does not substitute a compiled world. Syntax and field errors report the file and source location. Link and whole-world errors report the affected asset file.

Each JSON file is limited to 1 MiB and 32 nested containers. An object can contain at most 64 fields. Text must be valid UTF-8 with no NUL bytes. Numbers must be finite and fit their field ranges. Names and labels fit 31 UTF-8 bytes. Paths fit 255 bytes, including the resolved directory. Colors are in `[0,1]` unless the field specifies radiance or scattering.

Storage holds 32 body types, 32 unit types, 8 building types, 16 placed bodies, and 64 starting unit placements. These are storage capacities. They do not set gameplay production limits. A level requires two distinct non-neutral match factions, ambient light, and at least one body or spawn. Gameplay also requires home bodies for both sides, production links, complete combat settings, and storage that covers every unit cost.

Schemas help with local field checks. The application also checks conditions that span files: reference resolution, faction ownership, production links, home bodies, complete combat settings, nonzero normals, and camera geometry.

Run the complete CPU application checks:

```sh
bash scripts/check.sh --cpu
```

Use `--smoke --world PATH.json` to check a level through application startup, simulation, render-scene construction, and shutdown without starting the GPU. `--state PATH.json` exports simulation state. `--scene-state PATH.json` exports render input. These are diagnostic exports, not saved games.

## Meshes and rendering

Biological meshes carry vertex color, surface finish, and subsurface scattering data. See [MATERIALS.md](MATERIALS.md) for authoring, render controls, and the mesh format.

Presentation builds a CPU `RenderScene` with mesh ranges, affine instances, tints, lights, bounded media, and effects. The Vulkan renderer consumes this data. It has no world, faction, unit-type, or building dependencies. Transforms, visibility, media, and effects describe the current scene.

The soft backdrop uses current-frame visibility queries and two native-resolution spatial blur passes. It supplies no bounced light. Interior media use single scattering. Light seen through a wall receives the current-frame wall filter. GPU runs require a user request under the project rules. CPU checks do not establish GPU appearance, validation, timing, or native Windows behavior.

## Direct ray tracing

The active shader is `shaders/ray_trace.comp.spv`. The renderer traces visible surfaces, evaluates the three directional studio lights, and traces their shadow rays. Emission and authored ambient fill are added at each visible surface. Ambient fill is not occluded and is not traced indirect lighting. Ambient specular response uses the authored environment gradient; it does not reflect scene geometry. There are no diffuse bounces, sampled glossy paths, stochastic light choices, or denoising passes.

Clear membrane transmission uses a weighted straight continuation. Reflected and diffuse transmitted light are approximated with direct lighting and ambient fill. There is no refraction or multiple-scattering SSS. `sampling.max_transmission_layers` bounds transparent crossings independently of lighting. The limit permits an opaque hit after the last allowed crossing. If another membrane exhausts the limit, remaining transmission contributes zero; the renderer does not substitute the backdrop.

`sampling.samples_per_pixel` controls fixed film points for spatial anti-aliasing. Mirrored pairs keep their centroid at the pixel center. An odd count includes a center sample. `sampling.seed` changes the fixed film pattern. It does not change lighting choices or simulation. The pattern is the same in every pixel and frame. Motion can still cause geometric aliasing.

`volume.rays_per_pixel` controls film rays for scattering. `volume.integration_steps` controls midpoint quadrature within each intersected medium. Every quadrature point evaluates all active studio lights with current geometry shadows. Ambient scattering uses an analytic, unoccluded gradient approximation. Integration has discretization error rather than Monte Carlo noise. Surface attenuation uses the same density integration step count. Zero volume rays disable in-scattering but retain attenuation.

The default is native 1920×1080, six surface film points, one volume film ray, eight integration steps, eight transparent crossings, and one backdrop query. This is not eight total hardware rays: shadow queries and membrane crossings add work. The reference config uses 256 film points and 32 integration steps for spatial and quadrature comparisons of this direct model. It is not a reference for full light transport, and GPU use still requires authorization.

F2 shows beauty, linear HDR, direct/emission, ambient fill, vertex color, normal, depth, volume, and query counts. Effects use current first-hit guides. No previous frame contributes lighting or filtering data. The normal config has bloom disabled; that path performs display conversion without running the blur passes.

The Windows executable and shader variants compile. The bounded GPU check passed through Proton on October 16 with Khronos host validation active. Three native 1080p images are saved as screenshots 000240–000242. The frozen one-frame and two-frame virus outputs are identical. See `PROFILING.md` for observed frame times and limits. Native Windows and wider motion checks remain unverified.
