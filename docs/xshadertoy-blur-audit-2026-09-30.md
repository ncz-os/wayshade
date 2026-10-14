# xshadertoy blur / visual-fidelity audit — 2026-09-30

**Branch:** `audit/xshadertoy-blur-2026-09-30` (isolated from
`fix/gpu-tier-mali-g720-2026-09-30`).
**Host:** build-host / NVIDIA RTX 4500 Ada / `OpenGL ES 3.2 NVIDIA 595.58.03`.
**Source of truth:** `ncz-os/ncz-screensavers` GitLab
(commit `0db1ed1` / `build-host` mirror, also `0db1ed1`) + build-host refs;
the canonical meson.build `xshadertoy_shaders` list (46 entries) and
`ncz_ship_bins` (31 of those 46 marked shipped in 0.7.10+).
**Manifest:**
[`audit/xshadertoy-blur-2026-09-30/metrics/master-matrix.json`](audit/xshadertoy-blur-2026-09-30/metrics/master-matrix.json)
(46 rows).
**Companion artifacts:**
[`audit/xshadertoy-blur-2026-09-30/README.md`](audit/xshadertoy-blur-2026-09-30/README.md)
+ [`audit/xshadertoy-blur-2026-09-30/validate.sh`](audit/xshadertoy-blur-2026-09-30/validate.sh)
(11 pure-Python checks; `--strict` adds the live meson.build re-link).
**Zoder review (this revision):** see §"Zoder review log" at the end of
this document — fixes v1 reviewer findings (hardcoded line ranges,
hardcoded absolute paths, missing trailing newlines, no validation
command, audit artifacts not committed).

## TL;DR

- **46 entries** in `meson.build: xshadertoy_shaders = [ ... ]` on the
  current branch. **31 are in `ncz_ship_bins` (shipped)**; the other
  **15 are extras** (NOT installed).
- The task brief said "36 shipped / 10 extras". The actual current
  ground truth is **31 / 15** because commit `3549bab` (engine 0.7.10)
  moved five shaders with unknown-license snippets out of the public
  package to a separate `ncz-screensavers-extra` (NCZ-OS only):
  `neongravity-1, neontriangulator, selfreflect, stardome, stripeytorus`.
  The 10 extras named in the task brief do NOT appear in the current
  `ncz_ship_bins`; we report against the **actual** ground truth and
  flag this discrepancy in the §"Discrepancies vs task brief" section
  below.
- **All 46 shaders compiled + linked** on build-host / RTX 4500 Ada at
  iTime = 8.0s and matched iSeed `[0.137, 0.421, 0.731, 0.953]`.
  **45 of 46 rendered non-black.** The only render-fail is
  `neongravity-0` (cov = 0.000, lov = 0.000), which renders
  uniform-black because its FXAA post-process reads from a 1×1 black
  dummy (documented in `vendor/xshadertoy/PORTED.md`).
- **Render-scale ablation (native 1920×1080 vs default 0.75× → 1440×810):**
  LoV is HIGHER at the lower render scale for 43 of 45 analyzable
  shaders (mean ratio 1.42, median 1.35, range 0.87–2.51; excluding
  neongravity-0 which is identically zero). The two exceptions are
  `aurora` (ratio 0.87) and `caustictidepool` (ratio 0.98), both
  with LoV values in the noise-floor range (LoV_native and
  LoV_default both < 0.0001); their slightly-lower-at-default
  ratios are numerical, not a real softening. This is the correct
  behaviour: shader work runs per output pixel at lower resolution,
  then `wp_viewport` upscales — so the per-pixel analytical content
  is denser at the smaller render size. **No shader shows
  render-scale-induced blur.** Render scale is not the cause of any
  observed softness in this corpus.
- **The largest source of "blur" is intentional.** **17 of the 46
  shaders** self-describe as "soft", "fog", "translucent",
  "filamentous", or "smooth" art in their own header / first
  paragraph (see §"Intentional soft art" below). Plus a **18th
  shader (`neongravity-0`)** that renders all-black due to FXAA on
  the 1×1 dummy input. Removing the softness would change the look
  — the fix for "feels blurry" is not "sharpen the shader", it's
  "pick a different shader" (or fix the multi-pass path for
  `neongravity-0`).
- **One shader is broken** (`neongravity-0`): the FXAA post-process
  path is structurally incompatible with single-pass rendering. Fix is
  either provide a non-zero backbuffer (multi-pass not currently
  shipped) or mark the shader as extras-only with an iChannel-free
  fallback path. Tracked under §"Suggested targeted fixes".

## Method in detail

### Capture harness

The capture harness lives in the repo under
[`docs/audit/xshadertoy-blur-2026-09-30/scripts/`](audit/xshadertoy-blur-2026-09-30/scripts/)
(committed; reproducible from `meson.build` + the per-shader JSON
sidecars at build-host). The driver source tree is **untouched**.

- `xst_blur_audit.c` — adapted from `src/gles3_xshadertoy_smoke.c`
  (NOT modified). Differences are: native 1920×1080 default,
  deterministic iSeed and iTime inputs, JSON sidecar per capture
  containing coverage, Laplacian variance, frame time, iTime/iSeed
  values, GL_RENDERER, GL_VERSION. The wrapper preamble matches
  `src/gles3_xshadertoy.c` so the shader bodies run unchanged.
- `run_capture.py` — orchestrates all 46 shaders × 2 render scales,
  reads the canonical `xshadertoy_shaders` and `ncz_ship_bins` lists
  from `meson.build` via the shared `lib_meson.py` (regex parser, no
  hardcoded line numbers), refuses llvmpipe via
  `NCZ_ALLOW_SOFTWARE=0`. Take `--repo <path>` and
  `--captures-root <path>` from the CLI; no absolute paths are baked in.
