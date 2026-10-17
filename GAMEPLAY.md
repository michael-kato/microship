# Gameplay and presentation

The launch menu offers Virus, Immune, and Fungal. Click a card or press its number to start. Escape closes the menu. All five playable levels start with neutral cells and a force for each side. Starting-cell identity is separate from ownership. One fighter starts the approach to establish your site; the remaining craft orbit. The enemy uses its expansion delay before its first settlement. The automatic fungal trial has a founder craft. A side with craft can remain in play before it has a factory. An enemy that has not yet established a site does not cause an early victory.

Fighters prefer units. Bombers prefer structures. Their fallback attack uses one eighth of the primary rate. Either mobile role can establish a site after defenders are gone. One craft approaches the landing point for each side and cell; the others keep clear. Destroy all established enemy factories to win. The HUD shows YOU WON. You can keep playing with orders, colonization, combat, and production. The win stays recorded until restart. Losing all factories and craft causes defeat.

Virus fighters carry eleven detachable spikes. Virus bombers launch their six visible payload sacs in sequence. Each sac regrows over twelve seconds. An empty bomber waits for a sac to finish. Bombs keep their shape and size in flight and do not leave embedded spike shafts. These weapons use the existing projectile hit rules.

Immune fighters have 432 health and take twenty-four hits. Immune bombers have 720 health and take twenty-four hits. Virus fighters and bombers retain three hits. Immune roots have 480 health and take twenty-four hits; immune child limbs have 300 health and take sixteen hits. Virus and fungal roots have 240 health and take twelve hits; their child limbs have 150 health and take eight hits.

Factory deployment takes 180 seconds. Structures emerge one at a time. Each completed leaf can grow one unit at a time. Base manufacture takes twelve seconds per leaf. Leaves share the cell nutrient store. AI manufacture uses the full 1.0 rate. Base collection is 2.5 nutrients per second. Damage regrowth uses fourteen seconds of growth and thirty-two-second child emergence intervals. Damaged limbs wait forty-eight seconds before regrowth. Mature roots share the base rate. Child limbs add to that rate. Leaves can start production while other factories are still growing. Virus production uses terminal leaves.

Each virus organelle root grows a curved trunk, two forked sub-trunks, and four smaller branches with terminal leaves. A large cell has ten roots, seventy structures, and forty terminal leaves. Medium and small cells have forty-two and twenty-eight structures. Child lengths and widths use a 0.62 scale at each level. Child production contribution is 0.011 per mature limb. Immune and fungal factories retain sixteen structures and a 0.12 child contribution. Storage supports 128 structures per cell.

Virus trees deploy one complete tree at a time. Trunks use sixty percent of the full deployment time; smaller branches share the rest. New units grow at terminal leaves. Support damage removes descendants. The growth plane is authored to match the startup view. Moving the camera does not change geometry or gameplay.

Ownership changes the existing cell wall shape over 24 seconds. Its tint changes over 180 seconds. Seeded pigment variation changes the wall brightness and shifts its hue between pink and blue-green. Virus control blends in a pointed radial displacement and a shinier blue-green material. Fungal control blends in broad rounded displacement and a rougher brown material. Immune control returns the wall to its neutral shape and moves its material toward pink and white. Vertex and triangle counts stay fixed. Normals and render bounds follow the current positions. No separate spines or lobes appear. Organelle geometry, collision rules, attachment points, and game outcomes do not change.

Damage removes stem sections and can remove descendants of a damaged support. Other roots keep working. Geometry and BLAS stay fixed during growth; instance transforms expose the current sections. Unit manufacture occurs at mature tips.

Fog uses a severe current-frame spatial blur and a dark red tint outside current sight. It also covers previously discovered cells after sight is lost. Neutral cell forms remain visible through the blur. Enemy units and factories still require current sight. Discovery persists for orders. Camera movement changes neither sight nor discovery. The HUD remains clear.

Cells keep one authored membrane wall. An angle fade makes the wall more transparent at oblique angles. `thin_surface.edge_fade` sets its strength and power. Visible membrane light, pigment, absorption, and straight transmission use the same fade. No extra wall geometry is added.

Cell scattering is RGB 0.10/0.075/0.085, with absorption 0.018. Cell depth blur starts one third of the way along each camera ray through the cell and reaches its eighteen-pixel limit at two thirds. The front third, including its opaque internals, stays sharp. The same depth rule applies to light and objects behind the wall. `medium.depth_blur.start_fraction`, `full_fraction`, and `max_radius` set this profile. The filter uses current-frame radiance only. The global miniature focus filter leaves cell-wall pixels to this depth filter.

