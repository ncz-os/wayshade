# GPU classes, per-hack requirements and offload

This is the reference for how the launcher decides which screensavers suit a machine and
which GPU renders them. Operator requirement: on systems that only have an Intel UHD or a
weaker graphics chip, show the screensavers that work well there and flag the ones that
will not.

## 1. Three classes

A class is a property of one GPU, measured by `ncz-screensaver calibrate`. The reference
micro-benchmark is a 160-step bent-ray marcher at 1920x1080 (RGBA8, no read-back, GPU timer
queries with a wall-clock fallback), the "heavy raymarch" case of `tools/probe/gles_probe.c`.

| Class | Reference time | Typical hardware | Measured |
|---|---|---|---|
| weak | 20 ms or more | Intel UHD/HD, low-end integrated, VideoCore, Mali-G31 / Mali-G52 (Bifrost low-end) | Intel UHD 630: 45.0 ms (Radeon Pro 5500M laptop), 46.5 ms (LEAD 12, the UHD 630 + RTX 2060 laptop) |
| mid | 8 to 20 ms | Mali-G720 and other Valhall/Immortalis parts, mid iGPUs and APUs | Mali-G720-Immortalis: 9.94 ms (Orion O6N, LEAD 12: 9.9 ms, MS-R1: 9.88 ms) |
| strong | under 8 ms | discrete GPUs, big iGPUs | AMD Navi14 (Radeon Pro 5500M): 5.57 ms; RTX 2060: 2.7 ms (LEAD 12) |

Thresholds live in `CLASS_WEAK_MS` and `CLASS_MID_MS` in `launcher/ncz-screensaver`.

### Detection

1. `ncz-screensaver-calibrate --identify` (about 0.3 s) prints the renderer string and driver
   version of the GPU the environment selects. The launcher compares them with the cache
   entry; if both match, nothing else runs.
2. On a miss, `ncz-screensaver-calibrate` runs the benchmark (2-3 s) and the score maps to a
   class. Never a software renderer: llvmpipe/softpipe/swrast exit with status 3, the launcher
   records a refusal and never benchmarks or trusts it.
3. If the calibrator is missing or fails, a renderer-string table is the fallback
   (`RENDERER_TABLE`), and when no renderer is known, the driver (i915, mali_kbase, amdgpu
   with 2 GiB VRAM or more, nvidia, ...).
4. The result is cached per GPU id in `$XDG_CACHE_HOME/ncz-screensavers/gpu-class.json`, keyed
   by GPU id (`pci-0000_00_02_0`), with renderer, driver version, score, timer kind and date.
   It is refreshed when the renderer string or the driver version changes, automatically at
   the first idle start (never while a hack runs) and from the settings app.

Commands: `ncz-screensaver calibrate [--force] [--json]`, `ncz-screensaver gpus [--json]`,
`status --json` (fields `gpu_class`, `gpu_class_score_ms`, `gpu_class_source`) and the
`doctor` report (`gpu_class`, `gpus`, `offload`).

## 2. Per-hack requirements

`assets/screensaver-chooser/tiers.tsv` (hacks.tsv keeps its four columns). Columns:
id, minimum class, then fps/p95 ms on the Intel UHD 630 at native resolution, on the UHD 630
at the weak-class default render scale (0.5 for shader hacks), on the Mali-G720, on AMD
Navi14 and on the RTX 2060 (`-` when not measured), then the date and commit.

Rule, produced by `tools/measure-tiers.py` from `host-test.sh --phases perf` runs (each hack
run for 10 s after a 3 s warm-up, fps and p95 from the hack's own frame counter, native
resolution unless noted):

- **weak-ok** (min class weak): at least 30 fps with p95 at most 40 ms on the UHD 630 at
  the weak-class default (render scale 0.5 for shader hacks, native for the others), and
  the same on the Mali-G720, so that a faster class never runs a weak-ok hack worse;
- **mid-ok** (min class mid): not weak-ok, but the same holds on the Mali-G720 at its
  platform defaults (1080 cap on Sky1, per-hack render hints from `render-hints.tsv`);
- **strong-only**: everything else.