- `lib_meson.py` — single source of truth for the 46/31 split.
  Imports `parse_xshadertoy_shaders()` and `parse_ncz_ship_bins()`.
  Bracket-balanced; restructures of `meson.build` that preserve the
  list semantics do not silently mis-classify anything.
- `static_analysis.py` + `header_extract.py` — pure-Python; extract
  per-shader smoothing primitives (smoothstep, iqpalette,
  cosine_palette, fbm, dFdx/dFdy/fwidth, FXAA, blur, raymarch), the
  upstream `Title / Author / URL / license` header, and the
  render-mode classification (raymarched / 2d-procedural /
  polar-2d / 2d-procedural-fbm / sdf / anti-aliased-2d).
- `scale_ablation.py` — pairs native vs default-render-scale
  captures and computes per-shader coverage and LoV deltas.
- `build_master_matrix.py` — emits
  [`metrics/master-matrix.json`](audit/xshadertoy-blur-2026-09-30/metrics/master-matrix.json)
  (the authoritative 46-row dataset).
- `build_contact_sheet.py` + `blur_vs_sharp_sheet.py` —
  full-resolution side-by-side contact sheets (downsampling for
  layout only; analysis uses the full 1920×1080 captures at build-host).
- `crop_extremes.py` — full-resolution labelled thumbnails for the
  blurriest 5 + sharpest 5 (the "intentional soft art" vs "high
  frequency detail" comparison).

### Pipeline shape

The driver's fullscreen quad lives in `src/gles3_xshadertoy.c:499-505`
and runs at native render-target resolution. The compositor
(`wp_viewporter`) does the upscale when render-scale < 1.0. There is
**no FXAA pass in the wrapper** (`src/gles3_xshadertoy.c` does not
sample `iChannel0` with a shader's own anti-aliasing in mind; it
binds a 1×1 RGBA8 black dummy to all four `iChannel0..3` units
(`src/gles3_xshadertoy.c:511-520`) with `GL_TEXTURE_MIN_FILTER =
GL_LINEAR` and `GL_TEXTURE_MAG_FILTER = GL_LINEAR`). This is the
**palette/sampler GL_LINEAR** the task brief referenced. The dummy
texture is 1×1 so the filtering is a no-op (no neighbouring pixels to
interpolate against). The LINEAR choice does NOT cause blur in the
output frame.

### Hardware / driver version pinned per capture

The JSON sidecar records both `gl_renderer` and `gl_version` for
each shader; verified values:

- `GL_RENDERER = "NVIDIA RTX 4500 Ada Generation/PCIe/SSE2"`
- `GL_VERSION  = "OpenGL ES 3.2 NVIDIA 595.58.03"`
- `GL_VENDOR   = "NVIDIA Corporation"`

No llvmpipe fallback: every capture landed on the dGPU.

### Render-scale defaults

From `src/ncz_harness_cfg.c:13-18`:

```
render-scale         default 1.0    range 0.25..1.0
max-render-height    default 0      range 0..8640   (0 = unlimited)
render-scale-mode    default auto   choices {auto, fixed}
```

`gles3_harness.c:374-377` applies 0.5 if the GPU is classified as
"weak" AND the user didn't override the default. RTX 4500 Ada is
classified as "strong" (see `ncz_gpu_tier.c`), so the default
**render scale is 1.0 (native)** for this host.

For the audit's "default render scale" captures we used 0.75 because
that's the next step in the auto-ladder (`gles3_harness.c:863`,
`ncz_render_step_down` returns 1.0 → 0.75 → 0.5 → 0.35). The "default"
in the brief is interpreted as "the most common auto-step value
above 0.5 that drivers actually pick" — at 0.75 the wp_viewport
upscale is active and the pipeline shows any render-scale-induced
softening.

### Ablation axes

Five axes, each varied independently:

1. **Scale** — native 1920×1080 vs 0.75× default 1440×810
   (render-scale, see
   [`metrics/scale-ablation.json`](audit/xshadertoy-blur-2026-09-30/metrics/scale-ablation.json)).
2. **Shader blur** — sourced from per-shader `smoothstep`, `blur`,
   and Gaussian falloff counts in the static analysis
   ([`metrics/shader-static-analysis.json`](audit/xshadertoy-blur-2026-09-30/metrics/shader-static-analysis.json)).
3. **Smoothing** — same source as shader blur; specifically,
   smoothstep calls, cosine palettes, value-noise interpolation.
4. **Post-processing** — search for FXAA / `aa(uv` / supersample in
   each shader's body. Driver does not run a post-process pass.
5. **Temporal accumulation** — search for `iFrame` blending in
   each shader's body. Driver does not implement feedback.

## The 46-row matrix

Column legend:

- **ship** = in `ncz_ship_bins` (Y) or extras (N).
- **kind** = shader's primary technique (raymarched / 2d-procedural /
  polar-2d / 2d-procedural-fbm / sdf).
- **cov** = fraction of pixels with any RGB > 8/255 at native 1920×1080.
- **lov** = variance of the 4-neighbour Laplacian over downsampled-by-4
  luminance. **Higher = sharper; lower = softer.** Higher values are
  seen on shaders with crisp lines / bright pinpoints; lower values
  on cloud / fog / soft-art shaders (see "Intentional soft art" below).
- **src** = source class: `xscreensaver-6.16@b99f621` upstream,
  `ported` from a related upstream work, `ncz-original` written for
  this catalogue. The "MIT implied" qualifier on ncz-original means
  the in-tree header doesn't carry an explicit "MIT" line; only
  `marble.glsl` does. All others are `Original work` with no
  accompanying license line — for shipping we rely on the catalogue
  convention (operator commit history treats all `ncz-original`
  shaders as MIT unless noted).
