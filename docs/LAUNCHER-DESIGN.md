# NCZ-OS screensaver launcher, idle daemon and settings UX - design

Status: MVP design, 2026-09-29. Owner: launcher/, tools/, config/ (LEAD 2).
The hack renderers (src/, vendor/) and packaging are owned by the consolidation
lead; this document only fixes the interfaces between them.

## 1. What the four test hosts actually run (investigated live, read-only)

| Fact | build-host .6 / build-host .20 | build-host .85 | O6N .3 |
|---|---|---|---|
| CPU/GPU | x86_64, MacBookPro16,1: Navi14 (radeonsi, GLES 3.2 Mesa 26.1.6) + Intel iGPU | x86_64, RTX 2060M + Intel CML iGPU (default EGL = Intel Mesa) | aarch64, Sky1 (session GPU env from `ncz-gpu-env`; a bare ssh shell sees llvmpipe) |
| Session | greetd -> `singularity-labwc-session` -> `/opt/singularity/bin/labwc` (wlroots 0.20) -> `singularity-desktop` shell | same | same |
| Wayland socket | `/run/user/1000/wayland-0` | same | same |
| Idle daemon present | none (no swayidle, no wlopm) | none | swayidle + `ncz-idle-manager` (older cix-installer 57/58 design) |
| Screensaver bits installed | none | none | `ncz-screensaver-run`, `dev.ncz.screensaver` schema, `hacks.tsv` |
| Lock | `/opt/singularity/bin/singularity-lockscreen` (ext-session-lock-v1), `ncz-lock` wrapper on O6N | same | same |
| Outputs | one (eDP-1) | one (eDP-1, 4K) | one (DP-2, 4K) |
| Tools | grim, wlr-randr, python3-gi, gir1.2-adw-1, imagemagick | same | same + meson/ninja/valac |

Wayland globals advertised by the compositor (verified on build-host): `ext_idle_notifier_v1` v2,
`zwp_idle_inhibit_manager_v1`, `ext_session_lock_manager_v1`, `zwlr_layer_shell_v1` v4,
`zwlr_output_power_manager_v1`, `zwlr_screencopy_manager_v1` v3,
`ext_image_copy_capture_manager_v1`, `zwlr_virtual_pointer_manager_v1`,
`zwp_virtual_keyboard_manager_v1`. The virtual pointer/keyboard globals let the test
harness inject real input inside the session.

How the hacks present themselves today (read from `src/wl-screenhack.c`): each hack
is its own process (`/usr/lib/ncz-screensavers/<id>`), creates a `zwlr_layer_shell` surface on
the OVERLAY layer, all four anchors, exclusive zone -1, EXCLUSIVE keyboard
interactivity, and quits on a key press. Only one `wl_output` is used. So a hack is already
a correct "screensaver surface"; the launcher must not add another shell protocol.

## 2. Decision: minimal robust MVP

Rejected: session-lock as the saver surface (a crashed client leaves the session
permanently locked; labwc keeps `locked` true with no client), wayshade, GNOME/X11 style
daemons, swayidle (not installed on 3 of 4 hosts; extra moving parts).
Chosen: three small programs plus one systemd user unit, all keyed on the existing
`dev.ncz.screensaver` GSettings schema so the already-written Singularity
"screensaver-chooser" plugin keeps working (schema is extended, never broken).

```
 systemd --user                       GSettings dev.ncz.screensaver (dconf)
   ncz-screensaver-idled  (C) <------- live "changed" signals
     |  ext-idle-notify-v1: timer A (start saver), B (lock), C (display off)
     |  zwlr-output-power-management: DPMS off/on
     |  logind PrepareForSleep/Lock: lock
     |-- idled(A)   -> exec `ncz-screensaver start --idle`
     |-- resumed(A) -> exec `ncz-screensaver stop`
     |-- idled(B)   -> `ncz-lock`   (stops the saver first)
     '-- idled(C)/resumed(C) -> DPMS off/on
 ncz-screensaver (Python 3, stdlib only): supervisor/launcher/CLI
     start|stop|status|list|preview|set-timeout|set-hack|set-mode|set-color|enable|disable|config
 (settings UX: the Singularity plugin on NCZ-OS; the GTK4 app is now the reference application in contrib/)
```

