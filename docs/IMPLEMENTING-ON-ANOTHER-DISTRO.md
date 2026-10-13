# Implementing ncz-screensavers on another Wayland distribution

The engine is independent of NCZ-OS and of the Singularity desktop. To offer these
screensavers on another distribution you provide the engine (packages) and a front end.

## Engine requirements

- **GPU:** OpenGL ES 3.2 through EGL on a hardware renderer. The hacks refuse to run on a
  software renderer (exit status 3) and the launcher never retries it. On a machine with no
  GPU at all set `NCZ_ALLOW_SOFTWARE=1`.
- **Compositor protocols** actually used:
  - hacks: `wl_compositor`, `wl_seat`, `wl_output`, `zwlr_layer_shell_v1` (background or
    overlay surface), `xdg_wm_base`, and optionally `wp_viewporter` (renders below native
    size and scales up; without it they render at native size);
  - idle daemon: `ext_idle_notifier_v1` (required) and `zwlr_output_power_manager_v1`
    (optional, for switching the display off);
  - locking: the daemon starts a lock program (`NCZ_LOCK_CMD`, else `/usr/local/bin/ncz-lock`,
    else the Singularity lock screen); that program should use `ext_session_lock_v1`. Any
    `zwp_idle_inhibitor_v1` held by a video player is honoured because the daemon uses the
    plain idle notification.
- **System services:** systemd user session (the idle daemon is a user unit) and logind for
  lock-on-suspend and the `Lock` signal.
- **Libraries at run time:** libwayland-client, libEGL, libGLESv2, glib2, python3; `grim` is
  optional (the one-time black-frame check).

## GPU behavior a front end should surface

Each GPU is calibrated once (`ncz-screensaver calibrate`, about 3 s, cached per GPU id) and
classed `weak` (20 ms or more on the reference test), `mid` (8 to 20 ms) or `strong`. Every
hack has a minimum class in `tiers.tsv`; a hack above the system's best usable class is
`flagged` in `list --json`. On machines with a second GPU the launcher renders shader hacks on
the faster GPU while on AC power (NVIDIA offload variables or Mesa `DRI_PRIME`, per process
only; the compositor is never touched). See `docs/GPU-CLASSES.md`.

## Idle and lock integration

Do not run your own idle timer. Enable `ncz-screensaver-idled.service` in the session and let
the user set `hack-idle-delay`, `lock-enabled`, `lock-delay` and `display-off-delay` through
the schema. If your desktop already locks the session by itself, leave `lock-enabled` false
(the default) so two lockers do not race.

## Writing a front end

Follow `docs/CLI-CONTRACT.md`. The worked example is the reference GTK4 application in
`contrib/gtk4-reference-app/` (PyGObject and libadwaita, no Singularity code): it lists the
catalog grouped by GPU suitability, previews with a confirmation for flagged hacks, edits the
per-hack options generated from `options/*.tsv`, and writes only the documented keys.

## Packaging

Build with `meson setup build -Dgtk4-reference-app=true` (or use `debian/`). The Debian
source package produces `ncz-screensavers` (engine, CLI, idle daemon, data files and schema),
`ncz-screensavers-gtk4-reference` (the reference application) and, when built against the
Singularity SDK, `ncz-screensavers-plugin`.