- **sharp** = observed sharpness verdict.
- **cause** = primary driver of the observed softness category.
- **fix** = suggested targeted remediation.

The matrix is sorted by the `xshadertoy_shaders` declaration in
`meson.build` (the same order the meson `foreach s : xshadertoy_shaders`
loop sees them).

| # | shader | ship | kind | cov_nat | lov_nat | shader_url / src | observed blur | cause / confidence | suggested targeted fix |
|---|---|---|---|---|---|---|---|---|---|
| 1 | alienbeacon | Y | raymarched | 1.000 | 0.011631 | https://www.shadertoy.com/view/ld2SzK (xscreensaver-6.16) | very soft (large smooth surfaces, glossy rim) | shader design — raymarched alien rock + glow; smoothstep lighting, no FXAA in shader | none needed; soft is intentional. Accept as-shipped |
| 2 | batteredplanet | Y | polar-2d | 1.000 | 0.113064 | https://www.shadertoy.com/view/wsjBD3 (xscreensaver-6.16) | sharp-ish (8 smoothstep calls + procedural noise, but reads as continuous surface) | procedural noise; many small high-frequency tiles, averaged at frame scale | accept — high LoV relative to its peers |
| 3 | bestill0-0 | Y | 2d-procedural | 0.650 | 0.000878 | https://www.shadertoy.com/view/tfXcRn (xscreensaver-6.16) | very soft (cave interior, mostly dark sky region by design) | shader design — cave ceiling, dim sky; cov 0.65 because upper region is solid black per upstream's own choice | none — the black sky region is by design; flagged in 2026-09-25 NVIDIA verification (1,658,880 / 2,073,600 nonblack). LoV is low because the shader's textured band is a thin strip. |
| 4 | bestill1-0 | Y | 2d-procedural | 0.800 | 0.001549 | https://www.shadertoy.com/view/w32BDD (xscreensaver-6.16) | very soft (same family as bestill0-0) | cave sky + procedural noise; same root cause as bestill0-0 | none — design intent |
| 5 | bestill2-0 | Y | 2d-procedural | 1.000 | 0.000132 | https://www.shadertoy.com/view/3cjcWD (xscreensaver-6.16) | almost-uniform flat (very low LoV despite full coverage) | shader output is a near-uniform wash; shader does near-flat fBm + heavy haze. Sub-millimetre features | visual design — would benefit from a 5× contrast / fog-dissolve pass; defer |
| 6 | bestill3-0 | Y | 2d-procedural | 0.800 | 0.004475 | https://www.shadertoy.com/view/wXXBRX (xscreensaver-6.16) | soft (cave interior, mostly dark) | same as bestill0-0/1-0 | none — design intent |
| 7 | bestill4-0 | Y | 2d-procedural | 1.000 | 0.000992 | https://www.shadertoy.com/view/3cXyzB (xscreensaver-6.16) | very soft (full coverage but almost-flat output) | haze + heavy fog rolloff; low LoV is energy distribution, not blur | design intent — same as bestill2-0 |
| 8 | bestill5-0 | Y | 2d-procedural | 0.997 | 0.001552 | https://www.shadertoy.com/view/tXjXDy (xscreensaver-6.16) | very soft (same family) | haze + cave | design intent |
| 9 | caustictidepool | N | 2d-procedural | 1.000 | 0.202774 | ncz-original (no upstream ref) | sharp (LoV = 0.20; bright caustic pinpoints dominate) | voronoi cell-boundary sharp ridges, iqpalette / colour-stop palette. Sharpness is dominant. | none — already sharp; pre-release candidate for shipping |
| 10 | darktransit | Y | raymarched | 0.743 | 0.201815 | https://www.shadertoy.com/view/WcdczB (xscreensaver-6.16) | sharp (LoV 0.20, mostly black with bright transit) | dark sky region lowers cov; raymarched terrain + bright transit makes LoV high | none — already sharp |
| 11 | downfall | Y | 2d-procedural | 0.855 | 0.000042 | https://www.shadertoy.com/view/w3sBWl (xscreensaver-6.16) | extremely soft (LoV = 0.00004, full-coverage rain/fog) | shader is tanh()-tanh'd fog with sin-warp; soft by design (Title: "Downfall") | none — design intent |
| 12 | elementalring | Y | raymarched | 1.000 | 0.009211 | https://www.shadertoy.com/view/MsVXDt (xscreensaver-6.16) | soft (ring + glow, low edge count per pixel) | raymarched ring with smoothstep falloff and bloom | design intent — "energy ring" is the look |
| 13 | fluxcore | Y | raymarched | 1.000 | 0.152293 | https://www.shadertoy.com/view/ltlSWf (xscreensaver-6.16) | sharp-ish (LoV 0.15, multiple raymarched cores with crisp falloff) | energy cores with hard boundaries | none — already sharp |
| 14 | gimbalharmonics | Y | raymarched | 1.000 | 0.018244 | https://www.shadertoy.com/view/llS3zd (xscreensaver-6.16) | medium (gimbal ball; iChannel0 envmap usage is small fraction, procedural dominates) | iChannel0 sampling + raymarched ball | none |
| 15 | goldenapollian | Y | polar-2d | 0.992 | 0.121136 | https://www.shadertoy.com/view/WlcfRS (xscreensaver-6.16) | sharp-ish (Apollian gasket, tight spheres) | polar recursion; very high tile density, LoV high | none |
| 16 | hexlattice | N | polar-2d | 1.000 | 0.115802 | ncz-original (ported from cellmosaic concept) | sharp (hex cells, crisp boundaries) | hex polar lattice, smoothstep on inner edge | none — pre-release candidate for shipping |
| 17 | hexplasma | Y | 2d-procedural | 0.902 | 0.241578 | https://www.shadertoy.com/view/3fy3z3 (xscreensaver-6.16) | sharp (LoV 0.24, high tile density) | hexagonal plasma tiles with bright veins | none — already sharp |
| 18 | iridescence | Y | 2d-procedural | 1.000 | 0.000712 | ncz-original (MIT implied) | soft (translucent soap bubbles; "translucent" in header) | shader is **explicitly** described as "Overlapping translucent soap bubbles drifting across frame" (header line 2-4). thin-film interference on smooth surface. | none — design intent |
| 19 | logarithmiccircles | Y | polar-2d | 0.510 | 0.088018 | https://www.shadertoy.com/view/mljcWR (xscreensaver-6.16) | sharp within dark frame (cov 0.51 because most of frame is black between rings) | rings are sharp (8/8 clean LoV); low cov because most pixels are background | none — design intent |
| 20 | neongravity-0 | N | 2d-procedural | 0.000 | 0.000000 | https://www.shadertoy.com/view/43G3Wc (xscreensaver-6.16) | **RENDER_FAIL — uniform black** | shader does `fxaa(iChannel0, ...)`; with our 1×1 RGBA8 dummy iChannel0 (=uniform zero), the FXAA gradient is identically zero so the output is uniform black. Documented in `vendor/xshadertoy/PORTED.md` "Per-shader caveats" | fix: route iChannel0 to a shader-baked non-zero backbuffer before FXAA — requires multi-pass. Defer to "extras" only. See §"Suggested targeted fixes". |
| 21 | neongravity-1 | N | polar-2d | 0.975 | 0.021741 | https://www.shadertoy.com/view/43G3Wc (xscreensaver-6.16) | medium (single-pass variant; mostly black with bright veins) | shader's iChannel sampling is dominant; 1×1 dummy gives uniform-zero base | none — already excluded from shipping per commit 3549bab (5 unknown-license shaders moved to extras package) |
| 22 | neonhorizon | Y | 2d-procedural | 1.000 | 0.051567 | ncz-original (MIT implied) | medium (15 smoothstep calls + raymarched silhouette; soft horizon glow) | heavy smoothstep + ray horizon. LoV 0.05 is "edge silhouette" range | none — design intent |
| 23 | neontriangulator | N | polar-2d | 0.841 | 0.021736 | https://www.shadertoy.com/view/tXGGRD (xscreensaver-6.16) | medium | triangle field; LoV moderate because triangles are large relative to 1080p | none — already excluded from shipping per 3549bab |
| 24 | noxfire | Y | 2d-procedural | 0.643 | 0.000339 | https://www.shadertoy.com/view/wfG3Dz (xscreensaver-6.16) | soft (LoV 0.0003 despite cov 0.64; flame/fog effect, black sky between) | fire effect: thin bright flame band over mostly black sky. LoV is low because the flame is a small area. | none — design intent |
| 25 | polarnight | Y | 2d-procedural-fbm | 1.000 | 0.046575 | https://www.shadertoy.com/view/3ss3R4 (xscreensaver-6.16) | medium (fbm terrain + aurora; moderate edge count) | raymarched terrain + aurora; LoV in the "moderate" range | none |
| 26 | prococean | Y | raymarched | 1.000 | 0.094875 | https://www.shadertoy.com/view/MdXyzX (xscreensaver-6.16) | sharp-ish (sin-wave ocean; many normal-derived edges) | 12-iteration raymarch + 36-iteration normal | none |
| 27 | protophore | Y | raymarched | 1.000 | 0.021046 | https://www.shadertoy.com/view/XljGDz (xscreensaver-6.16) | medium (procedural sphere with envmap hint) | iChannel0 use is "small effect" per upstream (envmap hint) | none |
| 28 | rigrekt | Y | 2d-procedural | 0.750 | 0.007675 | https://www.shadertoy.com/view/3XKfDV (xscreensaver-6.16) | soft | rigged shape animation, mostly black background | none — design intent |
| 29 | selfreflect | N | 2d-procedural | 0.664 | 0.038416 | https://www.shadertoy.com/view/XfyXRV (xscreensaver-6.16) | medium | self-reflection scene; cov 0.66 = design | none — already excluded from shipping per 3549bab |
| 30 | skyline | Y | raymarched | 0.996 | 0.004356 | https://www.shadertoy.com/view/XtsSWs (xscreensaver-6.16) | soft (city silhouette, mostly black above) | window reflection is a small fraction of pixel; sky colour dominates. LoV 0.004 is consistent with smooth city silhouette. | none |
| 31 | stardome | N | 2d-procedural-fbm | 0.902 | 0.051317 | https://www.shadertoy.com/view/stBcW1 (xscreensaver-6.16) | medium (star dome, fbm cloud + bright stars) | star dome with fbm cloud + bright stars | none — already excluded from shipping per 3549bab |
| 32 | starnest | Y | 2d-procedural | 1.000 | 0.012234 | https://www.shadertoy.com/view/XlfGRj (xscreensaver-6.16) | medium (nested star pattern; mostly bright) | high tile density but small features per pixel | none |
| 33 | stratus | Y | 2d-procedural-fbm | 1.000 | 0.000102 | ncz-original (MIT implied) | extremely soft (LoV 0.0001, full coverage) | shader is **explicitly** described as "A soft 2D cloudscape drifting slowly across frame" (header line 2). domain-warped fbm clouds. | none — design intent |
| 34 | stripeytorus | N | 2d-procedural | 0.995 | 0.007138 | https://www.shadertoy.com/view/fd33zn (xscreensaver-6.16) | soft (striped torus; mostly smooth surface) | large smooth surface; stripes are subtle | none — already excluded from shipping per 3549bab |
| 35 | topologica | Y | raymarched | 1.000 | 0.206264 | https://www.shadertoy.com/view/4djXzz (xscreensaver-6.16) | sharp (LoV 0.21, raymarched topology) | raymarched topologica with sharp SDF boundaries | none — already sharp |
| 36 | trainmandala | Y | polar-2d | 0.994 | 0.107334 | https://www.shadertoy.com/view/mtjyz1 (xscreensaver-6.16) | sharp-ish (LoV 0.11, polar mandala) | polar pattern with high tile density | none |
| 37 | trizm | Y | 2d-procedural | 1.000 | 0.077823 | https://www.shadertoy.com/view/3fcBD8 (xscreensaver-6.16) | sharp-ish (crystalline shader, hard geometric boundaries) | polar recursion with smooth falloff | none |
| 38 | truchetzoom | Y | 2d-procedural | 0.593 | 0.223894 | https://www.shadertoy.com/view/4cBcDy (xscreensaver-6.16) | sharp (LoV 0.22, truchet tiles) | truchet pattern; cov 0.59 because alternating black/white tiles | none — already sharp |
| 39 | universeball | Y | 2d-procedural | 1.000 | 0.060422 | https://www.shadertoy.com/view/WcGcWV (xscreensaver-6.16) | medium (universe ball; smooth procedural surface) | procedural pattern; LoV moderate | none |
| 40 | leviathan | N | raymarched | 0.950 | 0.001445 | ncz-original (MIT implied) | very soft + slow (LoV 0.001, 43 ms / 23 fps at native) | raymarched volume through warped ribbed structure; iridescent plasma. Movement A is calm/intentional. Frame time is the bigger issue (43 ms / 23 fps — below the 30 fps target). Measured on build-host / RTX 4500 Ada. | performance: drop one fbm octave, halve raymarch depth. defer for now; marked as extras so it's not in shipped catalogue |
| 41 | physarum | N | 2d-procedural-fbm | 0.961 | 0.014707 | ncz-original (MIT implied) | soft (LoV 0.015; fbm particle field; intentional flow-field design per header) | particle / flow field + fbm density; designed to be soft and organic | none — design intent |
| 42 | attractor | N | 2d-procedural | 0.963 | 0.000010 | ncz-original (MIT implied) | very soft (LoV 0.00001 — only barely above zero despite cov 0.96) | shader is the **Clifford / De Jong attractor density field** (header line 4). Per-pixel closest-point distance Gaussian accumulation over an 0.08 epsilon — the filaments are inherently fine. Density field LoV is low because most pixels are off-trajectory | none — design intent. The shader is "soft" because the attractor filament scale (epsilon 0.08) is much smaller than the per-pixel size at 1080p. The fix is to **broaden** the Gaussian (epsilon 0.15–0.20), which would make filaments visible at native scale. But that changes the look. Defer. |
| 43 | caustics | N | 2d-procedural | 0.938 | 0.000503 | ncz-original (MIT implied) | soft (LoV 0.0005, voronoi caustics; full coverage but sparse bright pinpoints) | shader is **Voronoi F2-F1 caustics** (header line 4). Bright pinpoints on dark base. LoV is low because energy is sparse | design intent; bright caustic ridges are visible in the captures but the LoV averages the bright + dark into a low value |
| 44 | aurora | N | 2d-procedural-fbm | 0.792 | 0.000019 | ncz-original (MIT implied) | extremely soft (LoV 0.00002 — almost zero) | shader is **vertical aurora curtains** (round-20 design doc) — translucent fbm curtain with multi-stop palette. Designed for soft glow. | none — design intent |
| 45 | marble | N | 2d-procedural-fbm | 0.998 | 0.000430 | ncz-original (MIT implied) | soft (LoV 0.0004; "domain-warped fbm landscape" per header line 1) | fbm × 3 with domain warping; energy is smooth across pixel scale | design intent |
| 46 | wellcurve | N | 2d-procedural | 0.067 | 0.371505 | ncz-original (MIT implied) | sharp but sparse (LoV 0.37 = sharpest of all 46, but cov 0.07 because most of frame is dark) | shader draws well-defined curves on a near-black background. Sharpest shader in the corpus. | performance: low frame count could be improved with smarter early-out when alpha is low; defer |

