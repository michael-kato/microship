# Gameplay and presentation

The launch menu offers Virus, Immune, and Fungal. Click a card or press its number to start. Escape closes the menu. All five playable levels start with neutral cells and a force for each side. Starting-cell identity is separate from ownership. One fighter starts the approach to establish your site; the remaining craft orbit. The enemy uses its expansion delay before its first settlement. The fungal trial has a founder craft. A side with craft can remain in play before it has a factory. An enemy that has not yet established a site does not cause an early victory.

Fighters prefer units. Bombers prefer structures. Their fallback attack uses one eighth of the primary rate. Either mobile role can establish a site after defenders are gone. One craft approaches the landing point for each side and cell; the others keep clear. Destroy all established enemy factories to win. The HUD shows YOU WON. You can keep playing with orders, colonization, combat, and production. The win stays recorded until restart. Losing all factories and craft causes defeat.

Virus fighters carry eleven detachable spikes. Each spike regrows in three seconds. Virus bombers launch their six visible payload sacs in sequence. Each sac regrows over eight seconds. An empty bomber waits for a sac to finish. Bombs keep their shape and size in flight and do not leave embedded spike shafts. These weapons use the existing projectile hit rules.

Immune fighters are white blood cells. A free cell shoots a blob (the 80-triangle `immune_blob` mesh) at an enemy unit up to 4.5 units away, beyond contact, every 3 seconds. The blob does no damage but incapacitates the unit for 1.5 seconds: it cannot move or fire. The cell then charges in and latches on to devour it with its digest attack. A cell whose held victim dies glows green for 8 seconds; friendly units within 4 units of it heal one wound every second, even in combat. `attack.blob` and `attack.aura` in `unit-types.json` set these values; the glow uses `heal_aura` in `combat-effects.json`. Immune fighters have 864 health and take twenty-four hits. Immune bombers have 1440 health and take twenty-four hits. Virus fighters have 72 health; bombers have 120. Both take four hits. Immune roots have 1440 health and take twenty-four hits; immune child limbs have 900 health and take sixteen hits. Virus roots have 480 health and take twelve hits; their child limbs have 300 health and take eight hits. Fungal roots have 240 health and take twelve hits; their child limbs have 50 health and take three hits.

Virus and Immune use per-unit manufacture times. These times apply before leaf
rate modifiers. Fungal production and damage settings retain their prior values.

| Unit | Speed | Cost | Manufacture seconds | Attack interval | Range |
| --- | ---: | ---: | ---: | ---: | ---: |
| Virus fighter | 3.2 | 6 | 6 | 0.6 | 4 |
| Virus bomber | 2.4 | 18 | 8 | 2 | 4.5 |
| Immune fighter | 2.6 | 22 | 8 | 0.6 | 0.65 |
| Immune bomber | 2 | 40 | 10 | 0.9 | 0.85 |
| Trial Virus fighter | 4.2 | 5 | 5 | 0.5 | 4 |
| Trial Virus bomber | 3.1 | 12 | 7 | 1.8 | 4.5 |

Projectile cooldown also uses the shared projectile interval. Digestion uses
the unit interval directly. Trial Virus fighters have 64 health and take three
hits; trial bombers have 100 health and take three hits. Both Virus rosters
use the shorter spike and payload regrowth times. The Fungal player level
uses the trial Virus roster as its opponent.

Health and hit counts serve different purposes. A projectile causes one
wound. Fungal contact, clouds, and haze accumulate damage; one wound requires
health divided by hit count. More health with the same hit count increases
resistance to fungal damage. It does not add resistance to projectile hits.

Fungal factories release individual spores with a 20-triangle mesh. A complete leaf
can release one spore every 0.5 seconds at the base rate, with no brood burst.
When more leaves are ready, they share the cell output budget described below.
This gives 50 percent more spores per second than the former 16-spore job
that took 12 seconds. Each spore costs 0.1 nutrients and moves at 5.04 world
units per second, 20 percent faster than before. A spore takes one hit, can
take orders, and can colonize. Contact destroys the spore and causes 3 damage.
Its stationary cloud has a 1.4-unit radius and causes 1 damage per second for
5 seconds. Both damage values are half their former values. Partial damage
builds up as exposure until it forms a whole hit. Set these values in
`unit-types.json`: `speed`, `cost`, `brood`, `manufacture_seconds`,
`attack.damage`, and `attack.cloud`. Omit the per-unit manufacture time to
use the global value.

