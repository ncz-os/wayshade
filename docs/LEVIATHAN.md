# Leviathan

A chaptered flight to, across and into a vessel too large to take in, through a gate, into a quiet room, and out to the next vessel. It is an homage to the idea of a star gate. It does not copy any film's footage or vessel, and it has no text, logos or monolith.

- Binary: `xshadertoy_leviathan_gles3`, the same id as v1, now built by its own driver, `src/gles3_leviathan.c`, instead of the generic xshadertoy driver. The `xshadertoy_` prefix is kept on purpose: the launcher (`is_shader_hack`) and `tools/measure-tiers.py` give shader hacks the weak-class render scale of 0.5 and the common render options. A first cut named it `leviathan_gles3`, which would have run it at native resolution on a UHD 630 (16 fps on the hull pass).
- Shader: `vendor/xshadertoy/glsl/leviathan.glsl`. It keeps the Shadertoy `mainImage` convention, so the headless smoke tester still renders it with default options.
- Options: `src/leviathan_opts.h` (option group `xshadertoy_leviathan`, the launcher short name, so the key file is `~/.config/ncz-screensavers/xshadertoy_leviathan.conf`; env `NCZ_LEVIATHAN_*`). Schemas are `assets/screensaver-chooser/options/xshadertoy_leviathan.tsv` (canonical; the launcher looks schemas up by short name) and `xshadertoy_leviathan.json` (derived, via `tools/schema-tsv-to-json.py`). Presets are in `assets/screensaver-chooser/options/presets/xshadertoy_leviathan/` (where the launcher lists scenes).
- Tests: `leviathan-schema-in-sync`, `leviathan-schema-json-in-sync`, `leviathan-preset-precedence`, `leviathan-randomize`.
- Not in `ncz_ship_bins` or `hacks.tsv`: the operator decides.

## Why v2 (2026-09-29)

The operator's verdict on v1: "it might be leviathan but it is not convincing as being a flight through a gigantic spaceship / 2001 space odyssey ending". A Fable consult diagnosed the causes:

- Scale-free primitives: glowing sphere shells in fog read as rings.
- One octave of detail.
- Self-lit plasma instead of a lit hull.
- Fog everywhere.
- A camera that says "tube".
- No arrival and no singular landmark.
- Three unrelated looks interleaved on a 75-120 s cycle, when the piece needed an arc.

v2 follows that brief: a storyboard, a single-pass analytic technique plan, safety caps and an A/B. Where it departs from the brief, this document says so and why.

## The arc

One chapter lasts 390-420 arc seconds; the length comes from the seed, or the `chapter-length` option sets it.

| arc s | act | what is on screen |
|---|---|---|
| 0-40 | silhouette | Stars and a planet. The hull slides in from one side and eats the stars. The camera closes in on a Hermite path and turns less than 1 deg/s. |
| 40-150 | hull pass | A glide 12 units (~60 m) above the deck, down a runway lined with chase lights, toward a superstructure wall of window decks with a lit bay aperture. Blocks, trenches and towers pass with real parallax. A spire eclipses the sun for about 2 s at 95 s; the ship then enters the superstructure's shadow. Small craft with blinking running lights; one leads the camera into the bay. |
| 150-270 | interior | The aperture is crossed without a cut. Exposure adapts over 3 s. The hangar is 240 wide by 68 tall, with arches every 16 units (heavy every 4th, a spar every 12th), slotted light shafts with lit pools on the floor, floor machinery and stores with real relief, and dust at three depths that brightens inside the shafts. Speed ramps from 1x to 2.1x. |
| 250-276 | into the gate | The arches retract into the walls, lamps smear into lines along the travel axis, and a horizon slit forms ahead. The run into the gate accelerates to 12x (C1). |
| 270-314 | the gate | Two planes of streaks above and below a stable horizon slit, built in display space from capped OKLab lightness. |
| 306-331 | colour fields | The planes dissolve into drifting colour fields and soft panels. |
| 331-356 | coda | False-colour terrain (solarised lightness bands; mid and strong only; weak keeps the colour fields). |
| 360-end | quiet | A still, low-contrast chamber lit from the floor, turning slowly. It thins from the ceiling down back to the stars and ends at the orientation where the next chapter's silhouette begins, so the stars do not jump across the chapter boundary. |

Each chapter re-seeds everything except the star field: hull proportions, landmark (a ring the runway passes under, or three fins), window palette, planet, interior light, gate hue path, and the side the vessel appears on. The side is set by mirroring the ship frame; screen x is mirrored for the ship rays, and the world rays used for the stars are made consistent with it (see `actExterior`).

## Technique (single pass, analytic)