### Idle detection and dismissal
Compositor-side `ext_idle_notification_v1` (v1 request, which honors idle inhibitors)
gives both "idle" and "resumed". "resumed" fires on ANY seat input (key, pointer motion,
touch), so dismissal is universal and does not depend on the hack handling input. The
hack's own quit-on-key is a second, independent path. Idle inhibitors (video players,
Singularity caffeine, browsers) are enforced by the compositor: while inhibited the idle
notification never fires, so no extra code is needed. Harness proves this with a
zwp_idle_inhibit client.

### Launcher behavior (`ncz-screensaver start`)
- Single instance: `flock` on `$XDG_RUNTIME_DIR/ncz-screensaver/lock`; state file
  `state.json` (supervisor pid, hack id, pgid, start time, failure counters).
- Selection by `mode`: `off` | `one` (hack-id) | `random` (pool = `random-hacks`, empty = whole
  catalog; never repeats the previous hack) | `playlist` (`playlist` order). Rotation every
  `cycle-delay` seconds for random/playlist.
- Hack process runs in its own session/process group; stop = SIGTERM to group, 2 s grace,
  then SIGKILL. Supervisor removes state on exit.
- Safety net: a hack that exits within 3 s, or dies of a signal, counts as a failure; the
  supervisor immediately moves to the next candidate; 3 consecutive failures ends the
  session quietly (the idle chain proceeds to lock/DPMS) and a per-hack failure record under
  `$XDG_STATE_HOME/ncz-screensaver/failures.json` benches a repeatedly failing hack for 24 h.
- Black-frame guard (`verify-render`, default on when `grim` exists): ~4 s after start, grab
  a downscaled screenshot via screencopy; an entirely black frame counts as a failure.
- Environment: derives `WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR` when missing, passes
  `NCZ_BLACKHOLE_COLORS` for `blackhole_gles3` from `blackhole-color-mode`
  (`stylized` = variable unset; `kipthorne`; `faithful`; anything else passed through).
- Multi-monitor MVP: the hack covers the output the hack picks (first output); idled
  DPMS handles all outputs. Per-output hack instances need an `NCZ_OUTPUT` option in the
  hack harness (open item for src/).

### Lock and DPMS interplay
`lock-delay` is counted from saver start (existing semantic); with `mode=off`, lock happens at
`hack-idle-delay`. `ncz-lock` (wrapper installed by cix-installer, or a built-in fallback
that calls `singularity-lockscreen`) is invoked by idled and always calls
`ncz-screensaver stop` first. `display-off-delay` (0 = never) is absolute idle seconds.

### Config schema (config/dev.ncz.screensaver.gschema.xml, superset of the current one)
Existing keys keep names, types and semantics: `mode` (off/one/random + new `playlist`),
`hack-id`, `random-hacks`, `hack-idle-delay`, `cycle-delay`, `lock-enabled`, `lock-delay`,
`lock-on-suspend`. New: `playlist` (as), `blackhole-color-mode` (s), `display-off-delay` (i),
`verify-render` (b). Ranges lowered to min 1 s so the test harness can use short timers.

### Testability
`GSETTINGS_BACKEND=keyfile` plus `XDG_CONFIG_HOME` runs idled and the launcher against
an isolated config with no dconf. `NCZ_SCREENSAVER_DIRS` overrides the binary search path,
`NCZ_SCREENSAVER_CATALOG` the catalog. `ncz-screensaver-idled --dry-run` prints the notification
plan without acting.

## 3. Out of scope (per operator)
GPU probing/fidelity tiers, wayshade, ISO builds, kernel changes.

## 4. As built and verified (2026-09-29)

Files: `launcher/ncz-screensaver` (Python launcher and CLI), `launcher/ncz-screensaver-idled.c` (idle daemon),
`launcher/ncz-screensaver-idled.service`,
`launcher/ncz-screensaver-run-compat` (old `--start/--stop/--preview` interface), `config/dev.ncz.screensaver.gschema.xml`,
`debian/ncz-screensavers.postinst` (disables the older `ncz-idle-manager` user unit), `tools/host-test.sh` with
`tools/host-test-agent.py`, `tools/wl_poke.py` (raw-wire Wayland client: virtual pointer, virtual keyboard, idle inhibitor,
registry dump), `tools/gen-host-results-md.py`. Results: `docs/HOST-TEST-RESULTS.md`.

