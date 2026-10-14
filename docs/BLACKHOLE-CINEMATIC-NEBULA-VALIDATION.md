# Black hole cinematic and nebula validation — 2026-09-25

Base: `a7fd204`. Camera-only commit: `e13e55e`.
Evidence on the work host: `~/build-tmp/blackhole-upgrade/`;
per-host original PNGs/logs in `build-host/` and `build-host/`. Remote originals
are in the same build-tmp directory on each test host. No temporary
artifacts were deliberately written outside build-tmp.

## Implementation and visual assessment

The plunge now has quintic arrival/hold/release envelopes, accumulated
azimuth, held bank, small framing drift and arrival-only lens distortion.
The inner disk's animated hot sector is evaluated at disk intersections,
so direct and lensed images share the feature. The integration loop,
distance floor and existing capture classification are unchanged.

The optional synthetic critical-curve accent was omitted after viewing
the camera-only captures: the existing lensed rim is already prominent.
No photon-sphere camera move or multi-ray blur was attempted.

The background uses seamless 3D direction-space value noise, one coarse
warp and three fixed octaves. Six xorshift-generated values control hue,
scale, coverage, yaw, tilt and noise offset; a separate `[diag] blackhole`
line logs all six with nine significant digits. The radiance is bounded
below the disk. The sky is evaluated from the post-integration direction
inside the existing background branch, with no screen-space overlay.

Real `grim` captures at 2/10/20/30 seconds after initial draw:

| Host / launch | Logged shader seed | Visible backdrop |
| --- | ---: | --- |
| build-host / final-1 | 254077296 | Broad violet clouds with dark channels |
| build-host / final-2 | 318869824 | Finer, sparse teal clouds, different orientation |
| build-host / final-1 | 772673536 | Fine, sparse purple filaments around a gold disk |
| build-host / final-3 | 2308259328 | Broader, denser mauve clouds around a gold disk |

build-host final-2 is rejected as visual evidence: another saver covered the
surface during capture. Final-3 is a fresh randomized replacement, not a
replay. Launches were selected for randomized `flyby=3`, with no forced
shader parameters or test-only edits to the shipped binary. Other camera
modes retain their existing motion and receive the new background, but
were not given the same extended screenshot review.

The 2s views establish a small disk in a clouded sky. At 10–20s the banked
emitting sheet fills the frame, with the hole beside an overhead arc.
By 30s the views are retreating or already wide, depending on randomized
camera rate (period 27–36s). Cloud channels curl tightly around the hole
and appear in narrow arcs beside the disk; they do not remain flat behind
it. The disk and its bright lensed rim remain the subject.

Additional captures at 12/14/16/18s and five 0.2s-spaced frames centered
on each first cycle boundary show no black flicker or orientation jump.
These are sampled stills, not continuous video and not proof about every
frame or random seed. Numeric sampling of two laps (20,001 points)
confirmed strictly monotone azimuth; the analytic quintic derivative is
nonnegative, so multiplying by either approach sign preserves direction.
Across +/-1e-7 laps at the boundary, azimuth changes 4.39823e-7 radians
before approach scaling, with zero distance/elevation/bank discontinuity.

Background side strips at 2s (outer 20% of width, y=10–85%, avoiding disk
and desktop panel) differ between launches by mean absolute RGB error
15.025/255 on NVIDIA and 17.861/255 on AMD. 92.51% and 81.84% of those
pixels respectively change by more than 8 in at least one channel. This
supports the visual assessment but does not isolate orientation from
other randomized parameters. Adjacent full-frame cycle-boundary MAEs
remain comparable across the wrap: NVIDIA 2.60–2.74 / 5.92–6.10 and AMD
3.54–3.67 / 2.39–2.60 for the two accepted launches on each host.

## Hardware and cost

Real application stderr confirms:

- build-host: `NVIDIA GeForce RTX 2060/PCIe/SSE2`, NVIDIA 615.71.09.
  All three PRIME/EGL selection variables were exported.
- build-host: `AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64,
  7.2.7-2-t2-trixie)`, Mesa 26.1.6-1.
- Every accepted application launch reports first-draw `gl_error=0x0`.

`bench.c` in the evidence directory creates a real EGL device context and
1280x720 pbuffer, runs the actual shader with identical fixed parameters,
and measures 40 draw+glFinish calls after 10 warm-up draws. Values below
are medians of three runs, in milliseconds. This is synchronized rendering
wall time, not a GPU timer-query result or compositor FPS. Baseline is the
new cinematic shader with its old starfield, isolating nebula cost.

| GPU / shader | 2s | 10s | 16s | 20s | 30s |
| --- | ---: | ---: | ---: | ---: | ---: |
| RTX 2060 / baseline | .891 | 1.220 | 1.284 | 1.293 | .984 |
| RTX 2060 / nebula | .971 | 1.288 | 1.352 | 1.363 | 1.059 |
| AMD Navi14 / baseline | 2.387 | 2.911 | 3.142 | 3.126 | 2.397 |
| AMD Navi14 / nebula | 2.567 | 3.070 | 3.237 | 3.293 | 2.575 |
| Intel UHD CML GT2 / baseline | 20.516 | 25.234 | 27.496 | 27.316 | 20.511 |
| Intel UHD CML GT2 / nebula | 21.874 | 26.515 | 28.714 | 28.519 | 21.852 |

Intel was explicitly selected and its renderer string confirmed with
NVIDIA offload variables unset. Added cost is about 4–9%, depending on
GPU and view. Intel remains approximately 35–46 rendering FPS at 720p;
this does not establish 60 FPS or native-1080p performance on that iGPU.

The same benchmark reads RGBA32F framebuffer output at each integer
shader time 12 through 20: zero nonfinite components on NVIDIA, AMD and
Intel, with `gl_error=0x0`. This catches NaNs/infinities that ordinary PNGs
cannot reveal, for these fixed parameters. Application PNGs were captured
at 1920x1080 on build-host and 1536x960 on build-host. Local and both remote
blackhole targets compile successfully; `git diff --check` passes.

## Installed shader regression

Meson now installs blackhole.frag and the 35 hyprsaver shaders to the
absolute `/usr/share/ncz-screensavers/shaders` directory that both loaders
already use. This deliberately matches the existing runtime contract even
with Meson's default `/usr/local` prefix. The `runtime-shaders` install tag
permits asset-only installation without unrelated executables. Standard
DESTDIR packaging works; both remote staged installs contain 36 shaders
beneath `stage/usr/share/ncz-screensavers/shaders`.

On both hosts, used `meson install -C build --no-rebuild --tags
runtime-shaders`, then launched the full binary path after `cd /`.
Every final blackhole capture above was made from that working directory.
The actual runtime log on both vendors states:

```
[diag] blackhole shader=/usr/share/ncz-screensavers/shaders/blackhole.frag
```

Both vendors compile/link and draw successfully from `/`. A representative
hyprsaver_aurora_gles3 also initializes and reaches frame 120 from `/` on
both vendors after the install. The other 34 hyprsaver shaders were staged
and installed, but not individually runtime-tested in this change.

The fopen audit found these two runtime shader loaders. molecule.c reads
optional user-supplied PDB files and quickhull.c writes output; neither is
a missing packaged runtime asset. No unrelated asset changes were made.
