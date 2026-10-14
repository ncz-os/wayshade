# magmasimplex: the sixties redesign (LEAD 25, 2026-09-29)

Design notes and change log for the redesign of the goo engine's look. The
goal is that it reads as a real 1960s lava lamp, and as the liquid light
shows of the *In-A-Gadda-Da-Vida* era, rather than as candy on a flat field.
Branch `feat/magmasimplex-60s-2026-09-29`, cut from
`feat/new-screensavers-2026-09-29` at `ace1c81` (LEAD 19's goo engine).

## What the operator asked for, in order

1. After watching the goo engine on build-host: "interesting but still not
   reading as 'Lava Lamp'. I want more 1960s, more In-A-Gadda-Da-Vida." The
   photo showed a flat saturated background, a flat pool band, and glossy
   jellybean blobs with pinpoint white highlights.
2. "I like the full screen field." Keep the full-screen composition, with no
   lamp standing in a room as the default.
3. "I don't want to limit it to a tall lamp." The world can be any shape:
   a wall-sized tank, a horizontal dish, a radial hot spot, or gravity that
   swings. Launches should vary.
4. "I want the feeling of being INSIDE the lamp or having your eyes right up
   against it." Two immersive cameras (inside, glass macro), plus the light
   show, joined by cross-fades and never cut.
5. On a red-liquid variant: "the globs look like bubbles, not lava goo. They
   lack texture." Give the wax real material.
6. Four reference photographs of real lamps (below), then three public
   videos (two time-lapses and a 5-hour real-time recording), for analysis
   only.
7. "Do we need actual fluid dynamics data?" Calibrate against real lamps
   with numbers, not vibes; record every source.
8. "Preserve these variations as variable settings like we have with black
   hole." Every variation becomes a documented option, there are named scenes
   and a reproducible randomize mode, and a schema lets the settings UI draw
   the controls with no per-saver code.
9. "Remember we had a very large palette of available color combos." Keep
   the original colourway table, add real lamp color codes (a collector reference site
   chart), period pairings and a generator.

## Ground truth: what real lamps look like

Sources, URLs and names: docs/research/magmasimplex-palette-sources.md (not packaged).

Reference photos (operator-supplied, found online; kept on build-host
`.../magmasimplex-60s/references/`, not in this repository):

| ref | what it shows | what we took |
|---|---|---|
| ref1 orange on cyan | lumpy cloudy cap layer at the top with scalloped hanging bells; one smooth glowing sphere; sparse specks | cap mass with bells; palette `orange-on-cyan` |
| ref2 green lamp | pale, milky, near-opaque yellow-green wax, matte, covered in micro-pits; flat egg and disc shapes; a mushroom/tower stalk from the pool; hot dome of light at the base | pits, matte wrap diffuse, towers; palette `lime-on-green` |
| ref3 red drips collage | stalactites and stalagmites, nodular beaded strands, branching arms; translucent lit edges, dense dark cores; amber liquid | beaded strands, stalactites and drips; palette `red-on-amber` |
| ref4 yellow on pink | opaque, smooth-lumpy eggs; a big fused cap; hot white-yellow bulb dome; intensely saturated pink liquid that is darker at the edges | "the liquid carries the colour"; palette `yellow-on-pink` |

The main takeaway: the **liquid** is the colored light, saturated and
glowing, brightest over the bulb and dark toward the top and edges. The
**wax** is its complement: opaque to milky, matte, lumpy and pitted, and
translucent only where it is thin. It is not a glass balloon.

Operator-supplied public time-lapses, **analysis only** (not embedded, not
redistributed, not shown):

- video A (time-lapse), 309 s, analyzed
  at 640x360. Red wax in clear liquid with a hot orange-yellow base; shape
  vocabulary (bulging dome, stalk with a bulbous head, detaching head, hanging
  threads). Time-lapse factor unknown, so it is not used for timing.
- video C (5 h, real time), 
  18100 s, 640x360, **real time**, three warmed-up lamps. This is the timing
  ground truth: `tools/magmasimplex-footage-stats.py`, windows 3600-7200,
  9000-12600 and 14400-18000 s, sampled at 2 Hz.
- video B (start-up time-lapse),
  21:56, 360x640, about 100x per its description, with
  normal-speed highlight sections. This is the primary reference for texture
  and shape: knobbly, cauliflower-like columns, folded domes, layered
  translucency with white-blue rims and violet cores, and the warm-up phases
  (cold slab, first column, cap formation).

Calibration numbers from these sources go in the table below. Every number is
marked **published** or **estimated from footage**.

## Real lamp vs simulation (calibration)

Measured from the operator-supplied real-time footage (analysis only) with
`tools/magmasimplex-footage-stats.py` (per lamp: crop the globe, mask the wax
by color, 4-connected components, pool and cap bands excluded, specks under 20
px and thin strands counted apart, nearest-centroid tracks at 2 Hz). The
simulation side comes from `tests/magmasimplex_calib.c` (tank, 16:9, 12
bodies, 8 seeds x 1800 s), converted at the calibrated time scale of 1.3
simulated seconds per wall second at `speed=1`.

| quantity | real lamps (L1 red/amber, L2 cyan/blue, L3 violet/teal) | source | simulation | notes |
|---|---|---|---|---|
| moving free masses, rise speed p50 | 0.006 / 0.019-0.026 / 0.029-0.031 globe heights/s | measured, real-time footage | 0.022 heights/s | L1's mask includes hovering blobs; 0.02 taken as the target |
| rise speed p90 | 0.030-0.036 / 0.054-0.073 / 0.061-0.072 | measured | 0.044 | |
| sink speed p50 | 0.008 / 0.022-0.026 / 0.027-0.033 | measured | 0.019 | |
| time to rise one globe height, from mid-band crossings | 21-23 s (n=3) | measured | about 45 s (p50), 23 s (p90) | few crossings survive the tracker |
| free masses on screen, mean | 1.8-2.0 / 0.5-0.6 / 0.5-0.5 per lamp | measured | 8.3 per 16:9 screen | the screen is about 4 lamp-widths wide; the operator's gate asks for at least 3 |
| share of time with no free mass | 11-15 % / 51-61 % / 59-62 % | measured | 0 % | deliberate: the blob gate |
| diameter (fraction of globe width) p50 | 0.32-0.34 / 0.24-0.26 / 0.18 | measured | | |
| diameter / height p50 (the globe is 40-80 px wide, about 0.43 of its 140 px height at mid-height) | about 0.14 / 0.11 / 0.08 | derived | 0.13 | |
| aspect h/w p50 (p90) | 1.0 (1.8-2.0) / 0.8 (2.5-2.9) / 0.86 (2.1-2.3) | measured | stretch from motion, up to 1.8 | |
| thin strands or glints per frame | 0.15 / 0.8 / 0.7 | measured | necks, towers, stalactites | |
| quiet minutes (fewer than 0.2 masses on average) | 0 / 2-7 / 0-5 of 60 | measured | none | |
| warm-up to steady state | about 2 h for a real lamp; wax still heating at 1 h | time-lapse B description (published by the video's author), ~100x time-lapse | `startup=true`: 120 simulated s (about 90 s wall), then 25 % longer per depth layer | compressed on purpose |

Not measurable at 360p: neck lifetimes before a snap, and thread diameters.
The neck model is the viscous pinch law (linear thinning to the break) with
lifetimes of 5-9 simulated s (towers 14-22 s), set by eye from the time-lapses.

## Settings, scenes and randomize

Every variation is an option (`src/gles3_magmasimplex.c`, schema in
`assets/screensaver-chooser/options/magmasimplex.tsv` and `.json`), settable as
`--name=value`, `NCZ_MAGMASIMPLEX_<NAME>` or in a scene. With `randomize=auto`
(the default), every setting that was not set explicitly (by you, a scene or
the environment) is drawn from the seed, each from its own random stream, so
fixing one setting never reshuffles the others. The saver prints the resolved
configuration in one `[diag] magmasimplex engine=goo ...` line, and the same
(scene, settings, seed) always gives the same lamp.

| option | type | range / choices | default | drawn when unset (randomize=auto) |
|---|---|---|---|---|
| preset | string | a scene name | none | |
| randomize | enum | auto, off | auto | |
| view | enum | auto, inside, glass, show, field, butterfly ("Flowerchild") | auto (chapters) | chapters start inside 60 %, glass 25 %, show 15 %; a light-show chapter is show 65 % / Flowerchild 35 % |
| world | enum | auto, tank, dish, radial, drift | auto | tank 50 %, dish 15 %, radial 15 %, drift 20 % (off: tank) |
| palette-era | enum | auto, 1960s, 1970s, 1980s, 1990s, modern | auto | narrows palette draws |
| palette | enum | auto, generated-vintage, generated-neon, 110 table ids | auto | readable table entry (1960s and photos weighted x2) 85 %, generated 15 % (off: era-60s-yellow-orange) |
| material | enum | auto, milky, translucent, jelly | auto | milky 70 %, translucent 30 %; jelly is never drawn (opt-in, 2026-10-05) |
| texture | float | 0-2 | 1 | 0.6-1.4 |
| glitter | enum | auto, none, silver, gold, rainbow | auto (as the palette says) | |
| glitter-amount | float | 0-2 | 1 | |
| motes | float | 0-2 | 1 | 0.5-1.5 |
| glass | float | 0-2 | 1 | 0.5-1.5 |
| depth | float | 0.3-2 | 1 | 0.7-1.3 |
| drift | float | 0-2 | 1 | 0.5-1.5 |
| kaleidoscope | int | -1 (draw), 0 off, 2-12 | -1 | off 60 %, 4/6/8 sectors 15/15/10 % |
| chapter | float | 0.1-5 | 1 | |
| speed | float | 0.1-8 | 1 = real-lamp pace | not drawn |
| viscosity | float | 0.5-2 | 1 | 0.8-1.1 |
| heat | float | 0.5-1.5 | 1 | 0.85-1.15 |
| wax | float | 0.6-1.4 | 1 | 0.85-1.1 |
| cap | enum | auto, on, off | auto | on 75 % |
| startup | bool | | false | never drawn (it is a scene) |
| projector-mask | enum | off, soft, dish, disc | soft | never drawn (light shows: fill the screen, the soft round throw, a small dish on black, a large cool disc) |
| overlay | enum | off, streaks, neon, both | off | never drawn (Flowerchild: extra projector layers) |
| poster | bool | | false | never drawn (Flowerchild: flat hard-edged bands) |
| engine, colourway | | | goo; colourway is for engine=fluid only | |
| quality, seed | | | auto; 0 = random | |

Drawn ranges for viscosity and wax are narrower than the option ranges: with
viscosity up to 1.3, 2 of 1000 draws dipped to 2 masses at 60 s. Explicit
values can use the whole range.

Scenes (`assets/screensaver-chooser/options/presets/magmasimplex/`, each as
`.json` for the launcher and `.conf` for `--preset=`, both generated by
`tools/magmasimplex-schema.py` from one table): classic-lamp, inside-the-wax,
glass-macro, liquid-light-show, red-in-amber, cyan-in-blue, pink-in-teal,
startup-warmup, lime-on-green, yellow-on-pink, sixties, glitter-emerald,
candy-jelly, and (2026-10-05) amber-glow, teal-suspended and the nineteen
Flowerchild scenes (docs/MAGMASIMPLEX-REALISM.md; the old `butterfly-*` ids
are `.conf` aliases). A scene sets only what it names, and everything else is still
drawn from the seed.

The JSON schema follows docs/OPTIONS-SCHEMA.md (LEAD 2): `level` basic for
preset, seed, randomize, view, world, palette-era, material and speed, and
advanced otherwise. The palette carries `choice_groups` (Auto, Generated,
1960s lamps, 1970s and 1980s lamps, Color codes 1992-2004, Reference photos,
Real-lamp footage, Original colourways) and `choice_labels` (liquid/wax
names), agreed with LEAD 2 as optional fields.

## Palettes (129 in the table, plus two generators)

`src/magmasimplex_palettes.c`, each with id, liquid/wax label, era and source:

| source | count | how the colors were obtained |
|---|---|---|
| photos | 9 | the four reference photographs plus five sixties pairings, set by eye |
| footage | 5 | medians of masked pixels in frames of the three videos (analysis only) |
| era-pairings | 20 1960s + 3 1970s/80s | pairings the a collector reference site era pages, the a 1960s UK maker page and the a collector timeline timelines document in words (research summary on build-host `references-data/`), with colors chosen for the words: yellow/orange and blue/green (the standard sixties pair), clear/red, Champagne Mist (cream in hazy amber), fluorescent green/red, blue-green/yellow, lavender/cream (the 1965 original) and lavender/blue, the 1968 late-1960s mists (blue mist/blue, orange mist/yellow, green mist/lime, green mist/mint and red), the prototypes red/pale blue and amber/yellow, glitter lamps (emerald, gold, silver), a 1960s UK maker Port (ruby in rose) and Starboard (topaz in blue-green), 1970s blue/white, 1980s yellow/red and clear/gold |
| codes-1992-2004 | 43 | USA color codes 1992-2004 (a collector reference site reference chart), ids keep the code numbers. Colors were SAMPLED: each swatch cropped, outline and specular pixels dropped, 2-means clustering, liquid = the cluster that dominates the swatch edges. Corrections: 08, 21, 22 and 25 had the clusters swapped (fixed by the liquid named first); 16a's wax was taken as the median of the dark pixels; white wax (03, 05, 26, glitter lamps) set to white, since the chart draws it tinted by the liquid. The chart itself is not reproduced anywhere in the repository |
| magmasimplex-ways | 30 | the original colourways (`ways[]`, same values and indices, way-NN = colourway=N) |

"Clear" liquids are modeled as an untinted carrier lit by the warm bulb, so
the wax carries the color. Mist liquids add a milky haze. Liquids are
normalized to full intensity (the bulb sets their brightness), and dark wax
stays dark (clear/black is a real lamp). The readability rule
(`ms_palette_readable`) rejects a desaturated liquid, a muddy mid-tone wax,
and pairs too close in both hue and luminance; every draw passes it.
Generators: `generated-vintage` (warm or deep liquids with a complement,
split complement or cream wax) and `generated-neon` (electric complements, or
analogous pairs with a luminance step).

Naming: the Round 18 comment in meson.build asks for no manufacturer or
brand references. The coordinator asked for the `code-NN` ids, and this
branch follows that request; the conflict is flagged for a decision. Video
channel names are not used in ids (`footage-*`).

## Design

### Views (`view=auto|inside|glass|show|field`)

- **inside**: the camera hangs in the liquid. Three layers of the simulation
  (far, mid, near) are drawn at scales 1.02, 1.5 and 2.6, and each layer's
  camera drifts slowly, the near one most, which gives parallax. The far
  layer fogs into the liquid, and the near layer is huge and out of focus.
  The pool and the cap exist only in the far layer. A cheap volumetric feel
  comes from layered implicit fields with analytic thickness, not a raymarch.
- **glass**: extreme macro against the glass. Wax pressed flat on the inside
  shows a sharp contact line (a dark meniscus with a bright lip) and thin
  films around it. The deep lamp sits soft behind. Dust, smudges and a
  fingerprint refract the view, and the curved glass adds barrel distortion.
- **show**: a 1960s liquid light show. Inks are domain-warped with feathered
  edges and dark oily veins. The simulated bodies become oil lenses with
  chromatic fringing. There are expanding rings, trapped bubbles, a projector
  hot spot, a circular throw and grain, with slow rotation, an optional
  kaleidoscope (4/6/8 sectors per run) and a 2 % heartbeat swell at 0.8 Hz.
  No text, no album art, no copied poster.
- **field**: the classic full-screen lamp: one layer, the pool below and the
  cap above. It is the cheapest view.
- **auto**: chapters. inside/glass/field last 6-10 min, show lasts 2-4 min
  (scaled by `chapter`), the first chapter lasts 1-3 min, and each change
  cross-fades over 30 s. A chapter change may also cross-fade the palette (to
  one of the same era; never when the palette was chosen), and that is the
  only time the colors change.

### Worlds (`world=auto|tank|dish|radial|drift`)

The simulation always runs "heat below, up is +y". The world maps screen to
simulation space:

- **tank**: identity. Heat is below.
- **dish**: rotated 90 degrees, so heat is at one side. It shows the middle
  of a 4:3 column.
- **radial**: log-polar, which is conformal, so shapes keep their proportions.
  The hot centre is the pool, and wax grows as it moves outward.
- **drift**: the view swings +/-29 degrees over 10 minutes.

### Material

- Liquid: `L * (0.05 + 1.25 g) + bulb * g^3` with g the bulb light, plus
  faint rising convection sheets, light shafts, motes and micro-bubbles.
- Wax: milky albedo with marbling (dense cores against milky clouds),
  striations along the stretch direction, and flecks. Lighting is a wrapped
  matte diffuse from the bulb below plus ambient in the liquid's own color.
  Thin edges transmit warmer, lighter light (an orange core with an amber
  edge), and hot wax glows near the bulb. There is a broad satin sheen and a
  soft rim; no point highlight exists anywhere. Micro-pits appear in patches.
  Cooled wax gets a wrinkled skin. Texture coordinates travel with each body,
  so the texture does not swim.
- Light bleeds from the wax into the liquid just around it.

### Simulation additions (`src/magmasimplex_goo_sim.c`)

- **Strands**: a neck joins each new bud to the pool. It thins linearly (the
  viscous pinch, h(t) = h0 (1 - t/tb)), beads as it thins, then snaps. 30 %
  of buds grow a **tower** stalk that holds the cap back (a mushroom). With
  the cap enabled, **stalactites** grow down from the cap, drop their tips as
  cold drips, and retract.
- **Cap mass** (75 % of runs): cooled wax collects under the top. Bodies
  fused into it do not count as separate masses.
- **Vertical slide-past**: when two bodies overlap, the warmer one is pushed
  up past the cooler one (see the wrong turns below).
- Each body has a texture id.

### Performance

| host | GPU | settings | inside | glass | show | field |
|---|---|---|---|---|---|---|
| build-host (headless sway) | AMD Phoenix iGPU | 1920x1080, full quality | 60 fps, p95 16.1 ms | 60, 16.1 | 60, 16.1 | 60, 16.1 |
| build-host | Intel UHD 630 (DRI_PRIME offload) | weak-class defaults: scale 0.5 (768x480 of 1536x960), light quality | 60 fps, p95 16.9 ms | 60, 16.9 | 60, 16.8 | 60, 16.8 |
| build-host | Intel UHD 630 | headroom probe: scale 1.0, light | 33 fps, p95 32.6 ms | 39, 27.6 | 60, 16.8 | 60, 16.9 |
| build-host | Intel UHD 630 | scale 1.0, full quality, show | | | 25 fps, p95 43.0 ms | |

All numbers are steady-state harness `[stats]`, 30 s runs, with the idle daemon stopped and the host lock held. At the weak-class defaults every view is vsync-bound. The scale-1.0 probe puts the GPU cost of inside at about 31 ms per 4x pixels, so about 8 ms at scale 0.5. A 30 s chapter cross-fade renders two views (inside plus glass is about 14 ms), which still fits in a 16.7 ms frame. Weak GPUs stay on light quality (auto). Mali-G720 (MS-R1/O6N) is not measured yet.

## Gates

- `magmasimplex-goo-blob-gate`: at least 3 separate masses at 20/60/120/300 s,
  never zero, and fewer than 2 in at most 0.05 % of sampled seconds. It now
  covers 4 budget/cap configurations (12/20 without a cap, 9/12 with one) x 4
  aspects (16:9, 16:10, 4:3, 21:9) x 40 seeds = 640 runs. It passes with 0
  failures, and the worst case is 1 mass for 0.0005 % of seconds.
- `magmasimplex-randomize`: 1000 seeded draws. Each must be reproducible,
  in range and readable, and must keep at least 3 masses at 20/60/120/300 s
  wall time at the drawn physics and speed. The test also checks that every
  palette id in the option's choices exists in the table.
- `magmasimplex-json-in-sync`, `magmasimplex-preset-precedence`: the
  generated schema and scenes match the binary; scene < environment <
  command line.
- `tools/magmasimplex-frame-gates.py`, run on a wf-recorder capture of the
  compositor decoded at exactly 30 Hz (PNG dumps stall the renderer, see
  the wrong turns below). It checks three things:
  - **flat**: at most 25 % of 32x32 tiles are truly flat, and the p5-p95
    luminance spread is at least 25/255.
  - **smooth**: the operator's complaint. At most 25 % of tiles have a
    luminance std under 3/255.
  - **flash**: after removing the 1 s trend, no component above 3 Hz is
    larger than 4/255, in the whole frame or in any cell of a 16x9 grid.

  Validated in both directions, on synthetic controls and on real
  recordings (build-host, seed 4242):

  | recording | flat tiles | smooth tiles | worst component above 3 Hz |
  |---|---|---|---|
  | control: 5 Hz flicker of +-14/255 | FAIL | | **FAIL** 13.8/255 at 5.0 Hz |
  | control: flat green field | **FAIL** 1.0 | | pass |
  | control: 0.5 Hz swell of +-14/255 | | | pass 0.34 (slow changes are allowed) |
  | previous goo engine (ace1c81) | pass 0.042 | **FAIL 0.486** | pass 0.02 |
  | inside | 0.000 | 0.081 | 0.17/255 |
  | glass | 0.000 | 0.062 | 0.10/255 |
  | show, kaleidoscope 6 | 0.017 | 0.067 | 0.08/255 |
  | field | 0.000 | 0.025 | 0.02/255 |
  | chapter cross-fade glass to show, with a palette change, 45 s | 0.017 | 0.062 | 0.05/255 |

## Change log, wrong turns included

- **2026-10-05, realism review** (docs/MAGMASIMPLEX-REALISM.md): the wax is
  now a scattering body lit from below (pale luminous core, saturated glowing
  edge, soft Gaussian silhouette, no highlight, no outline), pits only on cool
  thick wax, no cold-wrinkle stripes, motes 4-5x sparser and clumped, no ring
  bubbles, soft stalactites and staggered strand beads, a darker clear-liquid
  carrier, merges 0.08/s; jelly is opt-in; a Flowerchild light-show view
  (view=butterfly) with dye palettes, projector mask, overlay and poster
  switches and nineteen named scenes in two families; measurable gates in
  tools/magmasimplex-realism-gates.py; the randomized driver draws
  categories (tools/magmasimplex-longrun/).

- **v1 drew the pool and the cap in every depth layer.** The result was three
  stacked horizontal bands. Fixed: they are far-layer only.
- **Texture offsets per body were `hash * 40`.** Blending those between two
  bodies across a neck compressed the marbling into wood-grain rings. They
  are now `hash * 1.7`, and strand coordinates use unit-length axes.
- **Radial hot spot**: log-polar sends r→0 to y→-inf, and the bulb glow blew
  up to a pinpoint star. Glow is now clamped at 1.25.
- **Glass macro, first try**: the pressed wax was pale inside a wide red halo
  and the meniscus was heavy, which read as cells under a microscope. The
  pressed wax is now saturated, the halo is narrower and the meniscus lighter.
- **Light show, first try**: confetti. The ink scale was too fine and cyan
  dominated. The ink scale is now 0.42, feathering is halved and the bands
  are rebalanced.
- **Pastel wax, v2**: the first reference-matched material mixed 15 % white
  into the wax, lit it with ambient in the liquid's color at up to 0.83x the
  bulb light, and tinted thin edges toward the near-white bulb. The liquid
  was also driven above 1.0 into the ACES shoulder. The palette contact
  sheet showed every palette pastel and low in contrast. v3: 6 % white,
  ambient 0.06-0.34, edges tinted by the bulb multiplicatively, liquid kept
  below about 1.0, and more opaque milky wax (alpha 0.75-1.0).
- **Clear liquid as beige**: "clear" was modeled as a full-intensity warm
  liquid, so clear lamps filled the screen with beige. A clear liquid is
  transparent: the room shows through dark, warm only near the bulb.
- **Light-show inks**: fixed era colors dominated at 70 %, so every show
  looked the same whatever the palette. The palette now carries 60-70 %.
- **Frame gates on PNG dumps**: NCZ_FRAME_DUMP writes two files per frame
  and stalls the renderer (p95 above 130 ms), so the real sampling rate was
  about 7.6 Hz, too slow to see 3 Hz flicker. The gates now read a
  wf-recorder capture decoded at 30 Hz (wf-recorder was installed on build-host:
  one package, zero removals, simulated first).
- **The flat gate alone would have passed the old engine** (0.042 flat
  tiles): its field is smooth and low in contrast, not truly flat. The
  smooth-tile measure separates them (0.486 vs at most 0.081).
- **Time scale placeholder**: the first cut slowed the simulation to 0.4x,
  assuming it ran fast. The calibration harness showed its free bodies already
  moved at 0.017 heights per simulated second, near the real lamps' median;
  the calibrated value is 1.3.
- **Palette draws**: the generator roll sat inside the retry loop, so every
  rejected table pick rolled again and generated pairs came out 29 % of the
  time. It is now one up-front roll (16 %).
- **zoder review of chunk 1** (all verified against the source). Real and
  fixed: a strand could follow a new body that reused a slot within one step
  (bodies now carry ids); a stalactite kept growing when its drip had no room;
  a hard sign flip in the vertical slide-past (now tanh); reversed smoothstep
  edges for the glass films (undefined in GLSL ES); the pool slope's second
  term (0.9, should be 0.35 x 5.1); the bounding test cut off strongly
  stretched bodies; the taper kink; the prewarm running at 16:9 on other
  screens (sims now start on the first frame at the real aspect). Rejected:
  a claimed NaN from sqrtf (both call sites are guarded); a claimed
  unnormalized normal (it is normalized); a claimed sampler-array limit
  (texture ids are not samplers).
- **Footage tracker**: the first pass counted the lit cyan liquid at the top
  of L2 as wax (it has red at about 0, the wax does not), and tracks broke at
  every merge with the pool or the cap. The rise time now comes from
  mid-band crossings, scaled to a full height.
- **Blob gate after adding strands**: 2 of 240 runs failed. The strand code
  changes the random stream, and holding buds on necks keeps them joined to
  the pool, which the gate now counts honestly. Extending coverage to the
  real configurations exposed 300 of 640 failures, almost all in the dish
  world's narrow 0.56 column. The failing states (diagnosed by dumping body
  states) were bodies stalled in a neutral-buoyancy band, touching sideways
  and forming one mass. Fixes: vertical slide-past, seeded radii scaled by
  aspect, necks released early when few masses show, and the dish column at
  4:3. A square column (1.0) still failed 13 runs.
