# Catalogue: the xshadertoy set

Thirty Shadertoy-API fragment shaders on the shared driver. Each keeps its
own licence: fourteen are CC0-1.0 and fifteen are MIT; the thirtieth,
`xshadertoy_neonhorizon`, is written in house and carries an
Apache-2.0 SPDX header.

Origins and credits. The shaders are vendored under
`vendor/xshadertoy/glsl/*.glsl`, each carrying its original Shadertoy
header (`Title`, `Author`, `URL`, `Date`, `Desc`) and its own licence
block. Every row below gives the author and the licence as recorded in
that header; `docs/PROVENANCE.tsv` is the machine-readable copy and
`NOTICES.md` the human-readable one.

What the port did. `src/gles3_xshadertoy.c` is a generic Shadertoy-API
host. It loads the shader at runtime (from the build tree, then a
repo-relative path, then `/usr/share/ncz-screensavers/shaders/`), strips
any leading `#version` / `#extension` / `#pragma` / `precision` /
C-comment block, prepends the Shadertoy preamble (uniform declarations,
GLSL-1.2-to-1.3 compatibility shims and a `main()` wrapper), then compiles,
links, binds a 1x1 RGBA8 black texture to `iChannel0..3` so a shader that
samples them reads a known value instead of uninitialised driver memory,
and draws a fullscreen quad. The preamble's GLSL helpers are adapted from
upstream xscreensaver's `xshadertoy.c` and carry that project's permission
notice.

Multi-pass chaining is deliberately not implemented: all thirty shipped
shaders are single-pass.

## The thirty

| Saver | Name | Category | Tier | Credits |
|---|---|---|---|---|
| [`xshadertoy_alienbeacon`](../screenshots/thumbs/xshadertoy_alienbeacon.png) | Alienbeacon | space | weak | otaviogood, CC0-1.0 |
| [`xshadertoy_batteredplanet`](../screenshots/thumbs/xshadertoy_batteredplanet.png) | Batteredplanet | space | weak | mrange, CC0-1.0 |
| [`xshadertoy_bestill0-0`](../screenshots/thumbs/xshadertoy_bestill0-0.png) | Bestill 0.0 | fluid | weak | Matt Vianueva, MIT |
| [`xshadertoy_bestill1-0`](../screenshots/thumbs/xshadertoy_bestill1-0.png) | Bestill 1.0 | fluid | mid | Matt Vianueva, MIT |
| [`xshadertoy_bestill2-0`](../screenshots/thumbs/xshadertoy_bestill2-0.png) | Bestill 2.0 | nature | mid | Matt Vianueva, MIT |
| [`xshadertoy_bestill3-0`](../screenshots/thumbs/xshadertoy_bestill3-0.png) | Bestill 3.0 | nature | mid | Matt Vianueva, MIT |
| [`xshadertoy_bestill4-0`](../screenshots/thumbs/xshadertoy_bestill4-0.png) | Bestill 4.0 | nature | mid | Matt Vianueva, MIT |
| [`xshadertoy_bestill5-0`](../screenshots/thumbs/xshadertoy_bestill5-0.png) | Bestill 5.0 | nature | weak | Matt Vianueva, MIT |
| [`xshadertoy_darktransit`](../screenshots/thumbs/xshadertoy_darktransit.png) | Darktransit | space | weak | Matt Vianueva, MIT |
| [`xshadertoy_downfall`](../screenshots/thumbs/xshadertoy_downfall.png) | Downfall | nature | mid | Matt Vianueva, MIT |
| [`xshadertoy_elementalring`](../screenshots/thumbs/xshadertoy_elementalring.png) | Elementalring | fluid | weak | otaviogood, CC0-1.0 |
| [`xshadertoy_fluxcore`](../screenshots/thumbs/xshadertoy_fluxcore.png) | Fluxcore | scenes | mid | otaviogood, CC0-1.0 |
| [`xshadertoy_gimbalharmonics`](../screenshots/thumbs/xshadertoy_gimbalharmonics.png) | Gimbalharmonics | geometry | weak | otaviogood, CC0-1.0 |
| [`xshadertoy_goldenapollian`](../screenshots/thumbs/xshadertoy_goldenapollian.png) | Goldenapollian | fractal | weak | mrange, CC0-1.0 |
| [`xshadertoy_hexplasma`](../screenshots/thumbs/xshadertoy_hexplasma.png) | Hexplasma | neon | weak | Nemerix, MIT |
| [`xshadertoy_logarithmiccircles`](../screenshots/thumbs/xshadertoy_logarithmiccircles.png) | Logarithmiccircles | fractal | weak | mrange, CC0-1.0 |
| [`xshadertoy_neongravity-1`](../screenshots/thumbs/xshadertoy_neongravity-1.png) | Neongravity 1 | neon | weak | mrange, CC0-1.0 |
| [`xshadertoy_neonhorizon`](../screenshots/thumbs/xshadertoy_neonhorizon.png) | Neonhorizon | scenes | weak | NCZ, Apache-2.0 (in house) |
| [`xshadertoy_neontriangulator`](../screenshots/thumbs/xshadertoy_neontriangulator.png) | Neontriangulator | neon | weak | mrange, CC0-1.0 |
| [`xshadertoy_noxfire`](../screenshots/thumbs/xshadertoy_noxfire.png) | Noxfire | fluid | mid | Matt Vianueva, MIT |
| [`xshadertoy_polarnight`](../screenshots/thumbs/xshadertoy_polarnight.png) | Polarnight | nature | mid | supervitas, MIT |
| [`xshadertoy_prococean`](../screenshots/thumbs/xshadertoy_prococean.png) | Prococean | nature | weak | afl_ext, MIT |
| [`xshadertoy_protophore`](../screenshots/thumbs/xshadertoy_protophore.png) | Protophore | fractal | weak | otaviogood, CC0-1.0 |
| [`xshadertoy_rigrekt`](../screenshots/thumbs/xshadertoy_rigrekt.png) | Rigrekt | fluid | mid | Matt Vianueva, MIT |
| [`xshadertoy_skyline`](../screenshots/thumbs/xshadertoy_skyline.png) | Skyline | scenes | mid | otaviogood, CC0-1.0 |
| [`xshadertoy_starnest`](../screenshots/thumbs/xshadertoy_starnest.png) | Starnest | space | weak | Kali, MIT |
| [`xshadertoy_topologica`](../screenshots/thumbs/xshadertoy_topologica.png) | Topologica | fluid | weak | otaviogood, CC0-1.0 |
| [`xshadertoy_trainmandala`](../screenshots/thumbs/xshadertoy_trainmandala.png) | Trainmandala | fluid | weak | mrange, CC0-1.0 |
| [`xshadertoy_trizm`](../screenshots/thumbs/xshadertoy_trizm.png) | Trizm | fluid | weak | Matt Vianueva, MIT |
| [`xshadertoy_truchetzoom`](../screenshots/thumbs/xshadertoy_truchetzoom.png) | Truchetzoom | fractal | weak | mrange, CC0-1.0 |