Result of the run of 2026-09-29 (84 hacks): **69 weak-ok, 10 mid-ok, 5 strong-only.**
The strong-only hacks are the four legacy classics that crawl on Mali (crackberg, cubestorm,
geodesic, gibson: 12 to 16 fps or no frame counter) and `xshadertoy_alienbeacon` (17.7 fps on
Mali even at its 0.35 hint). The ten mid-ok hacks are `xshadertoy` scenes (bestill1-0 to 4-0,
downfall, fluxcore, noxfire, polarnight, rigrekt, skyline) that reach 45 to 60 fps on the
Mali-G720 with their render hint but stay under 30 fps on the UHD 630 even at scale 0.5.
Hacks too slow to print two frame-counter lines in the 10 s window (below about 6 fps) count as
not ok and show "under 6 fps" as their expectation. Weak-ok includes Black Hole (48 to 54 fps
on the UHD 630 with the weak-class defaults: render scale 0.5, no bloom, halved density).
Per class the systems show: weak 69 hacks, mid 79, strong 84.

Preset rows (`presets.tsv`, id `hack--preset`) carry their own `min_gpu_class` and are read
by the launcher next to `tiers.tsv`; a scene that is heavier than its hack (binary merger,
galaxy zoom) is flagged by giving its row a higher class. Unmeasured rows may be declared
in `tiers.tsv` with `-` cells and `declared DATE`.

## 3. What each system shows

The flag is relative to the best GPU the system may use (see section 4): a hack is flagged
when its minimum class is above it.

- The chooser (settings app and the Singularity plugin page) lists "Works well on this
  graphics chip" first. Flagged hacks are under "May run poorly on this graphics chip", with
  a warning badge, the measured expectation ("about 12 fps on Intel UHD 630") and a
  tooltip. "Show all screensavers" (`show-all-hacks`) expands that group and lists the
  flagged hacks in the pool rows. Previewing or selecting a flagged hack asks for a
  confirmation ("this may stutter") and then runs it.
- Random and playlist pools follow `pool-gpu-class`: `auto` (best usable class: weak system
  gets weak-ok only, mid gets weak-ok and mid-ok, strong gets everything), `weak`, `mid`,
  `all` (opt in to flagged hacks). `igpu-only` is the old name of `weak`. Hacks listed in
  `broken.tsv` are excluded in every case.
- A strong system hides nothing and shows no switch.
- `ncz-screensaver list --json` returns `min_class`, `flagged` and `expect` per hack;
  `ncz-screensaver pool --json` shows the pool and why each excluded hack is out.
- Hacks receive `NCZ_GPU_CLASS` (the class of the GPU that renders them, unless already set)
  so the hack can lower its own detail. On a weak GPU the launcher's Auto render quality
  sets `NCZ_RENDER_SCALE=0.5` for shader hacks; High (fixed, 1.0) overrides it. On a mid GPU,
  Auto uses the per-hack scale from `render-hints.tsv`.

## 4. Which GPU renders a hack (offload)

Hybrid graphics is the norm for laptops with a discrete GPU. The mechanism is per process and
vendor-agnostic; only the variables differ. Setting `gpu-offload`: `auto` (default), `off`,
`prime` (every hack to the best other GPU) or a GPU id from `ncz-screensaver gpus`.

| GPU driver | Variables set on the hack process only |
|---|---|
| nvidia (proprietary) | `__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia`, `__VK_LAYER_NV_optimus=NVIDIA_only` |
| i915, xe, amdgpu, radeon, nouveau | `DRI_PRIME=pci-<domain>_<bus>_<dev>_<fn>`, `MESA_VK_DEVICE_SELECT=<vendor>:<device>` |
| the display GPU | none |

`__EGL_VENDOR_LIBRARY_FILENAMES` is never set from any source (it breaks Wayland compositors).
When `switcherooctl list` works, its per-GPU environment is used, filtered to the names above;
otherwise the same values are derived from sysfs (`/sys/class/drm/card*/device`: vendor
0x8086 Intel, 0x1002 AMD, 0x10de NVIDIA, the driver name, PCI slot, the connector that is
connected = the display GPU). Nothing is ever exported to the compositor or the user manager.