A miniature focus band keeps the middle of the view sharp and softens its upper and lower areas. The render file's `tilt_shift` object sets the band center, half width, falloff, and maximum blur radius. The HUD is drawn after both filters. Maximum camera distance is 180, up from 38; the view can cover 4.7 times the previous map width. Organelle vertex colors distinguish the purple nucleus, orange mitochondria, blue reticulum, and varied vesicles. Faction color changes affect the membrane.

The appearance file defines a finite global scattering medium as well as the cell interiors. Three studio lights provide direct light and shadows. Gameplay geometry does not cast shadows on the backdrop or the global atmosphere. Cell interiors keep geometry shadows. A 32×32×32 visibility grid softens these shadows and reduces repeated shadow queries. The grid is rebuilt from current geometry for each frame. Set `lighting.shadow_grid_resolution` to zero to use direct queries for comparison. Ambient fill approximates indirect light. Clear membranes use straight continuation. There is no refraction or multiple-scattering transport. The fog and backdrop filters use full-resolution current-frame buffers. There is no temporal processing, denoising, upscaling, or authored image map.

All biological surfaces use subsurface scattering settings. Diffuse light becomes softer, and thin parts transmit colored light from behind. Thickness follows the current solid mesh. Cell walls use a separate wall thickness because they enclose fluid. The effect is an approximation of scattering. It keeps specular highlights, wall clarity, selection marks, and HUD text separate. It uses only the current frame and does not change game outcomes.

Idle craft use per-unit task ranges. Direct orders take priority. Factory avoidance uses current stem and head bounds. The flight settings provide two seconds of lookahead and one world unit of extra clearance. Top speeds and acceleration are unchanged by these presentation changes.

Normal window starts choose a variation seed. Restart restores the same seed, neutral ownership, starting craft, and discovery. Asset edits require application restart.

Audio uses twelve generated PCM effects and a fixed sixteen-voice mixer. Off-screen enemy events do not produce sounds. `--smoke --ticks 1800 --audio-capture build/growth.wav` exports thirty seconds without a GPU or audio device. Live audio and native Windows require separate verification.

## Controls

| Input | Action |
| --- | --- |
| Left click / drag | Select one craft / select craft in a rectangle |
| Shift-drag | Add craft in the rectangle to the selection |
| Middle drag / wheel | Pan / zoom at the pointer |
| 1 / 2 / 3 | Select fighters / bombers / all units |
| Shift-click | Add or remove a unit from the selection |
| Middle-click open space | Move selected craft and scout |
| Middle-click a known cell | Attack, orbit, or establish a factory |
| Click your factory cell, then drag its slider | Set the fighter / bomber production ratio |
| F / S | Set production to fighters only / bombers only |
| Home | Focus the selected cell or starting cell |
| Space / R | Pause / restart |
| F1 / F2 | Show timing data / current render buffers |

Both sides start with craft at neutral cells. Select craft and middle-click your starting cell to establish the first factory. One craft is consumed. The enemy establishes its first site automatically. The fungal trial uses an automatic founder.

A normal launch opens the faction menu. `--world PATH` selects a level directly. `--no-menu` starts the default level. `--menu` opens the menu for a diagnostic run. `--menu-select 1..3` uses the same faction choice without opening a menu window.

The slider moves in one-percent steps. The left end makes fighters only; the right end makes bombers only. Each cell keeps its own setting. Units already in production finish before the new mix applies. The ratio controls future production, not surviving unit counts. Restart restores startup state. Automatic spore emitters have no ratio control.

A replay line has a tick, command, and argument. `0 ratio 75` requests 75 percent fighters and 25 percent bombers at the selected friendly cell. Use `0 cell 0` first to select cell zero. Replay orders use the production controls in the application. `--pointer-drag X1 Y1 X2 Y2` supplies one complete drag for application checks.

One starting fighter automatically approaches and colonizes your neutral home cell. Right-drag pans the camera. Middle-click sends an order.

F2 shows twelve current-frame panels. SSS CONTRIBUTION shows wrapped and transmitted scattering light. SSS FRACTION OF DIRECT shows its share of direct light. CELL BLUR RADIUS / 32 shows the cell blur radius, from black at zero to white at thirty-two pixels. These diagnostics use a shared current-frame buffer. Normal rendering does not allocate separate SSS or blur images.

Immune craft accelerate toward an enemy inside 5.5 world units, with 3.5 times their normal top speed and three times their flight acceleration. Contact attaches the immune craft and holds a mobile victim. Digestion damages the victim and heals one wound per successful bite. Three incoming hits break the attachment and prevent a new hold for 2.5 seconds. Killing the immune craft also releases its victim. A darker rear makes the forward direction easier to see.
