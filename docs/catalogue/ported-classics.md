# Catalogue: ported classics

Eleven xscreensaver screensavers, ported to run on the GLES3 engine. None
of them uses OpenGL 1: there is no immediate mode, no display list and no
`glBegin` anywhere in the port; the whole set is GLES 3.2. See
[classics-port-notes.md](classics-port-notes.md) for the port notes.

Two porting routes are in use, and they differ in how much work each saver
needed:

- **Shader-engine port** (8 savers) — the original C simulation is kept
  and the drawing is rewritten. The simulation state stays on the CPU
  exactly as upstream wrote it; the render side becomes instanced or
  fragment-shader work with its state in buffers and uniforms. Source lives
  in `src/classics_shader/`. Each of these ports carries its own
  `What this port keeps` / `What changed` comment at the top of the file.
- **Upstream GLSL** (3 savers) — the shader was already GLSL upstream, so
  it is compiled as-is against this harness with only build glue added.
  Source lives directly in `src/`.

| Name | Binary | Category | Tier | Port route | Licence | Credits |
|---|---|---|---|---|---|---|
| Cityflow | `cityflow` | Cities & Scenes | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Jamie Zawinski |
| Crackberg | `crackberg` | Nature & Weather | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Matus Telgarsky |
| Cubestorm | `cubestorm` | Geometry | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Jamie Zawinski |
| Data Towers | `datatowers` | Retro & Data | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Jamie Zawinski |
| Geodesic | `geodesic` | Geometry | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Jamie Zawinski |
| Hexstrut | `hexstrut` | Geometry | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Jamie Zawinski |
| Noof | `noof` | Abstract & Fluid | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Bill Torzewski |
| Voronoi | `voronoi` | Geometry | weak | shader-engine | xscreensaver notice + Apache-2.0 (glue) | Jamie Zawinski |
| Hypertorus | `hypertorus` | Fractals & Math | weak | upstream GLSL | xscreensaver notice | Carsten Steger |
| Klein | `klein` | Fractals & Math | weak | upstream GLSL | xscreensaver notice | Carsten Steger |
| Projectiveplane | `projectiveplane` | Fractals & Math | weak | upstream GLSL | xscreensaver notice | Carsten Steger |

## What each port kept and replaced

[classics-port-notes.md](classics-port-notes.md) has that record, one
entry per saver, taken from the header comment of each port.

## Options

All eleven take the three common render options (`render-scale`,
`max-render-height`, `render-scale-mode`).

The eight shader-engine ports additionally take `--style`:

| Value | Meaning |
|---|---|
| `classic` | The faithful xscreensaver look. This is the default. |
| `enhanced` | Anti-aliased borders, added lighting, glow and smoother palettes, each of which can also be set on its own. |
| `auto` | Picks by GPU class: `enhanced` when the GPU is not `weak`. |

The eight shader-engine ports also declare `seed` in their schemas in the
source tree (`assets/screensaver-chooser/options/<hack>.tsv`), so
`voronoi_gles3 --seed=42` and the equivalent on the other seven reproduce a
run. Those schema files are not installed, so a front end driven only by
`ncz-screensaver` sees the common render options for the classics; the
hyprsaver, xshadertoy and classic shader binaries all accept `--seed` on
the command line.

## Credits

The xscreensaver permission notice appears verbatim at the top of each
ported file. `NOTICES.md` and `debian/copyright` carry the machine-readable
version, and `docs/PROVENANCE.tsv` has one row per saver. The port glue
written for this project is Apache-2.0 and does not change the notice that
covers the upstream code.

## Screenshots

Each saver's thumbnail is under [`../screenshots/thumbs/`](../screenshots/thumbs/),
and the per-category contact sheets are listed in
[`../screenshots/README.md`](../screenshots/README.md). The classics appear
on the sheets for their own style categories: `geometry`, `nature`,
`retro`, `scenes`, `fractal` and `fluid`.
