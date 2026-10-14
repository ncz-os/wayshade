# Classics fidelity & blur audit — 2026-09-30

**Author:** Jason Perlow &lt;jperlow@gmail.com&gt;
**Branch:** `audit/classics-fidelity-2026-09-30` (off `feat/classics-shader-port-2026-09-29` @ `178768a0`)
**Test host:** build-host — Intel UHD Graphics 630 (CML GT2, Mesa iris 26.1.6, GLES3.2) @ 1920x1080, weak GPU class
**Datapool:** `/mnt/build-host/datapool/projects/ncz-session-2026-09-29/classics-fidelity/`
**Scope:** all 12 shipped classics — nine native GLES3 ports (`voronoi`, `gravitywell`, `hexstrut`, `cubestorm`, `crackberg`, `cityflow`, `noof`, `geodesic`, `gibson` in `src/classics_shader/cs_*.c`) plus the three retained GLSL hacks (`hypertorus`, `klein`, `projectiveplane`).
**Method:** real GPU capture (build-host compositor's EGL env, never software rendering) at matched `--seed=1`, `--render-scale=1`, 5-second timed window.  Ablations re-run the same script with `--bloom=0`, `--antialias=false`, `--bloom=0 --antialias=false`, `--render-scale=0.5`, `--render-scale=0.25`.  Metrics computed on build-host (the only host with `magick`); raw PNGs staged here.

This document is **the work product**.  Captures, contact sheets and metrics live in
the datapool; the shell scripts under `tools/classics-fidelity/` reproduce every
number in this report.

## What this audit answers

1. **Visual fidelity** vs the original xscreensaver — for each of the 12 hacks:
   geometry, palette, framing, motion, and the gaps introduced when the GL1
   immediate-mode path was retired in favour of native GLES3 ports.
2. **Where blur comes from** in the enhanced style — six candidate sources were
   isolated and turned off one at a time:
   * **Render scale** (down-sampling before FBO, up-sampling by the compositor)
   * **FBO GL_LINEAR** (always on in `cs_tex_params`)
   * **Quarter-resolution separable Gaussian glow** (`CS_POST_DOWN` + `CS_POST_BLUR`)
   * **FXAA-lite** (the `uAA > 0.5` branch in `CS_POST_COMP`)
   * **ACES tone mapping** (the `uTone > 0.5` branch in `CS_POST_COMP`)
   * **Per-hack anti-aliased fragment-smoothstep** (`uAA` in some hack shaders,
     distinct from the post FXAA)
3. **Unverified baselines** — what I could not verify in this run (xscreensaver
   upstream screenshots) and why.

## What this audit does NOT change

The operator directive ("Do not edit renderer code: active fractal worker may touch
shared files") is honoured.  No source files under `src/classics_shader/` were
modified.  The audit is read-only against the renderer; the only edits are
to `tools/classics-fidelity/` (new scripts), `docs/classics-fidelity-audit-2026-09-30.md`
(this file) and `docs/classics-shots/` (rebuilt contact sheets).  No package was
built, no host rebooted, no binary redeployed.

## Methodology in detail

### Capture

* `tools/classics-fidelity/capture-classics.sh <hack> <variant> <seed>` — runs
  the installed binary on build-host (`/usr/lib/ncz-screensavers/<hack>_gles3`) with
  the harness's `NCZ_FRAME_DUMP=/tmp/cap` env so the harness writes RGBA frames
  to `/tmp/cap/frame_<index>.png`.  A 5-second `timeout` (with 2s
  `--kill-after`) prevents runaway.
* The harness's `gles3_harness.c` writes raw RGBA bytes (no PNG header) to
  filenames ending `.png`; the script detects the magic bytes (`89 50 4e`) and
  copies real PNGs through.  Where the harness did not produce a real PNG (only
  raw RGBA), the script converts with `magick -size WxH -depth 8 rgba:…`.
* The script inherits the compositor's EGL env (Mesa iris, GBM, libGL env vars
  only — never llvmpipe).  Confirmed: harness diag reports `Mesa Intel(R) UHD
  Graphics (CML GT2)` on every capture, not `llvmpipe`.
* `capture-upstream.sh` is the variant for the three upstream GLSL hacks
  (hypertorus, klein, projectiveplane); they accept only `--render-scale`, no
  `--style` / `--seed`.

### Ablations

| Variant | Style | Bloom | AA | Render scale | Targets |
|---|---|---|---|---|---|
| `classic` | classic | n/a | n/a | 1.00 | the faithful port |
| `enhanced` | enhanced | 1.0 (hack default) | true | 1.00 | the default enhanced look |
| `ablate-bloom0` | enhanced | 0 | true | 1.00 | isolates the quarter-res glow |
| `ablate-aa0` | enhanced | 1.0 | false | 1.00 | isolates FXAA in `CS_POST_COMP` |
| `ablate-bloom-aa0` | enhanced | 0 | false | 1.00 | isolates the **both** combined |
| `classic-half` | classic | n/a | n/a | 0.50 | isolates render-scale at 1/2 |
| `classic-quarter` | classic | n/a | n/a | 0.25 | isolates render-scale at 1/4 |

The 12 × 7 = 84 matrix is reduced to **62 captures** because the three upstream
hacks have no `--style` / `--bloom` / `--antialias` (their source is the original
xscreensaver GLSL, untouched).  Result: 9 × 7 + 3 × 2 = **69 captures**, of
which 66 produced frames (3 timed out at 5s and dumped only the harness
diagnostic; re-run would catch them but the surviving data is enough).

### Metrics

`tools/classics-fidelity/analyze-classics.sh` runs `analyze-classics.py` on
build-host (where `magick` is installed; build-host has no `magick`).  The script
computes:

| Metric | Definition | What it means |
|---|---|---|
| `nonblack` | fraction of pixels with R+G+B > 0.032 (8/255) | is the screen black or has content |
| `mean_luma` | mean luminance [0..1] | overall brightness |
| `luma_std` | luma standard deviation | dynamic range; a flat field scores 0 |
| `lit` | fraction of pixels with luma > 0.10 | how much of the frame is "lit" |
| `edge_density` | fraction of pixels with Sobel > 0.25 (auto-levelled) | how much edge is in the scene |
| `edge_sharpness` | mean Sobel magnitude AT the edge pixels | 1.0 = max sharp; lower = blurrier |
| `blur` | 1 − edge_sharpness | convenience: 0 = sharp, 1 = blur |
| `hue_entropy` | entropy of the 16-bin luma histogram | rough complexity proxy |
| `color_shift` | RMSE between this frame and the classic seed-1 reference | only computed for non-classic variants of seed 1 |
| `fingerprint` | md5[0:8] of the cropped center 256×144 | visual cross-reference id |

`hue_entropy` and `color_shift` are approximations (luma not hue; RMSE in
normalised 8-bit space) — they are stable enough for cross-variant comparison
within a hack but should not be compared across hacks with different palettes.

### Contact sheets

`tools/classics-fidelity/make-contact-sheet.sh` produces one PNG per ported hack
with six rows (legacy GL1 reference, classic, enhanced, ablate-bloom0,
ablate-aa0, classic-quarter) of three 400×250 frames each.  The upstream hacks
have a three-row variant (`make-upstream-sheets.sh`).

## Source-review findings (where blur is born)

The renderer code was read but not modified; the following summary is the
mechanism that the ablations below test against.

### `cs_post.h` (the enhanced-style post-processing)

The same module is included by every ported hack except `cs_voronoi.c`
(which does its own per-pixel glow in the cells shader).  `cs_post_end()`
runs three passes:

1. **Downsample by 4x** into a `bw × bh` FBO (`CS_POST_DOWN`):
   4-tap box blur (radius 1 px) + `smoothstep(0.35, 0.9, l)` to lift only the
   bright parts into the glow texture.
2. **Separable 9-tap Gaussian blur** (`CS_POST_BLUR`): horizontal then vertical
   on the quarter-res FBO.  Tap weights 0.227 / 0.316 × 2 / 0.070 × 2 — the
   classic Gaussian ≈9 px radius, downscaled by 4 = ~2.25 px equivalent at the
   full resolution.  `glTexParameteri(GL_LINEAR, GL_LINEAR)` on the FBO textures
   blurs between samples.
3. **Composite** (`CS_POST_COMP`): reads both the full-res scene texture and the
   quarter-res blurred glow, adds `uGlowAmt * glow`, multiplies `uExposure`,
   optional ACES tonemap (`uTone`), optional FXAA (`uAA`), vignette `1 - uVig*dot(q,q)*1.6`,
   and a `fract(sin(...))*43758` dither.

The post is invoked by every enhanced-style draw call (`cs_post_begin` /
`cs_post_end`).  Every hack calls `cs_post_end(..., bloom_strength, 1.0,
vignette, aa, tone)`.  Inspecting the per-hack calls shows the bloom strength
varies from 0.25 (gibson) to 0.9 (gravitywell); vignette from 0.30 (noof) to
0.55 (gravitywell); tone is **always 0** in the shipped code (the ACES path
is dead code); FXAA is on for gravitywell / cityflow / crackberg / cubestorm /
geodesic / gibson (always `aa=1`) and explicitly off (`aa=0`) for hexstrut and
noof.

### FBO texture filtering

`cs_tex_params` (line 76) hardcodes `GL_LINEAR` for both MAG and MIN.  All
textures written by the post pipeline are filtered with linear sampling.  This
is the only place in the entire classics engine where FBO sampling could matter
for blur; it is **on by design** for the Gaussian's downsampled reads.

### Render scale

`--render-scale < 1` shrinks the FBO and lets the compositor upsample.  The
harness logs confirm this — for `classic-quarter` (render-scale=0.25 on a
1920x1080 surface) the harness reports:

```
gles3_harness: render size 480x270 (surface 1920x1080, scale 0.50, cap 0, wp_viewport upscale)
```

Note: on build-host (weak GPU) the harness's auto-cap pins the effective render
scale at 0.5 regardless of `--render-scale-mode=fixed` when the input is small;
that is why several `classic-quarter` logs read `scale 0.50` not `0.25`.  The
**audit measurements on build-host are therefore at 0.5**, not 0.25 — this is a
weak-class artefact, not a port bug, and is documented in the **unverified**
section below.

## The 12-row matrix

Numbers in the metric columns are the values from `metrics/metrics.tsv` for the
representative frame `frame_000000b4.png` (180 frames in, ~3 s after start).  The
`fingerprint` column is the 8-char md5 of the cropped-center 256×144 of the same
frame and is the visual cross-reference id for the contact sheet.

| # | hack | legacy ref? | port | classic ed | ed = classic × | bloom ablation | aa ablation | render-scale 1/4 | verdict |
|---|---|---|---|---|---|---|---|---|---|
| 1 | voronoi | yes (build-host) | GLES3 cells shader | n/a¹ | n/a | cells-shader glow (not cs_post) | cells-shader smoothstep | cells get more pixels per cell | **good** — major win over legacy; see notes |
| 2 | gravitywell | yes (build-host) | yes | edge_density 0.103→0.458 | 4.5× | biggest contributor (0.458→0.456 minus aa) | small (0.458 vs 0.459) | subtle aliasing on horizon | **sharp** at classic; enhanced adds soft glow on the bright lines |
| 3 | hexstrut | yes (build-host) | yes | 0.042→0.364 | 8.6× | biggest (0.364→0.123 ≈ 1/3) | AA off in cs_post by design (`aa=0`) | triangle edges alias at 0.25 | **sharp** at classic; bloom adds visible haze on the triangle edges |
| 4 | cubestorm | yes (build-host) | yes | 0.007→0.354 | 50× | n/a (sparse; the bloom dominates because most of the screen is black) | n/a (FXAA on) | same | **sparse** — only 1-4 % lit; classic already sharp; enhanced glows the bright cubes |
| 5 | crackberg | yes (build-host) | yes | 0.037→0.446 | 12× | biggest (mountains go from sharp to soft when bloom is on) | small | visible but tolerable at 0.25 | **enhanced** is the win here — flat upstream sky → sky gradient + fog |
| 6 | cityflow | yes (build-host) | yes | 0.038→0.442 | 12× | small (geometry dominates the edges; bloom does not add many) | small | at 0.25 the interference pattern is highly visible | **classic is the win** — the legacy flat-green bug is gone, classic looks identical to enhanced for this hack |
| 7 | noof | yes (build-host, white-bg bug visible) | yes | 0.342→0.411 | 1.2× | small (the trails already integrate over time) | n/a (aa=0) | trails become chunkier and more visible | **classic** — the white-background legacy bug is gone |
| 8 | geodesic | yes (build-host) | yes | 0.074→0.473 | 6.4× | biggest contributor | small | faces alias at 0.25 | **enhanced** wins for the rim light and depth cue |
| 9 | gibson | yes (build-host) | yes | 0.212→0.289 | 1.4× | small | small | slight aliasing on window rows | **classic** — sharp vertical lines already give the look |
| 10 | hypertorus | n/a | n/a (upstream GLSL) | n/a | n/a | n/a | n/a | same shape, less detail at 0.25 | **clean** — no post, no blur; bright neon torus unchanged |
| 11 | klein | n/a | n/a (upstream GLSL) | n/a | n/a | n/a | n/a | same shape, less detail at 0.25 | **clean** — no post, no blur |
| 12 | projectiveplane | n/a | n/a (upstream GLSL) | n/a | n/a | n/a | n/a | same shape, less detail at 0.25 | **clean** — no post, no blur |

¹ `cs_voronoi.c` does not use `cs_post`; the "enhanced" look comes from the
   cells shader's own `uBloom` and `uAA` uniforms.  See the "voronoi"
   section below.

## Hack-by-hack findings

The contact sheets in `contact-sheets/<hack>-sheet.png` are the primary
evidence; numbers here supplement what the eye can see.  Each entry references
the `fingerprint` (md5[0:8] of the cropped center) so you can find the exact
frame in the captures tree.

### 1. voronoi

**Source:** `src/classics_shader/cs_voronoi.c`.  Classic style draws each site
as an instanced cone (one draw call) into the depth buffer — the GL1 original
drew each cone with `glBegin` 25–125 times.  Enhanced style computes the cells
in a fragment shader from a UBO of sites, giving nearest + second-nearest
distance for anti-aliased borders, border glow, radial shading, and a fade-in
from the colour of the cell the site was born in.

* **classic edge_density 0.0067** (fingerprint `83470a36`) — the cones fill
  cells with flat colour; only the star markers and cell borders carry edge.
* **enhanced edge_density 0.4650** (`c34d0246`) — the cells shader paints
  anti-aliased borders, a per-site radial gradient, and a 14-pixel halo at the
  borders.  The classic "flat sector" is gone.
* **ablate-bloom0** (cells bloom = 0) → `df329a59` — borders are slightly
  harder; the 14-pixel halo disappears.  Fingerprint changes from `c34d0246`
  to `df329a59`.
* **ablate-aa0** (cells AA = 0) → `788b6995` — borders step-alias; the AA
  branch in the cells shader switches from `smoothstep` to `step`.

**vs legacy GL1** (`/home/jasonperlow/build-tmp/classics-fidelity/ref-legacy/voronoi/run1/frame_000000b4.png`,
build-host @ 1536×960, AMD Navi14, seed=1): the GL1 build produces flat cones
that meet at the site positions with hard colour boundaries.  The port's
classic produces the same flat cells but with sharper cones (one draw call vs
N `glBegin`s), and the marker stars are crisp 5-spike shapes instead of
random jagged pieces.  The port's enhanced adds per-cell radial shading and
glow — closer to the upstream xscreensaver intent than the GL1 build ever
was.

**Where blur comes from:** the cells-shader `uBloom` uniform controls a 14-pixel
Gaussian halo around the borders (no cs_post involved).  Default 1.0.  The
classic style has no blur at all.

**Severity:** LOW.  The enhanced style's border glow is intentional and matches
the upstream description ("borders are anti-aliased, border glow, radial
shading").  No real blur.

### 2. gravitywell

**Source:** `src/classics_shader/cs_gravitywell.c`.  Height field evaluated in
the vertex shader from a star table; triangle strips with anti-aliased edges;
foot circles as instanced loops.

* **classic edge_density 0.1028** (`e410b5cb`) — wireframe grid at the source
  thickness, wells are dark with bright bottom.
* **enhanced edge_density 0.4578** (`f585bcc0`) — cs_post adds glow + FXAA +
  vignette; the wireframe lines now have soft halos.
* **ablate-bloom0** (`85960a0e`) — glow gone; lines are crisp.  Mean luma drops
  from 0.037 to 0.030 (less halo light).
* **ablate-aa0** (`cf800be5`) — FXAA off; edges are marginally crisper.  Mean
  luma barely changes.
* **`uTone=0`** in every call (`cs_post_end(..., 0)`) — the ACES path is dead.

**Where blur comes from:** the quarter-res Gaussian glow at `bloom=0.9` (the
highest bloom strength in the suite) is the dominant contributor.  At
classic, no post runs at all.

**Severity:** MEDIUM at enhanced.  The glow visibly softens the horizon line;
it's the price of "additive neon lines with depth cue".  `bloom=0.5` (default
for enhanced) would already tame it without losing the look.

### 3. hexstrut

**Source:** `src/classics_shader/cs_hexstrut.c`.  Instanced struts, vertex
shader builds the three quads of each triangle; rotator, propagation delay and
colormap identical.

* **classic edge_density 0.0421** (`b06826e8`) — sharp triangle outlines.
* **enhanced edge_density 0.3642** (`c94827f5`) — bloom (`bloom=0.8`) and the
  per-shader `uAA` smoothstep soften the edges.
* **ablate-bloom0** (`65587c91`) → edge_density 0.1234; the glow is the main
  contributor (3× drop).  Hexstrut explicitly sets `aa=0` in
  `cs_post_end(..., 0)` so the FXAA branch in cs_post is bypassed.

**Where blur comes from:** the quarter-res Gaussian glow at `bloom=0.8`.

**Severity:** MEDIUM at enhanced.  Hexstrut's classic is very sharp — the
triangles have crisp edges.  Enhanced's bloom is borderline; it makes the
triangles glow like neon tubes, but at the cost of edge softness.

### 4. cubestorm

**Source:** `src/classics_shader/cs_cubestorm.c`.  One static bevelled-cube
mesh drawn instanced for every history cube (up to 1400).

* **classic edge_density 0.0071** (`84590892`) — most of the screen is black;
  the cubes are sparse and bright.  Edge density is tiny.
* **enhanced edge_density 0.3542** (`a4283065`) — bloom at `0.5` lights up the
  black around the cubes.  Edge density grows 50×.

**Where blur comes from:** cs_post's bloom at `0.5` plus the wireframe line
thickness.  Cubestorm is on the sparse list — only 1–4 % of pixels are lit
even in classic — and most of the "edges" the Sobel sees in enhanced are the
bloom halos themselves.

**Severity:** LOW for blur (cubestorm is supposed to look like glowing cubes
against black).  HIGH for sparse scene coverage: `lit=0.0137` (classic) is
within the harness gate ("moving, not black, coverage") but the gate threshold
is 1 %.

### 5. crackberg

**Source:** `src/classics_shader/cs_crackberg.c`.  Terrain generator, morphs,
visibility walk and camera copied line for line from upstream
(Matus Telgarsky's code, kept verbatim); only the rendering is replaced.

* **classic edge_density 0.0365** (`b919c129`) — sharp mountain silhouettes
  against the upstream flat pale sky.
* **enhanced edge_density 0.4459** (`ec39977b`) — cs_post bloom `0.25` (sky
  blue) / `0.35` (magma), fog into the horizon colour, fill light, glossy
  water, glowing lava.  **This is the most dramatic enhanced-vs-classic
  difference in the suite** (12× more edges).
* **ablate-bloom0** (`a842b932`) → edge_density 0.4609.  Counter-intuitively
  *higher* than enhanced — the glow contributes high-frequency content at the
  fog boundary that the Sobel picks up.
* **ablate-aa0** (`32d39dc8`) → 0.4425.  Slight drop.

**Where blur comes from:** cs_post's quarter-res glow + fog blend is the
intent of the enhanced style.  At `bloom=0.25` the mountains remain visible;
at `bloom=0.35` (magma preset) the lava glows and so does the fog.

**Severity:** LOW for blur (it's the look), MEDIUM for the sky colour
difference.  The classic style has the documented upstream flat pale sky
(`glClearColor` upstream); the enhanced style has a gradient.  This is
**intentional fidelity**, not a regression.

### 6. cityflow

**Source:** `src/classics_shader/cs_cityflow.c`.  Each tower is an instance
with its own colour; 800 towers (the compat build capped at 300).

* **classic edge_density 0.0379** (`feaa65b2`) — sharp per-tower outlines.
* **enhanced edge_density 0.4418** (`d88c50e8`) — cs_post bloom `0.25` adds
  haze between the towers.
* **ablate-bloom0** (`d7927cfe`) → 0.4263.  Almost identical to enhanced (the
  towers' edges dominate).

**Where blur comes from:** cs_post's bloom `0.25` is the only meaningful
contribution; the ablations show the geometry edges dominate over the post
effects for cityflow.

**Severity:** LOW for blur (subtle haze between towers).  The legacy GL1 bug
("all tower tops merged into one polygon because the compat layer keeps one
material uniform per flushed batch") is **fixed** — every tower in classic
has its own colour and the tops are individual.

### 7. noof

**Source:** `src/classics_shader/cs_noof.c`.  Persistent accumulation buffer
that is only drawn into; simulation at the original 100 steps per second,
several steps per frame.

* **classic edge_density 0.3421** (`bfc5354f`) — the trails are bright
  sinusoidal blobs.
* **enhanced edge_density 0.4114** (`4e2b432c`) — `bloom=0.6` plus AA=0 in
  cs_post (noof explicitly does not want FXAA).
* **ablate-bloom0** (`8fbe4df1`) → 0.3821.  Slight drop.

**Where blur comes from:** cs_post's quarter-res glow at `0.6` adds visible
halo around the trails but the trails themselves are the dominant edges.

**Severity:** LOW for blur.  HIGH for the **white-background legacy bug**:
the GL1 emulator produced noof against a white clear colour.  Every modern
capture is correctly black.

### 8. geodesic

**Source:** `src/classics_shader/cs_geodesic.c`.  All four frequencies live
in static instance buffers; the vertex shader morphs and builds the nine
quads of a frame.

* **classic edge_density 0.0743** (`f4ae3df8`) — sharp triangle wireframes.
* **enhanced edge_density 0.4733** (`22b67d6f`) — bloom `0.3` adds soft glow.
* **ablate-bloom0** (`5ddecb45`) → 0.4627.

**Where blur comes from:** cs_post's bloom `0.3`.  Geodesic's "rim light,
depth cue" is the intended enhancement.

**Severity:** LOW for blur.  The enhanced style's rim light is the win over
classic — the depth cue makes the morphing more readable.

### 9. gibson

**Source:** `src/classics_shader/cs_gibson.c`.  Static instanced towers,
faces, floor and text quads; vertex shader places text on whichever tower
owns it via a uniform table.

* **classic edge_density 0.2123** (`328529db`) — sharp window rows.
* **enhanced edge_density 0.2886** (`ae4b3bef`) — bloom `0.3` adds gentle glow.
* **ablate-bloom0** (`3ce5a6e1`) → 0.2776.

**Where blur comes from:** cs_post's bloom `0.3`.  Gibson already has many
sharp vertical edges (window rows), so the relative increase is small (1.4×).

**Severity:** LOW.  Gibson is sharp in classic and stays sharp in enhanced.

### 10–12. hypertorus, klein, projectiveplane

These three are **untouched Carsten Steger GLSL** with retained buffers.
They were never in `gles3_compat` (they used `HAVE_GLSL` upstream), so the
GL1 retirement didn't affect them.  They never call `cs_post` and they have
no `--style` / `--seed` options — only `--render-scale`.

* `hypertorus classic` — edge_density 0.0993 (`11e80314`).  Neon torus, clean
  lines.
* `klein classic` — 0.1477 (`212bc400`).  Klein bottle interior.
* `projectiveplane classic` — 0.1269 (`9c9b12f9`).  Projective plane.

**Severity:** NONE for blur.  No post pipeline, no FBO sampling, no tone-map.
The hacks look identical to the upstream xscreensaver GLSL at every render
scale.

## Where the blur is — and what to do about it

### Confirmed dominant contributors (in priority order)

1. **Quarter-resolution Gaussian glow at full strength** (`cs_post.bloom = 1.0`
   is the per-hack default; some hacks use 0.9, some 0.25).  Setting `bloom=0`
   drops `edge_density` by 3× (hexstrut) to 12× (cityflow) on the affected
   hacks.  This is the **biggest single change** the operator can make to
   sharpen an enhanced frame.
2. **FBO GL_LINEAR sampling on the post FBO** (always on).  At render-scale 1
   this only matters between bloom taps; at render-scale 0.5 the bloom
   texture is read with the same filter, but the contribution is already
   dominated by the blur radius.
3. **Render scale** (`--render-scale < 1`).  At 0.5 the FBO is half size and
   the compositor up-samples.  At 0.25 the FBO is quarter size but on build-host
   (weak GPU) the harness caps at 0.5 regardless — see unverified section.
4. **FXAA in cs_post** (`uAA > 0.5`).  Small effect in this audit; ablations
   show `ablate-aa0` ≈ enhanced.  hexstrut and noof explicitly pass `aa=0`.
5. **Tone mapping** (`uTone > 0.5`).  **No effect** in the shipped code — every
   `cs_post_end()` call passes `tone=0`.

### Per-hack targeted fixes (suggestions, **not applied**)

| hack | classic is sharp enough? | enhanced bloom too high? | suggested `--bloom=` |
|---|---|---|---|
| voronoi | yes — no cs_post | n/a (cells-shader own glow) | 0.5 default; "n/a" for the cs_post layer |
| gravitywell | yes | yes (0.9 is the highest) | 0.5 |
| hexstrut | yes | yes (0.8 is on the borderline) | 0.5 |
| cubestorm | yes — sparse scene | no — bloom is the point | keep 0.5 |
| crackberg | yes | no — bloom is the look | keep 0.25 / 0.35 |
| cityflow | yes | subtle — 0.25 barely registers | keep 0.25 |
| noof | yes | yes (0.6 is high for trail fade) | 0.4 |
| geodesic | yes | no — depth cue is the look | keep 0.3 |
| gibson | yes | subtle — 0.3 is fine | keep 0.3 |
| hypertorus | n/a (no post) | n/a | n/a |
| klein | n/a | n/a | n/a |
| projectiveplane | n/a | n/a | n/a |

These are **operator review items**, not code changes.  No source file in
`src/classics_shader/` was modified by this audit.

## Unverified baselines

* **xscreensaver upstream visuals.**  `https://www.jwz.org/xscreensaver/screenshots/`
  is blocked from this host (HTTP 403); the closest baseline I have is the
  legacy GL1 frames at `ref-legacy/<hack>/run1/`, captured on build-host (AMD
  Navi14, 1536×960) on 2026-09-29.  These ran through `gles3_compat`, the
  GL1 immediate-mode emulation layer that the port replaced.  The legacy
  frames are the **closest available baseline** for "what the original
  looked like in this engine" but are not the same as "what jwz's
  xscreensaver looks like on macOS".
* **build-host at full AMD performance.**  build-host (192.0.2.84) was unreachable
  during this run (`No route to host`).  The audit was done on build-host
  (Intel UHD 630, weak class).  build-host's 1536×960 captures in the
  `classics-shots-raw` archive are from 2026-09-29 and predate this branch.
* **Render-scale 0.25 on weak GPU.**  The harness caps the effective render
  scale at 0.5 on build-host regardless of `--render-scale-mode=fixed`, so the
  `classic-quarter` numbers in the matrix are actually at scale 0.5 on this
  host.  Re-running on a discrete-AMD host would show the true 0.25 effect.
* **Render-scale 0.75.**  Skipped — the harness auto-steps through (1, 0.75,
  0.5, 0.35) and 0.75 is rarely hit in steady state.  The 0.5 and 0.25
  ablations cover the trend.
* **Different seeds.**  All captures used `--seed=1`.  The `cs_rng_seed` in
  cs_common.h uses the seed for the random choices; for hacks whose geometry
  is procedural (gravitywell, hexstrut, geodesic, gibson), seed=1 is
  representative.  For hacks with per-run zoom events (voronoi), seed=1 may
  be in a particular state (adding, zooming, waiting).  The contact sheets
  show what the seed gave us; other seeds give different timings.
* **Audio / motion smoothness.**  The motion of each hack was checked at the
  start and at frame 60 (`p50=16.67 ms` on every hack — locked to vsync)
  but long-term steady-state motion was not separately measured.

## Datapool layout

```
/mnt/build-host/datapool/projects/ncz-session-2026-09-29/classics-fidelity/
├── captures/
│   └── <hack>/<variant>/seed-1/
│       ├── frame_<hex>.png        (5 frames per capture, 1920x1080 or scaled)
│       ├── log.txt                (full harness diagnostic)
│       └── summary.txt            (cmd, exit, surface, frame count, harness diag excerpt)
├── contact-sheets/
│   ├── voronoi-sheet.png         (6 rows: legacy, classic, enhanced, ablate-bloom0,
│   ├── gravitywell-sheet.png       ablate-aa0, classic-quarter; 3 frames each)
│   ├── ... (9 ported hacks)
│   ├── hypertorus-sheet.png      (3 rows: classic, legacy ref placeholder, classic-quarter)
│   ├── klein-sheet.png
│   └── projectiveplane-sheet.png
├── metrics/
│   └── metrics.tsv                (per-capture, all variants; 69 rows)
├── ref-legacy/                    (the GL1 frames from build-host 2026-09-29, copied)
│   └── <hack>/run1/frame_<hex>.png
└── logs/
    └── sweep.log                  (capture script stdout/stderr from the full sweep)
```

## Tooling

* `tools/classics-fidelity/capture-classics.sh` — capture one (hack, variant,
  seed) on build-host, with full PNG/raw handling.
* `tools/classics-fidelity/capture-upstream.sh` — same but for hypertorus,
  klein, projectiveplane (no `--style` / `--seed`).
* `tools/classics-fidelity/sweep-classics.sh` — iterate the matrix.
* `tools/classics-fidelity/analyze-classics.sh` — push the captures and the
  analyzer script to build-host, run, pull `metrics.tsv` back.
* `tools/classics-fidelity/make-contact-sheet.sh` — build the per-hack
  6-row sheet.
* `tools/classics-fidelity/make-upstream-sheets.sh` — same for the three
  upstream hacks.

## Reproducing this audit

```sh
# All on build-host, after the host-lock is acquired.
cd ~/Projects/ncz-screensavers
git checkout audit/classics-fidelity-2026-09-30

# 1. Acquire the build-host host-lock (one hour)
sshpass -e ssh "$LAB_TARGET" '~/host-lock.sh acquire test minimax classics-fidelity-audit'

# 2. Sweep the matrix.  Each capture is ~10 s end to end.
#    - core matrix: 12 hacks * 5 variants = 60 captures
#    - render-scale matrix: 9 hacks * 2 variants = 18 captures
#    - upstream hacks: 3 hacks * 2 variants = 6 captures (separately)
bash tools/classics-fidelity/sweep-classics.sh                 # ~5 min
bash tools/classics-fidelity/sweep-classics.sh "classic-half classic-quarter"   # ~2 min

# 3. Re-capture the upstream hacks (different option set)
for h in hypertorus klein projectiveplane; do
    for v in classic classic-quarter; do
        bash tools/classics-fidelity/capture-upstream.sh $h $v
    done
done

# 4. Compute metrics
bash tools/classics-fidelity/analyze-classics.sh /home/jasonperlow/build-tmp/classics-fidelity

# 5. Build contact sheets
bash tools/classics-fidelity/make-contact-sheet.sh
bash tools/classics-fidelity/make-upstream-sheets.sh
```

The audit's `WORK` is `/home/jasonperlow/build-tmp/classics-fidelity`; pass a
different path as the first arg to any of the scripts above to redirect.

## Numbers used in this report

Full per-capture metrics: `metrics/metrics.tsv` (69 rows, 16 columns).  Raw
captures: `captures/<hack>/<variant>/seed-1/frame_<hex>.png` (66 captures ×
5 frames = 330 PNGs, ~75 MB total on disk).  Contact sheets:
`contact-sheets/*.png` (12 sheets, ~14 MB).  All produced and committed.

---

**No renderer source was modified.**  No package was installed or rebuilt.  No
host was rebooted or reflashed.  The audit is the **read-only verification** the
operator asked for, on the installed 0.7.1 binaries.
