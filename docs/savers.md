# The shipped screensavers

74 screensaver programs ship in 0.8.9. This page is the index: it explains
how the catalogue is grouped and how to read a row, then links to the five
group pages that carry the per-saver detail.

| Page | Group | Count |
|---|---|---|
| [catalogue/in-house-engines.md](catalogue/in-house-engines.md) | In-house engines written for this project | 3 |
| [catalogue/in-house-shaders.md](catalogue/in-house-shaders.md) | In-house shaders | 1 |
| [catalogue/ported-classics.md](catalogue/ported-classics.md) | Ported xscreensaver-style classics | 11 |
| [catalogue/hyprsaver.md](catalogue/hyprsaver.md) | Ported hyprsaver shaders | 30 |
| [catalogue/xshadertoy.md](catalogue/xshadertoy.md) | Ported Shadertoy-style shaders | 30 |

That is 75 rows for 74 binaries: `xshadertoy_neonhorizon` is written in
house *and* ships as one of the thirty `xshadertoy_*` binaries, so it is
catalogued on the in-house page and listed again in the xshadertoy set.

## How the catalogue is grouped

| Group | What it means | Licence |
|---|---|---|
| In-house engines | A driver written for this project, plus its own shaders. Black Hole is a port of an upstream idea, heavily rewritten. | Apache-2.0, except Black Hole (MIT) |
| In-house shaders | A shader written for this project, running on the shared Shadertoy-API driver. | Apache-2.0 |
| Ported classics | xscreensaver screensavers, ported to the GLES3 engine. | xscreensaver permission notice |
| Ported hyprsaver | Fragment shaders from the hyprsaver project, ported to a GLES3 driver. | MIT |
| Ported xshadertoy | Fragment shaders from Shadertoy, ported to a GLES3 driver. | Per-shader: MIT or CC0-1.0 |

## How to read a catalogue row

Every row carries the same fields:

- **Name**: the display title the chooser shows, which is also
  `<hack>_gles3` minus the suffix.
- **Description**: one line, taken from the saver's own source header.
- **Category**: the style category from
  `assets/screensaver-chooser/category-list.tsv`.
- **Tier**: the minimum GPU class from
  `assets/screensaver-chooser/tiers.tsv` (`weak` or `mid`; see
  [`gpu-classes.md`](GPU-CLASSES.md)).
- **Options**: the saver's own options, or a pointer to them.
- **Credits**: the licence and the author, from the source header.

## Style categories

| id | Label | Savers |
|---|---|---|
| `nature` | Nature & Weather | 15 |
| `fluid` | Abstract & Fluid | 14 |
| `fractal` | Fractals & Math | 10 |
| `geometry` | Geometry & Wireframe | 9 |
| `space` | Space & Cosmos | 8 |
| `retro` | Retro, Terminal & Data | 8 |
| `neon` | Neon & Synthwave | 5 |
| `scenes` | Cities & Scenes | 5 |

## Options

Every saver accepts three common options, whatever its group:

| Option | Type | Default | Meaning |
|---|---|---|---|
| `render-scale` | float | 1 | Fraction of the output resolution the saver renders at; the compositor upscales the rest. Range 0.25 to 1. |
| `max-render-height` | int | 0 | Cap the render height in pixels, aspect ratio kept; 0 means the platform default (1080 on Mali, and on integrated Intel above 1440 lines; unlimited elsewhere). |
| `render-scale-mode` | enum | `auto` | `auto` steps the scale down (1, 0.75, 0.5, 0.35) when the 95th percentile frame time stays above 40 ms for three seconds, and never steps back up. `fixed` keeps the configured size. |

On top of that:

- **Black Hole** and **Magmasimplex** ship a full option schema, so the
  chooser and `ncz-screensaver list-options` can show every knob
  (55 options for Black Hole, 36 for Magmasimplex).
- **Fractasia** declares 12 options in the source tree
  (`assets/screensaver-chooser/options/fractasia.tsv`) and accepts them on
  its command line; the schema is not part of the installed data files, so
  a front end sees only the three common options.
- The **hyprsaver** and **xshadertoy** shader families declare their
  per-shader options inside the shader source, not in a separate file.
  Their binaries expose the three common options.
- The **ported classics** expose the three common options plus, for the
  eight shader-engine ports, `--style`.

## Screenshots

Each group page links to the saver's thumbnail under
[`screenshots/`](screenshots/), and each style category has a contact
sheet. [`screenshots/README.md`](screenshots/README.md) documents how they
are produced and what the script cannot capture.

## Licence and provenance

`docs/PROVENANCE.tsv` is the machine-readable record behind every credit
in the catalogue: one row per saver and per vendored-but-unshipped
component, with its origin, licence, attribution, source path, status and
the commit that decided it. [`credits.md`](credits.md) states the licence
policy and lists the components whose licence still needs confirming.