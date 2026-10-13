# CLI reference

Everything a front end needs to drive the engine. The engine never calls a
front end; it only exposes this interface.

`ncz-screensaver` is the user-side CLI. Every command that prints data also
accepts `--json`. Exit status 0 means success; `status` exits 3 when nothing
is running and still prints valid JSON.

## The three processes

| Process | Role |
|---|---|
| `ncz-screensaver-idled` | The idle timer, on `ext-idle-notify-v1`. It starts and stops the launcher, the lock and the display power-off. Installed in `libexecdir`. |
| `ncz-screensaver` | The launcher and CLI: it picks a saver, builds its environment and supervises it. |
| `<hack>_gles3` | One screensaver, a Wayland layer-shell client, installed in `libdir/ncz-screensavers`. |

The idle daemon runs as the user unit `ncz-screensaver-idled.service`.

## Commands

| Command | What it does |
|---|---|
| `list [--json]` | The catalogue. Per entry: `id`, `title`, `group`, `installed`, `enabled`, `min_class`, `flagged`, `expect`, `issue`, `issue_reason`. |
| `status --json` | `running`, `hack`, `mode`, `gpu_class`, `gpu_class_score_ms`, `gpu_class_source`, `gpu_verdict`, `gpu_verdict_rows`, `prime_offload`, `idled`. |
| `preview HACK [--seconds N] [--option k=v]` | Run one saver now, replacing a running preview. |
| `still HACK [--out PNG] [--size WxH] [--at S] [--option k=v]` | Render one frame with the stored options plus overrides to a PNG, through the saver's capture mode, with nothing on screen. Prints the path. Exit 4 when the saver has no capture mode — in 0.8.9 only Black Hole has one. |
| `stop` | Stop the running screensaver or preview. |
| `list-options HACK [--json]` | The saver's options and their stored values. |
| `options [HACK] [--all] [--json]` | The option schema, stored values and presets of one saver, or of every saver that has options. |
| `get-option HACK NAME`, `set-option HACK NAME VALUE`, `reset-options HACK` | Read, write or forget one saver's stored options. `set-option` validates against the schema. |
| `gpus [--json]` | The GPUs: `index` (`GPU0` is the panel), `id`, `vendor`, `driver`, `display`, `class`, `ms`, `renderer`. |
| `calibrate [--force] [--json]` | Measure the GPU class. Takes about three seconds and refuses to run while a saver is running. |
| `pool [--json]` | The savers the random and playlist modes would use now, with the reason each exclusion is out. |
| `plan HACK [--json]` | Which GPU would render this saver, and with which class. |
| `doctor` | An environment report. |
| `config get\|set\|dump` | Raw settings access. |
| `ensure-plugin` | On NCZ-OS: enable the settings plugin once per user. |
| `start --idle` | Used by the idle daemon to start the screensaver. |

Convenience setters, each of which writes one validated key:

```
ncz-screensaver set-timeout SECONDS      # hack-idle-delay
ncz-screensaver set-hack HACK            # mode = one
ncz-screensaver set-mode MODE            # off | one | random | playlist
ncz-screensaver set-gpu TARGET           # auto | off | prime | GPU id
ncz-screensaver set-pool CLASS           # auto | weak | mid | all
ncz-screensaver set-color PALETTE        # Black Hole colour mode
ncz-screensaver enable                    # mode = random
ncz-screensaver enable HACK              # add HACK to the random pool
ncz-screensaver disable                   # mode = off
ncz-screensaver disable HACK              # drop HACK from the random pool
```

The compatibility shim `ncz-screensaver-run` accepts `--start`, `--stop`
and `--preview ID` and forwards to this CLI; it exists for older callers.

## Settings: the `dev.ncz.screensaver` GSettings schema

