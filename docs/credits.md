# Credits and licences

This project is Apache-2.0, but the Apache-2.0 grant covers only the code
written for it. Every screensaver is a **separate program** — a separate
`*_gles3` binary — and every vendored or ported component keeps its own
licence and notice.

The authoritative per-component record is:

| File | What it holds |
|---|---|
| `LICENSE` | The full Apache License 2.0 text. |
| `NOTICE` | The Apache-2.0 attribution convention and the bundled-component summary. |
| `NOTICES.md` | The human-readable third-party notices: origin, licence and required attribution for every shipped saver and every built-but-unshipped one. Installed as `/usr/share/doc/ncz-screensavers/NOTICES.md`. |
| `debian/copyright` | The same information in machine-readable DEP-5, with one stanza per component. |
| [`../docs/PROVENANCE.tsv`](../docs/PROVENANCE.tsv) | One row per saver and per vendored-but-unshipped component: id, kind, origin, licence, attribution, source path, status and the commit that decided it. |

## The project's own code: Apache-2.0

Every statement agrees, and each NCZ-authored source file carries an
`SPDX-License-Identifier: Apache-2.0` header:

| Source | What it states |
|---|---|
| `LICENSE` | the Apache License 2.0 text |
| `NOTICE` | the attribution convention and a summary of the bundled components |
| `meson.build` (`project(... license: ...)`) | `Apache-2.0` |
| `debian/copyright` (`Files: *`) | `Apache-2.0`, plus a DEP-5 stanza per bundled component |
| source headers | `SPDX-License-Identifier: Apache-2.0` |

The grant covers the Wayland/EGL/GLES3 harness and drivers, the launcher,
the idle timer, the Singularity plugin, the tools and tests, and the
in-house engines and shaders.

## Component summary

| Component | Count | Origin | Licence |
|---|---|---|---|
| Black Hole | 1 | Port of `Adriwin06/black-hole`, rewritten in house | MIT |
| Magmasimplex | 1 | In house | Apache-2.0 |
| Fractasia | 1 | In house, with four of its seventeen families adapted from hyprsaver | Apache-2.0 + MIT (four families) |
| Neon Horizon | 1 | In house | Apache-2.0, with CC-BY-3.0 prior art credited |
| hyprsaver set | 30 | hyprsaver shaders by Mara Vexa | MIT |
| xshadertoy set | 29 | Shadertoy / xscreensaver 6.16 ports, per-shader authors | MIT or CC0-1.0 |
| Classics, shader-engine ports | 8 | xscreensaver hacks | xscreensaver notice + Apache-2.0 glue |
| Classics, upstream GLSL | 3 | Carsten Steger for xscreensaver | xscreensaver notice |
| Built but not shipped | 5 | In house | Apache-2.0 |

## Attribution

The exact lines a distributor must reproduce:

- **Black Hole** — based on `Adriwin06/black-hole` (MIT), © 2015 Otto
  Seiskari, © 2026 Adriwin; ported and rewritten by NCZ. Upstream licence
  text: `vendor/blackhole-LICENSE.txt`.
- **hyprsaver** — 30 shaders by Mara Vexa (MIT). Upstream licence text:
  `vendor/hyprsaver/LICENSE`.
- **xshadertoy** — Shadertoy-API shaders from xscreensaver 6.16 (commit
  `b99f621`), each retaining its own Title/Author/URL/Date/licence header.
  Authors: otaviogood, mrange, Matt Vianueva, Nemerix, afl_ext, Kali,
  supervitas. Eleven shaders were relicensed to MIT by written permission
  (eight March 2026 for the six `bestill*` entries and `darktransit`,
  `downfall`, `noxfire`, `rigrekt` and `trizm`; 28 July 2026 for
  `polarnight`).
- **Neon Horizon** — an in-house original. Its header credits the period
  prior art *Synthwave City* by Jan Mróz, 2019, CC-BY-3.0, without copying
  it. CC-BY-3.0 requires attribution, which the file header provides.
- **Classics** — xscreensaver hacks by Jamie Zawinski, Matus Telgarsky and
  Bill Torzewski (shader-engine ports), and Carsten Steger (upstream GLSL).
- **Fractasia** — in house, except the `julia_glow`, `orbit_trap`, `mobius`
  and `ship_glow` families, which are adapted from the hyprsaver shaders
  `vendor/hyprsaver/shaders/julia.frag`,
  `vendor/hyprsaver/shaders/fractaltrap.frag`,
  `vendor/hyprsaver/shaders/mobius.frag` and
  `vendor/hyprsaver/shaders/shipburn.frag` (MIT, © 2026 Mara Vexa). No Fractint source code, formula, map, parameter
  or colour-map asset is used; Fractint is credited only as the source of
  public type-name and callback metadata.
- **Magmasimplex** — in house.
- **Ephemeris, Megacity, Neonspacewar, Genxvectorcade, xshadertoy
  Leviathan** — in house, built but not shipped in 0.8.9.

## The one mixed-licence case: the classics

The classic ports are the only place where third-party and in-house source
are compiled into the same executable:

- The eight shader-engine ports (`src/classics_shader/cs_*.c`) contain
  unmodified upstream simulation code under the xscreensaver permission
  notice, with the GLES3 rendering glue written here. The xscreensaver
  notice is a permissive, MIT-style grant with no copyleft, so the combined
  binary may be distributed under Apache-2.0 while retaining the upstream
  notice.
- The three upstream-GLSL hacks (`src/hypertorus.c`, `src/klein.c`,
  `src/projectiveplane.c`) are likewise permissive and keep their Carsten
  Steger headers.

Fractasia is not mixed in the copyleft sense: it adapts four permissively
licensed (MIT) hyprsaver families, so its binary is Apache-2.0 plus MIT.

## Where the catalogue takes its credits from

Each row of each catalogue page states the licence and the author as
recorded in the source itself:

- the SPDX header of the driver in `src/`,
- the header comment of the vendored shader in `vendor/`,
- the upstream licence text kept next to it.

Where a licence could not be established from those, it is recorded as
unknown rather than guessed. See the project report for the current list
of components whose licence still needs confirming.

## Supersession record

When one saver replaces another, both rows stay in
[`../docs/PROVENANCE.tsv`](../docs/PROVENANCE.tsv) and in
[`../docs/SHADER-LEDGER.md`](../docs/SHADER-LEDGER.md): the replaced row
records `displaced-by:<id>` and the commit that decided it, the absorbed row
records `absorbed-into:<id>`. The catalogue pages list the same
displacements so nobody has to know the history to understand what ships.