- **Hull.** About 12 boxes by slab test, plus a ring by ray-cylinder, a spire, and 4 craft by ray-ellipsoid.
- **Deck relief.** A 2D grid DDA over 4-unit column cells (fine), then over 24-unit modules (coarse), then a flat textured deck. The level is chosen by footprint. The coarse level keeps tall blocks from popping in at the horizon.
- **Hall floor.** The same DDA over 6-unit cells.
- **Arches.** Intersected analytically plane by plane (up to 14); no sphere tracing.
- **Shafts.** Closed-form ray-slab overlap length for the nearest 3-6 slots.
- **Dust.** Hashed sprites on planes 6 units apart.
- **Windows** (the main scale cue). Decks of small windows in runs, the odd run dark, and every fifth deck a bright promenade. Level of detail by footprint:
  1. single windows;
  2. each run as a line of light;
  3. only the promenades, as lines 6 units apart;
  4. the face mean, with a slow large-scale variation.
- **Lighting.** One hard sun key, a planet fill, sun glint on plates, and a lit rim on box edges that face the sun. The sun is occluded by the superstructure and the spire; the deck casts shadows by sampling heights toward the sun.
- **Hashes.** Integer PCG throughout, with no `sin()` of large arguments (the lesson of the ed58c2c `hash3` fix).
- **Cost.** Only the acts visible at the current time are evaluated. The act choice depends on time only, so the branches are uniform.

### Where this departs from the brief

- **Relief.** The brief planned 0 relief steps (weak) or 8-step relief mapping (strong) on a flat hull. v2 uses a real heightfield DDA in every tier (18/28/40 fine steps) because parallax of real blocks is the strongest scale cue. It is affordable: see the measurements.
- **Antialiasing.** The brief said "fwidth-based". `fwidth()` is undefined inside non-uniform control flow, and every surface here is shaded inside a per-pixel hit branch. So every pattern uses an analytic footprint instead, `t * pixel angle / |cos(incidence)|`, with the same smoothstep widths and fade-to-mean rules.
- **Interior shape.** The brief suggested a hexagonal prism tunnel. That was built first and read as concentric hexagons, the same "concentric rings" failure as v1. The hall is now much wider than tall, with an open floor and arches only over the walls and ceiling.

## Safety (in the maths)

- **Gate.** OKLab lightness 0.30-0.76 (scaled down by `gate-intensity`) in display space. Every channel is clamped to 0.85 while the gate family is on screen. The measured mean is about 0.36 (seed 0.137...).
- **Rates.** Hue drift is at most 0.03 Hz. Blinking lights run at 0.5-0.7 arc Hz, and `arc-scale` is clamped at 0.7 or more so the fastest stays at or below 1 Hz. Breathing lamps are at most 0.6 Hz arc (sinusoid). Nothing is above 3 Hz.
- **Camera.** No roll: the basis is always built on world up. Every turn is well under the 10 deg/s cap. The largest is the approach yaw (about 17 deg over 40 s); the rest are slow sways under 2 deg/s. Acceleration into the gate is C1 (integrated smoothstep).
- **Burn-in.** No static bright element: the slit drifts slowly, the sun moves with the camera, and the quiet room turns.
- **Banding.** Triangular dither of 1 LSB at output.
- **Randomize test.** `leviathan-randomize` (48 random seed and option draws at 7 arc times) checks for non-flat frames, a gate peak of at most 0.85 (+2 LSB), and a gate mean of at most 0.50. It was proved in both directions: a copy with the clamp removed and the lightness raised fails every draw (mean 0.54-0.60, peak 247-251), and the real shader passes all 48.

## Options, presets, randomization

These follow the operator rule for new savers: simulations with variable settings and randomization.

- **Options** (env `NCZ_LEVIATHAN_*`, `--option=value`, the key file, `--preset`): `hull`, `landmark`, `palette` (warm, cool, sodium, white, or harmonic: a colourway generated per chapter that the gate follows), `aperture-side`, `planet` (none included), `interior`, `chapter-length`, `arc-scale`, `gate-hue`, `gate-intensity`, `star-density`, `randomize`, `quality`, `seed`.
- **Automatic values.** Every enum defaults to `auto`, drawn per chapter from the run seed.
- **Seeds.** The seed is logged (`[leviathan] seed=N ...`) and kept in 1..999999, so any run can be replayed with `--seed=N` and the same preset and options.
- **Presets.** `odyssey`, `night-liner`, `slow-drift`.
- **Caps.** No option value can move the caps: they are applied after the options in the shader.

## A/B against v1

The contact sheet uses the same seed (0.137, 0.421, 0.731, 0.953) and time codes 5/20/45/90/150/240/300/360/400 s. Rows are v1, v2 rich, v2 weak; the sheet was built on build-host (see the round notes for its path).

- **v1:** the lavender ring lattice at every time code except 45 and 240 s (pink terrain).
- **v2:** t=5 a hull edge beside a planet; t=20 a long lit hull, a ring and the bay; t=45/90 the runway toward the superstructure; t=150/240 the hangar; t=300 the gate; t=360 the coda handing to the quiet room; t=400 the room thinning to stars.