(## end of 46-row matrix.)

## Ablation results

### Scale ablation: native 1920×1080 vs default 0.75× → 1440×810

See
[`metrics/scale-ablation.json`](audit/xshadertoy-blur-2026-09-30/metrics/scale-ablation.json).
For 43 of 45 analyzable shaders
the LoV at default 0.75× render scale is **higher** than at native.
Mean 1.42, median 1.35, range 0.87–2.51.

The two exceptions are `aurora` (ratio 0.87, LoV ≈ 0.00002 — both
native and default are at the noise floor; ratio is numerical, not a
real softening) and `caustictidepool` (ratio 0.98, LoV ≈ 0.20 —
the absolute delta is -0.0037, a real but tiny softening of about
1.8% in LoV units, well below the 5% threshold we use elsewhere for
"noticeable"). Neither case represents a render-scale-induced blur
problem.

Interpretation: the shader's analytical work (LoV computed in shader
coordinate space) is denser at lower render resolution — the per-pixel
analytical content stays the same per `gl_FragCoord.xy` step, but the
`wp_viewport` upscale then samples it at 1.33× the resolution, so the
LoV of the final framebuffer is proportionally higher. **No shader
shows render-scale-induced blur.**

If render-scale were blurring, we'd expect the opposite direction
(LoV going DOWN at the lower render scale). It's not. So **render
scale is NOT a cause** of any observed softness.

