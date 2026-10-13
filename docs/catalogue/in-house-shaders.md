# Catalogue: in-house shaders

One shader in this set: **Neon Horizon**. It ships as the binary
`xshadertoy_neonhorizon_gles3` because it runs on the shared
Shadertoy-API driver, but it is the only shader in that family written for
this project.

![Neon Horizon](../screenshots/thumbs/xshadertoy_neonhorizon.png)

## Neon Horizon

**`xshadertoy_neonhorizon_gles3`** — a retro-futurist night cityscape in
continuous forward flight: the 1980s synthwave idiom in the NCZ-OS 26.10
Cygnus palette. A violet-black sky; a large striped sun disc, magenta at
the base and orange at the cap, cut by horizontal bands, sitting on the
horizon as the focal point; a cyan neon grid with a magenta halo scrolling
toward the viewer and fading into the haze; two parallax rows of thin, tall
skyscraper silhouettes of uneven width and spacing, a few landmark towers
with spires, sparse dim lit windows in warm and cool tones, neon edge glow
along the rooflines, a low haze band and a handful of dim stars. Dark and
saturated: no white and no lavender wash anywhere.

| Field | Value |
|---|---|
| Category | Cities & Scenes |
| Tier | `weak` |
| Licence | Apache-2.0, from the file's own SPDX header |
| Attribution | NCZ (in-house) |
| Source | `vendor/xshadertoy/glsl/neonhorizon.glsl`, driver `src/gles3_xshadertoy.c` |
| Style category tags | `city`, `neon`, `retro-futurist`, `flight`, `synthwave` |

### Provenance

The shader's own header states it is an original work with no reference to
any external shader source, and that all technique comes from published
graphics literature: analytic plane projection (Inigo Quilez, *Modeling with
distance functions*, 2008), perspective grid via 1/z spacing, and the
standard 1980s demo-scene vocabulary.

It cites one prior artwork, *Synthwave City* by Jan Mróz, 2019, under
CC-BY-3.0, as prior art and explicitly not copied. CC-BY-3.0 permits
commercial use and requires attribution; the credit is in the shader's own
header, in `NOTICES.md` (§3.4) and in the `debian/copyright` stanza for the
in-house `vendor/xshadertoy/glsl/*.glsl` files.

### Options

The three common render options plus the options the shader declares in its
own `//@option` annotations, which the driver turns into a schema. Among
those:

| Option | Meaning |
|---|---|
| `randomize` | Default `true`: draw every unset option that has a `rand` range from the seed. `--randomize=false` gives the native look. |
| `seed` | Fixes the run so it can be replayed; 0 picks a new seed and logs it. |
| `palette` | `auto` (default), `native`, or one of the 45 role palettes in `src/xst_palettes.h`. The scene was art-directed for the Cygnus colours. |
| `palette-cycle` | Cross-fade to another palette of the family every N minutes; 0 disables it. |

See [`../architecture.md`](../architecture.md) for how seeds and the
randomize-everything mode work, and [`../PALETTES.md`](../PALETTES.md) for
the palettes.

## Neongravity: the name to be careful about

There are two different things called "neongravity" in this tree, and only
one of them is in the shipped set.

| Binary | What it is | Licence | Shipped |
|---|---|---|---|
| `xshadertoy_neongravity-1_gles3` | The CC0-1.0 port of the Shadertoy scene *Abstract gravitational well II* by mrange. This is the shipped `neongravity` saver. | CC0-1.0, mrange | Yes |
| `vendor/xshadertoy/glsl/neongravity.glsl` | An in-house shader written as the replacement for the third-party ports, with its own preset files. | Apache-2.0, NCZ | No: built but gated out of the install set, so it has no binary in a release build |

`docs/PROVENANCE.tsv` records both rows and the commit that corrected the
earlier claim that the in-house shader displaced `universeball` and
`gravitywell`. Until the in-house shader ships, the catalogue entry for
neongravity is the third-party port, and that is what the
`xshadertoy_neongravity-1` row above documents.