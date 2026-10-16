# Gameplay and presentation

All five playable levels start with neutral cells and a force for each side. Starting-cell identity is separate from ownership. Craft orbit until ordered to establish a site. The enemy uses its expansion delay before its first settlement. The automatic fungal trial has a founder craft. A side with craft can remain in play before it has a factory. An enemy that has not yet established a site does not cause an early victory.

Fighters prefer units. Bombers prefer structures. Their fallback attack uses one eighth of the primary rate. Either mobile role can establish a site after defenders are gone. One craft approaches the landing point for each side and cell; the others keep clear. Destroy all established enemy factories to win. Losing all factories and craft causes defeat.

Immune fighters have 216 health and take fifteen hits. Immune bombers have 360 health and take fifteen hits. Virus fighters and bombers retain three hits. Immune roots have 480 health and take twenty-four hits; immune child limbs have 300 health and take sixteen hits. Virus and fungal roots have 240 health and take twelve hits; their child limbs have 150 health and take eight hits.

Factory deployment takes 180 seconds. Structures emerge one at a time. Base manufacture takes twelve seconds before production modifiers. Base collection is 2.5 nutrients per second. Root growth takes fourteen seconds; child emergence intervals are thirty-two seconds, and damaged limbs wait forty-eight seconds before regrowth. Mature roots share the base rate. Child limbs add to that rate. A partly established cell can produce from mature tips.

Virus factories can have twenty-four structures, including independent organelle roots and two child levels on a large cell. Child contribution is 0.05 per mature limb. Immune and fungal factories retain sixteen structures and a 0.12 child contribution. Small, medium, and large cells have four, six, and ten organelle roots. Storage supports thirty-two structures per cell. The growth plane is authored to match the startup view. Stems extend mainly across that plane; moving the camera does not change geometry or gameplay.

Damage removes stem sections and can remove descendants of a damaged support. Other roots keep working. Geometry and BLAS stay fixed during growth; instance transforms expose the current sections. Unit manufacture occurs at mature tips.

Fog uses a severe current-frame spatial blur outside current sight. It also covers previously discovered cells after sight is lost. Neutral cell forms remain visible through the blur. Enemy units and factories still require current sight. Discovery persists for orders. Camera movement changes neither sight nor discovery. The HUD remains clear.

The appearance file defines a finite global scattering medium as well as the cell interiors. Three studio lights provide direct light and shadows. Ambient fill approximates indirect light. Clear membranes use straight continuation. There is no refraction or multiple-scattering transport. The fog and backdrop filters use full-resolution current-frame buffers. There is no temporal processing, denoising, upscaling, or authored image map.

Idle craft use per-unit task ranges. Direct orders take priority. Factory avoidance uses current stem and head bounds. The flight settings provide two seconds of lookahead and one world unit of extra clearance. Top speeds and acceleration are unchanged by these presentation changes.

Normal window starts choose a variation seed. Restart restores the same seed, neutral ownership, starting craft, and discovery. Asset edits require application restart.

Audio uses twelve generated PCM effects and a fixed sixteen-voice mixer. Off-screen enemy events do not produce sounds. `--smoke --ticks 1800 --audio-capture build/growth.wav` exports thirty seconds without a GPU or audio device. Live audio and native Windows require separate verification.

## Controls

| Input | Action |
| --- | --- |
| Left drag / wheel | Pan / zoom at the pointer |
| 1 / 2 / 3 | Select fighters / bombers / all units |
| Shift-click | Add or remove a unit from the selection |
| Right-click open space | Move selected craft and scout |
| Right-click a known cell | Attack, orbit, or establish a factory |
| Click your factory cell, then drag its slider | Set the fighter / bomber production ratio |
| F / S | Set production to fighters only / bombers only |
| Home | Focus the selected cell or starting cell |
| Space / R | Pause / restart |
| F1 / F2 | Show timing data / current render buffers |

Both sides start with craft at neutral cells. Select craft and right-click your starting cell to establish the first factory. One craft is consumed. The enemy establishes its first site automatically. The fungal trial uses an automatic founder.

The slider moves in one-percent steps. The left end makes fighters only; the right end makes bombers only. Each cell keeps its own setting. A unit already in production finishes before the new mix applies. The ratio controls future production, not surviving unit counts. Restart restores startup state. Automatic spore emitters have no ratio control.

A replay line has a tick, command, and argument. `0 ratio 75` requests 75 percent fighters and 25 percent bombers at the selected friendly cell. Use `0 cell 0` first to select cell zero. Replay orders use the production controls in the application. `--pointer-drag X1 Y1 X2 Y2` supplies one complete drag for application checks.
