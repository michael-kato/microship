# Gameplay and presentation

The launch menu offers Virus, Immune, and Fungal. Click a card or press its number to start. Escape closes the menu. All five playable levels start with neutral cells and a force for each side. Starting-cell identity is separate from ownership. One fighter starts the approach to establish your site; the remaining craft orbit. The enemy uses its expansion delay before its first settlement. The fungal trial has a founder craft. A side with craft can remain in play before it has a factory. An enemy that has not yet established a site does not cause an early victory.

Fighters prefer units. Bombers prefer structures. Their fallback attack uses one eighth of the primary rate. Either mobile role can establish a site after defenders are gone. One craft approaches the landing point for each side and cell; the others keep clear. Destroy all established enemy factories to win. The HUD shows YOU WON. You can keep playing with orders, colonization, combat, and production. The win stays recorded until restart. Losing all factories and craft causes defeat.

Virus fighters carry eleven detachable spikes. Virus bombers launch their six visible payload sacs in sequence. Each sac regrows over twelve seconds. An empty bomber waits for a sac to finish. Bombs keep their shape and size in flight and do not leave embedded spike shafts. These weapons use the existing projectile hit rules.

Immune fighters are white blood cells. A free cell shoots a blob (the 80-triangle `immune_blob` mesh) at an enemy unit up to 3 units away, beyond contact, every 6 seconds. The blob does no damage but incapacitates the unit for 1.5 seconds: it cannot move or fire. The cell then charges in and latches on to devour it with its digest attack. A cell whose held victim dies glows green for 8 seconds; friendly units within 3 units of it heal one wound every 1.5 seconds, even in combat. `attack.blob` and `attack.aura` in `unit-types.json` set these values; the glow uses `heal_aura` in `combat-effects.json`. Immune fighters have 432 health and take twenty-four hits. Immune bombers have 720 health and take twenty-four hits. Virus fighters and bombers retain three hits. Immune roots have 480 health and take twenty-four hits; immune child limbs have 300 health and take sixteen hits. Virus and fungal roots have 240 health and take twelve hits; their child limbs have 150 health and take eight hits.

Fungal craft are a swarm of individual spores. Each manufacture job hatches a brood of sixteen spores (`brood`), using the 20-triangle `fungal_swarm_spore` mesh. A spore has one hit, takes orders like other craft, and can colonize a cell. Spores are melee: on contact a spore bursts and dies, doing 6 damage to what it touched. It leaves a toxic cloud with a 1.4-unit radius that does 2 damage per second to enemies inside for 5 seconds (`attack.damage` and `attack.cloud` in `unit-types.json`). Damage below one hit builds up on the target (`exposure`) until it adds up to a whole hit; a virus fighter needs about two bursts per hit, an immune fighter about three. Clouds stay where the spore burst and glow with the `spore_cloud` color in `combat-effects.json`. Spores may enter a cell they attack to reach its factory heads.

Factory deployment takes 180 seconds. Every factory root starts growing when the cell is colonized; roots after the first start up to two seconds later (`branches.root_offset_seconds`). During deployment each structure grows in three seconds (`sprout_seconds`). Each root's limbs then emerge one at a time, depth first, at even intervals so the last one matures as deployment ends. The first child of the first root is always a leaf, so a virus cell makes its first fighter about 20 seconds after colonization. Immune cells take about 24 seconds because a fighter costs 28 nutrients. Each completed leaf can grow one unit at a time. Base manufacture takes twelve seconds per leaf. Leaves share the cell nutrient store. AI manufacture uses the full 1.0 rate. Base collection is 2.5 nutrients per second. Damage regrowth uses fourteen seconds of growth (`growth_seconds`) and thirty-two-second child emergence intervals. Damaged limbs wait forty-eight seconds before regrowth. Mature roots share the base rate. Child limbs add to that rate. Leaves can start production while other factories are still growing. Virus production uses terminal leaves.