Eleven shaders were relicensed to MIT by written permission, as recorded
in `docs/PROVENANCE.tsv`: the six `bestill*` entries, plus
`darktransit`, `downfall`, `noxfire`, `rigrekt` and `trizm` (8 March
2026), and `polarnight` (28 July 2026).

## Options

Each saver takes the three common render options (`render-scale`,
`max-render-height`, `render-scale-mode`) plus the options declared in the
shader's own `//@option` annotations, which the driver turns into a real
schema. Among those are `randomize` (default `true`), `seed`,
`palette` and `palette-cycle`.

`randomize=true` is the shipped default: every option that has a `rand`
range and that nobody set is drawn from the seed inside that range, so
each launch is a coherent but different look. `--randomize=false` gives
the native look, and `--randomize=off` is the same as false.

Seeds split into independent streams keyed by name (geometry, palette,
director, and one per randomised option), so adding an option never
changes the palette or geometry of an existing seed. `seed=0` draws a new
seed between 1 and 999999 and logs the one in use.

`palette=auto` draws a palette whose tags match the saver's family,
`palette=native` uses the shader's own declaration, and any of the 45 role
palettes in `src/xst_palettes.h` can be named. See
[`../PALETTES.md`](../PALETTES.md).

## Licence notes on individual shaders

Three shaders in this set carry embedded third-party snippets; the port
records what was decided about each:

- `xshadertoy_neongravity-1` — the original embedded a tonemapper snippet
  whose header read "License: Unknown". It was replaced with an in-house
  filmic curve. Its `mercury` snippet is dual MIT / CC-BY-NC-4.0 and the
  MIT option is used. No unknown-licence snippet remains.
- `xshadertoy_neontriangulator` — three snippets with an "License:
  Unknown" header were replaced with Inigo Quilez's MIT hashes plus the
  in-house filmic curve. No unknown-licence snippet remains.
- `xshadertoy_trainmandala` — embeds MIT (Inigo Quilez) and
  "MIT OR CC-BY-NC-4.0" (mercury) snippets; the MIT option is available
  and the shader itself is CC0-1.0.

Three further Shadertoy shaders were removed from the shipped set
earlier because they embed snippets with an unknown or non-redistributable
licence: `selfreflect`, `stardome` and `stripeytorus`. They remain in the
tree, are not installed, and are not built. `docs/PROVENANCE.tsv` records
the commit that moved each one out.

## Not shipped from this family

`docs/PROVENANCE.tsv` also records the in-house xshadertoy shaders that
are in the tree but not in the 74: `attractor`, `aurora`, `caustics`,
`caustictidepool`, `marble`, `physarum`, `ridgeline`, `wellcurve` and
`xst_common` (a shared include). They are gated out of the install set and
have no binary in a release build. The in-house `neongravity` shader is in
the same position: it is the intended in-house replacement for the
third-party ports, but it has no shipped binary, so the shipped
`neongravity` saver is the CC0 `neongravity-1` port documented above.
## The thirty in detail

[xshadertoy-sources.md](xshadertoy-sources.md) has one row per shader with
its author, its licence and its Shadertoy link.