Factory deployment takes about 514 seconds, at half the previous rate (`shared.establishment_seconds`). All factions start their factory roots in parallel: the first starts at colonization, and the others have independent random delays of 0–10 seconds (`shared.branches.root_offset_seconds`). During deployment each structure grows in about 8.57 seconds (`shared.sprout_seconds`). Each root's limbs then emerge one at a time, depth first, at even intervals so the last one matures as deployment ends. The first child of the first root stays a leaf, so production can start before the rest of the factory completes. Each completed leaf can grow one unit at a time. Base manufacture takes twelve seconds per leaf, except where a unit defines its own time. Leaves share the cell nutrient store. AI manufacture uses the full 1.0 rate. Base collection is 2.5 nutrients per second. Damage regrowth uses fourteen seconds of growth (`growth_seconds`) and thirty-two-second child emergence intervals. Destroyed limbs wait at least forty-eight seconds and require paid repair before regrowth. Mature roots share the base rate. Child limbs add to that rate. Leaves can start production while other factories are still growing. Virus production uses terminal leaves.

Each virus organelle root grows a curved trunk, two forked sub-trunks, and four smaller branches with terminal leaves. Factories root on the three largest organelles, so a virus cell has at most twenty-one structures; each fork has a 72 percent chance of a second branch. Child lengths and widths use a 0.62 scale at each level. Child production contribution is 0.05 per mature limb, so a full virus tree roughly matches an immune fan's nutrient income. Immune factories have nine structures and a 0.12 child contribution. Fungal factories have 21 structures; their total mature contribution retains the prior nine-structure budget. Storage supports 128 structures per cell.

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

Ownership changes the existing cell wall shape over 18 seconds. Tint uses a shared 24-second transition rate. These timings apply to all factions. Seeded pigment variation changes the wall brightness and shifts its hue between indigo and cyan. Virus control blends in a pointed radial displacement and a shinier green material. Fungal control blends in broad rounded displacement and a rougher brown material. Immune control makes the wall nearly spherical and tints it white. The base wall is blue; Virus tints it green and Fungus tints it brown. Vertex and triangle counts stay fixed. Normals and render bounds follow the current positions. No separate spines or lobes appear. Organelle geometry, collision rules, attachment points, and game outcomes do not change.

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
| Esc | Open or close the pause menu |
| F1 / F2 | Toggle the frame profiler / current render buffers |
| F5 | Toggle subsurface scattering (SSS) |
| F6 | Toggle volume scattering; keep absorption |
| F7 | Toggle cell depth and membrane blur |
| F8 | Toggle tilt shift |
| F9 | Toggle the fog-of-war blur and tint |
| F10 | Toggle surface self-glow and its halo |

Both sides start with craft at neutral cells. Select craft and right-click your starting cell to establish the first factory. One craft is consumed. The enemy establishes its first site automatically. The fungal trial uses an automatic founder.

A normal launch opens the faction menu. `--world PATH` selects a level directly. `--no-menu` starts the default level. `--menu` opens the menu for a diagnostic run. `--menu-select 1..3` uses the same faction choice without opening a menu window.

Press Esc during play to open the pause menu. The match clock stops and game sound is muted. Resume continues the same match. Main Menu ends the match and returns to faction selection. Quit to Desktop closes the program. Both exit actions ask before ending the match.

Settings has six pages. Display, Lighting, Volume, and Effects contain 40 graphics controls. Audio and Controls contain six sound and camera controls.

