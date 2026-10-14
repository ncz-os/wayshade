# Sky1 (MS-R1 / cixmini / 192.0.2.66) re-validation evidence — 2026-09-30 (turn 7)

Continuation dispatch — operator requested "Continue the SAME fix...
the previous attempt was NOT accepted. Address ALL of the following
and update the code and tests accordingly, then re-run the validation
command and make it pass." (dispatch 2026-09-30T12:32Z, the r20
continuation.)

Branch state at dispatch entry: `fix/gpu-tier-mali-g720-2026-09-30`
at `f365279` on `origin/`. All 212 prior tests pass, ruff clean.
This dispatch adds a fourth independent live ms pin captured against
the same .66 host with the same fix code.

This dispatch **does not modify any package, service, or system
config on `.66`**. The fixed `launcher/ncz-screensaver` (md5
`e7e77d4cc7cf0294902086149486096e`, byte-for-byte identical to
`f365279` at dispatch entry) is copied to
`/tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z` on `.66`
via `scp`, made executable, and run against the live `/sys`, the
live `~/.cache/ncz-screensavers/gpu-class.json`, and the live
compositor session. The shipped `/usr/bin/ncz-screensaver`
(md5 `1bd3ee4b00e7dcd0acb83a1b17274135`, ncz-screensavers 0.7.1)
is unchanged.

## 0. What this dispatch verifies

The previous dispatches (r0..r15-cont) put the fix in place at the
source level (commits `8f221fb`..`f365279`, 16 commits on top of
`feat/launcher-ux @ 3549bab`). This dispatch:

1. **Captures a fourth independent live ms pin** on .66
   (`SKY1_R20_CONT_CAPTURE`, ms=9.89, captured
   2026-09-30T16:39:07+0000Z on .66, with the fix code at HEAD
   `f365279`).
2. **Re-validates the fix against the live .66 host**: 1 GPU,
   `class=mid`, `Mali-G720-Immortalis`. The four `linlondp`
   display controllers are filtered out by `list_gpus()`.
3. **Captures fps evidence for the shader screen saver
   `hyprsaver_donut_gles3`** (7200 frames in 120.01 s = 60.0 fps
   sustained).
4. **Re-verifies the no-software-fallback policy** (vulkaninfo
   llvmpipe count = 0; hack env has no `LIBGL_ALWAYS_SOFTWARE`,
   no `GALLIUM_DRIVER=llvmpipe`, no `swrast`).
5. **Adds an adversarial cross-check** that all four live ms pins
   (9.88, 14.71, 11.93, 9.89) classify as `mid` per the threshold
   band contract.

## 1. Live `gpus` (FIXED `/tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z`)

After the calibration run completes (cache populated):

```
$ XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
    /tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z gpus
soc-CIXH5000_00 display  other   mali     mid    9.9 ms
```

One row. `soc-CIXH5000_00` is the platform-bus Mali device (driver
`mali`, bound by `/sys/bus/platform/drivers/mali` on the CIX
`CIXH5000:00` device). The four `linlondp` display controllers
(`CIXH5010:00/01/03/04`) are filtered out by `list_gpus()` once
a real GPU is found via `/sys/class/misc/mali0`.

## 2. Live `gpus --json` (FIXED)

```json
[
  {
    "id": "soc-CIXH5000_00",
    "card": "",
    "slot": "CIXH5000:00",
    "vendor": "other",
    "driver": "mali",
    "device": "",
    "display": true,
    "boot_vga": false,
    "discrete": false,
    "render": true,
    "display_only": false,
    "class": "mid",
    "ms": 9.89,
    "renderer": "Mali-G720-Immortalis"
  }
]
```

`display_only: false` on the Mali is exactly what the r15-cont
dispatch's test pins. The four linlondp display controllers are
filtered out by `list_gpus()`.

## 3. Live `status --json` (FIXED)

```json
{
  "gpu_class": "mid",
  "gpu_class_score_ms": 9.89,
  "gpu_class_source": "calibration",
  "running": false,
  ...
}
```

`gpu_class=mid`, `gpu_class_score_ms=9.89`,
`gpu_class_source=calibration`. (After `ncz-screensaver stop`
+ calibrate + start, the running hack was still `hyprsaver_donut_gles3`
just before stop.)

## 4. Live `doctor --json` (FIXED, abbreviated)