Decisions confirmed or changed while building:

- The daemon uses the plain `ext_idle_notification_v1` request, so idle inhibitors (proved with a `zwp_idle_inhibit_v1`
  client) hold the saver off and DPMS uses `zwlr_output_power_v1` directly; neither swayidle nor wlopm is needed.
- The hack environment matters: a systemd user service does not inherit the compositor's GPU variables (the O6N Mali
  EGL vendor file and `LD_LIBRARY_PATH`), so the launcher copies them from the compositor's `/proc/PID/environ`
  (never `DRI_PRIME` or NVIDIA offload variables, never names containing password, token or secret).
- Stop protocol: SIGTERM to the hack's process group, 1 s grace, then SIGKILL (a hack blocked in `eglSwapBuffers`
  behind a lock surface ignores SIGTERM on Mali). The launcher does not start a hack while a lock screen runs.
- Lock chain: on the lock timer the daemon runs `ncz-screensaver stop`, waits for it, then the lock command
  (`NCZ_LOCK_CMD`, `ncz-lock`, or `singularity-lockscreen`).
- Multi-monitor: hacks bind one output; DPMS covers all outputs. Per-output hack instances need an output option in the
  hack harness (open item for `src/`). All four test hosts have a single output, so this is untested.
- Not done by design: GPU probing or fidelity tiers, wayshade.

Bugs found by the harness and by adversarial review and fixed: alias-unsafe settings snapshot (crash at start), main loop
never quitting on SIGTERM, missing stop on input resume, wrong `Inhibit` signature, dangling `GPollFD` and a busy
`GSource` check, `xdg` and `wl_surface` opcodes and the invalid XKB keymap in the test client, PPM header parsing that
ate whitespace-valued pixels (false zero coverage), destroyed `zwlr_output_power_v1` proxy kept in the array, unbalanced
`wl_display_prepare_read`, leaked idle-notification data, and a hang on truncated PPM input in the black-frame guard.

## 5. The display stays black: session locked, no lock process

labwc keeps an `ext-session-lock-v1` session locked when the lock client dies without unlocking, so the screen stays
black and nothing draws a prompt (`pgrep -f singularity-lockscreen` finds nothing). The idle daemon now restarts a lock
command that exits abnormally (nonzero status or signal, up to 5 times, one second apart), which covers a crashing or killed
locker. A clean exit means the user unlocked. Only the compositor's own locker is ever started for locking; the saver never
locks by itself, and the harness recovers a blank display before timing anything.