Planning (`ncz-screensaver plan HACK`): a hack the display GPU can carry stays on it (power).
A heavier hack goes to the best allowed offload target, but only when that target is a faster
class. It never goes to a slower GPU. `prime` or a GPU id force the target for every hack. On
battery, `auto` never wakes a second GPU; an explicit choice is honoured. If a launch with
offload fails, the launcher retries once on the display GPU without benching the hack.
A measured cross-GPU copy cost above 8 ms (`copy_ms` in the cache, written by the harness
via `calibrate --copy-ms`) lowers Auto render scale to 0.75 on the offload path.

Each candidate GPU is calibrated separately; the pool class is the best class among the
display GPU and the allowed targets, so a weak Intel panel plus an RTX gets the whole
catalog, with the heavy hacks rendered on the RTX.

Offload evidence (2026-09-29, `--phases env,install,gpuclass,offloadproof`, full tables in
`docs/GPU-CLASS-RESULTS.md`):

| Layout (host) | Display GPU | Other GPU | Auto on AC | Evidence |
|---|---|---|---|---|
| Intel iGPU on the panel + NVIDIA dGPU (UHD 630 + RTX 2060 laptop) | UHD 630, weak 46.5 ms | RTX 2060, strong 3.7 ms | every shader hack renders on the RTX, classics stay on the iGPU | hack env has `__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia`, `__VK_LAYER_NV_optimus=NVIDIA_only`; renderer "NVIDIA GeForce RTX 2060/PCIe/SSE2"; `nvidia-smi pmon` lists the hack pid; GPU util 31-39 %, P0 (P8 and 0 % with offload off) |
| AMD dGPU on the panel + idle Intel iGPU (Radeon Pro 5500M laptop) | Radeon Pro 5500M, strong 5.6 ms | UHD 630, weak 45.0 ms | nothing offloaded (the iGPU is slower) | forcing `pci-0000_00_02_0` renders on "Mesa Intel(R) UHD Graphics 630 (CFL GT2)" via `DRI_PRIME=pci-0000_00_02_0`; compositor untouched; light hack 60 fps on both GPUs |
| single SoC GPU (Orion O6N, MS-R1) | Mali-G720, mid 9.9 ms | none | no offload | cache entry per GPU id `soc-CIXH5000_00` (MS-R1; the platform-bus Mali is exposed via `/sys/class/misc/mali0`, not as a DRM card) |
| battery (simulated with a fake sysfs root) | any | any | no second GPU wakes | plan offload=false for every tier; a real run renders on the Intel GPU with no offload variables |

Not measurable on this fleet: AMD APU + AMD dGPU and MUX-switch layouts (covered by the
fixture tests only); a cross-GPU copy cost above the frame budget (light hacks reach 60 fps
on both GPUs at native resolution, so the copy cost is below 16.7 ms; the harness records
`copy_ms` per target and the launcher lowers the render scale when it exceeds 8 ms).

## 5. Tests

- `tools/tests/test_gpu_class.py`: class detection from renderer strings and calibration
  scores (UHD 630, Mali-G720, Navi14, RTX 2060, an unknown chip by score alone, llvmpipe
  refused, cache refresh on renderer or driver change), fake-sysfs layouts (NVIDIA + Intel,
  AMD dGPU on the panel + idle Intel, AMD APU + dGPU, single SoC, MUX dGPU-only, no
  switcheroo-control, with a fake switcherooctl, battery), planning, pools per class,
  render scale rules, the copy-cost cache.
- `tools/tests/test_settings_gpu_class.py`: the settings app's groups, badges, pool labels.
- Harness phase `gpuclass` (`host-test.sh --phases env,install,gpuclass`): calibrates the
  real GPUs, checks the flagged list against `tiers.tsv` and the default pool (no hack above
  the system class), plans offload, forces each non-display GPU for one hack (renderer must
  match, compositor untouched) and records the frame rate against the display GPU.

## 6. Re-measuring

```
tools/measure-tiers.py assets/screensaver-chooser/tiers.tsv --uhd630 R/native --uhd630-scaled R/half \
    --mali R/mali [--navi14 DIR] [--rtx2060 DIR] --commit SHA
```