```json
{
  "wayland_sockets": ["wayland-0"],
  "grim": true,
  "idled_running": true,
  "mode": "random",
  "gpu": {"display_class": "integrated", "nvidia_offload": false},
  "gpus": [
    {
      "id": "soc-CIXH5000_00",
      "card": "",
      "slot": "CIXH5000:00",
      "vendor": "other",
      "driver": "mali",
      "device": "",
      "display": true,
      "boot_vga": false,
      "discrete": false,
      "render": true,
      "display_only": false
    }
  ],
  "gpu_class": {
    "display": {
      "class": "mid",
      "ms": 9.89,
      "renderer": "Mali-G720-Immortalis",
      "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
      "timer": "gpu",
      "source": "calibration",
      "when": "2026-09-30T16:39:07+0000",
      "gpu": "soc-CIXH5000_00",
      "driver": "mali"
    },
    "offload_targets": {},
    "power": {"ac_online": true},
    "thresholds_ms": {"weak_at_or_above": 20.0, "mid_at_or_above": 8.0},
    "cache": "/home/mini/.cache/ncz-screensavers/gpu-class.json",
    "calibrator": "/usr/libexec/ncz-screensavers/ncz-screensaver-calibrate"
  },
  "pool_class": "mid",
  "hack_env": {
    "HOME": "/home/mini",
    "XDG_RUNTIME_DIR": "/run/user/1000",
    "WAYLAND_DISPLAY": "wayland-0",
    "LD_LIBRARY_PATH": "/opt/singularity/lib:/opt/singularity/lib/aarch64-linux-gnu:/opt/singularity/lib:/opt/cixgpu-pro/lib/aarch64-linux-gnu:/usr/share/cix/lib",
    "MESA_SHADER_CACHE_DIR": "/home/mini/.cache/ncz-screensavers/mesa",
    "NCZ_GPU_BACKEND": "mali",
    "NCZ_MAX_RENDER_HEIGHT": "1080",
    "__EGL_VENDOR_LIBRARY_FILENAMES": "/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json"
  },
  ...
}
```

Key signals:
* `gpu_class.display.class=mid`, `ms=9.89`, `source=calibration`,
  `renderer=Mali-G720-Immortalis`.
* `pool_class=mid` — the random-mode pool now includes the mid
  hacks (the 78-of-84 pool that previously reported `weak` and
  excluded `voronoi_gles3`, `hyprsaver_stonks_gles3`, etc).
* `hack_env.NCZ_GPU_BACKEND=mali` and the CIX EGL vendor JSON is
  pinned — no software fallback.
* `thresholds_ms: weak_at_or_above=20.0, mid_at_or_above=8.0`
  — the operator contract for the threshold band.

## 5. Live `pool` (FIXED)

```
$ XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
    /tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z pool
pool class mid: 78 of 84 installed hacks
  out: crackberg_gles3 (needs strong GPU, pool allows mid)
  out: cubestorm_gles3 (needs strong GPU, pool allows mid)
  out: geodesic_gles3 (needs strong GPU, pool allows mid)
  out: gibson_gles3 (needs strong GPU, pool allows mid)
  out: klein_gles3 (listed suspect)
  out: xshadertoy_alienbeacon_gles3 (needs strong GPU, pool allows mid)
```

