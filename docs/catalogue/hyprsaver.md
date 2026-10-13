# Catalogue: the hyprsaver set

Thirty fragment shaders from the hyprsaver project, ported to a GLES3
driver. Every one of them is MIT licensed and credited to Mara Vexa.

Origins and credits. The shaders are vendored unchanged under
`vendor/hyprsaver/shaders/*.frag`; the upstream licence text is kept at
`vendor/hyprsaver/LICENSE` and repeated in `NOTICES.md`. Three file names
differ from the catalogue id, and the mapping is `hyprsaver_ids` in
`meson.build`: `matrix.frag` is `hyprsaver_digitalrain`, `stonks.frag` is
`hyprsaver_ticker` and `tesla.frag` is `hyprsaver_arccoil`.

What the port did. Upstream compiles each shader inside a Rust runtime that
splits the source into a `#version`/`precision` header and a body, then
prepends a generated preamble: uniform declarations, a `palette(float t)`
helper, and a `main()` wrapper calling `_hyprsaver_main()` that multiplies
`fragColor` by `u_alpha`. That `prepare_shader()` is ported verbatim into
`src/gles3_hyprsaver.c`, so the vendored `.frag` files are byte-for-byte
upstream. One deliberate difference: palette injection is scoped to a baked
lookup table instead of upstream's image and gradient palette system. The
reasons are in the header comment of `src/gles3_hyprsaver.c` and in
`PORTED.md`.

## The thirty

| Saver | Name | Category | Tier |
[`hyprsaver_arccoil`](../screenshots/thumbs/hyprsaver_arccoil.png) | neon | weak |
[`hyprsaver_attitude`](../screenshots/thumbs/hyprsaver_attitude.png) | retro | weak |
[`hyprsaver_aurora`](../screenshots/thumbs/hyprsaver_aurora.png) | nature | weak |
[`hyprsaver_bezier`](../screenshots/thumbs/hyprsaver_bezier.png) | fractal | weak |
[`hyprsaver_blob`](../screenshots/thumbs/hyprsaver_blob.png) | fluid | weak |
[`hyprsaver_caustics`](../screenshots/thumbs/hyprsaver_caustics.png) | nature | weak |
[`hyprsaver_circuit`](../screenshots/thumbs/hyprsaver_circuit.png) | retro | weak |
[`hyprsaver_clouds`](../screenshots/thumbs/hyprsaver_clouds.png) | nature | weak |
[`hyprsaver_digitalrain`](../screenshots/thumbs/hyprsaver_digitalrain.png) | retro | weak |
[`hyprsaver_donut`](../screenshots/thumbs/hyprsaver_donut.png) | geometry | weak |
[`hyprsaver_fireflies`](../screenshots/thumbs/hyprsaver_fireflies.png) | nature | weak |
[`hyprsaver_flames`](../screenshots/thumbs/hyprsaver_flames.png) | nature | weak |
[`hyprsaver_geometry`](../screenshots/thumbs/hyprsaver_geometry.png) | geometry | weak |
[`hyprsaver_hypercube`](../screenshots/thumbs/hyprsaver_hypercube.png) | geometry | weak |
[`hyprsaver_kaleidoscope`](../screenshots/thumbs/hyprsaver_kaleidoscope.png) | fluid | weak |
[`hyprsaver_lissajous`](../screenshots/thumbs/hyprsaver_lissajous.png) | fractal | weak |
[`hyprsaver_marble`](../screenshots/thumbs/hyprsaver_marble.png) | fluid | weak |
[`hyprsaver_oscilloscope`](../screenshots/thumbs/hyprsaver_oscilloscope.png) | retro | weak |
[`hyprsaver_planet`](../screenshots/thumbs/hyprsaver_planet.png) | space | weak |
[`hyprsaver_plasma`](../screenshots/thumbs/hyprsaver_plasma.png) | fluid | weak |
[`hyprsaver_snowfall`](../screenshots/thumbs/hyprsaver_snowfall.png) | nature | weak |
[`hyprsaver_sonar`](../screenshots/thumbs/hyprsaver_sonar.png) | retro | weak |
[`hyprsaver_starfield`](../screenshots/thumbs/hyprsaver_starfield.png) | space | weak |
[`hyprsaver_temple`](../screenshots/thumbs/hyprsaver_temple.png) | scenes | weak |
[`hyprsaver_terminal`](../screenshots/thumbs/hyprsaver_terminal.png) | retro | weak |
[`hyprsaver_ticker`](../screenshots/thumbs/hyprsaver_ticker.png) | retro | weak |
[`hyprsaver_tunnel`](../screenshots/thumbs/hyprsaver_tunnel.png) | neon | weak |
[`hyprsaver_voronoi`](../screenshots/thumbs/hyprsaver_voronoi.png) | geometry | weak |
[`hyprsaver_waterfall`](../screenshots/thumbs/hyprsaver_waterfall.png) | nature | weak |
[`hyprsaver_wormhole`](../screenshots/thumbs/hyprsaver_wormhole.png) | space | weak |

Credits for all thirty: **Mara Vexa**, MIT. See
[`../credits.md`](../credits.md).

## What each one draws

