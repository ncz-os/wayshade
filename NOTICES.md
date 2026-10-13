# Third-party notices and licences — ncz-screensavers

This file records the origin, licence and required attribution of every
screensaver and shader distributed with ncz-screensavers 0.8.9 (the 74-entry
ship set) and of the projects built but not shipped. It is generated from the
file headers and the git history, not from memory; the per-item rows are in
`docs/PROVENANCE.tsv` and the supersession record in
`docs/SHADER-LEDGER.md`.

This file is installed as `/usr/share/doc/ncz-screensavers/NOTICES.md`.

## 1. Project licence — Apache-2.0

The project is licensed under the **Apache License, Version 2.0**. The
0.8.7 audit recorded a conflict; it is now resolved, and every statement
agrees:

| source | states |
|---|---|
| `LICENSE` at the repo root | the full Apache License 2.0 text |
| `NOTICE` at the repo root | the Apache-2.0 attribution convention and bundled-component summary |
| `meson.build` (`project(... license: ...)`) | `Apache-2.0` |
| `debian/copyright` (`Files: *`) | `Apache-2.0`, plus DEP-5 stanzas for each bundled component |
| `README.md` | "The project is Apache-2.0 … each screensaver is a separate program …" |
| NCZ-authored source files | `SPDX-License-Identifier: Apache-2.0` header |

The Apache-2.0 grant covers **only the original NCZ-authored code**: the
Wayland/EGL/GLES3 harness and drivers, the launcher, the idle timer, the
Singularity plugin, the development tools and tests, and the in-house engines
(Fractasia, Magmasimplex, Neonhorizon, Neonspacewar, Ephemeris, Megacity and
Genxvectorcade), plus the Black Hole port/rewrite glue. Every vendored or
ported component keeps its own licence and notice, listed below.

Each screensaver is a **separate program** (a separate `*_gles3` binary in
`/usr/lib/ncz-screensavers`); no third-party shader or hack is linked into an
Apache-2.0-claimed binary. The upstream material is compiled into its own
per-hack executable together with the Apache-2.0 glue, exactly as the
upstream licences permit. See section 6 for the one mixed-licence case.

## 2. Component licence summary

| component | count | origin | licence |
|---|---|---|---|
| Black Hole | 1 | port of Adriwin06/black-hole, rewritten in-house | MIT |
| Magmasimplex | 1 | original in-house | Apache-2.0 |
| Fractasia | 1 | original in-house; 4 of 17 families adapted from hyprsaver | Apache-2.0 + MIT (4 families) |
| hyprsaver | 30 | shaders by Mara Vexa | MIT |
| xshadertoy (Shadertoy / xscreensaver 6.16 ports) | 29 | per-shader authors | MIT or CC0-1.0 |
| xshadertoy (in-house) | 1 | neonhorizon, clean-room original | Apache-2.0; CC-BY-3.0 prior art credited |
| Classics (shader-engine ports) | 8 | xscreensaver hacks | xscreensaver permission notice (MIT-like) + Apache-2.0 glue |
| Classics (upstream GLSL) | 3 | Carsten Steger for xscreensaver | xscreensaver permission notice (MIT-like) |
| Built but not shipped | 5 | original in-house (4) + Shadertoy-API original (1) | Apache-2.0 |

Total shipped: 74 (`blackhole` + `magmasimplex` + `fractasia` + 30 hyprsaver +
30 xshadertoy + 11 classics).

## 3. Required licence texts

### 3.1 MIT (Black Hole upstream, hyprsaver, and the MIT-licensed xshadertoy ports)

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- Black Hole: `Copyright (c) 2015 Otto Seiskari`, `Copyright (c) 2026 Adriwin`
  (full text in `vendor/blackhole-LICENSE.txt`). The CC-BY-NC Milky Way texture
  named in upstream `COPYRIGHT.md` is **not** used: the port replaces it with a
  procedural sky.
- hyprsaver: `Copyright (c) 2026 Mara Vexa` (full text in
  `vendor/hyprsaver/LICENSE`); the individual `.frag` files carry no header, so
  the directory `LICENSE` is the attribution.
- xshadertoy MIT ports retain their per-file header (Title/Author/URL/Date and
  the "Relicensed as MIT License, by permission" line).

### 3.2 CC0-1.0 (xshadertoy ports by otaviogood and mrange)

CC0 1.0 Universal (Public Domain Dedication):
<https://creativecommons.org/publicdomain/zero/1.0/legalcode>.
Full text also at `/usr/share/common-licenses/CC0-1.0` on Debian.

### 3.3 xscreensaver permission notice (classics)

```
Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.  No representations are made about the suitability of this
software for any purpose.  It is provided "as is" without express or
implied warranty.
```

This notice is reproduced in every classic port's file header. Authors:
- Jamie Zawinski `<jwz@jwz.org>`: voronoi port, hexstrut port, cubestorm port,
  cityflow port, geodesic port, gibson (shipped as `datatowers`) port,
  gravitywell port (built, not shipped);
- Matus Telgarsky: crackberg port;
- Bill Torzewski: noof port;
- Carsten Steger `<carsten@mirsanmir.org>`: the upstream GLSL hacks
  `src/hypertorus.c`, `src/klein.c`, `src/projectiveplane.c`, and the shared
  `src/glsl-utils.c`;
- Jamie Zawinski: `src/rotator.c`, `src/yarandom.c` (vendored helpers).

### 3.4 CC-BY-3.0 (prior art credited by neonhorizon)

`neonhorizon.glsl` is an in-house, clean-room original. Its header records the
period prior art *Synthwave City* by Jan Mróz (jaszunio15), 2019, licensed
CC BY 3.0. CC BY 3.0 permits commercial use and requires attribution, which the
file header provides: <https://creativecommons.org/licenses/by/3.0/legalcode>.

