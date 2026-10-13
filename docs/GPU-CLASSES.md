# GPU classes, per-saver requirements and offload

This page explains how the launcher decides which screensavers suit a
machine, and which GPU renders them.

## Three classes

A class is a property of one GPU, measured by `ncz-screensaver calibrate`
and cached per GPU id. The reference benchmark is a 160-step bent-ray
marcher rendered into a 1920x1080 RGBA8 target, timed with a GL timer
query and a wall-clock fallback. The run takes about two seconds, creates
no window and reads nothing back.

| Class | Reference time | Typical hardware |
|---|---|---|
| `weak` | 20 ms or more | Intel UHD/HD Graphics, VideoCore, Mali-G31 / G52 / T-series / 4xx, Adreno 3xx-5xx |
| `mid` | 8 ms to 20 ms | Mali-G57 / G6xx / G7xx and Immortalis, Iris Xe, Intel Arc (non-A5xx/A7xx), Radeon Graphics / Vega, Adreno 6xx-7xx, Apple |
| `strong` | under 8 ms | NVIDIA GeForce / RTX / Quadro / GTX, Radeon RX and Navi 1x-9x, Apple M-series, Intel Arc A5xx and A7xx |

The thresholds are `CLASS_WEAK_MS = 20.0` and `CLASS_MID_MS = 8.0` in
`launcher/ncz-screensaver`: a score at or above 20.0 ms is `weak`, a score
from 8.0 ms up to 20.0 ms is `mid`, and anything faster is `strong`.

### How a GPU is classified

1. `ncz-screensaver-calibrate --identify` prints the renderer string and
   the driver version of the GPU the environment selects, in about 0.3 s.
   The launcher compares them with its cache entry and stops there when
   both match.
2. On a cache miss `ncz-screensaver-calibrate` runs the benchmark and the
   score maps to a class. A software renderer is refused: `llvmpipe`,
   `softpipe` and `swrast` exit with status 3, and the launcher records
   the refusal instead of benchmarking.
3. If the calibrator is missing or fails, the launcher's renderer-string
   table (`RENDERER_TABLE`) decides. It is a first-match list of regular
   expressions, so the Mali family is narrowed deliberately: `mali-g31`,
   `mali-g52`, `mali-t*` and `mali-4xx` are `weak`, while `mali-g57`
   through `mali-g79` and `immortalis` are `mid`.
4. When the renderer string is unknown too, the driver decides
   (`class_from_topology()`): a DRM node bound to `mali`, `mali_kbase`,
   `panthor`, `panfrost`, `msm` or `amdgpu` is at least `mid`; `i915`,
   `xe` and `nouveau` still need a renderer string.

The result is cached per GPU id (`pci-0000_00_02_0`) in
`$XDG_CACHE_HOME/ncz-screensavers/gpu-class.json` together with the
renderer, the driver version, the score, the timer kind and the date. It
is refreshed when the renderer string or the driver version changes,
automatically at the first idle start (never while a saver is running)
and from the settings UI.

Commands:

```
ncz-screensaver calibrate [--force] [--json]
ncz-screensaver gpus [--json]
ncz-screensaver status --json
ncz-screensaver doctor
```

`status --json` reports `gpu_class`, `gpu_class_score_ms`,
`gpu_class_source`, `gpu_verdict` and `gpu_verdict_rows`;
`ncz-screensaver gpus --json` reports each GPU's `index` (`GPU0` is the
panel), `id`, `vendor`, `driver`, `display`, `class`, `ms` and `renderer`.

## Per-saver requirements

`assets/screensaver-chooser/tiers.tsv` holds one row per saver: the
minimum class, then fps / p95 ms on an Intel UHD 630 at native
resolution, on the same UHD 630 at the weak-class default render scale
(0.5 for shader hacks, native for the others), on a Mali-G720, on AMD
Navi14 and on an RTX 2060 (`-` when not measured), then the measurement
date and commit. Rows produced by `tools/measure-tiers.py`.

The rule behind a row:

- **weak-ok**: at least 30 fps with p95 at most 40 ms on the UHD 630 at
  the weak-class default *and* on the Mali-G720, so a faster GPU never
  runs a weak-ok saver worse.
- **mid-ok**: not weak-ok, but the same holds on the Mali-G720 at its
  platform defaults, using the per-saver render hints from
  `assets/screensaver-chooser/render-hints.tsv`.
