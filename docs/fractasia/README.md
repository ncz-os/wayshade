# Fractasia: current implementation and source scope

`fractasia_gles3` is one launcher with 13 selectable shader labels. It has
not been verified to reproduce the full Xfractint algorithm catalogue. The
previous 67/184 coverage claim was based on an inaccurate name list and is
withdrawn. The [source catalogue](COVERAGE-MATRIX.md) records 173 active
registrations in Xfractint 20.04p16, of which 107 are menu-visible. It does
not equate a similar looking image with the original recurrence.

The [gap ledger](GAP-LEDGER.md) describes the work still required. This
project may keep one chooser entry and launcher while using more than one GPU
pass, persistent textures or other rendering stages. A single fragment pass
is an implementation constraint of the current version, not a limit imposed
by the requested full catalogue.

Source: [Xfractint 20.04p16 public Linux archive](https://www.fractint.org/ftp/current/linux/xfractint-20.04p16.tar.gz), SHA-256
`4e5401cfec0e6ffa1dab38e3c177e1445772096cbe554200c512edeea23c47ad`.
The inventory uses public type names and callback metadata for comparison;
shader implementation must respect the [license analysis](LICENSE-ANALYSIS.md).

Build and test entrypoints:

- Driver: `src/fractasia/gles3_fractasia.c` and `fractasia_data.c`
- Shaders: `vendor/fractasia/glsl/*.frag`
- Picker and GPU smoke: `tests/fractasia/`
- Chooser schema: `assets/screensaver-chooser/options/fractasia.tsv`

Picker determinism, shader compilation and non-black smoke tests do not
measure Fractint algorithm parity or visual quality.


## Families absorbed from hyprsaver

`julia-glow`, `orbit-trap`, `mobius` and `ship-glow` are adapted from hyprsaver's `julia`,
`fractaltrap`, `mobius` and `shipburn` shaders (MIT License, Copyright (c) 2026 Mara Vexa;
the licence text is in `HYPRSAVER-LICENSE` and each shader header names its
source). The standalone `hyprsaver_julia`, `hyprsaver_fractaltrap` and
`hyprsaver_mobius` savers are removed from the ship set; the vendored upstream
snapshot under `vendor/hyprsaver/` is left untouched. All three run at the pace
the standalone savers had when `--speed` is at its default.

## View selection for the escape-time families

`escape-time`, `multibrot`, `burning-ship`, `tricorn` and `julia` used to draw
their whole default window every cycle. The boundary, the only part of an
escape-time picture with structure, was then a thin line between a flat far
field and the set's interior, and about one frame in five was dull (a pale
field with one blob, a grey weave). Each cycle now gets a **view**:

* The driver probes the family's default window on the CPU at 48 x 27 with
  the same float iteration the shader runs (`fract_view_probe`), at the
  iteration budget the tier will actually draw with at the cycle start (48
  on the weak class; a point that needs more is interior on screen whatever
  the mathematics says, so the gate judges the picture the tier shows),
  marks the
  cells that straddle structure (interior next to exterior, or a jump of a
  quarter or more in the escape count past the far field), and centres the
  window on a random one of them with a zoom of x1.7 to x8; three draws in
  five zoom once more onto a central boundary cell of that view (x1.4 to x4).
* The candidate must pass `fract_view_acceptable`: at least 10% boundary
  cells (and 7% within the middle 60% of the frame, so the structure is not
  all in a corner), at most 70% interior, at most 60% far field, at least 30% of the
  frame working exterior between the two, and a spread of at least 0.45 in
  log2 escape count (the old whole-set window fails: 37% hole, 36% far). Up to 16 draws are tried (under 5% need the curated fallback
  list; the unit test pins that and the cost, a brief hitch at cycle start
  even in an unoptimised build).
* The zoom then rises by x2 to x6 per minute through the cycle, clamped at
  x600, the float-precision limit at 4K. The iteration budget grows with the
  depth (+30% per doubling), capped at x2 of the tier's base on the weak
  class (48 to 96) and x2.5 above.
* `julia` draws its c within 0.006 of the Mandelbrot boundary, where Julia
  sets are connected or barely not, then frames a boundary cell of that set;
  c sways on a radius of 0.0025 so the set breathes without changing shape.
* `multibrot` draws the Julia set of z^n + c for a c found the same way on
  the multibrot set's boundary (the parameter-space set is a near-disc for
  n above 4, half hole whichever rim is framed; a random c, as before, was
  mostly dust). The exponent rides in `u_type`.

