# neonspacewar: Melee

Melee is neonspacewar's default mode: self-playing battles between original
alien races, each with its own ship, weapon and special ability, around a
planet whose gravity bends every shot. `mode=classic` keeps the original
rock field. Melee is not in `ncz_ship_bins`; it ships only when the operator
approves it.

## What is on screen

- **Seven races.** Every race has an authored hull, an emblem, a flight model
  (mass, thrust, turn, drag, top speed), hull and energy, a primary weapon, a
  special and a pilot tactic. The names in the source are internal
  identifiers and are never drawn.

  | hull | weapon | special | tactic |
  |---|---|---|---|
  | needle with swept blades | three-bolt spread | blink beside the target | kite |
  | hexagonal armour block | slow heavy plasma | shield | close in |
  | ring with three fins | homing darts | mines | orbit |
  | segmented insect | chain lightning | drones | close in |
  | crescent | boomerang fired astern | cloak | flee and fire back |
  | scrap wedge with a toothed ram | flak | tractor beam, double ram damage | ram |
  | long lance with a ring | piercing lance | repulse (turns shots back) | snipe |

- **Battles.** 1 v 1 about two times in three, otherwise 2 v 2 or a
  three-way; never the same set of races twice in a row. A battle opens on a
  title card of the sides' emblems (geometry only, no text), the ships arrive
  from hyperspace, fight, and the survivor holds the frame over the wreck
  before the next battle cuts in.
- **Arena.** A disc with a soft boundary and the planet at its centre.
  Touching the planet hurts. After 12 s without a hit the boundary starts to
  close and the pilots press in, so a stalemate always ends. Three or four
  ships start in a tighter arena. Past a distance of 1.1 every tactic closes
  in, so no duel is fought across the whole arena.
- **Damage you can read.** The hull lattice fades as the hull drops from 60%
  to 30%; below 30, 20 and 10% a piece breaks away and tumbles off, the
  outline flickers between full and 55% brightness (never off, at most five
  changes a second) and the hull trails sparks. A shield is a second
  outline around the hull that cracks with each hit. A cloak leaves a faint
  rippling ghost. A kill disassembles the hull into its own segments.

## Camera

`src/nsw_melee_camera.c` is pure C, so the tests fly it over replays.

- It fits a box around the ships still in the fight, where each is going in
  the next half second and any shot about to land, plus the planet only
  when a ship is near it; 15% margin.
- No hull is ever shorter than 6% of the frame height. The closest zoom is
  the `zoom` option: the largest share of the frame height one hull may
  fill (default 0.22).
- Zoom-out is quick, quicker when something is at the edge. Zoom-in is slow
  and eased, needs to gain 10% before the camera commits to it, and every
  frame's change is rate-limited, so the camera never pumps.
- A spread too wide for the 6% floor is rolled onto the long side of the
  frame. Three seconds of close combat turn into a slow orbit.
- A kill freezes the battle for 250 ms, the camera punches in (up to 1.3x,
  never far enough to lose a live ship), the battle runs at 0.25x for 1.5 s,
  then the camera drifts back. The only shake is on a kill, 2 px at 1080p.
- `camera=fixed` shows the whole arena with no zoom or roll.

## Rendering

Everything is a line segment drawn twice: sharp, as an analytic capsule at
the output resolution, and as a soft glow into a half-size buffer (quarter
size on weak GPUs) added underneath. The glow never touches edges, so hulls
stay crisp on any GPU and outlines carry the silhouette without the glow.
Widths are in 1080-line units scaled by `min(width, height) / 1080`, so a
4K screen and a close zoom show more detail rather than thicker or softer
lines; the hull lattice and engine detail only appear once a hull is large
enough on screen. Melee opts out of the harness's weak-GPU half-resolution
default (`ncz_hack_native_scale`) because it scales only its glow.

## Determinism

The simulation steps in fixed 1/120 s substeps however the caller slices
time. Draws that can change a battle use one random stream, cosmetic draws
(particles, wreckage, which lattice piece breaks) another, so the quality
level never changes an outcome. Each decided battle records a state hash
that the log prints (`state xxxxxxxx`). Moving things are drawn up to one
substep behind the stepped state, so motion is smooth at any refresh rate.
A seed (`seed=N`, or `NCZ_NEONSPACEWAR_SEED`) reproduces a run: the live
seed-5 run and the fixed-rate clip capture of it logged the same hash for
battle 1 (`78ae978f`).

## Tests

`meson test neonspacewar-melee-battles` (`tests/test_nsw_melee.c`):

1. Every ordered race pair, 24 times (1008 duels): all resolve, no NaN,
   every race wins 20-80% overall, mean length 30-80 s.
2. 1000 director battles: all resolve, no back-to-back repeat.
3. 200 seeds replayed at a steady 60 Hz at full quality and at jittery
   4-50 ms frame times at low quality: the first two battles' hashes match.
4. The camera over those replays at 1920x1080: no hull under 6%, zoom steps
   at most 0.02 per frame, a ship out of frame in under 0.5% of fight frames
   and never for more than 1.5 s.

`meson test neonspacewar-schema-in-sync` keeps
`assets/screensaver-chooser/options/neonspacewar.tsv` in step with the
options in `src/gles3_neonspacewar.c`.

## Measured (2026-09-29)

- Duels: mean 51.9 s, 35 of 1008 under 20 s, 139 decided on time (the side
  with more hull left wins), the boundary closed in 120.
- Overall win rates 0.33-0.70; the matchups are strongly rock-paper-scissors
  (several pairs are 0.9+ one way), which suits a spectator but will be
  reviewed with the balance metrics.
- Camera: 1.21 M fight frames, smallest hull exactly the 6% floor, largest
  zoom step 0.020 per frame, ships out of frame 0.04% of frames, longest 68
  frames (1.1 s), all at the floor with ships on opposite rims.
- Frame rate: the first playable build ran at 60 fps (p95 16.5 ms) on an
  AMD integrated GPU at 1920x1080. This build has not yet been timed on an
  idle host; Mali, UHD 630 (weak tier) and 4K measurements are still to
  come.

## Still to do

- Special tells (a short wind-up glyph), pilot skill spread and per-race
  desperation behaviour, projectile families and a counter table, the
  variant layer, balance metrics beyond win rate, the weak-tier line caps,
  and visual gates (silhouette with glow off, background luminance, kill
  flash).
- Options and presets for every variation, with a randomize mode.
- The 3D chase camera and the bridge cutaway come after that.
