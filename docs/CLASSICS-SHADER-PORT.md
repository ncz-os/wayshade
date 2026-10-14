# Classics on the shader engine

Branch `feat/classics-shader-port-2026-09-29`. The xscreensaver classics used to run through
`src/gles3_compat.c` (a GL1 immediate-mode emulation: `glBegin`, display lists, fixed-function
lighting, the client-array `glDrawArrays` wrapper). That path is CPU-bound in the driver and,
as it turned out, also wrong in places. Nine of the twelve shipped classics are now native
GLES3 programs in `src/classics_shader/cs_<hack>.c`; the other three (hypertorus, klein,
projectiveplane) were already GLSL with retained buffers upstream and are kept as they are.

Binary names, hack ids, `HACK_TABLE` symbols and packaging are unchanged (`<hack>_gles3`).
The GL1 immediate-mode builds (`<hack>_legacy_gles3`) and the `-Dlegacy-classics` option were retired
after the review round (operator decision 2026-09-29); the tables below keep their numbers as the
historical baseline. The old GL1 sources stay in the tree because the xscreensaver-shim `_demo` build
(dev only, not shipped) and other hacks still use them; see "Retiring the GL1 layer".

## How a port is built

* One source file per hack that exports the same `xscreensaver_function_table` symbol as the
  original, so `src/gles3_harness.c`, the option parser, render scale, the software-renderer
  guard, the stats output and the launcher are untouched.
* `cs_common.h`: option macros (`style`, `speed`, `seed`, `bloom`, `antialias`), a seeded RNG,
  a fixed-step clock (the originals advanced per frame at a nominal rate such as 33 or 50
  steps per second; a port runs the same steps at the same rate in real time, so motion is
  identical at 30, 60 or 144 fps), matrix helpers, shader helpers.
* `cs_post.h`: optional scene FBO, quarter-resolution glow, FXAA-lite, vignette, dither. Used
  only by the `enhanced` style; the `classic` style renders straight to the window.
* Shaders are compiled once in `init`. No immediate mode, no client arrays, no per-frame
  `glBufferData`; dynamic data goes through 3-deep rings of `glBufferSubData`.
* Plain `glDrawArrays` is never used: the compat layer overrides it with its GL1 wrapper and a
  draw with no client arrays silently does nothing. Full-screen triangles are
  `glDrawArraysInstanced(..., 1)`.
* Options are declared once, in the hack, and dumped by `<hack>_gles3 --dump-schema` into
  `assets/screensaver-chooser/options/<hack>.tsv` (same columns as `blackhole.tsv`).
  Environment prefix `NCZ_<HACK>_`, config group `<hack>`.

## Styles and GPU classes

`--style=classic|enhanced|auto`.

* `classic`: faithful port. The look of the xscreensaver original, cheap: static or instanced
  geometry, per-fragment lighting only where the original lit, no extra passes.
* `enhanced`: the same hack with anti-aliasing, softer lighting and depth cue, glow, smoother
  palettes and transitions. Every effect is individually gated (`--bloom=0` removes the glow
  passes, `--antialias=false` the edge passes).
* `auto` (default): `enhanced` on the medium class and better, `classic` on the weak class,
  using `ncz_gpu_tier` (Intel iGPUs and older mobile parts are LOW; `NCZ_GPU_TIER=low|medium|high`
  overrides). The decision is logged as `[diag] classics style=auto -> ...`.

## Performance (before and after)

Method: vsync-paced runs of 12 seconds under the exclusive host lock, no readback in the loop, numbers are the harness's own swap-to-swap statistics after the first 5 seconds (`[stats] steady`). 60 fps means the display refresh was met with the listed p95 frame interval; fps below 60 is 1000 / median. `legacy` is the retired GL1 build (measured before retirement), `classic` and `enhanced` are the ports. Raw rows: `docs/classics-shots/perf.tsv`.