## Measurements

Method: in-session runs through the harness `[stats]` line, with ncz-screensaver-idled stopped for the run and restored afterwards. Each run is 14 s with `--start-time` set to each of the 9 arc times 0, 45, 100, 140, 200, 262, 300, 345 and 380 s, seed 42. fps = steady frames / steady seconds (about 9 s); p95 = steady p95. Each tiers cell is the worst act: the lowest fps and the highest p95.

| host | GPU | setting | worst act | tiers cell |
|---|---|---|---|---|
| build-host | Intel UHD 630 (CML GT2), gpu-offload off | render scale 0.5 (weak default, quality weak) | every act at vsync | 60.0/17.0 |
| build-host | Intel UHD 630 | native 1080p (quality weak) | hull pass 45-140 s: 16 fps, p95 62-64 ms; hall 30 fps; gate, coda and quiet 60 fps | 16.4/63.6 |
| MS-R1 | Mali-G720 Immortalis | 1080p cap (quality mid; strong also 60 fps on the hull) | coda: p95 18.6 ms; every other act 16.7 ms | 58.2/18.6 |
| build-host | RTX 2060 (PRIME offload) | native (quality strong) | every act at vsync | 60.0/16.9 |
| build-host (dev proxy, headless incl. readback) | Radeon 780M | 960x540 | weak 2.3-6.1 ms, rich 2.5-8.8 ms; v1 16.2 ms | - |

`tools/measure-tiers.py` classifies it as **weak** (UHD 630 at scale 0.5 and Mali both hold 30 fps with p95 <= 40 ms). The row is in `tiers-pending.tsv`. Navi14 was not measured: build-host is reserved.

Evidence on build-host in `/mnt/datapool/projects/ncz-session-2026-09-29/new-screensavers/leviathan-v2-lead24/`:
- `perf-logs/` (every `[stats]` log);
- `session-clips-build-host/` (four 10-s in-session wf-recorder clips on the UHD 630 at scale 0.5, starting 0:00, 1:30, 3:00, 5:00);
- `ab-clips-offline/` (the same four time codes for v1 and v2, headless at 960x540, 30 fps, same seed);
- `frames/` (the contact sheet and the t=20 s frames).

## Exit latency (harness check)

`gles3_harness.c` sets `running = 0` in the SIGTERM/SIGINT handler and in the key handler, and checks it once per loop iteration, after `draw_and_swap()`. A frame that takes N seconds on the GPU therefore delays exit by up to about two frame times: the frame in flight, plus the blocking `eglSwapBuffers`. That is how v1's 4.8 s p95 frames on the O6N produced `exit=KILLED (after 3.0 s SIGTERM)`.

v2's frame budget removes the cause. Measured SIGTERM-to-exit: 52-118 ms on every host and act (UHD 630 native in the 16 fps hull pass included: 52 ms). v1 was KILLED after 3 s. The harness itself still has no bound. Proposed, not made (it is a shared file): on the first SIGTERM, arm a 2 s `alarm()` whose handler calls `_exit(0)`, so a pathological frame can never hold the screen after dismissal.

## Wrong turns (kept so nobody repeats them)

- **Hexagonal tunnel interior:** read as concentric hexagons. Replaced by the wide hall.
- **Windows faded to a flat mean at mid distance:** walls read as grey concrete or a "QR code" of lit and unlit rectangles. Replaced by runs, then promenade lines, then the mean.
- **Sun at 14.5 degrees:** the superstructure's shadow swallowed the deck from 80 s on. Raised to about 19 degrees (still in frame); the shadow now arrives after the spire eclipse.
- **Coda terrain at 0.16 frequency with a camera at 2.4:** blobby hills with marching holes. Replaced by 0.3 frequency, a camera at 4.5, and a distance-growing minimum step.
- **Quiet room thinning tile by tile:** a checkerboard. Replaced by a soft dissolve from the ceiling down.

## Unproven

- The human blind test: the brief asks for two people who have not read it. That has not been run. A proxy has: two fresh model sessions (Sonnet, Haiku) with no brief and no file names were shown both t=20 s frames unlabeled and asked "what is this?". New frame: "a space station or ring structure in orbit above a blue-and-white planet" (high confidence) and "space station ... orbiting above a planet with atmosphere" (high). Old frame: "glowing purple rings and bubbles ... abstract bokeh or screensaver pattern" (high) and "glowing neon concentric rings and spheres floating in dark space" (high). By the brief's pass rule (ship / station / spacecraft) the proxy passes; people may still differ.
- Navi14 timing.
- UHD 630 at native 1080p runs the hull pass at 16 fps. This is inside the rule, since the weak default is scale 0.5, but a weak machine forced to native will stutter there.
