# Architecture: the processes, the harness and the shader pipeline

The engine end to end, in three parts. This page covers the processes and
what each one owns, the GLES3 harness every saver runs on, and the two
shader pipeline patterns. The rest of the engine is on its own pages:
[options-and-scenes.md](options-and-scenes.md),
[idle-and-wayland.md](idle-and-wayland.md),
[packaging.md](packaging.md), [GPU-CLASSES.md](GPU-CLASSES.md) and
[cli-reference.md](cli-reference.md).

## 1. Process model

Three small programs plus a systemd user service, all keyed on the
`dev.ncz.screensaver` GSettings schema:

| Process | Role | Implementation |
|---|---|---|
| `ncz-screensaver-idled` | idle timer on `ext-idle-notify-v1`, starts and stops the launcher, locks and switches the display off | `launcher/ncz-screensaver-idled.c` |
| `ncz-screensaver` | supervisor, launcher and CLI; picks a hack, builds its environment, supervises it | `launcher/ncz-screensaver` (Python) |
| `<hack>_gles3` | one screensaver, a Wayland layer-shell client | `src/gles3_<hack>.c` + `src/gles3_harness.c` |
| `ncz-screensaver-idled.service` | user unit that runs the idle daemon | `launcher/ncz-screensaver-idled.service` |

The idle daemon is a Type=simple user unit bound to the desktop session.
Restart=on-failure with a 5-second delay. It returns `75` (EX_TEMPFAIL) when
no usable Wayland session is up yet — that exit code is **not** in
`RestartPreventExitStatus`, so the unit stays in auto-restart during login,
greeter and compositor restarts; it returns `78` (EX_CONFIG) only when the
GSettings schema is permanently missing, which is a real error.

The launcher runs as a normal user process. It takes a per-user flock on
`$XDG_RUNTIME_DIR/ncz-screensaver/lock` so only one instance exists at a
time, and writes `state.json` (supervisor pid, hack id, pgid, start time,
failure counters) and `failures.json` (per-hack bench records).

A hack runs in its own process group. Stop is `SIGTERM` to the group, a 2 s
grace, then `SIGKILL`. The launcher never starts a hack while a lock screen
runs.

## 2. The GLES3 harness

`src/gles3_harness.c` owns Wayland + EGL + the frame loop. Per hack, the
meson build defines `HACK_PREFIX`, `HACK_PREFIX_ID` (C-symbol stem with
hyphens replaced by underscores) and `HACK_TABLE` (the
`xscreensaver_function_table` symbol the hack exports). The hack's table
is what the harness drives via `init_cb` / `draw_cb` / `reshape_cb` /
`free_cb`.

### What the harness does

1. Connects to Wayland, binds `wl_compositor`, `wl_seat`, `wl_output`,
   `xdg_wm_base`, `zwlr_layer_shell_v1` and `wp_viewporter`. If the compositor
   does not advertise `zwlr_layer_shell_v1`, the harness falls back to a
   fullscreen `xdg_toplevel` (set with `xdg_toplevel_set_fullscreen(NULL)`,
   which lets the compositor pick the real output, not whichever
   `wl_output` the registry happened to advertise first).
2. Opens EGL with `eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, ...)`.
   Requests `EGL_GL_COLORSPACE_LINEAR` when the extension is exposed, and
   sets `GL_FRAMEBUFFER_SRGB` off — the shader output is display-referred
   and the driver must not sRGB-encode it a second time.
3. Asks `zwlr_layer_shell_v1_get_layer_surface` for an
   `OVERLAY` layer with all four anchors (`TOP`/`BOTTOM`/`LEFT`/`RIGHT`),
   an exclusive zone of `-1` (extend below panels) and
   `KEYBOARD_INTERACTIVITY_EXCLUSIVE` (so a hack layer takes input focus).
   Sets the opaque region to the whole surface so the compositor does not
   blend against the desktop.
4. Calls the hack's `init_cb` once with the configured size; calls the
   hack's `draw_cb` each frame; calls `eglSwapBuffers` to commit. The
   frame loop does not manage its own `wl_surface.frame` callback — the
   EGL Wayland winsys paces presentation inside `eglSwapBuffers`, so
   manual frame callbacks race and never fire (the well-known Mesa Wayland
   winsys quirk that this code avoids by not using one).
5. Honors any key press as a quit signal (the layer surface has exclusive
   keyboard interactivity, so this is the universal "any key" handler).

### Render resolution

The buffer is sized to the surface by `apply_render_size()`. When the
shared options (`--render-scale`, `--max-render-height`,
`--render-scale-mode`) call for a smaller render, the harness allocates a
smaller EGL buffer and asks `wp_viewporter` to upscale it in the compositor
(no extra GPU pass). Without `wp_viewporter`, or when nothing needs
reducing, the buffer is native size.

