# The idle daemon, the lock and Wayland

When the screensavers start, when the screen locks, when the display goes
off, and how a saver puts itself on the screen. The processes themselves
are in [architecture.md](architecture.md); every setting named here is in
[cli-reference.md](cli-reference.md).

## Idle daemon and timer

`launcher/ncz-screensaver-idled.c` subscribes to `ext-idle-notify-v1` on
the session seat and fires three timers:

| Timer | Default | Action |
|---|---|---|
| saver | `hack-idle-delay` (default 300 s) | `ncz-screensaver start --idle`; `cycle-delay` (default 600 s) is how long each saver runs in `random` and `playlist` mode |
| lock | `lock-delay` (default 60 s) after the saver | `ncz-screensaver stop`, then the lock command (`ncz-lock`, `NCZ_LOCK_CMD`, or fallback to `singularity-lockscreen`) |
| display-off | `display-off-delay` (default 0 = never) | `zwlr_output_power_v1` DPMS off |

The daemon also subscribes to logind's `PrepareForSleep` and `Lock`
signals; on `Lock` the saver is stopped and a lock command is run; on
`PrepareForSleep(true)` the saver is stopped (so the screen is already
clean when the system wakes).

The `idle-notify-v1` request honors application-held idle inhibitors
(video players, caffeine utilities, browsers), so the verifier is
enforced by the compositor and no extra code is needed.

Dismissal is universal: `resumed` from the protocol fires on any seat
input (key, pointer motion, touch) so dismissal does not depend on the
hack handling input. The hack's own quit-on-key is a second, independent
path.

A composer-restart or session-relogin drops the Wayland connection; the
service auto-restarts, and the daemon waits up to 30 s
(`NCZ_IDLED_SESSION_WAIT_MS`) for a usable session before returning 75
(EX_TEMPFAIL). That exit code is **not** in `RestartPreventExitStatus`,
so the unit stays in auto-restart during greeter and compositor
restarts; only the permanent schema-missing failure (`78`, EX_CONFIG)
is allowed to latch `failed`.

## Lock and idle interaction

- `lock-enabled` (default `false`) lets the user opt into screensaver-side
  locking. When `false`, the shell's own locker is the only lock. The two
  defaults exist to prevent two lockers racing.
- `lock-on-suspend` (default `true`) locks before suspend via
  logind's `Lock` signal.
- `lock-delay` (default 60 s) is counted from the saver's start. With
  `mode=off`, lock happens at `hack-idle-delay`.
- `ncz-lock` is the wrapper script; the daemon runs `ncz-screensaver
  stop` first, waits, then runs the lock command.
- The daemon never starts the saver while a lock screen is running.

## Wayland integration

### Protocols used

| Protocol | Used for |
|---|---|
| `wl_compositor`, `wl_seat`, `wl_output`, `wl_buffer` | every hack |
| `xdg_wm_base`, `xdg_surface`, `xdg_toplevel` | fullscreen fallback when `zwlr_layer_shell_v1` is absent |
| `zwlr_layer_shell_v1` | the normal path: `OVERLAY` layer with all four anchors, exclusive zone -1, exclusive keyboard interactivity |
| `wp_viewporter` | upscale reduced render buffers in the compositor |
| `ext-idle-notify-v1` (daemon) | three timers for saver, lock and display-off |
| `zwlr-output-power-v1` (daemon) | DPMS off/on |
| `zwp-idle-inhibitor-v1` (compositor-side) | honored automatically because the daemon uses the plain idle request |
| `ext-session-lock-v1` (lock program) | taken by `ncz-lock`, not by the saver |

### Per-output

A single binary binds one `wl_output`. Per-output hack instances need an
`NCZ_OUTPUT` option in the harness (currently open). On systems with a
single output, this is the production contract; on multi-monitor systems,
the hack covers whichever output the layer-shell (or xdg-toplevel)
fallback selects.

The compositor's GPU environment reaches the saver through the launcher's
`gpu_env_for()` helper, which copies a filtered set of names out of the
compositor's `/proc/PID/environ`: never `DRI_PRIME` or an NVIDIA offload
variable, and never a name containing `password`, `token` or `secret`.

## Configuration files and options