Each virus organelle root grows a curved trunk, two forked sub-trunks, and four smaller branches with terminal leaves. Factories root on the three largest organelles, so a virus cell has at most twenty-one structures; each fork has a 72 percent chance of a second branch. Child lengths and widths use a 0.62 scale at each level. Child production contribution is 0.05 per mature limb, so a full virus tree roughly matches an immune fan's nutrient income. Immune and fungal factories have nine structures and a 0.12 child contribution. Storage supports 128 structures per cell.

Factory roots use `branches.root_limit` and `root_minimum_radius` in
`assets/gameplay/factory-types.json`. The defaults select up to three of the
largest eligible organelles. Small cells have two eligible roots. The radius
threshold is 0.1 in local mesh units, before cell scale. Small vesicles do not
produce roots. Mesh-part `attachment_radius` records the size used for selection;
startup organelle size variation also changes that value.

Virus branch controls are under `branches.tree`. `branch_probability` sets the
chance of a second child; the first child is always present. The first child of
the first root stays a leaf for early production. `angle_jitter_degrees`,
`length_jitter`, and `width_jitter` vary split angle, length, and width. Current
settings are 16 degrees, 25 percent, and 15 percent. `bend` adds direction variation
along the four stem segments. The world seed and branch seed select the shape.
All factions choose a new shape when a damaged branch regrows. Only that branch
and its descendants change; other roots keep their shapes. Shapes stay fixed
after growth. The separate slow sway animation still moves the tree.

Virus trees grow side by side. New units grow at terminal leaves. Support damage removes descendants. The growth plane is authored to match the startup view. Moving the camera does not change geometry or gameplay.

Ownership changes the existing cell wall shape over 24 seconds. Its tint changes over 180 seconds. Seeded pigment variation changes the wall brightness and shifts its hue between pink and blue-green. Virus control blends in a pointed radial displacement and a shinier blue-green material. Fungal control blends in broad rounded displacement and a rougher brown material. Immune control returns the wall to its neutral shape and moves its material toward pink and white. Vertex and triangle counts stay fixed. Normals and render bounds follow the current positions. No separate spines or lobes appear. Organelle geometry, collision rules, attachment points, and game outcomes do not change.

Every intact factory branch ends in a faction leaf head. This includes trunks
that extend past their forks. The head follows the stem during sway. Tips exposed
by growth or damage do not require a leaf; cut tips stay bare until regrowth.

Damage removes stem sections and can remove descendants of a damaged support. Other roots keep working. Geometry and BLAS stay fixed during growth; instance transforms expose the current sections. Unit manufacture occurs at mature tips.

Fog uses a severe current-frame spatial blur and a dark red tint outside current sight. It also covers previously discovered cells after sight is lost. Neutral cell forms remain visible through the blur. Enemy units and factories still require current sight. Discovery persists for orders. Camera movement changes neither sight nor discovery. The HUD remains clear.

Cells keep one authored membrane wall. A Fresnel-style response makes the wall more opaque at oblique angles. `thin_surface.edge_fade` sets its strength and power. Visible membrane light, pigment, absorption, and straight transmission use the same fade. No extra wall geometry is added.

Edit `thin_surface.edge_fade` in `assets/presentation/scene.json`. Set `strength` to `1` to stop clear transmission at the grazing-angle limit. Lower `power` to widen the opaque rim; `0.5` gives a wider rim than `2`. Higher values make the rim narrower. At full strength the face-on wall stays clear. As clear transmission closes at the rim, surface lighting uses the opaque material response. The rim shows the lit vertex color with its noise and faction tint; opacity does not add black pigment. Membrane normals and wrapped scattering stay continuous at the view and light horizons. Adjoining triangles use the same cached light sample at each shared wall vertex. This avoids triangle-shaped patches under strong Fresnel without increasing mesh density or film sampling. With the fade disabled (`strength: 0`), `transmission * clarity` sets the baseline clear transmission. `subsurface.membrane_thickness` controls scattering and does not set wall opacity. Build after editing source assets, then restart the game to reload the bundled files.

