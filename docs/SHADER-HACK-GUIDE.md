# How to write a shader hack

A shader hack is a fragment shader plus a thin C wrapper that the shared harness (`src/gles3_harness.c`) runs on a Wayland layer-shell surface with EGL/GLES 3.2. Two engines already exist; new work (for example the port of the 12 classics) should use them or add a third with the same contract.

## The two shader engines

**xshadertoy** (`src/gles3_xshadertoy.c`, shaders in `vendor/xshadertoy/glsl/*.glsl`). Shadertoy convention: the shader defines `void mainImage(out vec4 fragColor, in vec2 fragCoord)`. The wrapper prepends `#version 300 es`, precision highp, `out vec4 frag_color` and these uniforms:

| Uniform | Type | Meaning |
|---|---|---|
| `iResolution` | vec3 | render size in pixels (x, y, 1) |
| `iTime` | float | seconds since init |
| `iTimeDelta` | float | seconds since the previous frame |
| `iFrameRate` | float | running frame rate |
| `iFrame` | int | frame counter |
| `iDate` | vec4 | year, month, day, seconds of day |
| `iMouse` | vec4 | pointer (unused on the desktop) |
| `iSeed` | vec4 | four random floats in [0,1), constant for the run, different every run: use it to pick per-launch composition |
| `iChannel0..3` | sampler2D | bound to a 1x1 black texture |

**hyprsaver** (`src/gles3_hyprsaver.c`, shaders in `vendor/hyprsaver/shaders/*.frag`). The shader is a complete `#version 320 es` body with `void main()` writing `fragColor`; the wrapper injects the uniforms `u_time`, `u_resolution`, `u_mouse`, `u_frame`, `u_alpha`, `u_speed_scale`, `u_zoom_scale` and a `vec3 palette(float t)` helper (baked color LUT).

**Black Hole** (`src/gles3_blackhole.c`, `vendor/blackhole/blackhole.frag`) is a dedicated wrapper with its own uniforms and option table; use it as the reference for a hack with options.

Shaders are found at `vendor/...` relative to the working directory (development) and at `/usr/share/ncz-screensavers/shaders/` (installed); `NCZ_SHADER_DIR` overrides for xshadertoy.

## Adding a hack

1. Put the shader in the vendor tree and list it (`xshadertoy_shaders` or `hyprsaver_shaders` in `meson.build`; a new engine gets its own `executable()` block that links `common_gles3_sources`).
2. Add the executable name (`<engine>_<name>_gles3`) to `ncz_ship_bins` in `meson.build` to ship it. Then run `python3 tools/generate-catalog.py meson.build assets/screensaver-chooser/hacks.tsv`; `hacks.tsv` keeps exactly four columns. A build test fails if the catalog drifts.
3. If the scene is legitimately sparse (line art, a grid, a chart on black) list it in `assets/screensaver-chooser/sparse.tsv` (id, tile floor, reason).
4. Keep GLSL ES 3.00 clean: global initializers must be real constant expressions (Mali rejects `effects[5]` or built-in calls in a global initializer, error S0012); test on a Mali GPU.

## What the harness gives every hack

* **Options.** Every binary handles `--help`, `--list-options`, `--version`, `--dump-schema` before any GL work; unknown options exit 2. A hack with its own options defines the weak function `ncz_hack_options(size_t *n, const char **env_prefix, const char **group)` returning an `ncz_opt_def` table (see `src/blackhole_opts.h`) and reads values with `ncz_opts_get*(ncz_harness_opts(), "name")`. Precedence: command line > environment `<PREFIX><OPTION>` > `~/.config/ncz-screensavers/<group>.conf` > defaults. Invalid values are reported once and ignored. Ship a declarative schema `assets/screensaver-chooser/options/<hack>.tsv` (10 tab-separated columns: name, type bool|int|float|enum|string, default, min, max, choices, label, description, group, env); generate it with `<binary> --dump-schema` and a meson test keeps it in sync. The launcher passes stored values as environment variables and the settings UI renders widgets from the schema.
* **Render resolution.** Generic options `--render-scale`, `--max-render-height`, `--render-scale-mode` (env `NCZ_RENDER_SCALE`, `NCZ_MAX_RENDER_HEIGHT`, `NCZ_RENDER_SCALE_MODE`). The harness shrinks the EGL buffer and the compositor upscales it with `wp_viewport`, so a hack must read its size from `iResolution` / `u_resolution` / the frame size the harness publishes each frame, never from the output size. Mali defaults to a 1080p cap.
* **Software-renderer guard.** Exit code 3 with a `[guard]` message when the context is a CPU renderer and the machine has a GPU. Never handle it in the hack.
* **Diagnostics.** `[stats]` line at exit and on `SIGUSR1` (compile/link counts, first-frame time, frame-time percentiles, CPU timestamps only). Do not add periodic `glReadPixels`: a synchronous readback every second was the source of the visible stutter; periodic samples are opt-in with `NCZ_DIAG_FRAMEBUFFER=1`. Frame 4 is sampled once for the black-frame validator.
* **Lifecycle.** The wrapper exports an `xscreensaver_function_table` (init, draw, reshape, free callbacks). Input dismissal, DPMS, multi-output and the exit signals are handled by the harness and the launcher; the hack draws one frame per call and must stop cleanly on `SIGTERM`.
* **Time and seed.** Use the uniforms above for time; use the per-run seed (`iSeed`, or `--seed` for options-based hacks) for anything random so `--seed` reproduces a run. Compile every shader variant at init: changing an option must never recompile (options are uniforms).

## Testing

`meson test` runs the option parser, render-size, guard (with a fake sysfs fixture) and schema-sync tests. On a host: run `<binary> --seed=42` inside the compositor session (GPU environment from the labwc process), with `timeout`, and look at the `[stats]` line: steady-state p99 near the refresh interval and no hitches. Never run a hack over ssh without the session environment (it would fall to a CPU renderer; the guard now refuses that).
