# Sky1 (and x86) screensaver stutter: root cause

Date: 2026-09-29. Investigation branch: `investigate/sky1-stutter-2026-09-29`
(from `consolidated/ncz-os-mvp` 9ef00ca).

## Summary

The periodic stutter is not a GPU bandwidth, driver, cache or compositor
problem. It is our own diagnostic code: every GLES3 hack ran a synchronous
`glReadPixels()` on a fixed frame cadence, and each readback drains the GPU
pipeline and freezes the animation.

| Source | Cadence | What it does | Freeze (O6N, Mali-G720) |
|---|---|---|---|
| `gles3_harness.c` `report_framebuffer()` | every 60th frame (about 1 Hz) | full-frame `glReadPixels` (RGBA8), then a serial per-byte FNV hash, histogram and dark-quadrant pass on the CPU | 72-110 ms (readback alone) plus CPU pass; p99 frame interval 125-147 ms |
| `gles3_xshadertoy.c` (37 hacks) | every 60th frame | four 1x1 `glReadPixels` | 55 ms |
| `gles3_transitions.c` | every 30th frame | four 1x1 `glReadPixels` | same class (not separately timed) |

Fix (branch above): frame 4 stays as a one-shot startup sample; periodic
samples are opt-in via `NCZ_DIAG_FRAMEBUFFER=1` (`NCZ_FRAME_DUMP` still gets
its frames). Shared helper `ncz_diag_sample_frame()` in `gles3_compat.h`,
static regression test `tools/tests/test_no_periodic_readback.py` (verified to
fail on the old xshadertoy code and pass on the new).

## Method

`tools/stutter/ncz_gltimer.c`: an LD_PRELOAD shim that logs the interval and
duration of every `eglSwapBuffers` and every slow `glReadPixels`,
`glCompileShader`, `glLinkProgram`, `glFinish`, `glTex*Image2D`. Analysis with
`tools/stutter/frame-stats.py`. A/B = the installed 0.3.3 binary versus the
same source rebuilt with the fix, alternating, 15-20 s runs, same session.

Host state (O6N): 192.0.2.3, kernel 7.2.8-sky1-ncz, Mali (mali_kbase)
entry, Mali GPU devfreq pinned at 1.0 GHz (`performance`) in every run,
labwc, panel ViewSonic VP2488-4K DP-2 3840x2160@59.997 Hz, output scale 1.75
so hacks render **2194x1234** logical (not 3840x2160), temperature 55-57 C.
build-host: 192.0.2.6, radeonsi Navi14, 3072x1920 at scale 2.0 so hacks
render 1536x960.

## Measurements: frame interval (ms) at 60 Hz vsync (16.7 ms)

hitches = intervals > 25 ms. `orig` = installed 0.3.3; `fix` = this branch.

O6N (Mali-G720, 2194x1234):

| Hack | Build | fps | p50 | p95 | p99 | max | hitches |
|---|---|---|---|---|---|---|---|
| blackhole (run 1) | orig | 46.6 | 20.0 | 20.3 | 146.8 | 156.6 | 15 |
| blackhole (run 1) | fix | 60.1 | 16.6 | 16.8 | 16.9 | 18.1 | 0 |
| blackhole (run 2) | orig | 48.6 | 19.1 | 20.6 | 135.5 | 144.1 | 16 |
| blackhole (run 2) | fix | 48.9 | 20.6 | 20.9 | 21.0 | 22.6 | 0 |
| blackhole (sweep) | orig | 53.2 | 17.4 | 19.2 | 134.5 | 141.6 | 13 |
| blackhole (sweep) | fix | 56.8 | 17.8 | 18.9 | 19.1 | 20.7 | 0 |
| hyprsaver_plasma | orig | 58.4 | 16.7 | 16.7 | 84.0 | 126.6 | 15 |
| hyprsaver_plasma | fix | 60.1 | 16.7 | 16.7 | 16.7 | 16.7 | 0 |
| xshadertoy_starnest | orig | 48.8 | 19.1 | 19.5 | 125.5 | 127.7 | 24 |
| xshadertoy_starnest | fix (harness only) | 52.3 | 19.1 | 19.4 | 55.3 | 57.1 | 13 |
| xshadertoy_starnest | fix (harness + xshadertoy) | 60.1 | 16.7 | 16.7 | 17.0 | 18.2 | 0 |

The same 1 Hz hitch, all with the unfixed harness (orig), other hacks:
klein 79/109 (p99/max ms, 9 hitches), hypertorus 75/110 (9), projectiveplane
76/108 (9), cityflow 87/89 (9), hexstrut 90/92 (9), noof 62/73 (10),
gravitywell 59/65 (10).