Cell scattering is RGB 0.10/0.075/0.085, with absorption 0.018. Cell depth blur starts one third of the way along each camera ray through the cell and reaches its eighteen-pixel limit at two thirds. The front third, including its opaque internals, stays sharp. The same depth rule applies to light and objects behind the wall. `medium.depth_blur.start_fraction`, `full_fraction`, and `max_radius` set this profile. The filter uses current-frame radiance only. The global miniature focus filter leaves cell-wall pixels to this depth filter.

A miniature focus band keeps the middle of the view sharp and softens its upper and lower areas. The render file's `tilt_shift` object sets the band center, half width, falloff, and maximum blur radius. The HUD is drawn after both filters. Maximum camera distance is 180, up from 38; the view can cover 4.7 times the previous map width. Organelle vertex colors distinguish the purple nucleus, orange mitochondria, blue reticulum, and varied vesicles. Faction color changes affect the membrane.

The appearance file defines a finite global scattering medium as well as the cell interiors. Three studio lights provide direct light and shadows. Gameplay geometry does not cast shadows on the backdrop or the global atmosphere. Cell interiors keep geometry shadows. A 32×32×32 visibility grid softens these shadows and reduces repeated shadow queries. The grid is rebuilt from current geometry for each frame. Set `lighting.shadow_grid_resolution` to zero to use direct queries for comparison. Ambient fill approximates indirect light. Clear membranes use straight continuation. There is no refraction or multiple-scattering transport. The fog and backdrop filters use full-resolution current-frame buffers. There is no temporal processing, denoising, upscaling, or authored image map.

All biological surfaces use subsurface scattering settings. Diffuse light becomes softer, and thin parts transmit colored light from behind. Thickness follows the current solid mesh. Cell walls use a separate wall thickness because they enclose fluid. The effect is an approximation of scattering. It keeps specular highlights, wall clarity, selection marks, and HUD text separate. It uses only the current frame and does not change game outcomes.

Idle craft use per-unit task ranges. Direct orders take priority. Factory avoidance uses current stem and head bounds. A digesting craft attacking a factory ignores every limb of that colony, so it can reach heads inside dense trees. The flight settings provide two seconds of lookahead and one world unit of extra clearance. Top speeds and acceleration are unchanged by these presentation changes.

Normal window starts choose a variation seed. Restart restores the same seed, neutral ownership, starting craft, and discovery. Asset edits require application restart.

Audio uses twelve generated PCM effects and a fixed sixteen-voice mixer. Off-screen enemy events do not produce sounds. `--smoke --ticks 1800 --audio-capture build/growth.wav` exports thirty seconds without a GPU or audio device. Live audio and native Windows require separate verification.

## Controls

| Input | Action |
| --- | --- |
| Left click / drag | Select one craft / select craft in a rectangle |
| Shift-drag | Add craft in the rectangle to the selection |
| WASD / arrow keys | Move the camera across the map |
| Wheel | Zoom at the pointer |
| Middle-button drag | Pan the camera |
| 1 / 2 / 3 | Select fighters / bombers / all units |
| Shift-click | Add or remove a unit from the selection |
| Right-click open space | Move selected craft and scout |
| Right-click a known cell | Attack, orbit, or establish a factory |
| Click your factory cell, then drag its slider | Set the fighter / bomber production ratio |
| F / B | Select your fighters / bombers on screen |
| Home | Focus the selected cell or starting cell |
| Space / R | Pause / restart |
| F1 / F2 | Toggle the frame profiler / current render buffers |
| F5 | Toggle subsurface scattering (SSS) |
| F6 | Toggle volume scattering; keep absorption |
| F7 | Toggle cell depth and membrane blur |
| F8 | Toggle tilt shift |
| F9 | Toggle the fog-of-war blur and tint |
| F10 | Toggle surface self-glow and its halo |

Both sides start with craft at neutral cells. Select craft and right-click your starting cell to establish the first factory. One craft is consumed. The enemy establishes its first site automatically. The fungal trial uses an automatic founder.

A normal launch opens the faction menu. `--world PATH` selects a level directly. `--no-menu` starts the default level. `--menu` opens the menu for a diagnostic run. `--menu-select 1..3` uses the same faction choice without opening a menu window.