Caveat: other test agents share these machines and do not all honor the lock. A run that overlapped another fullscreen window or a locked display stalls (no frame callbacks; the process ignores SIGTERM until killed); such runs were discarded and repeated.

### O6N (Radxa Orion O6N, Mali-G720, 1080p render cap)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 35 fps / p95 29.8 ms | 60 / 16.7 | 60 / 16.7 |
| gravitywell | 60 / 19.4 | 60 / 16.7 | 60 / 16.7 |
| hexstrut | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| cubestorm | 2 fps / p95 767.1 ms | 60 / 16.7 | 60 / 16.7 |
| crackberg | 14 fps / p95 83.5 ms | 60 / 16.7 | 60 / 16.7 |
| cityflow | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| noof | 60 / 16.8 | 60 / 16.7 | 60 / 16.7 |
| geodesic | 12 fps / p95 88.7 ms | 60 / 16.7 | 60 / 16.8 |
| gibson | 9 fps / p95 185.1 ms | 60 / 16.7 | 60 / 16.7 |

Not ported (upstream GLSL): hypertorus 60 / 16.7, klein 60 / 16.7, projectiveplane 60 / 16.7.

### MS-R1 (Sky1, Mali-G720, 4K panel, 1080p render cap)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 56 fps / p95 28.9 ms | 60 / 16.7 | 60 / 16.7 |
| gravitywell | 60 / 21.6 | 60 / 16.7 | 60 / 16.7 |
| hexstrut | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| cubestorm | 2 fps / p95 782.8 ms | 60 / 16.7 | 60 / 16.7 |
| crackberg | 12 fps / p95 92.4 ms | 60 / 16.7 | 60 / 16.8 |
| cityflow | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| noof | 60 / 16.8 | 60 / 16.7 | 60 / 16.7 |
| geodesic | 21 fps / p95 85.1 ms | 60 / 16.7 | 60 / 16.7 |
| gibson | 9 fps / p95 118.2 ms | 60 / 16.7 | 60 / 16.7 |

Not ported (upstream GLSL): hypertorus 60 / 16.8, klein 60 / 16.7, projectiveplane 60 / 16.7.

### build-host (Intel UHD 630, Mesa iris, 1920x1080) - the weak class

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 60 / 16.9 | 60 / 16.9 | 45 fps / p95 22.3 ms |
| gravitywell | 60 / 16.9 | 60 / 16.8 | 60 / 16.9 |
| hexstrut | 60 / 16.9 | 60 / 16.9 | 60 / 16.8 |
| cubestorm | 28 fps / p95 37.6 ms | 60 / 16.9 | 60 / 16.9 |
| crackberg | 60 / 16.9 | 60 / 16.9 | 60 / 16.9 |
| cityflow | 60 / 16.8 | 60 / 17.0 | 60 / 17.0 |
| noof | 60 / 16.9 | 60 / 16.9 | 60 / 16.9 |
| geodesic | 60 / 19.5 | 60 / 16.9 | 60 / 16.9 |
| gibson | 60 / 16.8 | 60 / 17.0 | 60 / 16.9 |

Not ported (upstream GLSL): hypertorus 60 / 16.9, klein 60 / 16.9, projectiveplane 60 / 16.9.

### build-host (AMD Radeon Pro 5500M / Navi14)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| gravitywell | 60 / 16.8 | 60 / 16.8 | 60 / 16.7 |
| hexstrut | 60 / 16.8 | 60 / 16.8 | 60 / 16.7 |
| cubestorm | n/a | 60 / 16.8 | 60 / 16.8 |
| crackberg | 45 fps / p95 25.2 ms | 60 / 16.7 | 60 / 16.7 |
| cityflow | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| noof | 60 / 16.7 | 60 / 16.8 | 60 / 16.8 |
| geodesic | 60 / 17.0 | 60 / 16.8 | 60 / 16.8 |
| gibson | 33 fps / p95 31.8 ms | 60 / 16.7 | 60 / 16.8 |

Not ported (upstream GLSL): hypertorus 60 / 16.7, klein 60 / 16.8, projectiveplane 60 / 16.8.