| Page | Controls |
| --- | --- |
| Display | Borderless fullscreen, window size, VSync, film samples, spatial anti-aliasing, exposure, field of view, frame statistics |
| Lighting | Transmission layers, subsurface scattering and samples, light wrap, transmission, optical thickness, energy compensation, lighting grid and caches |
| Volume | Scattering, rays, integration steps and method, spatial filter and stride, optical lookup, lighting grid |
| Effects | Cell blur, tilt shift, fog blur, glow, bloom radius and strength, bloom threshold, focus band and blur radius |
| Audio | Master volume and sound effects volume |
| Controls | Drag from a cell to another cell to send every unit stationed at the first cell (orbiting or defending it, not moving elsewhere). The drag replaces the selection with those units and orders them to the second cell. A drag that starts on a cell without stationed units, or ends on open space, box-selects as before.

Keyboard camera speed, mouse pan and zoom sensitivity, inverted wheel zoom, control reference |

Apply changes and saves the settings. Display changes apply immediately, with no countdown. Cancel discards changes made since the last Apply. Restore Defaults prepares the authored defaults; select Apply to use them. Invalid setting combinations show an error. Higher sample counts and larger grids increase GPU work and memory use. Borderless mode uses the primary monitor's native resolution. Window width and height apply in windowed mode.

Player settings are saved in `%LOCALAPPDATA%\Nanova\player_settings.json`. They remain in effect after a faction change or program restart. `--settings PATH.json` selects another player file. Bounded diagnostics use authored defaults unless this option is present.

The slider moves in one-percent steps. The left end makes fighters only; the right end makes bombers only. Each cell keeps its own setting. Units already in production finish before the new mix applies. The ratio controls future production, not surviving unit counts. Restart restores startup state. Fungal emitters produce spores only and have no ratio control.

A replay line has a tick, command, and argument. `0 ratio 75` requests 75 percent fighters and 25 percent bombers at the selected friendly cell. Use `0 cell 0` first to select cell zero. Replay orders use the production controls in the application. `--pointer-drag X1 Y1 X2 Y2` supplies one complete drag for application checks.

One starting fighter automatically approaches and colonizes your neutral home cell. WASD or arrow keys move the camera. Right-click sends an order. Drag with the middle button to pan the camera.

F2 shows twelve current-frame panels. SSS CONTRIBUTION shows wrapped and transmitted scattering light. SSS FRACTION OF DIRECT shows its share of direct light. CELL BLUR RADIUS / 32 shows the cell blur radius, from black at zero to white at thirty-two pixels. These diagnostics use a shared current-frame buffer. Normal rendering does not allocate separate SSS or blur images.

Immune craft accelerate toward an enemy inside 5.5 world units (often one their blob has already incapacitated), with 3.5 times their normal top speed and three times their flight acceleration. Contact attaches the immune craft and holds a mobile victim. A held victim cannot move or fire. Digestion damages the victim and heals one wound per successful bite. Three incoming hits break the attachment and prevent a new hold for 2.5 seconds. Killing the immune craft also releases its victim. A darker rear makes the forward direction easier to see.

Selected units show a faint line to their current movement or attack target. The line follows a visible, valid combat target. Attached units and held victims have no target line. Path lines stay visible for half a second, then fade over half a second, so they are gone one second after the order. Selection or a new order shows them again. Selection does not change an order. `target_line.hold_seconds`, `fade_seconds`, `width`, and `alpha` in the effects file control the line.

Keyboard camera speed is twenty-four world units per second at the starting zoom, set by `camera.keyboard_pan_speed`. Zooming out further raises it in proportion to camera distance, so the view crosses the screen at the same pace. Diagonal movement has the same speed as movement along one axis. Opposing directions cancel. WASD and arrow keys can be used together. Movement stops when the game loses focus. S moves the camera down. F and B replace the selection with your fighters or bombers in the current gameplay view. Craft behind the camera, outside the view, or behind the bottom HUD are excluded. These keys do not change production. The 1, 2, and 3 keys select across the map.

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

`factory_appearance.emission_color` sets the factory glow independently of its
surface pigment. The default `[1.0, 0.55, 0.25]` is warm amber in linear RGB.
Lower blue or raise red for a warmer glow. The existing self-glow controls set
brightness, and the Fresnel controls still set the dark rim. The color also
applies to detached factory parts. Omit `emission_color` to use the surface color
for emission again. Setting Fresnel strength to zero keeps the color override.

Virus factories use faceted phage heads, six pointed tail fibres, and
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
no geometry. `cell_infection.morph_seconds` in `gameplay-rules.json` sets the time to reach the faction shape for every faction.

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