| File | Installed to | Reading |
|---|---|---|
| `config/dev.ncz.screensaver.gschema.xml` | `/usr/share/glib-2.0/schemas/` | the daemon; the launcher; the Singularity plugin; the GTK4 reference app |
| `assets/screensaver-chooser/hacks.tsv` | `/usr/share/ncz-screensaver-chooser/` and `/usr/share/ncz-screensavers/` | the launcher and the chooser |
| `assets/screensaver-chooser/options/<hack>.tsv` | `/usr/share/ncz-screensavers/options/`, for the hacks that ship a schema (Black Hole, Magmasimplex, and the common `options/_render.tsv`) | the launcher, both UIs, and the per-hack `--dump-schema` test |
| `assets/screensaver-chooser/options/<hack>.json` | `/usr/share/ncz-screensavers/options/` (select hacks) | the chooser dialog |
| `assets/screensaver-chooser/presets.tsv` | both directories | the chooser (preset list) |
| `assets/screensaver-chooser/categories.tsv` | both directories | the chooser (primary style category of every hack) |
| `assets/screensaver-chooser/category-list.tsv` | both directories | the chooser (style category definitions) |
| `assets/screensaver-chooser/tiers.tsv` | both directories | the launcher (flag check); both UIs (badge text) |
| `assets/screensaver-chooser/broken.tsv` | both directories | the launcher (exclusion); both UIs (badge) |
| `assets/screensaver-chooser/render-defaults.tsv` | both directories | the launcher (render scale rules) |
| `assets/screensaver-chooser/render-hints.tsv` | both directories | the launcher (per-hack render hints) |
| `assets/screensaver-chooser/sparse.tsv`, `ground.tsv` | both directories | the launcher (debug hooks) |

Every saver without its own `.tsv` still gets the three common render
options from `options/_render.tsv` (`render-scale`, `max-render-height`,
`render-scale-mode`), so `ncz-screensaver list-options HACK` never returns
an empty list for a shipped saver.

User state:

- `$XDG_CONFIG_HOME/ncz-screensavers/render.conf` — option file read by
  every saver that supports the shared render options.
- `$XDG_CONFIG_HOME/ncz-screensavers/chooser.conf` — chooser UI state
  (favourites, recently used, developer toggle, engine filter).
- `$XDG_CACHE_HOME/ncz-screensavers/gpu-class.json` — calibration cache.
- `$XDG_CACHE_HOME/ncz-screensavers/mesa/` — Mesa shader cache.
- `$XDG_RUNTIME_DIR/ncz-screensaver/` — flock + state.json.
- `$XDG_STATE_HOME/ncz-screensaver/failures.json` — per-hack bench
  record.

## The Singularity settings plugin

`plugin/screensaver/` is a normal Singularity plugin (libpeas extension
of `Singularity.Plugin`), laid out like the existing `sensors` and
`wallpapers-*` plugins. The settings widget is reached through
Settings > Plugins > Screensaver and Lockscreen.

The plugin uses libsingularity widgets only; it checks the schema, the
catalog, the launcher and the idle daemon binary, and shows a status page
instead of controls when one is missing.

The controls cover mode, the chooser, the random and playlist pools, the
idle and cycle delays, display power-off, the Black Hole colour mode,
render scale, the GPU-offload row (only on a hybrid-graphics machine) and
the three lock rows. Per saver it shows a simple view and an "Advanced"
expander with the remaining options, plus **Preview**, a still frame,
**Randomize** and **Copy seed** where the saver declares a seed, and a
developer switch that reveals the capture options. Presets appear as one
toggle per named scene.

It is built separately from the engine, against the Singularity SDK under
`/opt/singularity`, and packaged with `tools/make-plugin-deb.sh`. Its
descriptor declares the module `screensaver` and the page name
**Screensaver and Lockscreen**.

Build with `meson setup build -Dsingularity-plugin=enabled` against a
Singularity SDK (the headers, vapi and pkg-config file from
`/opt/singularity`); install to
`/opt/singularity/lib/singularity/plugins/screensaver/` with
libscreensaver.so (0755) and screensaver.plugin (0644).
## Where to read next

- [cli-reference.md](cli-reference.md) — every key named above.
- [user-guide.md](user-guide.md) — the same, from a user's point of view.
- [architecture.md](architecture.md) — the harness that draws on the surface.