### build-host (same hardware as build-host)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| gravitywell | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| hexstrut | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| cubestorm | 30 fps / p95 33.6 ms | 60 / 16.7 | 60 / 16.7 |
| crackberg | 53 fps / p95 21.9 ms | 60 / 16.7 | 60 / 16.7 |
| cityflow | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| noof | 60 / 16.7 | 60 / 16.8 | 60 / 16.7 |
| geodesic | 60 / 17.1 | 60 / 16.7 | 60 / 16.7 |
| gibson | 36 fps / p95 30.3 ms | 60 / 16.7 | 60 / 16.7 |

Not ported (upstream GLSL): hypertorus 60 / 16.8, klein 60 / 16.8, projectiveplane 60 / 16.8.

### Class summary for tiers.tsv

* Weak class (Intel UHD 630): all nine ports hold 60 fps in `classic`; the GL1 cubestorm ran at 28 fps there.
  In `enhanced` everything holds 60 except voronoi (45 fps, per-pixel loop over all sites). `style=auto` therefore picks `classic` on the weak class. Suggested marks:
  weak-ok for every classic style, `enhanced` voronoi mid-class.
* Mali-G720 (O6N, MS-R1): the GL1 build was unusable for cubestorm (1.9 fps), gibson (9), geodesic (11 to 21),
  crackberg (12 to 14) and marginal for voronoi (35 to 56); every port holds 60 fps p95 <= 17.4 ms in both styles.
* Discrete AMD (build-host, build-host): everything holds 60 fps in every style.


## What changed per hack

| hack | approach | fidelity notes (intentional differences) |
|---|---|---|
| voronoi | classic: one instanced cone draw into the depth buffer (the original cone trick, one draw call instead of 25-125 `glBegin`s); enhanced: fragment shader over a site table (nearest and second nearest site) for anti-aliased borders, border glow, radial shading, sites fade in from the color of the cell they are born in | same simulation and colormap; the sites start clustered and the cells are large flat sectors until the first zoom, exactly as upstream (this is correct, not a bug). Marker size scales with render height. |
| gravitywell | height field evaluated in the vertex shader from a star table (no CPU grid); lines are triangle strips with an anti-aliased edge; foot circles as instanced loops | Upstream applies exp2 fog (density 0.005). The GL1 build in this catalog ignored fog, so the deep wells were bright; `--fog` (default none for classic, 0.5 for enhanced, 1 = upstream) exposes both looks. The far cutoff of each well is exact instead of a 16-sample linear approximation. |
| hexstrut | instanced struts, vertex shader builds the three quads of each triangle; rotator, propagation delay and colormap identical | enhanced adds edge AA, a brightness boost on turning struts and glow. |
| cubestorm | one static bevelled-cube mesh drawn instanced for every history cube (up to 1400) | per-pixel version of the GL1 light (ambient 0.2, diffuse, sharp cyan specular). enhanced: rim light, depth cue, older cubes fade. |
| crackberg | terrain generator, morphs, visibility walk and camera copied line for line; each tile is generated once into a static VBO (`cs_crackberg.c` contains upstream code, Matus Telgarsky) | the flat pale sky is `glClearColor` upstream and stays; enhanced adds a sky gradient, fog into the horizon color, fill light, glossy water, glowing lava. |
| cityflow | every tower is an instance with its own color, 800 towers (the compat build capped at 300) | fixes the flat single-color picture (see below). Light fixed to the model as upstream. enhanced: smooth (not integer-stepped) wave heights, street-level ambient occlusion, fog. |
| noof | persistent accumulation buffer that is only drawn into; simulation at the original 100 steps per second, several steps per frame | the per-frame `glCopyTexSubImage2D` of the GL1 version is gone. enhanced: anti-aliased glowing outlines and very slow trail fade so the picture does not clog. |
| geodesic | all four frequencies live in static instance buffers holding each face's corners both flat and on the sphere; the vertex shader morphs (one uniform) and builds the nine quads of a frame | mesh, solid, stellated, stellated2, random all supported; `wire` is not. enhanced: rim light, depth cue. |
| gibson | static instanced towers, faces, floor and text quads; the vertex shader places each text set on whichever tower currently owns it (uniform table), so the original random swapping is preserved | text is drawn with a built-in 5x7 bitmap font (no font dependency); upstream used a system font. Same strings, same layout logic. |
| hypertorus, klein, projectiveplane | not ported | upstream `HAVE_GLSL` code: static VBOs and a shader; 60 fps on every reference GPU (tables above). klein's close-up "walk" view mode is upstream behavior. |