### Headless mode

`NCZ_HARNESS_HEADLESS=WxH` (or `1` for 256×144) skips Wayland entirely
and runs the hack against a surfaceless EGL context. Hacks whose capture
mode renders into their own framebuffer (only Black Hole today, via
`--capture=PATH`) exit cleanly in this mode and are useful for offline
preview and test scripts. Hacks without a capture mode spin forever in
headless mode and are killed after 100 000 draw calls by the harness.

### Guard against llvmpipe / swrast

`src/ncz_gpu_guard.c` reads `glGetString(GL_RENDERER)` and refuses to run on
`llvmpipe`, `softpipe` or `swrast` (exit 3). The launcher never retries
on this exit and never benches the hack. On a machine with no GPU at all,
set `NCZ_ALLOW_SOFTWARE=1`.

## 3. Shader pipeline

Two patterns, both GLES 3.2 native:

1. **In-house engines** — one driver `src/gles3_*.c` plus one or more
   `.frag` / `.glsl` files in `vendor/<engine>/`. The driver compiles them
   at startup, owns the FBO and feedback buffers (where used), and pushes
   per-frame uniforms (time, seed, palette, options). Three ship in 0.8.9:
   Black Hole, Magmasimplex and Fractasia. The same driver shape is used by
   the engines still in the tree but gated out of the install set (Ephemeris,
   Megacity, Neonspacewar, Genxvectorcade, Leviathan); see
   `docs/PROVENANCE.tsv`, which records their status.
2. **Shader hacks** (30 hyprsaver + 30 xshadertoy) — one driver per family
   (`src/gles3_hyprsaver.c`, `src/gles3_xshadertoy.c`) and one `.frag` /
   `.glsl` per hack. Meson instantiates the driver in a `foreach` loop,
   one binary per shader, with `-DHACK_PREFIX=<id>`,
   `-DHACK_PREFIX_ID=<id-with-hyphens-replaced>`,
   `-DSHADER_FILE=<file>`.

### xshadertoy driver (the larger of the two)

`src/gles3_xshadertoy.c` is a generic Shadertoy-API host. It:

- Loads `vendor/xshadertoy/glsl/<shader>.glsl` at runtime, with fallbacks
  build-tree → repo-relative → `/usr/share/ncz-screensavers/shaders/`.
  Blackhole's "dies when launched from /" regression is explicitly
  avoided: the absolute-install path is the production contract.
- Strips any leading `#version` / `#extension` / `#pragma` /
  `precision` / C++ comment block from the `.glsl`.
- Prepends the Shadertoy-API preamble (uniform declarations + GLSL-1.2-to-1.3
  compat shims + a `void main()` wrapper). The preamble's GLSL helpers
  are adapted from upstream xscreensaver's `xshadertoy.c` (commit
  `b99f621`), preserved under the same X Consortium MIT-style permission
  notice.
- Compiles, links, uploads the Shadertoy uniform set every frame
  (`iResolution`, `iTime`, `iTimeDelta`, `iFrameRate`, `iFrame`,
  `iDate`, `iMouse`), binds a 1×1 RGBA8 black texture to `iChannel0..3` so
  shaders that sample them get a known value instead of uninitialized
  driver memory, and draws a fullscreen quad.
- Multi-pass (`--program0`..`--program4` chained) is intentionally
  **not** implemented. All 30 shipped shaders are single-pass; the three
  that reference `iChannel0` (`gimbalharmonics`, `protophore`,
  `skyline`) render correctly with the 1×1 dummy black texture.

### hyprsaver driver

`src/gles3_hyprsaver.c` is the equivalent for the hyprsaver family. It
ports the upstream Rust runtime's `prepare_shader()` verbatim — preamble
+ uniform declarations + a `vec3 palette(float t)` LUT helper + a
`void main()` wrapper that calls `_hyprsaver_main()`. The vendored `.frag`
files are unchanged; the round-11 driver without the preamble failed compile
on every binary.
## Where to read next

- [options-and-scenes.md](options-and-scenes.md) — per-saver options,
  presets, the randomize-everything mode and the palettes.
- [idle-and-wayland.md](idle-and-wayland.md) — the idle daemon and timer,
  the lock interaction, the Wayland integration and the configuration
  files.
- [GPU-CLASSES.md](GPU-CLASSES.md) — the class system, the calibration
  benchmark and the per-saver tier table.
- [packaging.md](packaging.md) — the packages, the plugin and how to add a
  saver.
- [cli-reference.md](cli-reference.md) — every command and settings key.
