# genxvectorcade — PORTED

Original work, GLES3-native. One fragment shader, one driver .c, a
half-resolution RGBA8 feedback FBO ping-pong, and a fade-and-rotate
pass that spirals the trails each frame.

See `docs/superpowers/specs/2026-09-25-genxvectorcade-vision.md`
for the creative spine this is part of. Differs from the
`neonspacewar` Round-18 piece in that there is no game and no
geometry to model — the journey itself is the artwork.

## What it does

A continuously-evolving psychedelic vector-arcade journey: five
movements (vector tunnel, grid horizon, starfield warp,
kaleidoscope bloom, lattice canyon) blend smoothly into each other
over a randomised per-launch journey order. The dominant movement
gets ~80% of the energy at any moment with the previous one
fading out as the next one fades in. Heavy phosphor bloom,
persistent feedback trails (the through-line that holds the
journey together), recursive feedback (each frame the trail buffer
is rotated by ~0.2° + scaled slightly + faded, so wakes spiral
inward and self-replicate into fractal ghosting), domain warping,
chromatic aberration, palette cycling through six complementary
neon pairs in HSL hue space, beat-like pulsing, brief full-frame
inversions at movement boundaries.

Per-launch randomisation seeded from `/dev/urandom` (overridable
via `NCZ_GVC_FIXED_SEED` for A/B testing): cross-section shape
(triangle/square/pentagon/hexagon/circle/star), travel speed
(0.65..1.55×), segment count (8..22), rotation rate, palette
pair (0..5), palette cycling rate (0.08..0.22 cycles/sec), trail
persistence (0.78..0.92), warp amount (0.30..1.10), chromatic
aberration amount (0.0..0.012), symmetry base order (4..16),
burst frequency (0.3..1.2 Hz), journey length (~150..215s).

## Architecture

```
src/gles3_genxvectorcade.c                   (driver .c, ~700 lines)
└── fade_copy_pass    fade+rotate+scale copy of prev FBO -> dst FBO
└── scene_pass        draws scene shader on top of faded prev frame
└── blit_pass         renders current FBO to default framebuffer

vendor/genxvectorcade/genxvectorcade.frag     (~430 lines)
├── 5 movement fns   tunnel, gridHorizon, starfieldWarp, kaleidoBloom, latticeCanyon
├── palette()        HSL hue-space 6 complementary neon pairs
├── warp()           FBM-driven domain warp
├── kaleido()        wedge-fold for radial symmetry
└── main()           phases blend, tone curve, screen-blend trail mix, chromatic
```

## Render passes (every frame)

1. **fade_copy_pass**: bind the OTHER FBO, render the current FBO's
   texture into it with a small rotation (cos/sin of ~0.0035..0.0045
   rad, giving a ~0.2° per frame rotation) and a per-frame fade
   multiplier (`v_trail_persist`, ~0.85). After this pass, `fb_index`
   flips so the just-rendered-to FBO becomes "current".

2. **scene_pass**: bind the current FBO, render the geometry on top
   of the faded previous frame. The scene shader reads from the
   current FBO's texture (the just-faded buffer — the trail) and
   adds its own colour via a screen blend. Output writes to the
   same current FBO (GLES-legal sample-and-write, drivers return
   the texture value as it was at the START of the draw call).

3. **blit_pass**: bind the default framebuffer (the Wayland
   window), render the current FBO's texture with a 1:1 fullscreen
   quad. No fade, no rotation. Calls `glFinish()` at the end so
   the harness's `report_framebuffer()` pixel readback (which runs
   before `eglSwapBuffers`) sees the rendered content — without
   this some drivers defer the FBO→default-FB writes and the
   readback sees an empty surface.

The ping-pong gives the recursive feedback effect: each frame the
trail buffer is rotated slightly, so wakes from previous frames
spiral inward, and (because the rotation is per-frame, not per-
erase) the wakes self-replicate as they go, producing genuinely
fractal ghosting without any extra geometry.

## Performance