All five share `fract_escape_colour` from the preamble: palette bands in
log2(1 + n) with a small linear term, so bands survive any zoom depth; the
fastest-escaping far field is held to a dark ground; the interior is the
hole lifted only by its orbit-trap glow. The random palette generator gives
two draws in five to a Cygnus scheme (blue held high, red and green in
antiphase: magenta, violet and cyan bands); the quality gate is unchanged.
`mobius` casts a soft glow from the ribbon into the void and carries 28
bands per loop instead of 16, `plasma` is domain-warped and
contrast-stretched, `julia-glow` and `ship-glow` frame their figure larger
and keep the pixels away from it from falling to black, and `cellular`
starts one screen into the automaton so the first frame is already full.
None settles into a flat or dim frame.

## Palettes, mapping and effect recipes

Every cycle draws a **palette source** (`fract_palette_cycle`): a named
library scheme four draws in ten, a generated palette forced to the Cygnus
scheme two in ten, or a free generated harmony four in ten (itself Cygnus two
draws in five). The library (`fractasia_palette_library`, all selectable with
`--palette=NAME`): `fire`, `ice`, `aurora`, `sunset`, `ocean`, `synthwave`,
`mono` (duotone with an accent), `pastel`, `oilslick` (iridescent, many hues
per band), `gold`, `nebula`, `acid`, `film` (desaturated). Each entry carries
a *kind*, and the quality gate (`fract_palette_acceptable_kind`) is relaxed
per kind: `mono` and `film` may be low in chroma but must keep the dark low
end and the contrast, `pastel` may be brighter at the low end but must carry
colour; an accidental grey or a pale wash still fails every kind. The palette
unit test runs every library entry through its gate and pins the per-cycle
distribution: every entry reachable, library share 30..50%, Cygnus-leaning
share 30..75%.

The **mapping** (`FractMapping`, `u_map`) varies per cycle too: band period
x0.6 to x1.7, smooth or posterised to 4..9 levels (three draws in ten),
forward or reversed ramp, colour cycling at 0.01..0.04 palette units per
second (six draws in ten) and an interior tint position independent of the
exterior bands.

**Effect recipes** (`fractasia_recipes`, `u_fx` bits, `u_fxp` parameters)
are a curated table, one drawn per cycle within the GPU tier (`--style=auto`),
none (`--style=classic` or `--effect=none`), all whatever the tier
(`--style=enhanced`), or pinned (`--effect=NAME`):

| recipe | effects | tier |
|---|---|---|
| plain | bands only | weak |
| neon-edge | derivative emboss + rim light, vignette | weak |
| trap-point | orbit trap to the origin, vignette | weak |
| trap-cross | orbit trap to either axis, grain | weak |
| stripes | stripe-average colouring, vignette | weak |
| kaleido | kaleidoscope fold (3..8 sectors), vignette | weak |
| warp-drift | slow domain warp, grain | weak |
| prism | chromatic aberration along the bands, vignette, grain | weak |
| film | grain, vignette, line trap | weak |
| outline | distance-estimate outline, vignette | medium |
| glow | distance-estimate outline and glow, vignette | medium |
| trap-glow | line trap, distance estimate, glow | medium |
| kaleido-glow | kaleidoscope, distance estimate, glow, aberration | high |
| emboss-de | distance estimate, emboss, point trap | high |

The distance estimate tracks dz through the loop, which doubles its
arithmetic, so it is never drawn on the weak class in `auto`. The
kaleidoscope fold applies to the Julia families (`julia`, `multibrot`) and
`plasma`, where structure surrounds the screen centre; on the parameter-space
families the folded wedge may hold none, so the fold is dropped and the rest
of the recipe stays. The five
escape-time families honour every bit (`fract_escape_shade`); the other six
apply the post effects (vignette, grain) and `plasma` also the folds. The
GPU smoke test renders every family with no effects and with every bit on.
The driver prints one `[diag] fractasia: look palette=... recipe=... map=...`
line per cycle, which the capture tooling uses for the variety metric.

## Motion and colour

* **Hue drift** (`--hue-drift`, default 1): the whole picture's hue rotates in
  YIQ space, one turn about every four minutes at the default speed. Luma is
  unchanged, so a dark ground stays dark.
* **Palette crossfade**: with `--palette=random`, each cycle crossfades to a new
  generated palette over four seconds.
* **Every family moves.** newton, quaternion, attractor, ifs, lsystem and
  lyapunov used to show a still image for the whole cycle; each now drifts
  slowly (rotation, a zoom breath, or a parameter sway).
