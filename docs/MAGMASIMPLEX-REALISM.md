# magmasimplex realism: rubric, diagnosis, gates, Flowerchild (2026-10-05)

Operator review of the randomized 5-minute run on O6N (2026-10-04): "some of
these are genuinely interesting (like the tie dye one) but some do not look
like lava wax (such as the orange one)". This document is the written rubric
built from the real-lamp references, the diagnosis of the three photographed
looks, what changed, the measurable gates, and the butterfly light-show mode
added on 2026-10-05 (display name "Flowerchild", option value `butterfly`). Evidence captures live outside the repository (ULTRA
`~/oc-cyg/magma-realism-evidence/`, report `~/oc-cyg/MAGREAL.md`).

## 1. References studied

Real-lamp footage on build-host (`new-screensavers/magmasimplex-60s/references-video/`,
all 360p web video; the four reference photographs are root-owned 0600 behind
`root_squash` and could not be read from ULTRA):

| source | frames | what it shows |
|---|---|---|
| 5-hour real-time recording, three lamps (orange/yellow, cyan/blue, violet/teal) | 9 frames at 15..240 min, 27 globe crops | steady-state lamps side by side |
| start-up recording, blue wax in clear liquid (portrait, the best resolution) | 5 frames | cold column rising, cap forming, translucency |
| time-lapse, red wax in red-orange liquid | 5 frames | mushroom columns, pear drops, necks, the bulb's glow |

## 2. The rubric (from the footage, not from imagination)