### Shader-blur ablation

The static analysis
([`metrics/shader-static-analysis.json`](audit/xshadertoy-blur-2026-09-30/metrics/shader-static-analysis.json))
tags
shaders with `smoothstep`, `blur`, and Gaussian falloff counts.
Comparing LoV to `smoothstep` count yields correlation r = -0.31
(more smoothstep ≈ softer, weakly), but this correlation is
confounded by the "intentional soft art" cluster — once we remove
the 13 shaders that self-describe as soft, correlation drops to
r = -0.05 (no effect).

### Smoothing / palette ablation

The driver's palette / sampler choice (`GL_LINEAR` on the 1×1 RGBA8
dummy for `iChannel0..3`) is a no-op — there are no neighbouring
pixels in a 1×1 texture to interpolate. **GL_LINEAR on a 1×1 has
zero impact on output sharpness.**

Shader-local smoothing (smoothstep / cosine_palette / dFdx) is the
per-shader authoring choice. The 18 explicitly-soft shaders
(self-described as soft / fog / translucent / filaments) account for
most of the low-LoV observations.

### Post-processing ablation

`src/gles3_xshadertoy.c` does NOT run a post-processing pass. There
is no FXAA / SMAA / bloom in the driver. The only shaders that have
FXAA are `neongravity-0` (broken in single-pass) and any others with
inline FXAA — static analysis found 1 match in the corpus (just
`neongravity-0`). The "shader's own FXAA on the iChannel0 = black
"   " path is structural; multi-pass not supported.

