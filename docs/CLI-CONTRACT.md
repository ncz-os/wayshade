# The ncz-screensaver interface (CLI contract)

Every front end (the Singularity plugin on NCZ-OS, the reference GTK4 application in
`contrib/gtk4-reference-app`, or one you write) drives the engine through this interface
and nothing else. The engine never calls a front end.

## Processes

| Process | Role |
|---|---|
| `ncz-screensaver` (Python) | launcher and CLI: picks a hack, builds its environment, supervises it |
| `ncz-screensaver-idled` (C) | idle timer on `ext-idle-notify-v1`; starts and stops the launcher, locks and switches the display off |
| `<hack>_gles3` | one screensaver, a Wayland layer-shell client |

The idle daemon runs as the user unit `ncz-screensaver-idled.service`.

## Settings: GSettings schema `dev.ncz.screensaver`

A front end writes these keys; the launcher and the idle daemon read them on every start
(the daemon also on change). A front end must not assume other keys exist.

| Key | Type | Meaning |
|---|---|---|
| `mode` | s | `off`, `one`, `random`, `playlist` |
| `hack-id` | s | catalog id used in `one` mode |
| `random-hacks`, `playlist` | as | ids in the pool; empty means every hack |
| `hack-idle-delay` | i | seconds of idle before the screensaver starts |
| `cycle-delay` | i | seconds each hack runs in `random` and `playlist` mode |
| `lock-enabled`, `lock-delay` | b, i | lock `lock-delay` seconds after the screensaver starts |
| `lock-on-suspend` | b | lock before suspend |
| `display-off-delay` | i | seconds of idle before the display is switched off (0 = never) |
| `gpu-offload` | s | `auto`, `off`, `prime` or a GPU id (`ncz-screensaver gpus`) |
| `pool-gpu-class` | s | `auto`, `weak`, `mid`, `all` |
| `show-all-hacks` | b | UI preference: list hacks flagged for this GPU |
| `render-scale-mode`, `render-scale`, `max-render-height` | s, d, i | render quality (`auto` or `fixed`) |
| `hack-options` | a{sa{ss}} | per-hack option values, validated by the launcher |
| `blackhole-color-mode`, `verify-render` | s, b | legacy palette key; one-time black-frame check |

## Commands

Machine-readable output is JSON (`--json`). Exit status 0 is success; `status` exits 3 when
nothing is running and still prints valid JSON.

| Command | Use |
|---|---|
| `list [--json]` | catalog: `id`, `title`, `group`, `installed`, `enabled`, `min_class`, `flagged`, `expect`, `issue`, `issue_reason` |
| `status --json` | `running`, `hack`, `mode`, `gpu_class`, `gpu_class_score_ms`, `gpu_class_source`, `gpu_verdict`, `gpu_verdict_rows`, `prime_offload`, `idled` |
| `preview HACK [--seconds N] [--option k=v]` | run one hack now, replacing a running preview |
| `still HACK [--out PNG] [--size WxH] [--at S] [--option k=v]` | render one frame with the stored options (plus overrides) to a PNG through the hack's capture mode; nothing appears on screen. Prints the path. Exit 4 when the hack has no capture mode (today only Black Hole has one); front ends hide their still preview then |
| `stop` | stop the running screensaver or preview |
| `list-options HACK [--json]`, `get-option`, `set-option`, `reset-options` | per-hack options (schema: `options/<hack>.tsv`) |
| `gpus --json` | GPUs with `index` (`GPU0` is the panel), `id`, `vendor`, `driver`, `display`, `class`, `ms`, `renderer` |
| `calibrate [--force] [--json]` | measure the GPU class (about 3 s; refuses while a hack runs) |
| `pool [--json]` | hacks the random and playlist modes use now, with the reason for each exclusion |
| `plan HACK [--json]` | which GPU would render a hack and with which class |
| `doctor` | environment report |
| `config get/set/dump` | raw settings |
| `ensure-plugin` | Singularity only: enable the Settings plugin once per user |

The convenience setters (`set-timeout`, `set-hack`, `set-mode`, `set-gpu`, `set-pool`,
`set-color`, `enable`, `disable`) write the keys above with validation.

## Data files (`/usr/share/ncz-screensavers/`, mirrored in `ncz-screensaver-chooser/`)

| File | Columns |
|---|---|
| `hacks.tsv` | id, title, group, group (exactly four) |
| `tiers.tsv` | id, min class (`weak`, `mid`, `strong`), fps/p95 on UHD 630 native, UHD 630 at 0.5, Mali-G720, Navi14, RTX 2060, date and commit |
| `broken.tsv` | id, status (`broken`, `suspect`), reason, date, tracking |
| `presets.tsv` | id, title, group, hack, args, accuracy, description, min class, measured |
| `options/<id>.tsv`, else `options/<id without _gles3>.tsv`; plus `options/_render.tsv` for shader hacks (`hyprsaver_`, `xshadertoy_`, `blackhole_`) | name, type, default, min, max, choices, label, description, group, env name |
| `render-defaults.tsv`, `render-hints.tsv` | platform render caps; per-hack render scale hints |
| `category-list.tsv`, `categories.tsv` | style categories of the chooser (id, label, description) and the primary category, origin (`ncz`, `ported`) and search tags of every shipped hack; see `docs/CHOOSER-TAXONOMY.md` |
| `options/blackhole-simple.tsv`, `options/simple.tsv` | Black Hole simple-mode slider axes (axis, option, curve, stops); per-hack lists of the options a simple view shows |

## Rules for a front end

1. Read state from the CLI and the files above; write settings with `gsettings` (or
   the setters). Never edit the launcher's runtime files.
2. A flagged hack (`flagged`: true) may run poorly on this system: show it separately with
   its `expect` text and ask before previewing it. Hacks with an `issue` are excluded from
   the pools but can still be run on request.
3. Never set GPU offload variables yourself; use `gpu-offload`. The launcher sets them on the
   hack process only.
4. `Gio.Settings.new()` aborts when the schema is missing: look the schema up first.
5. Do not run `calibrate` while a hack is running.
6. Surface the GPU verdict per GPU, not per system. Use `status.gpu_verdict`
   ("GPU0 (Weak), GPU1 (Strong)") on the class row and `status.gpu_verdict_rows`
   for an indexed list. When `status.prime_offload` is a non-empty string, render
   it verbatim under the GPU-offload group: it already names the panel GPU, the
   offload target(s), the AC/battery rule and the per-process scope of the
   offload environment. Do not paraphrase and do not imply a software-rendering
   fallback.
