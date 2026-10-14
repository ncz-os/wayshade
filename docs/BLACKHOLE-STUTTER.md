# Black Hole stutter on build-host and build-host: root cause (2026-09-29)

Symptom: a visible hitch about once per second while Black Hole runs on build-host and build-host (both MacBookPro16,1, Radeon Pro 5500M / Navi14 on the eDP panel, labwc on card1, 60 Hz).

## Root cause

`src/gles3_harness.c` called `report_framebuffer()` on frame 4 and on every 60th frame in production. That function does a synchronous full-frame `glReadPixels` and a CPU histogram over every pixel, which stalls the pipeline for about 20 ms. It affected every hack that uses the harness, not only Black Hole. Fix: the every-60th-frame report now runs only when `NCZ_HARNESS_DIAG` or `NCZ_FRAME_DUMP` is set; frame 4 is still sampled once, and the cheap `[diag] frame #N` progress line stays unconditional.

## Evidence (NCZ_BLACKHOLE_PERF_LOG=2, 1536x960, default options, 45 s, nothing else running)

| Run | p50 | p95 | p99 | max | frames over 25 ms |
|---|---|---|---|---|---|
| build-host, periodic report on (before) | 16.67 ms | 16.7 | 36 to 40 | 40.9 | frames 62, 122, 182, 242, 302, 362, ... exactly every 60 frames |
| build-host, periodic report off (after) | 16.67 | 16.8 | 17.5 | 17.95 | only frame 6 (startup) |
| build-host, before | 16.67 | 16.8 | 38.8 | 41.0 | frames 302, 362, 422, ... every 60 |
| build-host, after | 16.67 | 16.8 | 17.4 | 17.5 | only frame 6 |

## Hypotheses checked and ruled out

* GPU load: amdgpu `gpu_busy_percent` 38 to 68 percent at 755 MHz; frames complete inside the 16.7 ms budget. Two extra ultra-quality instances running at the same time did not produce a single missed frame.
* DPM slow ramp: no forced performance level was needed; no permanent power policy was changed.
* GPU selection: the hack and labwc both use the AMD GPU (card1 / renderD129), no cross-GPU copy. The Intel iGPU has no connected output.
* Shader compile or link during the run: none. The program is compiled and linked once in init (`glCompileShader` and `glLinkProgram` appear only there); the palette, flyby, step count and cross-fade are uniforms. The only over-25 ms frame after the fix is frame 6 (startup). Uniform changes never relink.
* Present or vsync: `eglSwapInterval(1)`; p50 is exactly the 16.67 ms refresh.

## Adaptive quality (new, for slower GPUs)

The GPU tier module's frame-time hook was never called by this hack, so quality was the static prior all run. `blackhole_gles3` now measures the frame period, estimates the refresh, counts missed frames and moves one quality level (ray steps 80..260 at full resolution, then render scale 1.0..0.5) with hysteresis: down at more than 10 percent misses in the last 60 frames, up after 10 s without a miss, never back to a level that failed within 90 s. It logs each change (`[diag] blackhole adapt ...`). Tested by rendering on the build-host Intel iGPU across the GPU boundary (`DRI_PRIME`), where 50 to 100 percent of frames missed at the start: it converged to 80 steps at scale 0.5 within 10 s and then held 0 misses in 300 frames for the remaining 80 s with no oscillation. Options: `quality` (auto adapts; anything else is fixed), `adaptive`, `render-scale`.

## Observing it

`NCZ_BLACKHOLE_PERF_LOG=2` prints a line every 300 frames: p50, p95, p99, max, missed frames, refresh, level, steps, scale, plus the frame numbers of hitches over 25 ms.