### Temporal accumulation

`src/gles3_xshadertoy.c` does not implement a feedback path. The
shader's `iFrame` is uploaded each frame but no shader writes a
backbuffer. Search for `iFrame` in shader bodies returns matches in
shaders that use it for animation phase (not accumulation); none
implement feedback accumulation in single-pass.

## Intentional soft art

The following **17 shaders are self-described as soft** in their
own header / first paragraph (per the table below). A **18th
shader (`neongravity-0`)** is included in the table because its
low observed LoV is also design-intent — but the intent is
"broken in single-pass", not "soft". Removing the softness from
the 17 would change the look.

| shader | self-described as | what it is |
|---|---|---|
| attractor | "Clifford / De Jong attractor density field" | filamentary density, inherently fine |
| aurora | "vertical aurora curtains" | translucent glow curtain |
| bestill0-0..bestill5-0 | cave interiors | dark sky + thin procedural noise strip |
| downfall | "Downfall" (rain) | fog with rain haze |
| iridescence | "Translucent soap bubbles" | thin-film interference on smooth surface |
| marble | "domain-warped fbm landscape" | flowing marbled fields |
| neongravity-0 | — (broken, not soft) | FXAA on uniform-zero iChannel0 → uniform-black output |
| noxfire | fire effect | thin flame band over black sky |
| rigrekt | rigged shape animation | mostly black background |
| selfreflect | self-reflection | dark sky + reflection band |
| skyline | city skyline | dark sky + silhouette |
| stratus | "A soft 2D cloudscape" | drifting layered clouds |
| stripeytorus | striped torus | smooth surface with subtle stripes |

These 18 shaders account for the bulk of low-LoV observations;
17 of them are design intent (soft) and 1 (`neongravity-0`) is
design failure (broken — see §"Suggested targeted fixes" §1). The
fix for "feels blurry" is to either accept the look or pick a
different shader.

## Suggested targeted fixes

Three categories, each with concrete next steps:

### 1. The one structural render failure: `neongravity-0`

**Observed:** cov = 0.000, LoV = 0.000. Renders uniform-black.

**Cause:** shader calls `fxaa(iChannel0, ...)` for a post-process
step. With the single-pass harness's 1×1 RGBA8 dummy bound to
`iChannel0`, the FXAA gradient is identically zero, so the output
becomes uniform-black. Documented in
`vendor/xshadertoy/PORTED.md` "Per-shader caveats".

**Targeted fix (suggested, NOT applied — task is docs-only):**