A front end writes these keys; the launcher and the idle daemon read them.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `mode` | s | `off` | `off`, `one`, `random` or `playlist`. |
| `hack-id` | s | `blackhole_gles3` | The catalogue id used in `one` mode. |
| `random-hacks`, `playlist` | as | empty | Ids in the pool; empty means every saver. |
| `hack-idle-delay` | i | 300 | Seconds of idle before the screensaver starts. |
| `cycle-delay` | i | 600 | Seconds each saver runs in `random` and `playlist` mode. |
| `lock-enabled` | b | `false` | Lock `lock-delay` seconds after the screensaver starts. |
| `lock-delay` | i | 60 | Seconds from the screensaver's start. |
| `lock-on-suspend` | b | `true` | Lock before suspend. |
| `display-off-delay` | i | 0 | Seconds of idle before the display is switched off; 0 never. |
| `gpu-offload` | s | `auto` | `auto`, `off`, `prime`, or a GPU id from `gpus`. |
| `pool-gpu-class` | s | `auto` | `auto`, `weak`, `mid` or `all`. |
| `show-all-hacks` | b | `false` | UI preference: also list the savers flagged for this GPU. |
| `render-scale-mode` | s | `auto` | `auto` or `fixed`. |
| `render-scale` | d | 1.0 | Fraction of the output resolution the saver renders at. |
| `max-render-height` | i | 0 | Cap the render height in pixels; 0 is the platform default. |
| `hack-options` | a{sa{ss}} | empty | Per-saver option values. |
| `dynamics-default` | d | 0.0 | Default dynamics amount for savers that have one. |
| `audio-reactive` | s | `off` | Audio-reactive layer setting. |
| `blackhole-color-mode` | s | `stylized` | Legacy Black Hole palette key. |
| `verify-render` | b | `true` | The one-time black-frame check. |

## Data files

The launcher and the chooser read these from
`/usr/share/ncz-screensavers/` and `/usr/share/ncz-screensaver-chooser/`.

| File | Columns |
|---|---|
| `hacks.tsv` | id, title, group, group |
| `tiers.tsv` | id, minimum class, then measured fps/p95 on the reference GPUs, date and commit |
| `broken.tsv` | id, status (`broken`, `suspect`), reason, date, tracking |
| `presets.tsv` | id, title, group, saver, args, accuracy, description, minimum class, measured |
| `categories.tsv` | id, style category, origin (`ncz`, `ported`), search tags |
| `category-list.tsv` | id, label, description, in chooser order |
| `options/<id>.tsv` | name, type, default, min, max, choices, label, description, group, environment name |
| `options/_render.tsv` | the three options every saver has |
| `options/blackhole-simple.tsv` | Black Hole's simple-view slider axes |
| `options/simple.tsv` | which options a simple view shows per saver |
| `render-defaults.tsv`, `render-hints.tsv` | platform render caps and per-saver render scale hints |
| `sparse.tsv`, `ground.tsv` | launcher debug hooks |

Which option schemas ship with the package: `blackhole.tsv` and
`_render.tsv` always, plus `magmasimplex.tsv` (and its presets) because
Magmasimplex is in the shipped set. Savers without a schema of their own
get the three options from `_render.tsv`, so `list-options` never returns an
empty list for a shipped saver.

Environment overrides for reading a tree that is not installed:
`NCZ_SCREENSAVER_CATALOG` points at an alternative `hacks.tsv` and
`NCZ_SCREENSAVER_OPTIONS` at an alternative `options/` directory.

## Rules for a front end

1. Read state from the CLI and the data files. Write settings with
   `gsettings` or the setters. Never edit the launcher's runtime files.
2. A `flagged: true` saver may run poorly on this machine. Show it
   separately, with its `expect` text, and ask before previewing it. A
   saver with an `issue` is excluded from the pools but can still be run on
   request.
3. Never set GPU offload variables yourself; use `gpu-offload`. The
   launcher sets them on the saver process only.
4. `Gio.Settings.new()` aborts when the schema is missing, so look the
   schema up first.
5. Do not run `calibrate` while a saver is running.
6. Surface the GPU verdict per GPU, not per system. Use `status.gpu_verdict`
   ("GPU0 (Weak), GPU1 (Strong)") on the class row and
   `status.gpu_verdict_rows` for an indexed list. When
   `status.prime_offload` is a non-empty string, render it verbatim under
   the GPU-offload group: it already names the panel GPU, the offload
   target, the AC/battery rule and the per-process scope of the offload
   environment. Do not paraphrase it and do not imply a software-rendering
   fallback.

## Where to read next

- [`architecture.md`](architecture.md) for the engine behind this interface.
- [`gpu-classes.md`](GPU-CLASSES.md) for what the class and verdict mean.
- [`savers.md`](savers.md) for the catalogue the commands return.
- [`developer-guide.md`](developer-guide.md) to build the CLI yourself.