Recovery over ssh, without a reboot (any host, as the desktop user):

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u) WAYLAND_DISPLAY=wayland-0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
# 1. Take over the orphaned lock with a fresh client (the protocol allows it).
setsid nohup /opt/singularity/bin/singularity-lockscreen >/tmp/lock.log 2>&1 </dev/null &
# 2. Unlock it by typing the account password. From the keyboard, or through the virtual keyboard
#    (evdev codes: q=16 w=17 e=18 r=19 t=20 y=21 u=22 i=23 o=24 p=25 a=30 s=31 d=32 f=33 g=34 h=35 j=36
#    k=37 l=38 z=44 x=45 c=46 v=47 b=48 n=49 m=50, digits 1..9=2..10 and 0=11, Enter=28):
python3 tools/wl_poke.py motion; python3 tools/wl_poke.py key --codes 50,23,49,23,28   # example: "mini" + Enter
```

If the compositor does not answer at all, `sudo systemctl restart greetd` ends the session and returns to the greeter
(this closes every application). `wlr-randr --output NAME --off; wlr-randr --output NAME --on` re-enables a panel that a
crashed DPMS client left off; the idle daemon also powers all outputs on when it exits.

## 6. Singularity Settings plugin: "Screensaver and Lockscreen"

`plugin/screensaver/` is a normal Singularity plugin (libpeas extension of `Singularity.Plugin`, laid out like
`sensors` and `wallpapers-*` in singularity-plugins): `screensaver.plugin`, `screensaver.vala` (UI), `backend.vala`
(generic `ScreensaverBackend` contract), `ncz_backend.vala` (the only NCZ-specific file). It is built on libsingularity
widgets only (`PreferencesGroup`, `SwitchRow`, `SpinRow`, `SelectionRow`, `ActionRow`, `StatusPage`); no libadwaita.

- **Where it appears.** The shell has no plugin API for adding a top-level Settings page yet, so the page is the plugin's
  settings widget, reached exactly like the other plugins: Settings > Plugins > "Screensaver and Lockscreen" (gear), and it
  is found by Settings search ("Screensaver and Lockscreen, Plugins / Active Plugins"). When the shell grows an
  `add_settings_page` hook, `ScreensaverSettings` is a self-contained widget that can be hosted as its own page unchanged.
- **Enabling.** Like every plugin it is off until listed in `dev.sinty.desktop enabled-plugins` (the test hosts have
  `'screensaver'` appended; the distro override should list it next to `'sensors'`).
- **Gate.** `is_available()` checks the settings schema, the catalog, the `ncz-screensaver` launcher and the idle daemon
  binary; otherwise the page shows a status page instead of controls.
- **No write-only settings.** Every control has a consumer: mode, chooser, pool, start and change delays, display-off,
  color model and GPU offload are read by `ncz-screensaver` / `ncz-screensaver-idled`; the lock switch, lock delay and lock
  on suspend are the shell's own `dev.sinty.lockscreen` keys (the shell is the locker), so `dev.ncz.screensaver lock-enabled`
  now defaults to false and the idle daemon only locks when explicitly asked. The playlist mode has no editor and is not offered.
  The GPU row only appears on a hybrid NVIDIA machine.
- **Build.** `tools/build-plugin.sh SDK_PREFIX OUT_DIR` builds against a Singularity SDK (headers, vapi and pkg-config file
  from `/opt/singularity`; libsingularity itself is only linked by soname, so the same recipe builds on amd64 and arm64).
  The main `meson.build` builds it with `-Dsingularity-plugin=enabled`; it installs `libscreensaver.so` (0755) and
  `screensaver.plugin` (0644) into `/opt/singularity/lib/singularity/plugins/screensaver/`, the layout of the siblings.
- **Evidence.** `docs/ux-shots/` (build-host, build-host, build-host after a greetd restart and scripted login): settings search
  result, the Plugins list with the entry, and the plugin page including the black hole, graphics (build-host only) and per-hack
  groups. Clicking the switches and spin buttons changes `dev.sinty.lockscreen` and `dev.ncz.screensaver` as expected.

## 7. GPU classes, offload, per-hack options, render quality, host lock

The GPU work has its own reference, `docs/GPU-CLASSES.md`: calibration, the weak, mid and
strong classes, the measured per-hack requirements in `tiers.tsv`, how each system presents
the catalog, vendor-agnostic per-process offload and the tests. This section keeps the rest of
the launcher contract.

**Per-hack options.** A hack with `options/<id>.tsv` (columns: name, type `bool|int|float|enum|string`, default, min, max,
choices, label, description, group, env name) gets user values from `dev.ncz.screensaver hack-options` (`a{sa{ss}}`),
validated by the launcher and passed as environment variables to that process only; unknown names and out-of-range values
are dropped with a warning. Values arrive as environment, so they override the hack's own config file.
`ncz-screensaver list-options|get-option|set-option|reset-options` manage them, `preview HACK --option name=value` tries
values without storing them, and the legacy `blackhole-color-mode` fills `palette` when no palette option is stored. Both
UIs generate their controls from the schema. `options/_render.tsv` is the common schema for every shader hack.

**Render quality.** `render-scale-mode` (auto|fixed), `render-scale` and `max-render-height` reach shader hacks as
`NCZ_RENDER_SCALE_MODE`, `NCZ_RENDER_SCALE` and `NCZ_MAX_RENDER_HEIGHT` (an explicit 0 means unlimited, unset means the
platform default). Auto applies platform data (`render-defaults.tsv`: 1080 cap on Sky1 and on a large integrated-GPU
display), a render scale of 0.5 on a weak-class GPU and the per-hack scale from `render-hints.tsv` on a mid-class GPU;
High (fixed, 1.0) overrides all of them. The UI presets are Auto, High, Balanced, Fast plus an advanced height field.

**Environment and cache.** A hack process gets the compositor's GPU environment (EGL vendor file, library path, backend
selection), so it never falls back to llvmpipe; an exit status of 3 means the hack's software-renderer guard refused and the
launcher never retries or benches it. The Mesa shader cache stays on, in `$XDG_CACHE_HOME/ncz-screensavers/mesa`. The
one-time black-frame check runs once per boot per hack. `ncz-screensaver doctor` prints the hack environment against the
unit environment, cache size, unit resource limits, the GPU classes and the effective offload plan.

**Host lock.** `tools/host-lock.sh` takes an exclusive per-host lock (`acquire build|test WHO WHAT`, atomic `mkdir`,
refresh, release, status; a stale lock is reported and never removed silently). `tools/host-test.sh` takes it in test mode,
waits for a load below 2, runs the agent under `timeout`, refreshes the lock and cleans up on exit. Builds take it in build
mode, run under `nice -n 10` with at most six jobs, and never on a host that is being tested.

**Transport and drivers.** `tools/host-test.sh` connects with password authentication only (`sshpass -e`, forced, no key
fallback, host keys checked). The harness records the kernel GPU driver (Mali: `mali_kbase` or `panthor`) in every result
and fails an aarch64 run when only `panthor` is loaded unless `--allow-panthor` is given: the open driver reports GLES 3.1
and the hacks need 3.2 shader syntax, so numbers from it are not comparable.

**Broken and suspect hacks.** `assets/screensaver-chooser/broken.tsv` (id, status broken|suspect, reason, date, tracking)
keeps listed hacks out of every pool, marks them in `list` and both UIs, and still allows an explicit run. The harness
flags a hack that is lit and moving but looks wrong (flat fill, one dominant color, tiny motion) as REVIEW instead of PASS
and builds per-host contact sheets with `tools/make-contact-sheet.py`.

## 8. Lock-hosted saver: `ncz-screensaver-feed` (2026-10-11)

While a session is locked, `ext-session-lock-v1` presents only the lock client's
surface, so a separate saver process cannot draw there. The patched NCZ lock
client can instead import a buffer fd from a separate renderer over an
`AF_UNIX SOCK_SEQPACKET` socket (`lock_framesrc`, singularity-shell build option
`-Dlock_framesrc=enabled`). `ncz-screensaver-feed` is that renderer.

- **Where it runs.** The lock client spawns `ncz-screensaver-feed` (its command
  comes from `SINGULARITY_LOCK_RENDERER`; the socket from
  `SINGULARITY_LOCK_RENDERER_SOCKET`). The feed connects as the "saver" side of
  the protocol, and for each lock output starts ONE hack binary in off-screen
  mode and relays its frames, one buffer in flight.
- **How a hack renders off-screen.** Every hack binary (built by the shared
  `src/gles3_harness.c`) gains an `NCZ_FEED_SOCKET` mode: instead of a Wayland
  layer-shell surface it creates an EGL **surfaceless** (or device-platfom)
  context and an FBO of exactly the lock's requested buffer size, runs the same
  `init_cb`/`draw_cb`, and streams the framebuffer as a memfd (XRGB8888,
  top-down). No compositor is involved, and the hack source is unchanged.
- **No logic fork.** The feed loads `ncz-screensaver` and reuses its catalog,
  `mode one|random|playlist`, pool, `cycle-delay`, per-hack options, GPU offload
  and render-scale (`build_child_env`), plus `Supervisor.pick` for selection.
- **One buffer in flight / bounded memory.** The feed forwards a child frame to
  the lock, waits for the lock's READY, then acks the child; each child holds at
  most one frame. During warm-up or a rotation gap it re-sends the last frame
  (or a black frame) at the output geometry so the lock's 3 s frame watchdog
  never trips.
- **Fail-closed.** A crashing/hung/garbage renderer can only make the lock drop
  back to its own prompt: the feed never signals the lock client; any fault
  makes it exit, and the lock reaps it with backoff.

### 8.1 Lock-saver policy (0.8.12; freeze rule 0.8.14; frame rate 0.8.16)

On NCZ-OS the lock IS the idle screensaver: at the idle time the session is
locked with no prompt and the feed renders the saver behind it. Nobody is
interacting, but someone may be watching, so the saver must look like a real
animation. The defaults are compiled in (no env/gsettings needed) and printed by
`ncz-screensaver-feed --print-policy`. Each knob is chosen in this order: the
environment (`/etc/ncz/lock-renderer.env`), the Settings value (Screensaver and
Lock page; `dev.ncz.screensaver` keys, 0 = Auto), then Auto. `NCZ_SAVER_AUTO_FPS`
in the env file changes what Auto means (the image writes it for hardware that
runs hot) without overriding the Settings value; the Settings page then warns
before a higher rate is applied:

| knob | default | env override | meaning |
|---|---|---|---|
| frame rate | Auto: 30 fps, 15 on battery, 15 on a weak GPU class; never above the display refresh | `NCZ_SAVER_LOCK_FPS` (`NCZ_FEED_FPS` legacy); key `lock-fps` | frames forwarded to the lock |
| render divisor | Auto: 1 (full resolution), 2 on a weak GPU class | `NCZ_SAVER_RENDER_DIV`; key `lock-render-divisor` | render at 1/N resolution, nearest-neighbour upscaled |
| freeze mode | `prompt` | `NCZ_SAVER_FREEZE=prompt\|always\|auto\|never` | when the saver stops on a frame (below) |
| freeze after | 15 s | `NCZ_SAVER_FREEZE_AFTER_S` | intro length, used only by `always`/`auto` |

**Suspend behind the prompt.** A saver that advertises `LFS_FLAG_PAUSE` in its
HELLO is suspended, not ended, when input raises the unlock prompt: the lock
sends `LFS_PAUSE`, the feed stops the renderer's process group (SIGSTOP) and
forwards nothing, and the lock keeps presenting the last frame it received. CPU
and GPU work for the saver drop to zero. When the prompt is dismissed without
an unlock (60 s without input at most; every key or pointer event restarts the
timeout) the lock sends `LFS_RESUME`, the renderer continues (SIGCONT) and its
monotonic clock skips the suspended time, so the animation continues from the
held frame. A renderer that dies while suspended is restarted on resume. With a
saver or lock that predates the pair, the lock falls back to freezing the
source and starting a new one when the prompt goes away.

**Frame path.** The renderer draws into its FBO; the GPU flips it to top-down
rows, scales it to the lock buffer and, where the driver reads back only RGBA,
swizzles it to BGRA, into a lock-sized FBO. glReadPixels then writes the frame
straight into one of three reused memfds that the lock imports as wl_shm
buffers. No pixel is touched by the CPU and nothing is allocated per frame.
`NCZ_FEED_LEGACY_COPY=1` restores the old CPU copy for comparison. The renderer
logs its cost per frame (draw, readback, convert, send, CPU ms) once a minute.

**Render GPU.** By default the renderer uses the GPU the launcher's plan picks
(the display GPU, or a faster PRIME offload target for shader savers). For the
saver process only, `NCZ_FEED_RENDER_DRIVER=<kernel driver>` or
`NCZ_FEED_RENDER_NODE=/dev/dri/renderDNNN` pins it to one GPU (resolved by
driver name through sysfs, never by index); if that GPU cannot be used the
renderer refuses to start rather than falling back. The log line
`render node /dev/dri/renderDNNN (<driver>)` names the GPU in use.

**When the saver freezes.** With the default `prompt` mode the saver never
freezes on a timer: it animates for as long as nobody touches the machine. It
freezes exactly when the lock takes over the screen: the first key press or
pointer motion raises the unlock prompt, and the lock client freezes the saver
at the frame on screen (`lock_framesrc_freeze()`), ends the renderer and
composites the prompt over that still frame. If the prompt is left alone until
it lowers again (60 s without input, `SINGULARITY_LOCK_RENDERER_DELAY_MS` may shorten it), the
lock restarts the frame source and the saver animates again (lock clients
built before that change keep the frozen frame until unlock); a successful
unlock ends the lock and the saver with it. `always` restores the older
intro-then-freeze power saver (HOLD after 15 s even with nobody at the machine);
`auto` does that on Apple T2 Macs only; `never` disables the timer explicitly
(the lock still freezes on input). An unknown value means `prompt`.

**Thermal protection.** A thermal governor in the off-screen child re-reads
hwmon once a second (sysfs only, never amdgpu debugfs). Above the warm trip
(75 C, `NCZ_FEED_TEMP_WARM_MC`) it drops to the warm frame rate (15 fps,
`NCZ_FEED_FPS_WARM`; never faster than the normal rate) and half resolution; at
the hot trip
(85 C; 98 C on Apple T2 Macs when the image sets `NCZ_FEED_TEMP_HOT_MC`) it
stops rendering and the lock keeps the last frame until the sensors have cooled
5 C below the trip, then the saver resumes. This pause is the only freeze an
unattended saver can show, and only while the machine is hot.

**GPU class.** The class comes from the launcher's GPU-class cache (the same
calibration the chooser uses; no benchmark runs in the feed) for the GPU that
renders the saver, including a PRIME offload target. The launcher's per-saver
render scale (`render-scale`, `max-render-height`, the weak-GPU and Sky1 caps)
still applies inside the saver on top of the divisor.

**Frame cadence.** The feed logs, once a minute and when it stops, how many
fresh frames reached the lock and the longest gap between two of them
(`cadence: N frames in T s = F fps, max gap G ms` in
`$XDG_RUNTIME_DIR/ncz-screensaver/ncz-saver-feed/feed.log` and the session log).

**HOLD capability negotiation.** The freeze is only sent to a lock that
understands it. A HOLD-aware lock sets `LFS_FLAG_HOLD` in the `flags` field of
the WELCOME it sends; the feed reads that bit and, if it is absent (a lock built
before `LFS_HOLD` existed), it does **not** send HOLD. It then keeps the low
frame rate and the thermal governor, logs the degrade once and keeps streaming,
so a new feed can never drive an old lock into an "unknown message type" fault
loop. The bit lives in the message header, which an old feed ignores, so the
reverse (new lock, old feed) also works: the old feed never sends HOLD and the
lock still freezes itself on input.

**Off-screen platform selection.** The child tries EGL surfaceless, then the
device platform, then a **GBM** platform (libgbm resolved with `dlopen`, no
build dependency). Mesa/AMD/Intel succeed on surfaceless unchanged; the CIX
Mali blob, which implements only `EGL_KHR_platform_gbm`, now animates instead
of returning no display.

**Delegated mode in `ncz-screensaver-idled`.** When
`/etc/ncz/lock-renderer.env` names a renderer (the same signal
`post-install/64-ncz-lock-saver-plan.sh` uses), the daemon must not start its
own wlr-layer-shell saver: the lock hosts the saver. `plan_compute()` then
disables the saver role and arms **no idle lock timer of its own**: the SHELL is
the single idle-lock owner, because that same hook sets
`dev.sinty.lockscreen:idle-delay = dev.ncz.screensaver:hack-idle-delay`, so the
shell's own `IdleManager` locks at the saver time and its lock client hosts the
saver. The daemon must not also call `org.freedesktop.ScreenSaver.Lock` at that
instant — two lock clients started together race the lock's renderer socket, the
loser's `unlink()` leaves the surviving lock with no frame source, and the saver
never renders. Suspend locking still belongs to the daemon (it covers system
suspends the shell's own path does not). With the renderer env absent the
previous behaviour is unchanged.

**Install.** The feed is installed in libexec (`/usr/libexec/ncz-screensaver-feed`)
by this package; the image hook writes `SINGULARITY_LOCK_RENDERER` to
`/etc/ncz/lock-renderer.env` only when both the patched lock and the feed are
present. The shared wire protocol is installed under
`share/ncz-screensavers/lock_framesrc_proto.h`.