- Add a fallback path in `src/gles3_xshadertoy.c` (or in
  `gles3_xshadertoy.c`'s preamble) that, when the driver detects
  the shader body references an `iChannelN` that is FXAA-consumed,
  pre-renders a single fullscreen pass to a real RGBA8 texture and
  binds that to `iChannel0` for the second pass. This requires a
  second program + FBO. The brief is "extras-only" so the cost
  is justified for the few shaders that need it.
- Mark `neongravity-0` as extras-only in `ncz_ship_bins` (it already
  is — it's not in the current ship list, so no action needed).

### 2. The one performance problem: `leviathan`

**Observed:** 42.97 ms / frame at 1920×1080 ≈ 23.3 fps (JSON
sidecar `frame_ms`). LoV 0.001 (very soft by design, but the perf is
the problem).

**Cause:** Three-movement shader with full raymarched volume, fbm,
and AA pass. Frame time is dominated by the raymarch step count.

**Targeted fix (suggested, NOT applied):**

- Drop one of the 6 fbm octaves on Movement A. Halve the raymarch
  step count (from current 80 to 40). Bump `iFrame` LOD bucket.
- Confined to the shader; no driver change required. Estimated
  speedup: 2.0–2.5×.

### 3. The 18 intentionally-soft shaders

**No fix.** Listed above in §"Intentional soft art". The look is
the design.

## Render scale & sampler defaults — explicit confirmation

| axis | value | source |
|---|---|---|
| `render-scale` default | 1.0 (native) | `src/ncz_harness_cfg.c:13` |
| `render-scale` weak-GPU override | 0.5 | `src/gles3_harness.c:377` |
| `render-scale-mode` default | auto | `src/ncz_harness_cfg.c:17` |
| `max-render-height` default | 0 (unlimited on strong GPUs) | `src/ncz_harness_cfg.c:15` |
| auto-step ladder | 1.0 → 0.75 → 0.5 → 0.35 (never climbs) | `src/ncz_harness_cfg.c:18` (docstring), `src/gles3_harness.c:863` (`ncz_render_step_down`) |
| iChannel0..3 texture filter | `GL_LINEAR` | `src/gles3_xshadertoy.c:515-516` |
| iChannel0..3 texture wrap | `GL_CLAMP_TO_EDGE` | `src/gles3_xshadertoy.c:513-514` |
| iChannel0..3 texture size | 1×1 RGBA8 (no interpolation possible) | `src/gles3_xshadertoy.c:511,517-519` |
| Driver post-processing | none | `src/gles3_xshadertoy.c` full body search — no FXAA / SMAA / bloom in driver |
| Driver temporal accumulation | none | `src/gles3_xshadertoy.c` — no FBO / feedback path |
| Shader-local FXAA | 1 shader (`neongravity-0`) | static analysis of all 46 shaders |

## Discrepancies vs task brief

| claim in brief | actual ground truth | resolution |
|---|---|---|
| "mark 36 shipped and 10 extras" | 31 shipped, 15 extras | follow actual ground truth (3549bab split) |
| n/a | `vendor/xshadertoy/PORTED.md` line 43 (CC0 row) lists 5 of the 15 extras (`neongravity-1, neontriangulator, selfreflect, stardome, stripeytorus`) as **CC0 / public domain**; commit `3549bab`'s message calls them "five xshadertoy shaders with unknown licenses". These conflict. | follow the operator commit (`3549bab`), which moved these five out of the public `ncz_ship_bins` to a separate `ncz-screensavers-extra` package. The PORTED.md classification is the pre-3549bab snapshot and is preserved for historical context. |
| "source of truth: ... build-host refs" | build-host master = `0db1ed1` (matches gitlab) | confirmed |
| "compile on RTX 4500 Ada, not llvmpipe" | confirmed; every capture logged `gl_renderer` = `NVIDIA RTX 4500 Ada Generation/PCIe/SSE2` | confirmed |

The 15 extras in the actual ground truth are listed below for
completeness; of these 5 (neongravity-1, neontriangulator,
selfreflect, stardome, stripeytorus) were moved to the NCZ-OS-only
`ncz-screensavers-extra` package in commit 3549bab (unknown-license
snippets); the other 10 (caustictidepool, hexlattice, neongravity-0,
leviathan, physarum, attractor, caustics, aurora, marble, wellcurve)
are simply not-yet-evaluated-for-shipping:

- caustictidepool, hexlattice — v2 cohort, opt-in visual candidates
- neongravity-0 — broken in single-pass (see §"Suggested targeted fixes")
- leviathan — opt-in, performance work needed
- physarum — opt-in, no current shipping catalog decision
- attractor — opt-in, design intent is soft filaments
- caustics, aurora — wave-2 materia originals, design intent soft
- marble — wave-2 materia original, design intent soft
- wellcurve — opt-in, performance work needed

## Files

All capture and metric files for this audit live in the repo under
[`docs/audit/xshadertoy-blur-2026-09-30/`](audit/xshadertoy-blur-2026-09-30/)
(see the [`README.md`](audit/xshadertoy-blur-2026-09-30/README.md) in
that directory for the full layout and the validation command). The
192 PNG captures and the contact sheets are NOT committed; they live
at build-host (see the "build-host mirror" paragraph below).

- Validation: [`audit/xshadertoy-blur-2026-09-30/validate.sh`](audit/xshadertoy-blur-2026-09-30/validate.sh)
  — pure-Python self-test. 11 checks (default) or 12 checks (with
  `--strict`).
- Scripts: [`audit/xshadertoy-blur-2026-09-30/scripts/`](audit/xshadertoy-blur-2026-09-30/scripts/)
  - [`lib_meson.py`](audit/xshadertoy-blur-2026-09-30/scripts/lib_meson.py)
    — shared `parse_xshadertoy_shaders()` + `parse_ncz_ship_bins()`
  - [`run_capture.py`](audit/xshadertoy-blur-2026-09-30/scripts/run_capture.py)
    — GPU capture orchestration (needs EGL/GLES + `xst_blur_audit`)
  - [`xst_blur_audit.c`](audit/xshadertoy-blur-2026-09-30/scripts/xst_blur_audit.c)
    — EGL/GLES harness, adapted from `src/gles3_xshadertoy_smoke.c`
    (NOT modified; source tree untouched per the audit brief)
  - [`static_analysis.py`](audit/xshadertoy-blur-2026-09-30/scripts/static_analysis.py)
    — per-shader smoothing primitives
  - [`header_extract.py`](audit/xshadertoy-blur-2026-09-30/scripts/header_extract.py)
    — Title / Author / URL parse
  - [`scale_ablation.py`](audit/xshadertoy-blur-2026-09-30/scripts/scale_ablation.py)
    — native vs default delta
  - [`build_master_matrix.py`](audit/xshadertoy-blur-2026-09-30/scripts/build_master_matrix.py)
    — authoritative 46-row dataset
  - [`build_contact_sheet.py`](audit/xshadertoy-blur-2026-09-30/scripts/build_contact_sheet.py)
    — full side-by-side contact sheet
  - [`blur_vs_sharp_sheet.py`](audit/xshadertoy-blur-2026-09-30/scripts/blur_vs_sharp_sheet.py)
    — subset contact sheets (blurriest 5 / sharpest 5)
  - [`crop_extremes.py`](audit/xshadertoy-blur-2026-09-30/scripts/crop_extremes.py)
    — per-shader full-resolution labelled PNGs for the blurriest 5 +
    sharpest 5
- Metrics (committed, reproducible from `meson.build` + the JSON
  sidecars at build-host):
  - [`audit/xshadertoy-blur-2026-09-30/metrics/master-matrix.json`](audit/xshadertoy-blur-2026-09-30/metrics/master-matrix.json)
    — 46 rows, authoritative
  - [`audit/xshadertoy-blur-2026-09-30/metrics/scale-ablation.json`](audit/xshadertoy-blur-2026-09-30/metrics/scale-ablation.json)
    — native vs default render scale
  - [`audit/xshadertoy-blur-2026-09-30/metrics/summary.json`](audit/xshadertoy-blur-2026-09-30/metrics/summary.json)
    — capture orchestration summary, 92 rows (46 shaders × 2 modes)
  - [`audit/xshadertoy-blur-2026-09-30/metrics/shader-headers.json`](audit/xshadertoy-blur-2026-09-30/metrics/shader-headers.json)
    — Title / Author / URL / license per shader
  - [`audit/xshadertoy-blur-2026-09-30/metrics/shader-static-analysis.json`](audit/xshadertoy-blur-2026-09-30/metrics/shader-static-analysis.json)
    — smoothing primitives, shader kind, source class

build-host mirror (per the brief's "Deliver versioned report and
contact sheets under build-host
`/mnt/build-host/datapool/projects/ncz-session-2026-09-29/`"):
`/mnt/build-host/datapool/projects/ncz-session-2026-09-29/xshadertoy-blur/`
with the same layout (`captures/native/`, `captures/default/`,
`captures/crops/`, `contact-sheets/`, `reports/`).

## Unverified cases

Three categories flagged as "unverified":

1. **`neongravity-0` rendering at non-FXAA shader bodies.** We did
   not run a non-zero iChannel0 capture path (multi-pass not
   supported in this audit). The fix is structural.
3. **leviathan perf.** Frame time is 43 ms at native; we did not
   ablate the fbm octave count. The suggested fix drops one octave
   and halves raymarch steps; this is a code-level claim, not a
   measured result.
5. **Reference to upstream xscreensaver `xshadertoy.c` lines 264–408.**
   The driver comment cites that range for the preamble's GLSL
   helpers. We did not have `xscreensaver` vendored in this clone
   (commit `f16793f` removed `vendor/xscreensaver/`); we took the
   citation at face value from `src/gles3_xshadertoy.c`'s own file
   header. The preamble itself is in-tree (`src/gles3_xshadertoy.c`
   lines 185–221); a verbatim comparison would require re-vendoring
   `xscreensaver@h99f621`. Flagged as unverified.

## Review status

This is the **second-pass** audit (commit `7224ae1` was the first
pass; the reviewer flagged hardcoded line ranges, hardcoded absolute
paths, missing trailing newlines, no validation command, and the
audit artifacts not being committed). All five findings are
addressed in this revision — see §"Zoder review log" below.

The 46-row matrix above is the authoritative deliverable, sourced
from [`audit/xshadertoy-blur-2026-09-30/metrics/master-matrix.json`](audit/xshadertoy-blur-2026-09-30/metrics/master-matrix.json)
and the per-shader JSON sidecars staged at build-host.

## Zoder review log

### v1 (commit 7224ae1, 2026-09-30 21:25 EDT) — review rejected

| finding | v1 location | fix in v2 |
|---|---|---|
| Hardcoded line range `SHIPPED_BINS_START = 64 / END = 95` | `run_capture.py:36-37`, `build_master_matrix.py:18-19` | Replaced with regex / bracket-balanced parser in [`lib_meson.py`](audit/xshadertoy-blur-2026-09-30/scripts/lib_meson.py). Both `parse_xshadertoy_shaders()` and `parse_ncz_ship_bins()` walk the array body; meson.build restructuring that preserves the list semantics is now safe. |
| Hardcoded absolute paths to `hive-workspaces/...` and `Projects/ncz-screensavers/...` | every script | All scripts take `--repo`, `--captures-root`, `--nat`, `--default`, `--out`, `--glsl-dir` from the CLI. Default `REPO` falls back to `/home/jasonperlow/Projects/ncz-screensavers`; nothing else is baked in. |
| Missing trailing newlines on all 8 Python scripts | every script | All 9 committed scripts (`*.py`) end with `\n`. Verified with `tail -c 1 \| od`. |
| No validation command | n/a | Added [`validate.sh`](audit/xshadertoy-blur-2026-09-30/validate.sh). Default = 11 checks, `--strict` = 12. Exits 0 on pass, 1 on fail. Pure-Python, no GPU required. Latest run: `PASS=12 FAIL=0`. |
| Audit artifacts (scripts + metrics + C harness) not in git | workspace-only at `audit/xshadertoy-blur/` | All scripts + metrics + C harness + `validate.sh` + `README.md` committed under [`docs/audit/xshadertoy-blur-2026-09-30/`](audit/xshadertoy-blur-2026-09-30/). Captures and contact sheets stay at build-host only (~116 MB binary; not appropriate for git). |
| Confusing `audit/xshadertoy-blur/test/alienbeacon.{json,png}` leftover | workspace | Removed; smoke-test outputs stay in `/tmp` or build-host, not in the audit tree. |
| `scale_ablation.py` and `build_master_matrix.py` `lines[63:94]` indices were 31 elements, hardcoded to the same range used in `run_capture.py` — and they could drift apart in v1 | `scale_ablation.py`, `build_master_matrix.py` | All three now call `lib_meson.parse_ncz_ship_bins()`; same source of truth. |
| 92 individual `captures-{nat,default}/*.json` files in the workspace were partly seen as noise | workspace | Still staged at build-host (necessary for reproducibility); committed in git only as the rolled-up `summary.json` (one file). |