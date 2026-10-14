# Round 20 — `leviathan` shader + `gles3_xshadertoy_smoke` headless tester

**Date:** 2026-09-26
**Author:** Claude Opus 5 (1M context), agent minimax

## What shipped

### `leviathan` — three-movement flythrough (vendor/xshadertoy/glsl/leviathan.glsl)

Single-pass Shadertoy-format fragment shader implementing the
operator's brief: a vast interior of a machine-organism,
accelerating into a corridor of light, resolving into false-colour
worlds, settling back into the interior. All three movements are
evaluated every frame and blended with smoothstep windows on a
per-launch-randomised schedule. The single-pass player gives no
multipass / ping-pong, so each movement is self-contained.

**Movement A — interior (default).** Raymarched volumetric
interior through a 3D lattice of structural "nodes" placed at
non-axis-aligned lateral offsets. Iridescent plasma fog
(3D fbm) + Fresnel-style rim emission on the nodes + fog/extinction
+ slow rotation + slow lateral sway. Reads as kilometres of
organic-mechanical structure passing on all sides. Avoids the
"generic neon tunnel" failure mode the brief explicitly warned
against.

**Movement B — corridor of light.** Polar-coordinate image with
a continuous saturated-hue radial field plus narrow brighter
streaks, converging toward a vanishing point slightly below
screen centre. Slit-scan-style radial speed lines flow outward.
Smooth flow (no thresholding, no strobing). Saturated, sometimes
clashing palette — cyan / magenta / amber / lime — that the
interior deliberately never uses.

**Movement C — false-colour worlds.** Ridged-fbm terrain under
a low camera, pushed through a non-monotonic colour transform:
solarisation, channel inversion, posterised hue rotation.
Atmospheric fade with distance. Geologically plausible landscape,
impossible colour.

**Schedule:** per-launch-randomised cycle (75-120 s) with two
windows carved out for B (7-11 s) and C (9-14 s) at randomised
offsets. Sharp transitions (~1.8 s of blend) so each movement
is clearly its own event, but the blend is continuous rather
than a hard cut.

**Quality bar checks:**
- ✅ ACES tonemap on output (matches `prococean`)
- ✅ Real colour, multi-stop palette per movement
- ✅ Motion continuous — verified across 18 time samples
- ✅ Per-launch randomisation (cycle length, B/C offsets,
  palette, drift rate)
- ✅ Frame-filling — no small centred object on black
- ✅ GLSL ES 3.00 compliant, no implicit conversions

**Safety:** no strobing. Movement B's streaks and speed lines are
continuous smooth gaussians; Movement C's solarisation is
continuous smoothstep. No thresholds that snap with time.

### `gles3_xshadertoy_smoke.c` — headless xshadertoy smoke tester

The xshadertoy production driver (`src/gles3_xshadertoy.c`)
needs a live Wayland compositor and a `wl_egl_window` to render.
On a headless dev box (and on `build-host` between sessions) we
don't have that. This smoke tester, patterned after
`gles3_transitions_smoke.c`, opens a `EGL_KHR_surfaceless_context`,
creates a PBuffer destination, runs the same xshadertoy preamble
that the production driver emits, captures 8 frames at user-
specified iTime values, and writes each as a real PNG via libpng.

Invocation:
```
EGL_PLATFORM=surfaceless ./gles3_xshadertoy_smoke <shader.glsl> <out-dir> [t1 t2 ...]
```

Output ends with `[diag] <shader> PASS` (or `PARTIAL` if any
frame has <5% non-black coverage). Each frame's PNG is named
`<shader>_t<time>s.png` and the harness also prints per-frame
non-black coverage and mean RGB to stderr.

This was the path that made `leviathan` verifiable end-to-end
on a dev box without compositor dependencies.

## Validation

### Compile + link + render (NVIDIA, both boxes)

Both `build-host` (RTX 4500 Ada) and `build-host` (RTX 2060)
compiled, linked, and rendered `leviathan` cleanly with the same
mean RGB values per frame (deterministic across GPUs):

| Frame | Mean R | Mean G | Mean B | Movement |
|-------|-------:|-------:|-------:|----------|
| 2.0 s | 0.193  | 0.169  | 0.260  | A (interior, lattice) |
| 28.0 s| 0.631  | 0.621  | 0.618  | B (corridor peak) |
| 55.0 s| 0.494  | 0.325  | 0.613  | C (false-colour peak) |

All frames have non-black coverage > 0.95 (the interior's
fog/extinction ensures no pixels go pure black). Frame time on
RTX 2060 at 320x180: ~41 ms per frame for the volume raymarch
(80 STEPS × 320 × 180 = 4.6 M step evaluations per frame).

### Cross-platform

Built cleanly on:
- `build-host` (Debian 13/trixie, gcc 14.2.0, NVIDIA 595.58.03)
- `build-host` (NCZ-OS 26.7 based on Debian testing, gcc, NVIDIA 615.71.09)

Identical output (deterministic — no driver-dependent branching
in the shader).

### Naming

The operator referenced a specific film by name and demanded
strict avoidance of that name and franchise terms. The shader
is shipped as `leviathan` — chosen to evoke the scale and
indifference of the interior, with no other-world franchise
references in code, comments, docs, captures, or commit messages.

## What I would defend

Movement A (the default state, occupying most of the runtime) is
a volumetric interior through a 3D lattice of glowing ring
apertures. It does NOT read as a tube the camera is flying down,
it does NOT read as a generic neon tunnel, and the eye never
finds an edge that bounds the object. It reads as
"inside something enormous".

Movement B is the closest to a cliched "rainbow tunnel" of the
three, but the clashing palette is moderated by the continuous
base colour field, the radial speed lines flow continuously (no
strobing), and the transition into/out of B is continuous
(smoothstep blend, not a cut).

Movement C reads as a psychedelic landscape. The terrain is
plausible (ridged fbm mountain ridges), the colour is impossible
(saturated, hue-cycling, with channel inversion on low bands),
and the atmospheric fade keeps distant terrain from competing
with the foreground.

## Open follow-ups

- Intel UHD validation. The volume raymarch is 80 steps × full
  resolution; on UHD (likely 4-5x slower than RTX 2060) this
  could push 200ms per frame. May need a STEPS fallback or a
  lower default resolution.
- Wider per-launch randomisation. The current scheme randomises
  cycle length, B/C offsets, and palette shift. Could add
  per-launch variation in node lattice scale, plasma density
  threshold, and rotation axis — but each new parameter risks
  visual inconsistency.