| GPU                   | vsync-locked | vblank_mode=0 |
|-----------------------|--------------|---------------|
| Intel UHD CML GT2     | ~60 fps (16.7ms) | ~90-110 fps (9-11ms) |
| NVIDIA RTX 2060       | ~60 fps (16.7ms) | ~60 fps (16.7ms, still vsync-locked at compositor) |
| AMD Radeon (navi14)   | ~60 fps (16.7ms) | ~600-1100 fps (0.9-1.7ms) |

Measured on a UHD 630 + RTX 2060 laptop (Intel + NVIDIA via EGL vendor override) and
the Radeon Pro 5500M laptop (AMD Radeon). The compositor on a UHD 630 + RTX 2060 laptop caps the frame
rate at the panel refresh even with vblank_mode=0; the Radeon Pro 5500M laptop's
compositor permits full unconstrained throughput.

Trail buffer is half-resolution RGBA8 (`NCZ_GVC_FB_SCALE=0.5` by
default, overridable). Full-resolution RGBA8 ping-pong on Intel
UHD nearly doubled per-frame cost; half-res costs a quarter the
bandwidth and the trails look correctly soft through the bloom.
If even half-res proves too heavy on a future weaker Intel target,
override the env var to 0.25.

The shader does 5 movement evaluations, 1 tone curve, 1
chromatic-aberration feedback sample (3 texture reads), and 1
screen blend per fragment per frame. The host does 2 additional
fullscreen passes (fade-copy + blit). On the spec the dominant
cost is fragment shading, not bandwidth.

## Photosensitive-seizure safety

No sustained rapid full-field luminance flashing. Intensity comes
from colour, motion, trails and density:

- Full-frame flashes fire ONLY at movement boundaries (last 6%
  and first 6% of each ~30s leg), at most ~0.45 amplitude
  (blended with inverted content, not pure white), throttled to
  once per ~120 frames.
- No element of the shader produces full-field >3 Hz luminance
  oscillation. The pulse uniform is a sharpened sine at
  burst-frequency (0.3..1.2 Hz) — well below the 3-30 Hz photosensitive-
  seizure band.
- Hot cores (tunnel vanishing point, sun disc, kaleido ring)
  clip to white at SINGLE PIXEL radius; the surrounding geometry
  stays in the [0, 1] colour range.

## Shader install is mandatory

The shader is at
`/usr/share/ncz-screensavers/shaders/genxvectorcade.frag` after
`meson install`. The driver .c looks for the shader in this order:

1. `vendor/genxvectorcade/genxvectorcade.frag` (cwd — dev)
2. `../vendor/genxvectorcade/genxvectorcade.frag` (cwd — dev)
3. `../../vendor/genxvectorcade/genxvectorcade.frag` (cwd — dev)
4. `/usr/share/ncz-screensavers/shaders/genxvectorcade.frag`
   (installed)

Verified working from `/` cwd on a UHD 630 + RTX 2060 laptop (Intel UHD). The
cwd-relative-only path was a real ship-blocker on the flagship
blackhole hack (it died instantly when launched from any directory
other than the build tree while still passing the validation
gate). The installed-path lookup prevents that here.

## Verification (real hardware)

**the UHD 630 + RTX 2060 laptop, Intel UHD (CML GT2):**
```
[diag] GL_VERSION=OpenGL ES 3.2 Mesa 26.1.6-1
RENDERER=Mesa Intel(R) UHD Graphics (CML GT2)
[diag] genxvectorcade frame_t frame=60  dt_ms=16.679
[diag] genxvectorcade frame_t frame=120 dt_ms=16.678
[diag] genxvectorcade frame_t frame=240 dt_ms=16.676
[diag] genxvectorcade frame_t frame=480 dt_ms=16.666
```
vblank_mode=0: ~90-110 fps (9-11ms).

**the UHD 630 + RTX 2060 laptop, NVIDIA RTX 2060 (EGL vendor override):**
```
[diag] GL_VERSION=OpenGL ES 3.2 NVIDIA 615.71.09
RENDERER=NVIDIA GeForce RTX 2060/PCIe/SSE2
[diag] genxvectorcade frame_t frame=60  dt_ms=16.667
[diag] genxvectorcade frame_t frame=120 dt_ms=16.683
[diag] genxvectorcade frame_t frame=240 dt_ms=16.672
[diag] genxvectorcade frame_t frame=480 dt_ms=16.681
```
vblank_mode=0: still capped at the panel refresh (~60 fps).