- **strong-only**: everything else.

Of the 74 shipped savers, **63 are `weak` and 11 are `mid`**. The eleven
`mid` savers are Magmasimplex and ten xshadertoy scenes: `bestill1-0`,
`bestill2-0`, `bestill3-0`, `bestill4-0`, `downfall`, `fluxcore`,
`noxfire`, `polarnight`, `rigrekt` and `skyline`.

Presets (`assets/screensaver-chooser/presets.tsv`, id
`<hack>_gles3--<preset>`) carry their own `min_gpu_class`, so a scene
heavier than its saver gets a higher class. An unmeasured row may be
declared in `tiers.tsv` with `-` cells and a `declared DATE` note.

`assets/screensaver-chooser/broken.tsv` lists savers with a known
problem and why. In 0.8.9 it holds one row: `klein_gles3`, status
`suspect`, "large single-color areas, low color count".

### What a front end does with the class

- A saver whose minimum class is above the best usable class is reported
  as `flagged: true` in `ncz-screensaver list --json`, together with
  `min_class` and the measured `expect` text. The chooser lists
  "Works well on this graphics chip" first and the flagged ones under
  "May run poorly on this graphics chip", with a badge and a
  confirmation before previewing.
- The random and playlist pools follow `pool-gpu-class`: `auto` (the best
  usable class decides what enters the pool), `weak`, `mid`, `all` (opt
  in to flagged savers). Savers in `broken.tsv` are excluded in every
  case. `ncz-screensaver pool --json` prints the pool and the reason each
  excluded saver is out.
- Every saver is given `NCZ_GPU_CLASS` for the GPU that will render it,
  so it can lower its own detail.

## Which GPU renders a saver

Hybrid graphics is normal on laptops with a discrete GPU. The mechanism
is per process and vendor-agnostic; only the variable names differ.

| GPU driver | Variables set on the saver process only |
|---|---|
| nvidia (proprietary) | `__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia`, `__VK_LAYER_NV_optimus=NVIDIA_only` |
| i915, xe, amdgpu, radeon, nouveau | `DRI_PRIME=pci-<domain>_<bus>_<dev>_<fn>`, `MESA_VK_DEVICE_SELECT=<vendor>:<device>` |
| the display GPU | nothing |

`__EGL_VENDOR_LIBRARY_FILENAMES` is never set from any source: it breaks
Wayland compositors. When `switcherooctl list` works, its per-GPU
environment is used, filtered to the names above; otherwise the same
values are derived from `/sys/class/drm/card*/device` (vendor `0x8086`
Intel, `0x1002` AMD, `0x10de` NVIDIA, the driver name, the PCI slot, and
the connector that is connected, which identifies the display GPU).
Nothing is ever exported to the compositor or to the user manager.

`gpu-offload` selects the policy: `auto` (the default), `off`, `prime`
(every saver to the best other GPU) or a GPU id from
`ncz-screensaver gpus`.

Planning (`ncz-screensaver plan HACK`) works like this:

- A saver the display GPU can carry stays on it, which saves power.
- A heavier saver moves to the best allowed offload target, but only when
  that target is a faster class. It never moves to a slower GPU.
- `prime` or an explicit GPU id forces the target for every saver.
- On battery, `auto` never wakes a second GPU; an explicit choice is
  honoured.
- If a launch with offload fails, the launcher retries once on the
  display GPU without benching the saver.
- A measured cross-GPU copy cost above 8 ms (`COPY_BUDGET_MS` in the
  launcher, recorded per target in the class cache) lowers the automatic
  render scale to 0.75 on the offload path.

Each candidate GPU is calibrated separately. The pool class is the best
class among the display GPU and the allowed offload targets, so a weak
integrated GPU plus a fast discrete GPU gets the whole catalogue with the
heavy savers on the fast GPU.

## Re-measuring

The tiers table is regenerated from measured runs, never typed by hand:

```
tools/measure-tiers.py assets/screensaver-chooser/tiers.tsv \
    --uhd630 RUNS/native --uhd630-scaled RUNS/half --mali RUNS/mali \
    [--navi14 DIR] [--rtx2060 DIR] --commit SHA
```

The calibration itself is covered by the launcher test suite, including
class detection from renderer strings and from scores, the refusal of a
software renderer, cache refresh on a renderer or driver change, planning,
pools per class, the render-scale rules and the copy-cost cache.