The slider moves in one-percent steps. The left end makes fighters only; the right end makes bombers only. Each cell keeps its own setting. Units already in production finish before the new mix applies. The ratio controls future production, not surviving unit counts. Restart restores startup state. Automatic spore emitters have no ratio control.

A replay line has a tick, command, and argument. `0 ratio 75` requests 75 percent fighters and 25 percent bombers at the selected friendly cell. Use `0 cell 0` first to select cell zero. Replay orders use the production controls in the application. `--pointer-drag X1 Y1 X2 Y2` supplies one complete drag for application checks.

One starting fighter automatically approaches and colonizes your neutral home cell. WASD or arrow keys move the camera. Right-click sends an order. Drag with the middle button to pan the camera.

F2 shows twelve current-frame panels. SSS CONTRIBUTION shows wrapped and transmitted scattering light. SSS FRACTION OF DIRECT shows its share of direct light. CELL BLUR RADIUS / 32 shows the cell blur radius, from black at zero to white at thirty-two pixels. These diagnostics use a shared current-frame buffer. Normal rendering does not allocate separate SSS or blur images.

Immune craft accelerate toward an enemy inside 5.5 world units (often one their blob has already incapacitated), with 3.5 times their normal top speed and three times their flight acceleration. Contact attaches the immune craft and holds a mobile victim. A held victim cannot move or fire. Digestion damages the victim and heals one wound per successful bite. Three incoming hits break the attachment and prevent a new hold for 2.5 seconds. Killing the immune craft also releases its victim. A darker rear makes the forward direction easier to see.

Selected units show a faint line to their current movement or attack target. The line follows a visible, valid combat target. Attached units and held victims have no target line. Path lines stay visible for half a second, then fade over half a second, so they are gone one second after the order. Selection or a new order shows them again. Selection does not change an order. `target_line.hold_seconds`, `fade_seconds`, `width`, and `alpha` in the effects file control the line.

Keyboard camera speed is twenty-four world units per second, set by `camera.keyboard_pan_speed`. Diagonal movement has the same speed as movement along one axis. Opposing directions cancel. WASD and arrow keys can be used together. Movement stops when the game loses focus. S moves the camera down. F and B replace the selection with your fighters or bombers in the current gameplay view. Craft behind the camera, outside the view, or behind the bottom HUD are excluded. These keys do not change production. The 1, 2, and 3 keys select across the map.

F1 opens the frame profiler. CPU and GPU icicle graphs show a rolling mean of up to 120 completed frames. Child bars are below their parent. Both graphs use the same millisecond scale, but their clocks are not aligned. Hover over a bar to see its latest duration and mean. The top three GPU costs are shown below the graphs. The panel starts closed; `--stats` opens it at launch.

CPU spans cover input, simulation, scene and audio preparation, render command recording, GPU submission and wait, presentation wait, and capture. CPU wall time includes time blocked on the GPU. GPU spans cover acceleration updates, backdrop, shadow grid, surface lighting and SSS, volume scattering, effects, cell blur, fog, tilt shift, bloom and display, the HUD, and output copy. Surface lighting and SSS share one shader and one timing span. Timings include stage barriers. The header shows both latest and mean durations. The panel uses the previous completed frame; no extra GPU wait is added for profiling. Timing history contains numbers only, with no image or lighting history. Bounded `--frames N --stats` runs also print each span and its mean.


F5 through F10 change rendering during play. A second press restores the
values from the loaded assets. A held key changes its feature once.
These keys open the F1 profiler, which shows ON, OFF or N/A for each
feature. N/A means that the loaded scene or config does not provide that
feature. Each change clears the rolling timing averages. The next
completed frame starts the new average. F1 can hide the panel again.

F5 selects a surface shader with SSS compiled out when disabled. Both
shader choices are prepared at startup. F6 skips the volume scattering
pass but keeps medium attenuation on surface and light paths. F7 removes
cell depth blur and transmitted membrane blur. F8 removes the miniature
focus filter. F9 removes the fog blur and tint; enemy visibility and
exploration rules still apply. F2 keeps its existing diagnostic behavior:
it bypasses the tilt-shift and fog filters. Changes last for the current
application session and do not edit assets. R restarts the match with the
current render switches.