## Why the GL1 build looked wrong (compat-layer findings)

* cityflow: `gles3_compat.c` keeps one material uniform per flushed batch, but cityflow calls
  `glMaterialfv` per tower inside a single `glBegin(GL_QUADS)`, so every tower took the last
  tower's color and the tops merged into one polygon.
* The compat layer stores `glLightPosition` raw (never transformed by the modelview current at
  the call), so back-lit hacks (cityflow, crackberg) got ambient only.
* crackberg: `GL_COLOR_MATERIAL` inside display lists collapses each row to one color;
  `glFrontFace` inside lists is not replayed.
* No fog, specular, `glPolygonMode`, smooth points/lines; state inside display lists.
* Every recorded batch replays as a full flush (index-buffer create/delete per QUADS): the
  reason cubestorm, gibson, geodesic and crackberg were CPU-bound.

## Copyright text for debian/copyright

```
Files: src/classics_shader/cs_voronoi.c src/classics_shader/cs_gravitywell.c
 src/classics_shader/cs_hexstrut.c src/classics_shader/cs_cubestorm.c
 src/classics_shader/cs_cityflow.c src/classics_shader/cs_geodesic.c
 src/classics_shader/cs_gibson.c
Copyright: 2003-2025 Jamie Zawinski <jwz@jwz.org>
           2026 Jason Perlow <jperlow@gmail.com> (shader-engine port)
License: xscreensaver and GPL-2+
Comment: Ports of the xscreensaver hacks of the same names; the simulation follows
 the original and keeps its permission notice. The GLES3 glue is GPL-2+.

Files: src/classics_shader/cs_crackberg.c
Copyright: 2005 Matus Telgarsky <catachresis@cmu.edu>
           2026 Jason Perlow <jperlow@gmail.com> (shader-engine port)
License: xscreensaver and GPL-2+
Comment: The terrain generator, morph state machine, visibility walk and camera are
 the unmodified upstream code; only the rendering is replaced.

Files: src/classics_shader/cs_noof.c
Copyright: 2004-2018 Bill Torzewski <billt@worksitez.com>
           (ported to xscreensaver by Jamie Zawinski, 2004)
           2026 Jason Perlow <jperlow@gmail.com> (shader-engine port)
License: xscreensaver and GPL-2+

Files: src/classics_shader/cs_common.h src/classics_shader/cs_post.h
Copyright: 2026 Jason Perlow <jperlow@gmail.com>
License: GPL-2+
```

## Gates

