# User guide

How to turn the screensaver on, choose one, set the timers, try one out and
fix it when it does not work.

## Requirements

A Wayland session. The savers need `zwlr_layer_shell_v1`; without it the
engine falls back to a fullscreen toplevel, which works but not everywhere.
A hardware GPU: the engine refuses to run on a software renderer when it
can see a real GPU, and says so instead of crawling.

## Enable it

On NCZ-OS, open **Settings > Plugins > Screensaver and Lockscreen**. The
page shows the controls once the schema, the catalogue, the launcher and the
idle daemon are all present; if one is missing it shows a status page saying
which.

Anywhere else, drive the engine from the command line:

```
ncz-screensaver enable            # turn the screensaver on
ncz-screensaver set-mode one      # run a fixed saver
ncz-screensaver disable           # turn it off again
```

`mode` is one of `off`, `one`, `random` or `playlist`. In `random` mode the
engine picks from the pool every `cycle-delay` seconds; in `playlist` mode
it walks your list in order.

## Choose a saver

```
ncz-screensaver list                        # every saver, with its minimum class
ncz-screensaver list --json                 # the same, machine readable
ncz-screensaver set-hack hyprsaver_wormhole # run exactly this one
```

In the chooser, savers that suit this machine come first under "Works well
on this graphics chip". The rest appear under "May run poorly on this
graphics chip" with a badge and the measured frame rate, and asking to
preview one of those confirms first. "Show all screensavers"
(`show-all-hacks`) expands that group into the pool rows.

To narrow the pool yourself:

```
ncz-screensaver set-pool weak   # only the savers a weak GPU carries
ncz-screensaver set-pool auto   # the default: the best usable class decides
ncz-screensaver set-pool all    # opt in to the flagged ones too
```

## Set the timers

```
ncz-screensaver set-timeout 600          # start after 10 minutes of idle
gsettings set dev.ncz.screensaver cycle-delay 900   # 15 minutes per saver in random mode
gsettings set dev.ncz.screensaver display-off-delay 1200   # screen off after 20 minutes idle
```

`hack-idle-delay` is the seconds of idle before the screensaver starts
(default 300). `cycle-delay` is how long each saver runs in `random` and
`playlist` mode (default 600). `display-off-delay` is the seconds of idle
before the display is switched off; 0 means never.

Locking is separate, because the desktop shell usually has its own locker:

| Key | Default | Meaning |
|---|---|---|
| `lock-enabled` | `false` | Lock `lock-delay` seconds after the screensaver starts. |
| `lock-delay` | 60 | Seconds from the screensaver's start. |
| `lock-on-suspend` | `true` | Lock before the machine suspends. |

## Try one out without waiting

```
ncz-screensaver preview hyprsaver_aurora           # run it now
ncz-screensaver preview blackhole --seconds 20      # for 20 seconds
ncz-screensaver preview blackhole --option palette=cygnus
ncz-screensaver stop                                # stop it
```

`preview` replaces a running preview, so you can try another one straight
away. Any key press also dismisses a running screensaver.

For a still frame instead of a live one:

```
ncz-screensaver still blackhole --out ~/shot.png
```

This exits 4 for any other saver, because in 0.8.9 Black Hole is the only
one with a capture mode. A front end should hide its still preview then.

## Adjust a saver's options

```
ncz-screensaver list-options blackhole             # what it takes
ncz-screensaver set-option blackhole palette eht   # change one
ncz-screensaver reset-options blackhole            # forget the lot
blackhole_gles3 --help                             # the same list on the command line
blackhole_gles3 --palette=eht --preset=cygnus      # run it directly with options
```

Options resolve in this order, lowest priority first: the built-in
defaults, `$XDG_CONFIG_HOME/ncz-screensavers/render.conf`, a preset, the
environment, then the command line. An unknown option or a value out of
range produces one warning on stderr and is ignored, so the next lower
source applies.

Every saver takes three common options:

| Option | Default | Meaning |
|---|---|---|
| `render-scale` | 1 | Fraction of the output resolution the saver renders at; the compositor upscales the rest. 0.25 to 1. |
| `max-render-height` | 0 | Cap the render height in pixels; 0 is the platform default. |
| `render-scale-mode` | `auto` | `auto` steps the scale down when frames run long, and never back up; `fixed` keeps what you set. |

Black Hole has 55 options and Magmasimplex 36; see
[`savers.md`](savers.md) and the catalogue pages.

## Choose which GPU runs it

```
ncz-screensaver gpus                 # what the machine has, with each class
ncz-screensaver plan hyprsaver_plasma  # which GPU would render this one
ncz-screensaver set-gpu auto         # the default
ncz-screensaver set-gpu off          # always the display GPU
ncz-screensaver set-gpu prime        # always the other GPU
```

The default already keeps a saver on the GPU that drives the panel and only
moves a heavier one to a faster GPU, and it never wakes a second GPU on
battery. See [`gpu-classes.md`](GPU-CLASSES.md).

## Check the machine

```
ncz-screensaver status               # what is running, the class, the verdict
ncz-screensaver status --json
ncz-screensaver calibrate            # measure the GPU class again
ncz-screensaver doctor               # full environment report
```

## Troubleshooting

**Nothing appears when the idle time is up.** Check that the daemon is
running (`systemctl --user status ncz-screensaver-idled.service`) and that
it is inside a Wayland session: it exits with a temporary-failure code
while there is no usable session and waits, rather than latching as failed.
`ncz-screensaver status --json` reports `idled`.

**The screensaver refuses to start: "software renderer ... refused".** The
engine will not render in software on a machine that has a GPU. The message
names the driver it found, which is the hint: the graphics environment is
pointing at the wrong renderer or EGL vendor. Fix the environment rather
than the saver. On a machine with no GPU at all, set `NCZ_ALLOW_SOFTWARE=1`
to allow the CPU.

**"wl_display_connect failed".** The saver is not in a Wayland session.

**A layer-shell error, or the saver draws in the wrong place.** The
compositor does not advertise `zwlr_layer_shell_v1`. The engine falls back
to a fullscreen toplevel; on a compositor that supports neither, nothing
can be drawn.

**It stutters or the frame rate is low.** Two knobs. Lower the render
scale, or set the mode to `fixed` and choose the scale yourself:

```
gsettings set dev.ncz.screensaver render-scale-mode fixed
gsettings set dev.ncz.screensaver render-scale 0.5
```

For a long run, `ncz-screensaver list --json` tells you each saver's
`min_class` and `expect`; `ncz-screensaver calibrate` re-measures this
machine's class.

**The screen never comes back.** Move the mouse or press a key: the
protocol's idle-resumed signal fires on any seat input, so dismissal does
not depend on the saver handling input itself. From another terminal,
`ncz-screensaver stop` stops it.

**The savers a machine can run are not listed.** `ncz-screensaver pool
--json` prints the pool and the reason each excluded saver is out. A saver
in `assets/screensaver-chooser/broken.tsv` is always excluded.

**Black-blank frames.** The engine has a one-time black-frame check
(`verify-render`, on by default) and recommends the `grim` tool for it. A
saver that passes the check and then goes black is worth reporting with the
saver id, the GPU and `ncz-screensaver status --json`.

## Where to read next

- [`savers.md`](savers.md) for what each saver looks like.
- [`architecture.md`](architecture.md) for how it works.
- [`cli-reference.md`](cli-reference.md) for every command and key.
- [`credits.md`](credits.md) for who wrote what.