Cell organs use a fixed arrangement for each world seed.
`shared.layout_rotation_degrees` in `cell-types.json` sets the range
of random arrangement rotation. The default is 180 degrees. Existing
per-organ position, size, and orientation variation still applies.
Factory roots use the same transform as the organs. Restart keeps the seed.

New colonies use an 80/20 fighter/bomber mix when both roles exist.
`economy.fighter_percent` in `gameplay-rules.json` sets this default.
Each cell retains its own slider value. Fungal cells produce spores only.

Factories use nutrients to repair damage before they produce more units.
Paid unit jobs pause during repair. Production resumes when repairs finish.
Each cell repairs up to 12 health per second, at 0.08 nutrients per health,
after a four-second damage delay. A destroyed limb must receive the full
replacement cost before it grows again. Its support must be complete first.
Initial construction does not require a repair payment. A fully lost colony
does not repair itself. Set `shared.repair.health_per_second`, `nutrients_per_health`,
and `delay_seconds` in `factory-types.json`. Omit `repair` for the legacy
regrowth rules.

Every active fungal cell has a protective haze. It reaches four world units
beyond the cell radius, causes 3 damage per second to enemy units, and heals
3 health per second on allied units. It heals partial damage and whole wounds;
it cannot restore dead units. Other cells block it. Overlapping fungal haze
does not multiply damage or healing. Losing the colony removes its haze.
Factory repair still requires nutrients. Set the `fungal_emitter.haze` values
in `factory-types.json`: `reach`, `damage_per_second`, and `heal_per_second`.
Set its visible color and strength under `cell_haze` in `combat-effects.json`.
The haze fades in over ten seconds after colonization (`cell_haze.fade_seconds`). It uses the existing cloud drawing pass and adds no volume rays.
Enemy haze is visible only when its cell is in current sight.

Unit births use a 16 ms noise click; attack hits use a 12 ms click. Their
playback gains are 0.14 and 0.13. Birth clicks have a 0.18-second cooldown;
hit clicks have a 0.1-second cooldown. Set gain and cooldown in
`assets/audio/audio.json`. Set duration and envelope in
`assets/audio/sound-synthesis.json`, then run `python3 scripts/make_sfx.py`.
Other sound clips keep their existing waveforms.

Shared settings have one authoring location:

| File | Shared values |
| --- | --- |
| `gameplay-rules.json`, `cell_infection` | Morph and tint timing for every faction. |
| `factory-types.json`, `shared` | Deployment, repair, collection, size, growth plane, root limits, and branch timing. |
| `cell-types.json`, `shared` | Wall noise and seeded organ variation. |
| `unit-types.json`, `shared` | Secondary attack interval factor. |

Faction entries retain their models, colors, wall displacement shape, roughness,
health, branch grammar, and production differences. Fungal root spacing remains
a faction trait. Factory roots keep their organ attachments, but their growth
directions spread across the common view plane. `shared.growth_plane.normal`
sets that plane; `depth_ratio` limits growth toward or away from the camera.
`spread_roots` separates the initial trunk directions. Trees keep their shape
after growth and regrowth. Moving the camera does not move their roots.

The October 20 faction buff was checked with the Windows application through
Proton, without rendering. Before the buff, Virus won 1 of 12 seeded matches
against Fungus. With the selected buff, Virus and Immune each won all 12
matches against Fungus. On five unused seeds, each won all 20 matches against
Fungus. Each comparison swapped both start cells and player/opponent roles.
Immune also won all 20 matches against Virus. These results show stronger
factions against the fungal AI; they do not establish equal faction strength.
The AI sends small groups, so these results do not establish balance for
player-controlled swarms. The balance report retains these dominance flags.
`tests/e2e/faction_balance.py --seed-start 101 --seeds 5 --minutes 10 --jobs 3`
selects the unused seed set; pass the built application path as its first argument.
A match that reaches the time limit remains undecided, even if one side leads.