| Saver | What it draws (its own header comment) |
|---|---|
| `hyprsaver_arccoil` | Tesla coil / electric arc screensaver. |
| `hyprsaver_attitude` | Aviation artificial-horizon gauge rendered as a circular instrument on a black field, in three concentric zones. |
| `hyprsaver_aurora` | Overhead aurora borealis, the viewer looking straight up at the sky; three curtain bands drift organically via FBM domain warping. |
| `hyprsaver_bezier` | Six animated cubic Bézier curves rendered simultaneously. |
| `hyprsaver_blob` | Lit blob with flowing energy emission and atmospheric halo. |
| `hyprsaver_caustics` | Underwater caustic light patterns, the rippling light you see on the floor of a pool. |
| `hyprsaver_circuit` | Hex-offset grid with hash-gated traces and gradient pulses, reading as a PCB or circuit network; fragment-native, one cell-neighbourhood per pixel. |
| `hyprsaver_clouds` | Slowly drifting procedural clouds over a tinted sky, using plain value noise. |
| `hyprsaver_digitalrain` | Classic Matrix digital rain: vertical columns of a procedural 5x5 bitmask. |
| `hyprsaver_donut` | Ray-marched torus scene. |
| `hyprsaver_fireflies` | Warm glowing wanderers drifting across a dark field, grid-based spatial lookup. |
| `hyprsaver_flames` | Single-layer fire, foreground only: finest detail, fastest movement. |
| `hyprsaver_geometry` | Wireframe polyhedron screensaver, inspired by the Windows 3D Flower Box. |
| `hyprsaver_hypercube` | Rotating 4D hypercube (tesseract) projected to a 2D wireframe. |
| `hyprsaver_kaleidoscope` | N-fold (N = 6) kaleidoscope driven by domain-warped FBM value noise. |
| `hyprsaver_lissajous` | Three overlapping Lissajous curves with frequency ratios 3:2, 5:4 and 7:4. |
| `hyprsaver_marble` | Curl-noise flow field; the velocity field is divergence-free. |
| `hyprsaver_oscilloscope` | Realistic Tektronix/HP-style CRT oscilloscope display with three animated traces. |
| `hyprsaver_planet` | Ray-marched unit sphere with aurora borealis bands wrapping latitude lines. |
| `hyprsaver_plasma` | Classic plasma effect: overlapping sine waves in screen space, time and depth. |
| `hyprsaver_snowfall` | Grid-based spatial-lookup snowfall in three parallax layers (near, mid, far). |
| `hyprsaver_sonar` | Static sonar-scope backdrop plus a rotating sweep and blip contacts. |
| `hyprsaver_starfield` | Hyperspace warp starfield. |
| `hyprsaver_temple` | Retro temple interior with floor, ceiling and scrolling pillars; flat-plane perspective, screen-space rectangles for the pillars, triangle waves throughout and no raymarching. |
| `hyprsaver_terminal` | Scrolling terminal / build-log output effect, with rows of monospaced text. |
| `hyprsaver_ticker` | Procedural candlestick chart with a MACD oscillator overlay. |
| `hyprsaver_tunnel` | Infinite tunnel flythrough; the camera moves forward along the Z axis while the walls stream past. |
| `hyprsaver_voronoi` | Animated Voronoi cells whose sites move along Lissajous-like paths. |
| `hyprsaver_waterfall` | Stylised 2D waterfall with a retro quantise-and-dither post. |
| `hyprsaver_wormhole` | Curved wormhole tunnel: a 3D raymarcher flying through a static curved axis. |

## Options

Every saver in this set takes the three common render options and nothing
else. `hyprsaver_aurora_gles3 --list-options` prints three rows:

| Option | Type | Default | Range |
|---|---|---|---|
| `render-scale` | float | 1 | 0.25 to 1 |
| `max-render-height` | int | 0 | 0 to 8640 |
| `render-scale-mode` | enum | `auto` | `auto`, `fixed` |

The look is whatever the shader draws: the palette lives inside each
vendored `.frag` and is resolved by the injected `palette()` helper, so
there is no per-saver palette option in this set. See
[`../PALETTES.md`](../PALETTES.md).

These shaders derive their animation from the clock, not from a seed
option, so two runs of the same saver differ.

## Not shipped from the same project

`docs/PROVENANCE.tsv` records the other vendored hyprsaver shaders and why
they are not in the 74:

| Shader | Status | Reason |
|---|---|---|
| `vendor/hyprsaver/shaders/gridwave.frag` | displaced | Replaced in 0.8.2 by the in-house `neonhorizon` look. |
| `vendor/hyprsaver/shaders/fractaltrap.frag` | absorbed | Adapted as the Fractasia `orbit_trap` family. |
| `vendor/hyprsaver/shaders/julia.frag` | absorbed | Adapted as the Fractasia `julia_glow` family. |
| `vendor/hyprsaver/shaders/mobius.frag` | absorbed | Adapted as the Fractasia `mobius` family. |
| `vendor/hyprsaver/shaders/shipburn.frag` | absorbed | Adapted as the Fractasia `ship_glow` family. |

The four absorbed families are why the Fractasia licence reads "Apache-2.0
(original) + MIT (4 adapted)": the attribution follows the code.