78 of 84 hacks are in the pool. The 6 excluded are 5 that need
`strong` (above the Mali-G720's measured class) and 1 that is a
listed suspect.

## 6. Cache file after calibrate

```
$ cat ~/.cache/ncz-screensavers/gpu-class.json
{
 "entries": {
  "soc-CIXH5000_00": {
   "class": "mid",
   "ms": 9.89,
   "renderer": "Mali-G720-Immortalis",
   "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
   "timer": "gpu",
   "source": "calibration",
   "when": "2026-09-30T16:39:07+0000",
   "gpu": "soc-CIXH5000_00",
   "driver": "mali"
  }
 }
}
```

The cache key is `soc-CIXH5000_00` (the platform-bus Mali), not
`pci-CIXH5010_0X` (a linlondp card). The pre-fix 0.7.1 cache
written by the shipped launcher used `pci-CIXH5010_03` because
the old `list_gpus()` did not enumerate the platform-bus Mali.

## 7. Live `vulkaninfo --summary` (no software fallback)

```
$ vulkaninfo --summary
Vulkan Instance Version: 1.4.357
...
	apiVersion         = 1.3.296
	driverVersion      = 53.0.0
	vendorID           = 0x13b5
	deviceName         = Mali-G720-Immortalis
	driverID           = DRIVER_ID_ARM_PROPRIETARY
	driverName         = Mali-G720-Immortalis
	driverInfo         = v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5
	conformanceVersion = 1.3.9.2
```

`llvmpipe` count = 0 in `vulkaninfo`. The only devices are
`Mali-G720-Immortalis`. **No software fallback.**

## 8. Live compositor and shader screensaver (60 fps sustained)

```
$ ps -ef | grep -E "labwc|hyprsaver|mali" | grep -v grep | head -5
root        1164       2  0 02:06 ?        00:00:03 [mali-simple-power-model-temp-poll]
root        1333       2  0 02:06 ?        00:04:25 [mali-gpuq-kthread]
mini        9206    9070  2 02:17 tty1     00:19:30 /opt/singularity/bin/labwc -S /opt/singularity/bin/singularity-desktop-session
mini        497171  497156  4 16:39 ?        00:00:00 /usr/lib/ncz-screensavers/hyprsaver_donut_gles3
mini        497156       1  0 16:39 ?        00:00:00 /usr/bin/python3 /tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z start --foreground --idle
```

* `labwc` is the Wayland compositor (PID 9206, session c2, on
  tty1) — running for ~19 minutes at dispatch.
* `mali-gpuq-kthread` and `mali-simple-power-model-temp-poll`
  confirm the mali_kbase driver is bound and active.
* `hyprsaver_donut_gles3` is a shader screensaver running under
  the supervisor.

FPS evidence from the hack log:

```
$ tail -5 /run/user/1000/ncz-screensaver/hack.log
[diag] hyprsaver[hyprsaver_donut] frame=7020 t=117.01s
[diag] frame #7020
[diag] hyprsaver[hyprsaver_donut] frame=7080 t=118.01s
[diag] frame #7080
[diag] hyprsaver[hyprsaver_donut] frame=7140 t=119.01s
[diag] frame #7140
[diag] hyprsaver[hyprsaver_donut] frame=7200 t=120.01s
[diag] frame #7200
[diag] hyprsaver[hyprsaver_donut] frame=7260 t=121.01s
[diag] frame #7260
```

**7200 frames in 120.01 s = 60.0 fps sustained.**

## 9. The 'weak' verdict source (recap, with fresh evidence)

The previous dispatches identified three independent failures
that all produced the same operator-visible symptom "the GPU is
shown as weak" on the shipped `ncz-screensavers` 0.7.1:

* `RENDERER_TABLE` weak pattern (old): `mali-g[35]\d\b` — a
  3-character range that swallows Mali-G57 (Valhall mid) and the
  entire Mali-G6xx / G7xx / Immortalis family into the weak
  tier.
* `class_from_topology()` (old): returns `weak` as the catch-all
  when the driver is not in its small hard-coded list. `linlondp`
  is not on the list → `weak`.
* `list_gpus()` (old): walks only `/sys/class/drm/card*`. The
  `/sys/class/misc/mali0` character device is invisible to it.

The fix touches all three. This dispatch re-verifies live that
the fix holds against the .66 host:

* `class_from_renderer("Mali-G720-Immortalis") == "mid"` (not
  `weak`) — the new narrow weak pattern (`mali-g(?:31|52)\b`) and
  the broad mid pattern (`immortalis|mali-g(?:5[7-9]|6\d\d|7\d\d)`)
  keep the Mali-G720 out of the weak bucket.
* `class_from_topology({"driver": "mali"}) == "mid"` — the
  `mali` glue driver is in `MID_DRIVERS`.
* `_iter_platform_gpu_devices()` yields a `soc-CIXH5000_00`
  entry from `/sys/class/misc/mali0`, and `list_gpus()` keeps it
  (and filters out the four `linlondp` display controllers).

## 10. Tests added in this dispatch

`tools/tests/test_gpu_class.py` (+195 lines, this dispatch's six new
tests, all PASS):

* `test_sky1_r20_cont_class_from_ms_keeps_9_89_in_mid_band` —
  pins the operator threshold-band contract with the live ms=9.89.
* `test_sky1_r20_cont_cmd_status_reports_mid_with_this_dispatches_capture` —
  pins the operator-visible cmd_status JSON output (gpu_class=mid,
  ms=9.89, source=calibration). Pairs with the three earlier pins.
* `test_sky1_r20_cont_cmd_gpus_reports_mid_against_live_capture` —
  pins the operator-visible `gpus` text output
  (`soc-CIXH5000_00 display  other   mali     mid    9.9 ms`).
* `test_sky1_r20_cont_cmd_doctor_pool_class_is_mid` — pins the
  operator-visible `doctor --json` pool_class=mid, gpu_class
  .display.class=mid, gpus[] has only one Mali (display=true,
  display_only=false).
* `test_sky1_r20_cont_no_software_fallback_in_hack_env` —
  adversarial policy test: the hack env on the .66 layout has
  no `LIBGL_ALWAYS_SOFTWARE`, no `GALLIUM_DRIVER`, no
  `MESA_LOADER_DRIVER_OVERRIDE=swrast`. Pairs with the live
  `vulkaninfo` llvmpipe count = 0.
* `test_sky1_r20_cont_all_four_live_ms_pins_classify_as_mid` —
  adversarial cross-check: all four independent live ms pins
  (9.88, 14.71, 11.93, 9.89) classify as `mid` per the
  threshold band contract.

## 11. Five-step validation (this dispatch, end-to-end, all pass)

```
$ PYTHONPATH=launcher:. python3 -m pytest tools/tests/
======================== 218 passed, 2 skipped in 7.01s ========================

$ PYTHONPATH=launcher:. python3 -m pytest tools/tests/test_gpu_class.py -k r20_cont
====================== 6 passed, 143 deselected in 0.10s =======================

$ PYTHONPATH=launcher:. python3 -m pytest tools/tests/test_gpu_class.py -k sky1
====================== 49 passed, 100 deselected in 0.16s =======================

$ PYTHONPATH=launcher:. python3 -m pytest tools/tests/test_gpu_class.py -k live_capture
====================== 11 passed, 138 deselected in 0.06s =======================

$ python3 -m ruff check launcher/ncz-screensaver tools/tests/test_gpu_class.py
All checks passed!

$ python3 -m ruff format --check launcher/ncz-screensaver tools/tests/test_gpu_class.py
2 files already formatted
```

Plus: `python3 -m unittest discover -s launcher/tests` →
Ran 38 tests in 15.012s, OK (unchanged from r15-cont).

218 pytest tests pass (was 212 at r15-cont, +6 from this dispatch's
new tests).

## 12. Evidence paths (this dispatch)

* build-host report (this file):
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/m66-weak-tier-continuation-r20-2026-09-30.md`
  (will be written at the end of this dispatch).
* In-repo live verification doc (committed at the new SHA):
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R6.md` (this file).
* Previous in-repo live verification docs (committed at r0..r5):
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30.md`,
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R2.md`,
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R3.md`,
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R4.md`,
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R5.md`.

## 13. Did NOT verify (this dispatch)

* I did not push to GitLab `origin/` (the remote rejected a
  forward-only push as non-fast-forward in earlier passes;
  merging the divergent GitLab-side history would introduce
  unrelated changes, and force-pushing is forbidden by the
  operator's standing rules). The build-host remote `origin/`
  (ssh://jasonperlow@build-host) is the canonical reference.
* I did not rebuild the ncz-screensavers .deb from this branch
  and install it on .66 (operator policy: no package installs
  on .66). The fixed binary on .66 is
  `/tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z`
  (md5 `e7e77d4cc7cf0294902086149486096e`, byte-for-byte
  identical to this branch's `launcher/ncz-screensaver`).
* I did not exercise every code path in the launcher (e.g.
  `cmd_calibrate`); only `gpus`, `gpus --json`, `status --json`,
  `doctor`, `pool`, `plan`, `calibrate`, and `start` were re-run
  live. The pre-existing pytest/ruff checks cover the rest.
* I did not reboot .66, did not install packages, did not modify
  `/usr/bin/ncz-screensaver` on .66, did not flash firmware, did
  not dd the disk, did not touch `boot.scr` or `extlinux.conf`.
* I did briefly stop+start the supervisor (`ncz-screensaver stop`
  + calibrate + `ncz-screensaver start`) so the calibrator
  could populate the cache file; this is a normal user action
  and does not change system config.