### 3.5 Apache-2.0 (in-house code)

Full text in the repository `LICENSE` and at
`/usr/share/common-licenses/Apache-2.0`. The `NOTICE` file carries the
attribution convention.

## 4. Previously flagged licences — status in 0.8.9

The 0.8.7 audit flagged seven items; five are resolved, two concern only
unshipped files and remain as described.

1. **`xshadertoy_neontriangulator_gles3` — RESOLVED, still shipped.** The file
   declares `CC0: Neon triangulator` (author mrange) and embedded three
   snippets marked `License: Unknown`: a Matt Taylor tonemapper and two
   uncredited hash functions. In 0.8.9 the two hashes are the **Inigo Quilez
   MIT** hash routines (iquilezles.org states all its technical snippets are
   MIT) and the tonemapper is an **in-house filmic tone curve** (the standard
   ACES-style approximation, written for this port). No unknown-licence snippet
   remains; the CC0 grant of the shader itself is unchanged.
2. **`xshadertoy_neongravity-1_gles3` — RESOLVED, still shipped.** Same story:
   the Matt Taylor `License: Unknown` tonemapper is replaced with the same
   in-house filmic curve. The mercury `mod1` snippet is dual
   `MIT OR CC-BY-NC-4.0` and this distribution relies on the **MIT** option
   (the header now says so). No unknown-licence or non-commercial-only code
   remains.
3. **`stardome` — not shipped.** Declares CC0 but embeds `License: Unknown`
   snippets **and** an explicit `License: CC BY-NC-SA 3.0` snippet
   (Stephane Cuillerdier / Aiekick, `stardome.glsl`). A CC BY-NC-SA component
   cannot be shipped in a product distributed with the OS. Correctly excluded;
   do not re-add without clear-licensing it.
4. **`selfreflect`, `stripeytorus` — not shipped.** Declare CC0 but embed
   `License: Unknown` snippets. Correctly excluded.
5. **`trainmandala_gles3` — shipped, cleared.** Its mercury snippet was dual
   `MIT OR CC-BY-NC-4.0`; the MIT option is available and is the one this
   distribution relies on, and the header now states that. No action required.
6. **In-house originals with no licence line in the file header.**
   `cellmosaic.glsl`, `chitin.glsl`, `iridescence.glsl` and `stratus.glsl`
   state "Original work" but carry no third-party header; in 0.8.9 they carry
   `SPDX-License-Identifier: Apache-2.0`. None of the four is shipped.
   `neonhorizon.glsl` is shipped and also now carries the SPDX header.
7. **`debian/copyright` attribution — corrected.** The old file misattributed
   `src/hypertorus.c`, `src/klein.c` and `src/projectiveplane.c` to Jamie
   Zawinski; the headers show Carsten Steger (2003–2026). The DEP-5 rewrite
   records the correct attribution and no longer calls the project licence
   undecided.

`vendor/xshadertoy/PORTED.md` remains stale (it describes a 38-shader
inventory and lists now-removed shaders); it is a development document and is
not installed. `docs/xshadertoy-blur-audit-2026-09-30.md` already records the
conflict.

## 5. Attribution lines

- **Black Hole** — based on Adriwin06/black-hole (MIT), © 2015 Otto Seiskari,
  © 2026 Adriwin; ported and rewritten by NCZ.
- **hyprsaver** — 30 shaders by Mara Vexa (MIT).
- **xshadertoy** — Shadertoy-API shaders from xscreensaver 6.16 (commit
  `b99f621`), each retaining its Title/Author/URL/Date/licence header; authors
  include otaviogood, mrange, Matt Vianueva, Nemerix, afl_ext, Kali and
  supervitas. `neonhorizon` is an in-house clean-room original (CC-BY-3.0 prior
  art credited, §3.4).
- **Classics** — xscreensaver hacks by Jamie Zawinski, Matus Telgarsky, Bill
  Torzewski (shader-engine ports) and Carsten Steger (upstream GLSL).
- **Fractasia** — original in-house; the `julia_glow`, `orbit_trap`, `mobius`
  and `ship_glow` families are adapted from hyprsaver `julia.frag`,
  `fractaltrap.frag`, `mobius.frag` and `shipburn.frag` (MIT, © 2026 Mara
  Vexa). No Fractint source code, `.frm` formula, `.map`/`.par`/`.ifs` asset or
  colour map is used; Fractint is credited only as the source of public
  type-name/callback metadata (`docs/fractasia/LICENSE-ANALYSIS.md`).
- **Magmasimplex** — original in-house.
- **Ephemeris, Megacity, Neonspacewar, Genxvectorcade, Xshadertoy Leviathan** —
  original in-house (leviathan declares MIT in its header); built but not
  shipped.

## 6. The one mixed-licence case: the classics

The classic ports are the only place where third-party and NCZ source are
compiled into the same executable, and this is why the project licence is
recorded as Apache-2.0 with the xscreensaver notice preserved rather than
relicensed:

- The eight shader-engine ports (`src/classics_shader/cs_*.c`) contain
  unmodified upstream simulation code under the xscreensaver permission
  notice, with the GLES3 rendering glue written here. The xscreensaver notice
  is a permissive MIT-style grant (no copyleft), so the combined binary may be
  distributed under Apache-2.0 while retaining the upstream notice. Unlike the
  retired GPL-2.0-or-later declaration, there is no licence conflict.
- The three upstream GLSL hacks (`src/hypertorus.c`, `src/klein.c`,
  `src/projectiveplane.c`) are likewise permissively licensed and keep their
  Carsten Steger headers.
- Fractasia does not derive from Fractint source; it adapts four MIT hyprsaver
  families, so the Fractasia binary is Apache-2.0 + MIT.