| aspect | real lamps | never present |
|---|---|---|
| silhouette | rounded lumps, pears, teardrops, mushroom columns; aspect 1:1 to 1:2; edges soft (a translucent skin, 2-5 % of the body's width) | a clean ellipse repeated, a hard outline, a dark-plus-bright double line |
| shapes over time | a body stretches, necks, pinches off; two slow bodies fuse; the pool buds columns; cooled wax sinks and drains into the pool; a flat lens of wax sits under the cap | identical beads on a string; shapes that only translate |
| wax luminance | the wax is a scattering body lit from below: near the bulb it is as bright as or brighter than the liquid; the dense core is pale (the dye washed toward the bulb's white), the thin edge is saturated | wax darker than a glowing liquid everywhere; a flat fill |
| wax surface | smooth, matte, milky; a broad soft sheen at most; pitted skin only where wax has cooled | a glossy point highlight ("glazed candy"); a uniform crater texture |
| liquid | one smooth vertical gradient, brightest over the bulb, dim at the top; coloured liquids saturated, clear liquids show the dim room with a warm glow at the base; no particles except a faint haze in a cold lamp | an even white speckle; drawn ring-outlined bubbles; a flat beige field |
| glass | a vertical highlight streak on the globe's side, a bright lip at the cap; on the glass, not on the wax | highlights on the wax |
| colour | hue luminance decides: cyan wax reads brighter than blue liquid, violet wax darker than teal liquid; both are real | |
| pace | rise 0.02 globe heights/s (p50), 0.05 (p90), set by TIME_SCALE 1.3 (unchanged) | |

## 3. Diagnosis of the three operator photos

All three were slices of the MAG smoke run (base seed 20261004, 60 s slices;
EXIF 20:01:51 / 20:02:28 / 20:04:14 EDT inside the 00:01:15-00:05:32 Z run);
`run-slice.sh logs/20261005-001504-seed20261004/slices.tsv 1|2|3` on O6N
reproduces them and the sweep names them R01/R02/R03.

| photo | config | matches the rubric | does not | responsible code / options |
|---|---|---|---|---|
| IMG_8237 orange ("not wax") | slice 1: `code-08`, tank, inside, **material=jelly**, **glitter=silver**, motes 1.24 | soft bodies, a pool and a cap exist | glossy point highlights, glass-clear bodies, identical ellipses, an even white speckle, ring-outlined bubbles | `material=jelly` (the candy branch of `waxOver`: `spec = pow(.., 90)`, `behind * tr * 1.35`); `glitter=silver` drawn explicitly by the old driver (uniformly from none/silver/gold/rainbow); the `ring` term of the out-of-focus bubbles in `renderInside`; motes densities 0.10/0.08/0.07 |
| IMG_8238 tie-dye ("genuinely interesting", keep) | slice 2: `way-28`, drift, **view=show**, kaleidoscope -1 | a liquid light show by design | | `renderShow`/`showInk`: unchanged. Reachable as the `liquid-light-show` scene (view=show) and the driver's `show` category |
| IMG_8240 blue (closer, issues) | slice 3: preset `startup-warmup` (`footage-blue-in-clear`, field, startup) | the pool, a rising column, a cap | a regular bead chain, a comb of hatching on the cap, an even speckle on the pool, wax darker than the "clear" liquid | tower lobes 0.75-0.95 with one lobe per 3.5 radii; the cold "wrinkle" stripes (`wr` at 26x) in `waxOver`; `pits()` at two scales over the pool; the clear-liquid carrier 0.42/0.32/0.20 (an opaque beige field) |

## 4. What changed

Shader (`vendor/magmasimplex/goo.frag`):
- `waxOver` rewritten for milky/translucent: the body is a scattering mass lit
  from below (`lw = 0.26 + 0.70 light`, a dome normal from the field
  gradient), the dense core is the dye lit from inside (`mix(sqrt(W0), bulb,
  0.08)`, never a mix toward white, never above 1.0 into the tonemapper's
  shoulder), the thin edge keeps the dye and glows with transmitted light
  (`edgeGlow`), the outermost band darkens (rim falloff), a broad soft sheen
  (`pow(.., 6) * 0.10`), and the silhouette is a Gaussian-in-thickness
  transmittance (`exp(-(4.5 th)^2)`) a few pixels wide: no outline, no dyed
  band. Pits only in patches, only on cool, thick wax; the cold-wrinkle stripes
  are gone. Jelly keeps its old branch, opt-in only.
- Bodies get a slow low-order deformation (3 and 5 lobes, 4.5 % and 2 %, a
  polynomial atan2) so no two are the same ellipse; strand beads are staggered
  and sized per lobe; the cap is a soft lens (bells 0.035, noise 0.09).
- Motes: densities cut 4-5x and gated by a plume mask (`plumeMotes`, evaluated
  only in the rare cells that pass the density draw); the ring bubbles are a
  rare soft disc; glitter is clumped by a swirl mask; glass dust 0.015/0.012;
  the glass contact line is a soft meniscus and the pressed wax takes
  `waxOver`'s own colour (a separate colour drew a ring, "fried eggs").
- `bleed` into the liquid 0.22 -> 0.03 + 0.08 light.

Simulation (`src/magmasimplex_goo_sim.c`): merge rate 0.03 -> 0.08 /s with
looser closing conditions (0.12 cost one randomize draw its third mass at
60 s); warming tower lobes 0.45-0.7, tower lobes 0.3-0.55; stalactite beads
0.1-0.35. The blob gate (640 runs) and randomize (1000 draws) stay green;
calibration unchanged (rise p50 0.0166, p90 0.0339 heights/s simulated).

Palettes (`src/magmasimplex_palettes.c`): the clear-liquid carrier is
0.26/0.19/0.11 (the wax glows against a dim room); nine `light-show` dye sets
for butterfly (`show-*`, never drawn for lamps; `MS_PAL_DARK` keeps a dark
dye field dark).

Settings: `material` draw weights milky/translucent/jelly 0.75/0.20/0.05 ->
0.70/0.30/0.00 (jelly is opt-in: `material=jelly` or the `candy-jelly` scene);
`view` gains `butterfly` (Flowerchild); new `projector-mask` enum (off/soft/
dish/disc, default soft), `overlay` (off/streaks/neon/both) and `poster`
(bool), none drawn.

Driver (`tools/magmasimplex-longrun/`, deployed to O6N `~/magma-test/`):
`draw.py` draws a category per slice and logs it in `slices.tsv`:

| category | weight (env) | what it draws |
|---|---|---|
| lamp | 70 (`MAGMA_LAMP_PCT`) | view inside/field/glass 45/30/25, material milky/translucent 65/35, glitter as the palette says (85 %), motes 0.3-1.0, lamp palettes only |
| show | 15 (`MAGMA_SHOW_PCT`) | view=show (the tie-dye), kaleidoscope -1 or 3-8 |
| flowerchild | 15 (`MAGMA_FLOWERCHILD_PCT`; `MAGMA_BUTTERFLY_PCT` still read) | view=butterfly (Flowerchild), `show-*` dye sets 70 %, kaleidoscope 0 or 4-8 |
| candy | 0 (`MAGMA_CANDY_PCT`) | material=jelly: opt-in only |

One slice in four uses a named scene of its category. 200-slice dry run with
base seed 20261004: lamp 132 / show 39 / flowerchild 29, explicit glitter 6 %.

## 5. Gates (`tools/magmasimplex-realism-gates.py`, test `tools/tests/test_magmasimplex_realism.py`)

Frames are analysed at 540 px high (a 360p reference globe crop and a 1080p
screenshot at one lamp-relative scale); wax and liquid are split by a
two-cluster colour split, the liquid being the cluster that owns the side
borders at mid-height. Thresholds come from 37 globe crops of the three
videos (`calibrate`):

| metric | references p0 / p50 / p100 | gate | note |
|---|---|---|---|
| speckle_per_mpx | 0 / 0 / 659 | <= 40 | steady-state lamps 0-27; the 93-659 are the cold start-up frames of the clear lamp (cloudy liquid, noisy phone video), excluded |
| thin_bright | 0 / 0 / 0.027 | <= 0.003 | same exclusion (steady max 0.0023) |
| spec_per_100k | 0 / 0 / 11.1 | <= 12 | glossy spots: bright AND desaturated against their blob, small against it. Gated at the references' maximum only: bulb light transmitted through a thin translucent body at the hot centre is bright and desaturated too (the liked amber-glow look measures 4.6), so this count cannot separate gloss from transmission; the share below does |
| spec_blob_share | 0 / 0 / 0.20 (n=6) | <= 0.4 | share of bodies carrying a highlight, only with >= 4 bodies; a glazed material has one on every body |
| wax_over_liquid | 0.11 / 1.17 / 5.9 | >= 0.6 | hue decides the order: violet-in-teal is 0.60-0.72, so only a strongly darker wax fails |
| edge_rel, ellipse_iou, aspect_cv | 0.011-0.85 / 0.55-0.95 / 0.04-0.87 | report only | real lamps span the whole range (a blob in clear liquid films with a crisp edge; a lone rising blob is a near-perfect ellipse) |

Results (`gate`): the orange look (R01 before) fails three gates (speckle 2207,
thin 0.009, share 0.50); the blue look (R03 before) fails speckle 285 and
wax_over_liquid 0.55; the fixed lamp set (12 configs + R03, r4 shading) and
the two liked looks (amber-glow, teal-suspended) pass every gate (speckle
0-27, thin <= 0.0003, spec 0-4.6, share <= 0.25, w/l 0.76-2.7). The synthetic pytest (candy vs lamp) asserts the direction of every
metric and the verdicts.

## 6. Flowerchild (view=butterfly)

"Flowerchild" is the display name of the overhead-projector liquid light show
(the option value stays `butterfly`, the saver keeps its name and ids). It is
reproduced from the operator's reference footage (local reference only, not
in the repository): a domain-warped dye field with crisp seams and a bright
meniscus glow (`butterflyDye`), lacy fingering where dyes meet (`lacy`), two
layers of oil droplets with dark rims, lens interiors and a glint, rings on
the large ones (`droplets`), the simulated bodies as big oil discs, a slow
breathing of the thresholds and brightness (0.2-0.4 Hz, a few percent), a
wandering hot spot and bloom. Chapters: a light-show chapter is the ink show
65 % / Flowerchild 35 %. Cost on Mali-G720 at 1080p: 20-24 ms p50 (the ink
show is 35 ms); kaleidoscope and overlay scenes are marked `mid`.

Switches (never drawn; scenes set them):
- `projector-mask`: `off` fills the screen; `soft` is the round throw with the
  dark surround (the operator's liked red/yellow photo); `dish` a bright dish
  whose size pulses and drifts (1969 Fillmore footage); `disc` a larger,
  brighter, cooler dish. The ink show treats dish/disc as soft.
- `projector-mask-size` (0.30..1.00, 0 = the style's default): the dish/disc
  radius as a fraction of the screen's short side. The defaults are now large
  (about 0.92) so `dish`/`disc` and every dendrite preset fill most of the
  display (operator addendum, 2026-10-05); values between 0.01 and 0.30 are
  clamped up so the lit area never breaks at the screen edge. The old small
  Fillmore dish (the operator's recorded 0.47, the shader's prior 0.46) is the
  `fillmore-small` scene; `roundhouse` pins its old disc at 0.80.
- `dendrites` (`off|ink|veins`, default `off`): the Flowerchild dendrite layer.
  `ink` draws a dark branching ink tree with red droplets over the bright dish;
  `veins` a fibrous teal/yellow/pink vein network where two dyes interdigitate.
  Implemented GPU-cheap (domain-warped ridged fbm in a polar warp plus a
  multi-scale Voronoi edge distance), deterministic, slowly advecting the seed
  so it never freezes (OLED), and rooted in screen coordinates so it scales
  with the viewport (several roots across a wide display). Disabled on the weak
  GPU tier: `u_quality` gates it to zero, the presets are `mid`.
- `dendrite-strength` (0..1, default 1): how strongly the layer is drawn.
- `overlay` (`off|streaks|neon|both`): extra projectors over the dye, tall
  drifting colour streaks and neon-sign tubes (the 1969 footage shows both).
- `poster` (bool): flat colour bands with hard edges, no seam glow, no bloom.

The scene names were approved by the operator on 2026-10-05; they nod at the
era and never use a band name (tested). Two families are tagged in the
description (the scene JSON has no group field): **Haight-Ashbury** (the
authentic light-show set) and **Swinging London**.

| id | display name | family | palette (field / inks) | switches | class | maps to |
|---|---|---|---|---|---|---|
| conspiracy | Conspiracy | Haight-Ashbury | show-cyan-red | mask off | weak | the 2026 video's cyan/red/orange marbling (closest overall) |
| haight | Haight | Haight-Ashbury | show-yellow-red | **mask soft** | weak | the operator's liked red/yellow photo (regression baseline) |
| fillmore | Fillmore | Haight-Ashbury | show-fillmore-splat | **mask dish** (large, 0.92) | weak | 1969 Fillmore, white-hot dish filling most of the screen |
| fillmore-small | Fillmore small | Haight-Ashbury | show-fillmore-splat | **mask dish, size 0.47** | weak | the old small 1969 dish, kept exactly |
| strawberry | Strawberry | Haight-Ashbury | show-fillmore-red | mask off | weak | Fillmore deep-red macro |
| inkwell | Inkwell | Haight-Ashbury | show-fillmore-splat | **mask dish, dendrites ink** | mid | a black ink tree with red droplets on the white-hot dish |
| veins | Veins | Haight-Ashbury | show-veins | mask off, dendrites veins | mid | teal/yellow/pink radial vein network, macro |
| avalon | Avalon | Haight-Ashbury | show-cyan-red | mask off, dendrites ink | mid | ink fingers into a saturated two-dye field |
| grape | Grape | | show-royal-white | mask off | weak | royal blue with white marbling |
| airplane | Airplane | | show-holo-pink-teal | mask off | weak | pink/purple/teal |
| prune | Prune | | show-ember-gold | mask off | weak | orange/gold/black dark waves |
| butterfly | Butterfly | | show-neon-pulse | kaleidoscope 6 | mid | neon pulse with symmetry |
| elevator | Elevator | | show-green-blue-yellow | **overlay both** | mid | the 1969 vertical streaks and neon-sign overlays |
| silver-apple | Silver Apple | | show-silver-apple | mask off | weak | silver/teal/electric blue, cooler electronic feel |
| technicolor | Technicolor | Swinging London | show-technicolor | mask off | weak | many hues at once, big fields |
| ufo | UFO | Swinging London | show-ufo (dark) | mask off | weak | white droplets in dark violet |
| middle-earth | Middle Earth | Swinging London | show-middle-earth (dark) | mask off | weak | deep green and teal |
| roundhouse | Roundhouse | Swinging London | show-roundhouse | **mask disc, size 0.80** | weak | large bright cool dish (unchanged) |
| hapshash | Hapshash | Swinging London | show-hapshash | **poster** | weak | flat hard-edged bands |
| kaleidoscope | Kaleidoscope | Swinging London | show-kaleido | kaleidoscope 6 | mid | six-fold, blue/yellow/magenta |
| piper | Piper | Swinging London | show-piper | mask off | weak | pastel dawn |
| itchycoo | Itchycoo | Swinging London | show-itchycoo | mask off | weak | park green and gold |
| carnaby | Carnaby | Swinging London | show-carnaby | **poster**, kaleidoscope 4 | mid | hot pink/orange op-art |

Old ids keep working: `butterfly-sixties -> conspiracy`, `butterfly-royal ->
grape`, `butterfly-sunburst -> haight`, `butterfly-holographic -> airplane`,
`butterfly-ember -> prune`, `butterfly-neon-pulse -> butterfly`,
`butterfly-fillmore -> fillmore`, `butterfly-crimson -> strawberry`. Each is a
generated `.conf` alias (a comment names the target; no `.json`, so the chooser
lists each scene once); `tools/tests/test_magmasimplex_scenes.py` and the
`magmasimplex-preset-aliases` meson test check them.

Built 2026-10-05 in the `feat/magmasimplex-dendrites-20261005` branch: the
dendrite layer (`dendrites`, `dendrite-strength`), `projector-mask-size`, the
large dish/disc defaults and the `inkwell`, `veins`, `avalon`, `fillmore-small`
scenes (section 6). Not built (follow-ups): squeeze/boil impulses, an audio
hook, a half-resolution FBO for the warped fbm.

## 7. Regression baseline (operator-liked looks)

| photo | preset | configuration |
|---|---|---|
| IMG_8246 "pretty convincing as wax lava" (01:39:00 Z, sweep r4 L08) | `amber-glow` | view=inside world=radial palette=code-13 material=translucent |
| IMG_8248 "also good" (01:39:57 Z, sweep r4 L10) | `teal-suspended` | view=inside world=tank palette=footage-violet-in-teal material=translucent |
| butterfly-red-yellow-mask-optional (sweep r4 B03) | `butterfly-sunburst` | view=butterfly palette=show-yellow-red projector-mask=soft |

The lamp shading is frozen at the r4 state that produced the two wax photos;
r5-r7 changed only the glass view's contact colour, the butterfly droplets
and the mask option, and the two presets render pixel-for-pixel like their
source configurations (sweep r5 P01/P02 vs r4 L08/L10).

## 8. Performance (O6N, Mali-G720, 1920x1080, harness steady p50 ms)

Frame times on Mali are not vsync-quantised (19.9 / 24.2 / 29.0 / 34.2 ms
across 1440x810 .. 1920x1080), so p50 is a direct GPU-time proxy.

| view | before (750e653) | after (r7) |
|---|---|---|
| inside, cyan-in-blue, 1920x1080 | 34.19 | 35.54 (+3.9 %) |
| glass, blue-green, 1920x1080 | 31.16 | 31.48 (+1.0 %) |
| field (any) | 16.67 (vsync) | 16.67 (vsync) |
| show (tie-dye) | 34.15 | 35.4 |
| Flowerchild | n/a | 20.4-23.5 |

Seven GPU-bound lamp configs, mean p50: before 31.56 ms, r4 32.51 ms (+3.0 %); at 1600x900 the inside view is 25.19 vs 24.18 ms (+4.2 %). The r8 micro-optimisations (one shared exp for edge glow and rim, a multiplied sheen, the 3-lobe deformation only) brought the inside view from +5.4 % to +3.9 %.

## 9. Commands

```sh
# O6N, from /home/mini/magma-test
./run-long.sh 60                              # 12 x 5 min, categories 70/15/15/0
MAGMA_CANDY_PCT=5 ./run-long.sh 60 20261004   # replayable, with the candy look opted in
./run-slice.sh logs/<runid>/slices.tsv <n>    # reproduce one slice (column 4 is the category)
./run-sweep.sh sweep-lamp.tsv <runid> 30 "25" # the fixed regression list
# ULTRA, in the worktree
python3 tools/magmasimplex-realism-gates.py gate <frames...>
python3 tools/magmasimplex-realism-gates.py calibrate refs.tsv   # path<TAB>x0,y0,x1,y1 per globe crop
```