Self-glow strengths are in `assets/presentation/scene.json`, under `self_glow`.
Cell internals, organelles, factory stems, leaves, and units have separate controls.
These surfaces do not light nearby objects. The cell wall has no self-glow and
does not feed the halo. Virus shafts use their authored emission mask. Bloom
uses the current visible emissive strength,
a brightness threshold, and a quarter-resolution blur. Surfaces and display
output stay at native resolution. F10 switches self-glow and the halo together.

Factory emission uses an inverse Fresnel mask in `assets/presentation/scene.json`,
under `factory_appearance.emission_fresnel`. `strength` ranges from zero to one;
zero disables the mask, and one removes emission at the grazing-angle limit.
`power` sets the rim width. Lower values make the dark rim wider; higher values
make it narrower. The defaults are `strength: 1` and `power: 0.5`. Face-on emission
keeps its full strength. `self_glow.factory_stems` and `factory_leaves` still set
the emission level. Stems, leaf heads and their debris use this mask. Surface
pigment and reflected light remain visible at the rim. Build and restart after
editing source assets to load the new bundled values.

Virus factories use faceted phage heads, six pointed tail fibres, and tapered
continuous tapered shafts with irregular surfaces. Growth follows the forked
tree.
Factory stems and heads have zero SSS. Cells, organelles, and units retain SSS.
Factory shape controls are in `assets/models/source/cell-unit-factory-shape.json`,
under `factories.virus`. `shaft_radius` sets the shaft width; `shaft_sides` and
`stem_sections` set its mesh density; `surface_jitter` sets its surface variation.
`head_radius` and `head_height_scale` set the capsid size. `fibre_spread` and
`fibre_radius` set the tail fibres. Run the mesh authoring script after an edit.
These mesh controls do not change production or combat rules.

Virus wall mutation uses 256 pointed displacement features, each with a
6-degree angular width. Set these in `assets/gameplay/factions.json`, under
`cell_appearance.displacement`. Raise `features` for more peaks. Lower
`width_degrees` for narrower, steeper peaks. The supported limits are 256
features and a minimum width of 6 degrees for the current wall mesh. `height`
sets the maximum height as a fraction of cell radius. This moves the existing wall vertices; it adds
no geometry. `morph_seconds` sets the time to reach the faction shape.

Organelle color and glow controls are in `assets/presentation/scene.json`, under
`organelle_appearance`. Restart the application after you change the file.

| Control | Effect |
| --- | --- |
| `edge_saturation` | Color strength at the rim. One keeps the authored color; higher values increase saturation. |
| `edge_width` | Width of the saturated rim. Higher values make it wider. |
| `center_glow` | Multiplies `self_glow.organelles` at the center. Zero removes organelle emission. |
| `center_power` | Concentrates emission toward the center. Higher values make the bright area smaller. Zero gives uniform emission. |

The defaults are 1.8, 0.65, 4.5, and 2.5. Rim color and center glow depend on the
current viewing angle. The organelle keeps its pigment color. Its glow does not
cast light onto other surfaces. F10 removes emission and bloom; rim color remains.

Motion controls are in the same file, under `animation`.

| Object | Controls |
| --- | --- |
| Organelles | `squash` is the fractional stretch; `period_seconds` is one cycle. The default is 0.035 over 16 seconds. |
| Membrane | `ripple_strength` sets the normal change; `wavelength` is in world units; `period_seconds` sets the cycle. Defaults are 0.05, 1.6, and 20. |
| Factories | `sway_degrees` sets the maximum angle; `period_seconds` sets the cycle. Defaults are 1.8 degrees and 28 seconds. |

Set `squash`, `ripple_strength`, or `sway_degrees` to zero to stop that motion.
Organelle squash preserves volume and keeps factory attachment points fixed.
Factory trees sway about their roots, with connected joints, heads, and growing
units. The membrane ripple changes its shading normal. It does not move the
silhouette or add another wall. Faction mutation still moves the wall vertices.
All motion uses the current simulation time. Pause stops it.
