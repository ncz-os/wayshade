# RFC 0002: Screensaver integration

Status: draft for discussion, not implemented upstream. Date: 2026-10-05.
Scope: a generic, vendor-neutral screensaver contract for singularity-shell /
libsingularity, and how a third-party engine plugs in without upstream carrying
any downstream code.

## Problem

Singularity has a real session lock (`src/lockscreen/lock_main.c`, a
toolkit-less `ext-session-lock-v1` client, one lock surface per output, PAM
auth) and a greeter, but no screensaver concept.
The shipped upstream `dev.sinty.lockscreen` schema declares `lock-enabled`,
`idle-delay` and `lock-on-suspend`, and nothing reads them. `SessionManager.lock_screen()` tries
`singularity-lockscreen`, then `loginctl lock-session`, then
`org.freedesktop.ScreenSaver.Lock`, then external lockers. There is no idle
notification, no DPMS handling, no inhibitor policy, and no way for a third
party to ship an animated saver.

A screensaver is not a lock. Treating them as one either leaves the session
unlocked while a decoration plays, or forces every saver to reimplement lock
authentication. The missing piece is a contract between the session, a saver
host, and the savers.

## What a generic screensaver service needs from the session

1. Idle notification: a per-seat idle source. `ext-idle-notify-v1` is the
   Wayland-native choice (the compositor advertises it), with logind `IdleHint`
   or `GetSessionIdleTime()` as fallbacks. One notifier per session.
2. Lock coordination: the saver must never defeat the lock. The safe ordering
   is idle -> saver -> lock; on input, power on, show the lock, restore on
   successful auth. The saver is stopped or killed the instant the lock is
   requested and never holds the lock. `ext-session-lock-v1` permits exactly one
   lock client and requires the compositor to blank all outputs until a lock
   surface is presented, so a saver cannot share the lock surface unless the
   lock client draws it.
3. DPMS / blank ordering: `zwlr-output-power-management-v1`, per output.
   Ordering must be saver, then lock, then output off; on input, output on, then
   unlock prompt. Powering off before the lock is presented risks exposing
   desktop content on resume, and a saver still drawing should be paused, not
   left burning GPU.
4. Suspend and inhibit: on logind `PrepareForSleep`, lock before the transition
   completes when `lock-on-suspend` is set; a bounded delay inhibitor guarantees
   the locker spawned first and is released at once. Idle inhibition must
   suppress saver, lock and DPMS together.
5. Per-output rendering: one surface per output, sized with the logical size the
   compositor reported and the output's fractional scale; conflating the two
   renders at the wrong resolution.
6. Multi-GPU / PRIME: a saver is a normal client, so offload is per-process only
   (`DRI_PRIME` or the NVIDIA offload variables on its environment only;
   forcing offload into the session environment breaks the compositor).
7. Power-source awareness: a laptop should not spin up a discrete GPU for a
   decoration on battery. AC may offload shader savers; battery should not.

## Minimal vendor-neutral contract

- Discovery: `.desktop` entries under
  `$XDG_DATA_DIRS/singularity/screensavers/` (or the freedesktop `ScreenSaver`
  directory), one per saver, with `Exec`, `Name` and an `X-Singularity-*` set
  (engine, minimum GPU class, options manifest) - the xscreensaver/kiosk
  convention, so no saver needs a Singularity-specific build.
- Control: an optional session bus name, `org.freedesktop.ScreenSaver`, with
  `Lock`, `GetActive`, `SetActive` and `GetSessionIdleTime`. `SessionManager`
  already probes `ScreenSaver.Lock`, so this is the natural seam: a saver host
  that owns the name forwards lock requests to the session locker, and any
  freedesktop-aware app can ask whether the screen is active.

A libsingularity `ScreenSaverProvider` interface is reasonable for the settings
side but should not be the only mechanism: savers in any language should plug in
without linking Singularity. Recommendation: standardise on the `.desktop`
discovery first (inert) and expose the D-Bus name from a small session service
rather than making the shell the saver.

## Who owns what

- Shell / session: the lock (`singularity-lockscreen`), idle source, inhibition
  policy and ordering guarantees; DPMS ordering if centralised.
- Plugin: chooser, per-saver options, favourites, settings page; starts and
  stops the saver host and authenticates no one.
- Saver host / external hacks: the GL/EGL surface, frame loop, effect selection
  and failure containment; never credentials.
- Downstream integration lives outside the upstream tree.

## How a third-party engine plugs in without upstream carrying its code

Stage 0 needs no upstream change. A self-contained plugin (like `sensors`) ships
a settings widget and, through its own user service, binds
`ext-idle-notify-v1`, runs savers on layer-shell surfaces, powers outputs
through `zwlr-output-power-management-v1`, and calls the existing locker
(`singularity-lockscreen`, `loginctl lock-session`, `ScreenSaver.Lock`).
Upstream sees only a plugin, a schema it does not own, and `.desktop` entries.
When the session later grows idle/lock/DPMS ownership, the plugin consumes the
neutral signals instead of private ones.

## Settings page (a grouped chooser as reference UI)

The reference settings widget is a mode choice (off / one / random /
playlist), a grouped chooser (Favourites, Recently used, then style groups),
engine filters, search, a detail panel with sliders, palette swatches, presets
and a still preview, plus GPU class and PRIME explanation. It is built from
libsingularity rows and returned through `get_settings_widget()`, landing under
Plugins > Screensaver > Settings with no shell change; a future upstream page
can reuse the shape.

## Security model

- The saver runs unprivileged as the session user and never receives the lock
  credentials, PAM handle, PIN or keyring; authentication stays in the lock
  client.
- It is killed on unlock and must release its surfaces on lock. If it hangs, the
  lock still wins.
- A crashed or wedged renderer must never leave the session unlocked and never
  leave a blank screen with no way to authenticate. The engine is disposable;
  the lock client is not.
- If a future design draws the saver inside the lock client, authentication
  must stay out of the engine's reach, a frame must be time-bounded, and a dead
  engine must drop to a static background while the prompt stays live: a
  security property, not a quality one.

## Staged plan

- Stage 0 (plugin only, nothing in the shell): saver plugin, settings widget,
  `.desktop` entries and the user service. Idle, DPMS and lock requests are
  handled by the plugin/service through existing session interfaces. No upstream
  code change is needed (this document is the proposal).
- Stage 1 (settings keys): declare the screensaver settings contract in a
  plugin-owned schema, wired to real consumers; `dev.sinty.lockscreen` keeps its
  meaning.
- Stage 2 (lock integration): if agreed, upstream adds a neutral idle signal and
  a documented ordering contract (saver -> lock -> output off, inhibited as a
  unit), and either owns `org.freedesktop.ScreenSaver` or publishes the contract
  for a helper. Only this stage changes upstream, and it contains zero
  downstream paths or daemon names.

## Open questions for the maintainer

1. Is a screensaver wanted at all, or is an idle lock plus DPMS the end state?
2. Preferred contract: `.desktop` entries plus `org.freedesktop.ScreenSaver`, a
   libsingularity plugin interface, or "keep it out of the shell entirely"?
3. Should the shell own `ext-idle-notify-v1` once and broadcast idle, or is
   per-plugin binding acceptable?
4. Who owns DPMS ordering, and does the unread `idle-delay` key become the idle
   source?
5. Should the lock suppress the saver entirely, or run it inside the lock
   surface for continuity?
6. On suspend, is a bounded delay inhibitor acceptable, or must the lock be
   guaranteed before `PrepareForSleep` returns? One saver surface per output
   (recommended) or one spanning surface, and should settings live in
   `dev.sinty.desktop` or plugin-owned schemas?
