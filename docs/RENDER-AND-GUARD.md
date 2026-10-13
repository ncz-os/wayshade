# Shared harness options, render resolution and the software-renderer guard

Every GLES3 hack (hyprsaver, xshadertoy, Black Hole, wave2, the classics) is started through `src/gles3_harness.c`, which handles the following before any Wayland or EGL work.

## Command line

`--help`, `--list-options`, `--version` and `--dump-schema` (the hack's own options) / `--dump-schema-common` (the generic options below) print and exit 0. Any unknown `--option` or stray argument prints an error and exits 2: a typo can no longer fall through into running the full simulation. Invalid option values are reported and ignored.

## Render resolution

| Option | Env | Meaning |
|---|---|---|
| `--render-scale=0.25..1` | `NCZ_RENDER_SCALE` | fraction of the output the shader renders at |
| `--max-render-height=PX` | `NCZ_MAX_RENDER_HEIGHT` | cap on the render height, aspect kept, 0 = unlimited; when not set, the platform default applies |
| `--render-scale-mode=auto\|fixed` | `NCZ_RENDER_SCALE_MODE` | auto steps the scale down when frames are slow |

Also readable from `$XDG_CONFIG_HOME/ncz-screensavers/render.conf` (group `[render]`). Precedence: command line > environment > config file > defaults.

How it works: the harness creates a smaller `wl_egl_window` buffer and sets `wp_viewport` destination to the surface size, so the compositor upscales it (no extra GPU pass); screenshots (grim) show the full-size image, frame dumps show the render size. Without `wp_viewporter` the harness logs it and renders at native size. GPU class (`NCZ_GPU_CLASS=weak|mid|strong` from the launcher's calibration, else inferred from GL_RENDERER): a weak class (Intel, CPU renderers) starts at render scale 0.5 when the scale is not set, and Black Hole uses lighter defaults for its optional extras (see `BLACKHOLE-PRESETS.md`). Platform defaults when `max-render-height` is not set: Mali (Sky1) caps at 1080, integrated Intel caps at 1080 when the surface is taller than 1440, everything else is native. `auto` mode steps the scale 1.0 to 0.75 to 0.5 when the 95th percentile frame time stays above 40 ms for 3 s (at most once per 3 s, never back up), and logs each step.

The size math is pure and unit tested (`tests/test_harness_cfg.c`): aspect kept, rounded to even sizes, never larger than native.

## Software-renderer guard

After the context is created the harness checks `GL_RENDERER`. A CPU renderer (llvmpipe, softpipe, swrast, lavapipe) is refused with exit code 3 and a `[guard]` message in well under a second, before any shader work. When the machine has a hardware GPU (a DRM render node whose kernel driver is amdgpu, i915, xe, nouveau, nvidia, panthor, panfrost, msm, etc., or a `/dev/mali*` node) there is no override. `NCZ_ALLOW_SOFTWARE=1` (or the alias `NCZ_ALLOW_SOFTWARE_FALLBACK=1`) is honored only when no hardware GPU exists (GPU-less VMs). Virtual devices (vgem, vkms, simpledrm, virtio_gpu) do not count as hardware. Tested with a fake sysfs fixture in both directions. The launcher should treat exit code 3 as an environment fault: log loudly, notify, never retry in a loop, never bench the hack.

## Run statistics

Every run prints one `[stats]` line at exit and on `SIGUSR1`: shader compile and link counts and time, first-frame time, frame count, and frame-time p50/p95/p99/max for the first 5 s versus steady state. It uses CPU timestamps only (no GPU readback). Periodic framebuffer samples are opt-in via `NCZ_DIAG_FRAMEBUFFER=1`.

## Emulating a 4K panel (test only)

`NCZ_TEST_SURFACE_SIZE=WxH` makes the render-size logic (scale, max-render-height, platform cap, GPU class default) act as if the compositor had configured a WxH surface, while the viewport destination stays the real surface. It measures the GPU cost of a size larger than the attached panel, for example 3840x2160 (native 4K) or 2194x1234 (a 4K panel at output scale 1.75, where layer-shell surfaces are in logical pixels) on a 1080p monitor. The buffer is then larger than the surface and the compositor downsamples it, so the compositor cost is not the real one; the hack cost is. `tools/blackhole-eval/o6sweep3.sh` uses it. Not for production use.