`--dump-schema` works without a display; every port passes the frame checks of the harness (not
black, moving, coverage). Measured lit-pixel coverage on build-host, classic / enhanced (mean over the
run): voronoi 100/100 %, crackberg 100/100, cityflow 100/100, noof 60/83, gibson 54/50, geodesic 46/48,
hexstrut 33/38, gravitywell 24/19, cubestorm 18/15 (cubestorm is already on the sparse list). The
software-renderer guard applies unchanged (verified on O6N: llvmpipe refused with exit 3 until the
compositor's EGL environment is used). No port reads back pixels in its loop, all programs are
compiled in `init`, and geometry buffers are only written through `glBufferSubData` rings.

## Review record (first round)

zoder (MiniMax-M3, no-tools, one file per prompt) reviewed every file. Findings were checked
against the source; fixed: sampler precision in the shared shader head, scissor state and FBO
completeness re-check after resize in `cs_post.h`, guarded block index and star program in
voronoi, an off-by-one segment that drew one foot-circle arc twice in gravitywell, a leaked mesh
on a failed program build in cubestorm, a degenerate-bounds divide in cityflow. Refuted (with
reason): "apex depth inverted" and "cone scaled in pixel space" in voronoi (glOrtho(0,1,1,0,-1,1)
maps the apex to the near plane and the original scales the cone to 10 in the same normalized
space), "option key aa" (the macro registers `antialias`), "NPOT textures with REPEAT and mipmaps
are incomplete" and "body VAO needs a divisor" in gibson (ES 3.0 allows NPOT mipmapped repeat
textures, and the body draws through `gl_InstanceID`), "solid mode draws 3 of 4 sub-triangles"
in geodesic (each sub-triangle is its own instance), and the dead store in the stellated morph
(it is upstream's code). State left enabled at the end of a draw call is intentional: the
harness draws one hack per process.

## Fidelity review

Contact sheets (legacy GL1 build, `classic`, `enhanced`; three timestamps each, same colormap
family; captured on build-host, Navi14, 1536x960) are in `docs/classics-shots/<hack>.jpg`. Honest
notes: in gravitywell and hexstrut the enhanced style is a refinement rather than a clear win
(softer edges, glow); voronoi, crackberg, cityflow, noof, geodesic and gibson gain most from it.
The `enhanced` voronoi costs a per-pixel loop over all sites and is therefore not a weak-class
style.

## Formal review verdicts (final round)

Operator rule: author, zoder review, fix, re-review to APPROVE. Reviewer: zoder, agent
minimax-m3-cloud, model MiniMax-M3 (no tools), one file per prompt. Dates 2026-09-29/30. A run
that timed out or produced no text was not counted. Every finding was checked against the source;
the only real one in the final round was in gravitywell (division by zero in the star-depth sum,
fixed in 6dc6fe7 by adding 1e-3 to the denominator; the re-review of the fixed file approved).
Refuted in the final round: hexstrut "rotator degrees" (get_rotation returns fractions of a turn,
the *360 is the original's), gibson "divisor 0 attribute is invalid" (divisor 0 is a per-vertex
attribute).

| File | Verdict | Final line of review |
|---|---|---|
| `cs_common.h` | APPROVE | `APPROVE` |
| `cs_post.h` | APPROVE | `APPROVE` |
| `cs_voronoi.c` | APPROVE | `APPROVE` |
| `cs_gravitywell.c` | APPROVE (after 6dc6fe7) | `APPROVE` |
| `cs_hexstrut.c` | APPROVE | `No real defects found. APPROVE` |
| `cs_cubestorm.c` | APPROVE | `APPROVE` |
| `cs_crackberg.c` | APPROVE | `No real defects found in the criteria you listed. APPROVE` |
| `cs_cityflow.c` | APPROVE | `No defects found. APPROVE` |
| `cs_noof.c` | APPROVE (two chunks, a and b) | `APPROVE` each |
| `cs_geodesic.c` | APPROVE | `APPROVE` |
| `cs_gibson.c` | APPROVE | `APPROVE` |

Notes: the whole-file noof prompt timed out twice at 900 s, so it was reviewed in two chunks;
the build-host-slot agent is not configured on build-host and codex escalation was not needed. The
first gravitywell final-round run failed (no text captured) and was re-run to completion.
The upstream Carsten Steger hacks (klein, hypertorus, projectiveplane) were only stripped of their
dead GL1 functions (`*_ff`), not re-authored; that removal is verified by `nm` (no GL1 symbols).

## Retiring the GL1 layer (version 0.6.0)

Answer to "have we retired the need for the legacy engine and GL1?": yes for everything shipped.

(a) Shipped binaries: none link the GL1 layer or gl4es. Of the 84 shipped binaries (blackhole, 35
hyprsaver, 36 xshadertoy, 12 classics) `ldd` shows only libGLESv2, libEGL, Wayland and libc/libm;
`nm -u` shows no `glBegin`, `glVertex*`, `glMatrixMode`, `glLight*`, `glu*` or `gl4es` symbols.
Before the strip, 81 of the 84 already had no GL1 references; hypertorus, klein and
projectiveplane referenced them through dead `*_ff` paths, which are now removed. `dpkg -c` of a
`meson install` DESTDIR stage contains exactly these 84 hacks and no `libgl4es`, `libGL.so` or
compat object. The deb itself is built by LEAD 1 (build-host has no debhelper).

(b) Hacks that still depended on it (all dropped from tree, package, chooser and docs): rubikblocks,
topblock, tangram, glschool, fliptext, antinspect, antspotlight, beats, blinkbox, blocktube,
bouncingcow, chompytower, crumbler, cube21, cubestack, covid19, cubenetic, cubetwist, cubicgrid,
dangerball, discoball, energystream, gears, glblur, glcells, glknots, glsnake, hextrail,
highvoltage, hilbert, hydrostat, hypnowheel, jigsaw, juggler3d, kaleidocycle, lament, lockward,
mapscroller, menger, moebiusgears, molecule, papercube, peepers, polyhedra-gl, quasicrystal,
spheremonics, splodesic, squirtorus, stonerview, timetunnel, tronbit, flyingtoasters,
geodesicgears, glforestfire, glhanoi, gltext, handsy, kallisti, photopile, providence, unicrud,
unknownpleasures, headroom, nakagin, raverhoop, razzledazzle, sballs, skulloop, skytentacles,
splitflap, starwars, winduprobot, atlantis, flurry; plus boing, companion, etruscanvenus,
romanboy, sphereeversion, wl-screenhack, gl4es_triangle, glmatrix_demo/glmatrix, the rss-sdl2
tree, and the nine `_legacy_gles3` variants of the ported classics.

(c) What retiring took (done): migrate the loader and diagnostic helpers to `ncz_gl.*` and
`ncz_hack_shim.*`; strip the dead fixed-function code from klein, hypertorus and
projectiveplane; delete `gles3_compat.{c,h}`, `xscreensaver_compat.c` (now the trimmed
`ncz_hack_shim`), `src/gl4es_include/`, `src/GL/glu.h`, the X11 stubs, the 241-file GL1 hack
source tree, `vendor/rss-sdl2-gles2-src`, `vendor/xscreensaver`, `tools/test-gluScaleImage*`,
`validation/`; remove the meson options `gl4es`, `gl4es-lib-dir`, `xscreensaver-shim`,
`legacy-classics` and the `debian/rules` flags. Old docs are kept in `docs/archive/gl1-era/`, the
legacy-vs-port contact sheets and raw captures stay as history.

Verification for the 12 classics: GL1-free by `nm -u` on each binary; clean builds on arm64 and
amd64; gate (not black, moving, coverage) PASS on O6N (13 of 13 incl. blackhole), build-host (12
classics and blackhole, p95 at most 17 ms), build-host (83 of 84; the one flake, bestill4-0, passed
twice on rerun). Four xshadertoy shaders (alienbeacon, bestill2-0, bestill4-0, polarnight) are too
slow on Mali and Intel to reach frame 60 in 5 s and report STATIC; they are shaders, not GL1.

## Next batch

The earlier proposal to port more GL hacks is withdrawn with the GL1 removal. Any future hack
starts from the shader-engine recipe (retained geometry, instancing, no fixed-function emulation).

## Packaging note

Version 0.6.0 (`meson.build`, `debian/changelog`). New sources are built by the
`classics_shader_ported` block in `meson.build`; no new runtime dependencies (shaders are
embedded), and the build no longer needs gl4es. The ship set is 84 binaries. The cix-installer
`hacks.tsv` must list exactly this set (branch `fix/screensaver-catalog-ship-set-2026-09-30`).