A separate contact check placed sixty fungal spores next to three stationary
fighters in the application. Production and factory haze were disabled for
this comparison. Virus lost all three fighters with both the old and new
values, but fungal losses increased from 37 to 60. All three Immune fighters
survived both versions; their combined wounds decreased from 36 to 16 after
three seconds. This checks concentrated contact damage, not movement tactics
or equal-cost armies. A concentrated fungal attack can still kill Virus units.

After the fixture updates, 29 CPU application test groups passed. The existing
`world_files` failure remains: its empty building fixture omits the required
`shared` field. JSON formatting and the three changed asset schemas passed.
No GPU rendering, native Windows, or player session was tested for this buff.


Cells use a blue base membrane with procedural indigo and cyan variation.
Control tints the membrane white for Immune, green for Virus, and brown for
Fungus. The normal and trial faction assets use the same colors. The existing
24-second tint transition remains. Internal organelle colors remain distinct.

Immune factory stems and heads are 1.8 times as wide and deep. Their height
and triangle counts stay the same. `factories.immune.width_scale` in the mesh
source profile sets this shape. Navigation bounds include the wider meshes.

Fungal factories have 21 structures instead of 9. Their chains attach across
all available roots. `branches.spacing` is 1.8 and `spread_scale` is 1.6,
so the branches extend farther and turn farther from their parent direction.
Fungal heads use 0.55 of their previous size. `factories.fungal_spore.head_scale`
sets the head size in the mesh source profile. Run `scripts/make_biology.py`
with Blender to regenerate these meshes and the editable source scene.

A large fungal cell has six mature producing tips instead of four. A small
cell with two roots has seven instead of three. Ready tips share the rates
of four and three prior leaves, respectively. `branches.production_leaf_limits`
sets these budgets by root count. `establishing_leaves` holds the two-root
budget to two leaves during initial deployment. An omitted budget leaves
per-tip production unchanged. `production_reference_count: 9` retains the
prior mature nutrient income while distributing it across more limbs.
A small completion tolerance prevents float rounding from adding a tick to
normalized manufacture jobs.

Application comparisons from normal settlement measured 360 spores per minute
before and 361 after on a small cell, and 480 before and after on a large cell.
At ten minutes, cumulative production was 2400 before and 2397 after for the
small cell, and 3987 before and 3885 after for the large cell. New branch growth
changes early birth times; mature output is retained. Test spores expired after
0.2 seconds to keep storage from limiting the measurements. Production uses
the real application through Proton. `tests/e2e/fungal_sources.py` checks the
mature output, source distribution, nutrient income, and invalid budget data.

Smaller fungal child limbs have 50 health and take three hits. Roots retain
240 health and twelve hits. A full three-root factory retains 1620 total
health across its larger structure count. This prevents a threefold increase
in child-limb health from the added branches. The large-cell projected stem
area increased from 166 to 510 square world units in the application state.
The mean child opening angle increased from 19 to 27 degrees.

Thirty CPU application test groups passed for this update. The known
`world_files` empty-fixture failure was excluded. Four final focused
1920x1080 captures passed with the host Vulkan validation layer, one frame
per process. Their application frame times were 17.05, 19.04, 17.12, and
18.80 ms for neutral, Virus, Immune, and Fungus. These are single-frame
checks, not steady performance measurements. Native Windows remains untested.
The final three-seed AI trial retained 12/12 wins against Fungus for both
Virus and Immune; equal faction strength remains unverified.


Immune-controlled cell walls retain 20 percent of the original radial shape
variation. `cell_appearance.roundness: 0.8` blends each existing wall vertex
80 percent toward the nominal cell radius. A value of zero keeps the original
shape; one gives a sphere. This follows the existing ownership morph timing.
The normal and trial Immune factions use the same value. Vertex and triangle
counts stay fixed. Organelles, attachment points, factories, mobile units,
collision radii, and simulation results retain their previous behavior.

Seven CPU application test groups passed for roundness. Two native 1920x1080
single-frame captures passed with host Vulkan validation. In the fixed-seed
view, normalized wall radii changed from 0.847–1.096 to 0.969–1.019. Radial
standard deviation changed from 0.04753 to 0.00951. Simulation state matched
exactly with roundness disabled and enabled. The after capture is
`screenshots/000964_immune_roundness_after.png`. Native Windows remains untested.
