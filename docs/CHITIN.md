# chitin

Original near-monochrome study of an impossible carapace: lit 2.5D relief of ribs, conduits and
pores. Nothing depicts a recognizable thing. Shader `vendor/xshadertoy/glsl/chitin.glsl`, on the
xshadertoy_opts framework (docs/XSHADERTOY-OPTIONS.md). Not in the ship list until the open items
below are closed.

## Design rules and how each is met

- **2.5D relief.** A heightfield of analytic layers (rounded ridges, half-cylinder runs, pits),
  lit by one low raking light with two soft shadow taps, occlusion in grooves and pits, and a
  dry-bone to polished-gunmetal material (`sheen`).
- **Noise only warps.** Noise is used in exactly one function, `warp3`: three octaves, two
  channels each, with an analytic Jacobian so the shading normal stays exact under the warp.
  No noise adds height or colour. Gating uses integer hashes.
- **Scale hierarchy** as fractions of the screen height: rib 1/3, conduit 1/12, pore 1/60.
- **Bilateral break.** Mirrored about a slowly wandering axis, then broken by `asymmetry`
  (0.05 to 0.15): side amplitudes, conduit and pore presence, and the finest warp octave differ.
- **Palette.** Six chitin palettes (bone-charcoal, gunmetal, verdigris-iron, slate-bone,
  steel-green, charcoal-verdigris) added to `src/xst_palettes.h`; one muted accent tone on a few
  conduit runs; verdigris patina in recesses.
- **Not read as** cave, rock, intestine or anything gross: rigid cut runs with collars, straight
  tubes, no wet or coiling forms. No isolated ovals or paired recesses (they read as eyes in the
  first blind rounds), no grids of crossing ribs and runs (they read as woven panelling).

## Options and presets

`form` (nave, conduit, lathe, comb, sump), `relief`, `warp`, `asymmetry`, `conduits`, `pores`,
`sheen`, `accent`, `light`, `light-drift`, `scroll`, plus the common palette/speed/evolution/seed
set. Presets: **Nave** (arched vault of ribs on a spinal column), **Conduit** (bundled runs on
chevron ribs), **Lathe** (concentric turned ribs fanning from a hub, radial runs), **Comb**
(vertical spines with tooth rows of different lengths), **Sump** (chevrons converging downward on
a trench, dark verdigris).

## Measurements (`tools/chitin-measure.py`, build-host Radeon 780M, 1920x1080)

Run: `tools/chitin-measure.py --render build/xshadertoy_render`. Result of the final build:

| gate | result |
|---|---|
| noise octaves | 6 evaluations in `warp3` = 3 octaves x 2 channels; 0 noise calls elsewhere |
| scale, nave, warp 0 | rib 3.0, conduit 11.8, pore 60.8 cycles per screen height |
| scale, comb, warp 0 / warp 0.5 | rib 2.8 / 2.8, conduit 12.0 / 12.0, pore peak at 120 (the staggered lattice's projected harmonic) |
| asymmetry A = sum abs(L - L_mirror) / sum(L + L_mirror), light straight above | option 0.05 -> 0.056; 0.10 -> 0.093; 0.15 -> 0.128 (4 seeds each) |
| accent share (chroma over 0.015 and 45 degrees from the body hue) | 0.00 to 0.20 percent of pixels in every preset over 3 seeds x 2 times (limit 8) |
| saturated red pixels, blue-white pixels | 0, 0 |
| frame-to-frame (1/60 s) change | 0.18 to 0.32 (8-bit mean abs), no flicker |
| GPU time at 0.5 scale of 1920x1080 (quality 0 and 2, seed 4242, every preset, 60 s each) | p95 0.59 to 0.64 ms, drift 0.98 to 1.03 |

Weak-GPU (Mali) frame time was NOT measured; the 780M number is 0.6 ms at half scale.
Contact sheets: `docs/chitin/chitin-<preset>.jpg` (t = 5, 20, 45, 90, 150, 240 s, seed 4242).

## Blind test

Fresh viewers with no context read 10 unlabelled frames (5 presets and 5 random seeds) and scored
0 to 5 for biomechanical, bones, alien, cave, rock, intestines, gross, fabric, architecture,
machine, plus a face/eye/hand/genitalia check. Findings that changed the design: round-1 read
"machine/architecture"; round 3 flagged the egg-shaped vertebrae and paired square recesses as
eyes; rounds 2 and 3 read the crossing-rib grids as fabric and crates. Each was removed. Final
round: cave, rock, intestines, gross scored 0 to 2 everywhere (fail words absent); biomechanical
scored 3 on every frame, bones 3 to 4 on the comb and spine frames; no face, hand or genitalia.
One of the two final viewers also scored fabric/upholstery 3 (the rounded conduit runs read as padded bars) and biomechanical 1 to 3. The set still reads as "sci-fi hull / armoured carapace" more than as anatomy, so the biomech
criterion is met at a moderate level, not strongly.

## Open items

- Mali/O6N frame time and a live-session run.
- Not added to `ncz_ship_bins`.