build-host (radeonsi, 1536x960), blackhole: orig p99 43.7 ms max 44.9 (15
hitches) versus fix p99 16.9 max 17.6 (0 hitches), both 60.1 fps. The x86
cost is smaller (fast readback, smaller frame) but the periodic freeze is the
same mechanism, so build-host and build-host (same hardware) stutter for the same
reason. build-host and build-host were running idle-timer screensavers during the
window, so they were not re-measured (not disturbed).

## Ranking of causes

| # | Cause | Verdict | Evidence |
|---|---|---|---|
| 1 | Periodic `glReadPixels` diagnostics (harness, xshadertoy, transitions) | RULED IN, primary | hitch every 60 frames, each equal to the SLOW `glReadPixels` event; 0 hitches after fix on 5 hacks x 2 hosts |
| 2 | Steady-state GPU load of heavy shaders at the render size | Real but secondary: blackhole runs 17.4-20.6 ms (48-57 fps) at 2194x1234, marginal against 16.7 ms; light shaders hit 60 fps | LEAD 1's `render-scale`/`max-render-height` addresses it; CPU is only 9% so it is GPU bound |
| 3 | Classic hacks (cubestorm 1.7 fps, gibson 7-8, crackberg 12.8, geodesic 15.7, voronoi 48 with jitter) | Separate defect, not the stutter | CPU bound in the Mali driver threads (`mali-ev`, `mali-cp`; cubestorm process 139% CPU, geodesic 137%) from the GL1 immediate-mode compat path; unchanged by the fix |
| 4 | Shader compile/link hitches mid-run | RULED OUT | compile/link only at t=0 (blackhole: link 23 ms + 100-120 ms + compile 15 ms once); no SLOW compile/link event after start in any run |
| 5 | Shader cache cold vs warm | RULED OUT for stutter; startup only | no Mali binary cache: link time identical (98-124 ms) on repeated runs; cost is once at start. Mesa cache dir exists on O6N and is irrelevant to the Mali blob |
| 6 | Launcher/systemd env | RULED OUT | `ncz-screensaver-idled.service` has no sandboxing or resource control; hack env differs only by GPU env; the launcher hack process is the same binary |
| 7 | Launcher `verify-render` grim screencopy at ~4 s | Minor, one-shot | grim takes 312 ms wall and adds one 42 ms frame interval (about 26 ms extra) at that instant; not periodic |
| 8 | Refresh rate / 30 Hz | RULED OUT | mode 59.997 Hz; steady interval 16.7 ms |
| 9 | Thermal / DVFS | RULED OUT | 55-57 C, Mali devfreq fixed at 1.0 GHz (performance governor) through every run |
| 10 | Mid-run recompiles from adaptive quality tier | RULED OUT for blackhole | tier fixed at init ("tier=medium scalar=0.33 static prior"); no compile events later |
| 11 | Compositor path: no modifiers (`WLR_DRM_NO_MODIFIERS=1`), linear XR24 scanout, no direct scanout at scale 1.75 | NOT the stutter; efficiency question left open | with the readback fix blackhole/plasma/starnest reach 60.1 fps p99 <= 17 ms through this exact compositor path. Not A/B tested: it needs a labwc restart in the operator's session. No documented reason for the setting exists in cix-installer (`ncz-gpu-env`, `20-desktop.sh`) or MNEMOS; treat as an untested workaround and test on a scratch session before removing |
| 12 | wp_viewporter / fractional scale | Available | labwc advertises `wp_viewporter` v1, `wp_fractional_scale_manager_v1` v1, `wp_presentation` v2: LEAD 1's viewport-scaled half-res path is viable. Hacks today do not use fractional scale, so at scale 1.75 the compositor already upscales a 2194x1234 buffer (about 0.33 of 4K pixels) |

## Recommendations

1. Merge the fix (this branch). Expected: the 1 Hz 55-145 ms freeze disappears
   on every host; lightweight hacks become 60 fps clean.
2. Keep the resolution cap (LEAD 1) as a GPU-headroom measure for heavy
   shaders, not as the stutter fix; note that "4K" is already 2194x1234 at the
   current output scale.
3. Track the slow classics (cubestorm, gibson, crackberg, geodesic, voronoi)
   as their own bug: driver-thread CPU cost of the immediate-mode shim.
4. Do not add an application shader-binary cache: compile is a one-time
   ~150 ms at start.
5. Test `WLR_DRM_NO_MODIFIERS` removal separately, supervised, on a scratch
   labwc session (not required for the stutter fix).

## Housekeeping

O6N was left in its original state: no labwc/env changes; scratch files only in
`/home/mini/Projects/stutter` (shim, run scripts, out logs, a patched build
tree); no processes left running; lock directory removed.