**the Radeon Pro 5500M laptop, AMD Radeon (navi14):**
```
[diag] GL_VERSION=OpenGL ES 3.2 Mesa 26.1.6-1
RENDERER=AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64, 7.2.7-2-t2-trixie)
[diag] genxvectorcade frame_t frame=60  dt_ms=16.664
[diag] genxvectorcade frame_t frame=120 dt_ms=16.668
[diag] genxvectorcade frame_t frame=240 dt_ms=16.667
[diag] genxvectorcade frame_t frame=480 dt_ms=16.665
```
vblank_mode=0: ~600-1100 fps (0.9-1.7ms) — the AMD GPU is so fast
here that the harness's `clock_gettime` overhead dominates the
sample.

All three runs vsync-locked at ~60 fps in normal use — the
screensaver pacing respects the panel refresh.

## Per-launch diag line

```
[diag] genxvectorcade seed=12345 shape=5 speed=0.798 segments=21
 rotation=1.196 pal_pair=4 pal_rate=0.1426 pal_phase=0.399
 pal_contrast=1.201 sym_base=9.11 burst=1.160 trail=0.8332
 warp=1.011 ca=0.0115 journey=150.7s order=[2,3,0,4,1]
 fb_scale=0.50 fb=960x540 GL=OpenGL ES 3.2 Mesa 26.1.6-1
```

`shape` index maps to: 0=triangle, 1=square, 2=pentagon,
3=hexagon, 4=circle, 5=star. `pal_pair` index maps to: 0=magenta/
cyan, 1=orange/blue, 2=lime/violet, 3=hot/cool, 4=red/teal,
5=gold/magenta. `order` is the shuffled journey phase order.

## Build

```
meson setup --reconfigure ~/build-tmp/ncz-screensavers-build .
meson compile -C ~/build-tmp/ncz-screensavers-build genxvectorcade_gles3
```

## Files

- `src/gles3_genxvectorcade.c` — driver .c (init / draw / free,
  three render passes, FB ping-pong setup, per-launch randomisation,
  Gray-Scott state ping-pong, persistence)
- `vendor/genxvectorcade/genxvectorcade.frag` — fragment shader
  (5 movements + cabinet glow + habitat tint + reveal moment)
- `include/ncz_platform.h` — platform-abstraction header used for
  clock, entropy, asset-path and sensor cache reads
- `src/ncz_platform.c` — wayland/EGL-backend implementation of
  the platform abstraction (currently the only backend)
- `meson.build` — adds the executable + `install_data` rule for
  `/usr/share/ncz-screensavers/shaders/genxvectorcade.frag`

## Phase 2 additions

Per `docs/superpowers/specs/2026-09-25-genxvectorcade-design.md`:

* **Life simulation** — Gray-Scott reaction-diffusion on a low-res
  state texture (default 320x180, RGBA8). Ping-pong FBO. Genome in
  the B channel drives per-pixel feed/kill rates. Activity-driven
  mutation pressure prevents both heat death and explosion;
  rare-seeding recovers from local collapse.

* **Integer-tick clock** — `sim_tick` (uint64) is the authoritative
  time for the life system; the float time uniform drives visuals
  only. Float precision degrades over multi-day runs; ticks do not.

* **Arcade optics** — per-channel phosphor decay
  `vec3(0.94, 0.965, 0.985)`, max-based trail envelope so
  persistence cannot accumulate unbounded, three drifting cabinet
  glow pools in parallax (cyan / magenta / amber), genome-driven
  habitat tint.

* **Depth reveal** — single 4s Hann window around the midpoint of
  the second leg of the journey drives a chromatic radial zoom.

* **Persistence** — `GVC2` magic + versioned format writes the
  simulation tick to `$XDG_DATA_HOME/ncz-screensavers/genxvectorcade.
  lineage`. Loaded on launch; absent or stale = fresh lineage.

* **Environmental seed schema** — kp, solar_wind, cpu_load,
  cpu_temp, battery are read from sensors.env via the platform
  interface and mixed into the per-launch seed via xor with
  distinct primes. Never mapped to visuals.
