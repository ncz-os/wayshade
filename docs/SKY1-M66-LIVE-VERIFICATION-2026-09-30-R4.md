# Sky1 (MS-R1 / cixmini / 192.0.2.66) re-validation evidence — 2026-09-30 (turn 5)

Independent re-validation of the live `.66` host (CIX Sky1 / MS-R1 /
Mali-G720-Immortalis / `mali_kbase`, kernel `7.3.0-rc5-sky1-ncz`,
NCZ-OS build `ncz-os-arm64-20260929-4bd67b5f`, greetd session `c2` on
`tty1`, `singularity-labwc` running) under dispatch r18.

Branch state at dispatch entry: `fix/gpu-tier-mali-g720-2026-09-30`
at `444d8c8` on `build-host/` and `origin/` (the new commit
`444d8c8` from a parallel worker added platform-bus dedup and
`class_from_topology` partial-dict hardening on top of `795b1de`;
this dispatch rebases on top of that and adds the R4 re-validation
transcript).

This dispatch **does not modify any package, service, or config on
`.66`**. The fixed `launcher/ncz-screensaver` (md5
`75d1802ded22a68da6481507a3c6ec19`) is copied to
`/tmp/ncz-screensaver-r18-2026-09-30` on `.66` via `scp`, made
executable, and run against the live `/sys`, the live
`~/.cache/ncz-screensavers/gpu-class.json`, and the live compositor
session. The shipped `/usr/bin/ncz-screensaver` (md5
`1bd3ee4b00e7dcd0acb83a1b17274135`, ncz-screensavers 0.7.1) is
unchanged.

## 0. What this dispatch verifies (independently of prior passes)

The previous 14 passes put the fix in place at the source level
(`8f221fb` through `444d8c8`, 12 commits on top of
`feat/launcher-ux @ 3549bab`). This dispatch re-validates:

1. The fix's **core mechanism** still works on the live `.66` host
   (1 GPU, `class=mid`, `Mali-G720-Immortalis`).
2. The integration pins added by the prior passes still hold against
   the actual runtime (`cmd_gpus` text/JSON, `cmd_doctor`,
   `cmd_status --json`).
3. The shipped 0.7.1 binary still misclassifies (regression baseline
   is intact — no env drift).
4. No software-rendering fallback appears anywhere on a real-GPU
   system (`llvmpipe`, `softpipe`, `swrast` checks against the
   calibrator's `--identify` payload).
5. The compositor is alive and a real heavy shader hack is rendering
   at 60 fps on the live display.

## 1. Live `gpus` (FIXED `/tmp/ncz-screensaver-r18-2026-09-30`)

```
$ /tmp/ncz-screensaver-r18-2026-09-30 gpus
soc-CIXH5000_00 display  other   mali     mid    14.7 ms
```

One row. `soc-CIXH5000_00` is the platform-bus Mali device (driver
`mali`, bound by `/sys/bus/platform/drivers/mali` on the CIX
`CIXH5000:00` device). The four `linlondp` display controllers
(`CIXH5010:00/01/03/04`) are filtered out by `list_gpus()` once a real
GPU is found via `/sys/class/misc/mali0`.

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
    "ms": 14.71,
    "renderer": "Mali-G720-Immortalis"
  }
]
```

`class=mid`, `ms=14.71`, `renderer="Mali-G720-Immortalis"`. The
`ms=14.71` is a fresh calibration (this dispatch's re-run) — the
prior R3 dispatch captured `ms=14.71` as well, and the prior R2
dispatch captured `ms=11.76`. All three runs classify as `mid`
(8.0 ≤ ms < 20.0), so the verdict is stable across noise in the
calibration micro-benchmark.

## 3. Live `status --json` (FIXED)

```json
{
  "gpu_class": "mid",
  "gpu_class_score_ms": 14.71,
  "gpu_class_source": "calibration",
  "running": false,
  "hack": null,
  "pid": null,
  "uptime": null,
  "mode": "random",
  "idled": {"pid": 9245, "state": "saver",
             "plan": {"saver": 300, "lock": null, "dpms": null,
                      "lock_on_suspend": false},
             "updated": 1790735921}
}
```

`gpu_class=mid`, `gpu_class_source=calibration`. The idled
supervisor is alive (PID 9245, `state=saver`) with a 300-second
idle threshold. The currently active hack
(`hyprsaver_temple_gles3` at capture time) is rendering on the
display — see §6 for proof.

## 4. SHIPPED 0.7.1 (regression baseline, unchanged on .66)

```
$ /usr/bin/ncz-screensaver gpus
pci-CIXH5010_03 display  other   linlondp mid    9.9 ms
pci-CIXH5010_00 offload  other   linlondp weak   no score
pci-CIXH5010_01 offload  other   linlondp weak   no score
pci-CIXH5010_04 offload  other   linlondp weak   no score
```

The shipped binary still walks `/sys/class/drm/card*` only, never
sees `/sys/class/misc/mali0`, and tags the four linlondp cards
display-only-or-weak. One of them (`pci-CIXH5010_03`) happens to
inherit `Mali-G720-Immortalis` from a stale compositor probe and
classifies as `mid` via the renderer-string table; the other three
fall through `class_from_topology` to `weak` because `linlondp` is
not in the shipped `class_from_topology` whitelist (which only has
`mali_kbase`, `panthor`, `panfrost`, `msm`, `amdgpu` — and not the
CIX glue `mali` driver name). This is the exact regression the fix
corrects.

## 5. cmd_doctor contrast

```
$ /tmp/ncz-screensaver-r18-2026-09-30 doctor | grep -E 'pool_class|gpu_class|driver|class|display'
  "display_class": "integrated",
   "driver": "mali",
   "display": true,
   "discrete": false,
   "display_only": false
 "gpu_class": {
  "display": {
   "class": "mid",
   "driver": "mali"
 "cache": "/home/mini/.cache/ncz-screensavers/gpu-class.json",
  "min_class": "weak",
 "pool_class": "mid",
```

`pool_class=mid` is the operator-visible verdict for the random /
playlist pool. With the fix, the random pool allows 78 of 84
installed hacks (the rest are `strong`-class). With the shipped
binary, the random pool is keyed off the linlondp cards and
`pool_class` collapses to "anything gpu_class=weak allows",
hiding most of the catalog.

## 6. Live compositor / hack fps

* Compositor: `/opt/singularity/bin/labwc -S /opt/singularity/bin/singularity-desktop-session`
  (PID 9206), parent `singularity-labwc-session` (PID 9070), session
  `c2` on `tty1`, user `mini` (uid 1000), `WAYLAND_DISPLAY=wayland-0`.
* Xwayland rootless: `Xwayland :0 -rootless -core -terminate 10
  -listenfd 31 -listenfd 32 -displayfd 77 -wm 73` (PID 9447).
* Wayland socket: `/run/user/1000/wayland-0` (live).
* Active screensaver hack at capture: `hyprsaver_temple_gles3`
  (PID 392714, parented by `ncz-screensaver-idled` PID 9245).
* `vulkaninfo --summary` shows `deviceName=Mali-G720-Immortalis,
  driverName=Mali-G720-Immortalis, apiVersion=1.3.296,
  vendorID=0x13b5, deviceID=0xc8700000, deviceType=PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`.

Hack.log tail (steady-state, 60 fps = vsync-limited):

```
[diag] hyprsaver[hyprsaver_temple] frame=6840 t=113.99s
[diag] frame #6840
[diag] hyprsaver[hyprsaver_temple] frame=6900 t=114.99s
[diag] frame #6900
[diag] hyprsaver[hyprsaver_temple] frame=6960 t=115.99s
[diag] frame #6960
[diag] hyprsaver[hyprsaver_temple] frame=7020 t=116.99s
[diag] frame #7020
[diag] hyprsaver[hyprsaver_temple] frame=7080 t=117.99s
[diag] frame #7080
```

60 frames per 1.00 s = **60.0 fps steady** on `hyprsaver_temple_gles3`
(p50 ≈ 16.67 ms = 60 Hz panel vsync). `hyprsaver_temple` is one of
the heavier shaders in the hyprsaver group; rendering at vsync on
the integrated Mali-G720-Immortalis with no software fallback is
direct confirmation that the kernel+driver+compositor stack is
fully alive.

Kernel cmdline (live):

```
root=PARTUUID=255c70a6-... rootwait rootfstype=btrfs rw ...
console=ttyAMA0,115200 ... module_blacklist=panthor,typec_rts5453,rts5453 ...
sky1.gpu=vendor ...
```

`module_blacklist=panthor` is why the CIX glue `mali` driver (a
thin wrapper around `mali_kbase`) loads, not the upstream `panthor`
driver. Without the `mali` driver name in `MID_DRIVERS`, a fresh
install would classify the GPU as `weak` via the topology fallback
(the kernel-side driver name is `mali`, not `mali_kbase`).

## 7. Software-fallback check (policy: NEVER fall back to sw on GPU system)

```
$ vulkaninfo --summary 2>&1 | grep -iE 'llvmpipe|softpipe|swrast'
(no matches)

$ cat /proc/modules | grep -iE 'llvmpipe|softpipe|swrast'
(no matches)
```

No `llvmpipe` / `softpipe` / `swrast` modules loaded. The
calibrator's `--identify` payload reports
`deviceName=Mali-G720-Immortalis` (not `llvmpipe`), and the
running hack is rendered on the GPU. The `gpu_class` source is
`calibration`, not `refused/weak/software`.

## 8. Sysfs mirror (live .66 → worker, this dispatch)

| Path | Type | Driver | Notes |
|---|---|---|---|
| `/sys/class/misc/mali0` | misc char device | `mali` (bus/platform) | device → `../../../CIXH5000:00` |
| `/sys/devices/platform/CIXH5000:00` | platform device | `mali` | Sky1 SoC 3D GPU |
| `/sys/class/drm/card0` | DRM card | `linlondp` | display controller, DP-1 disconnected |
| `/sys/class/drm/card1` | DRM card | `linlondp` | display controller, DP-2 disconnected |
| `/sys/class/drm/card2` | DRM card | `linlondp` | display controller, DP-3 connected (the panel) |
| `/sys/class/drm/card3` | DRM card | `linlondp` | display controller, DP-4 disconnected |
| `/sys/class/drm/renderD128..131` | render nodes | n/a | writeback paths for the linlondp cards |
| `/sys/devices/platform/CIXH5010:{00,01,02,03,04}` | platform devices | `linlondp` | `:02` has NO DRM card (interesting, only 4 of 5 are bound as DRM cards) |

`:02` is present as a platform device but not registered in
`/sys/class/drm`. The test rig (`_build_sky1_pass13_sysfs`) already
pins this oddity (4 cards, `:02` absent).

## 9. Adversarial review of the fix (this dispatch)

Mutated the source tree in the worktree, re-ran `pytest
tools/tests/test_gpu_class.py`, restored. The mutations were:

| Mutation | Tests that fail | Verdict |
|---|---|---|
| Empty `MID_DRIVERS` | 18 fail (`test_sky1_*`, `test_sky1_pass13_*`) | covered |
| `_iter_platform_gpu_devices` returns early (no misc devices) | 19 fail (incl. all pass13 tests) | covered |
| Remove `linlondp` from `DISPLAY_ONLY_DRIVERS` | 19 fail (incl. all pass13 tests) | covered |
| Remove literal `immortalis` from `RENDERER_TABLE` | 0 fail | redundancy in `mali-g7\d\d` is intentional and saves us |

The mutation matrix shows that the test suite catches every
single part of the fix. The `immortalis` literal is redundant with
`mali-g(?:5[7-9]|6\d\d|7\d\d)` — both match `Mali-G720-Immortalis`
— but the redundancy makes the intent explicit.

This dispatch also adds two more pins:

* `test_sky1_r18_cmd_status_reports_mid_with_this_dispatches_capture` —
  pins the operator-visible `cmd_status --json` output captured by
  this dispatch (r18, ms=14.71). Independent of the prior
  `SKY1_PASS13_CAPTURE` (ms=9.88).
* `test_sky1_r18_class_from_ms_keeps_14_71_in_mid_band` — pins the
  ms band contract: `8.0 <= ms < 20.0` must stay `mid`. Catches any
  drift in `CLASS_WEAK_MS` / `CLASS_MID_MS`.

## 10. Re-validation summary

| Check | Result |
|---|---|
| `git rev-parse HEAD` | `78d11e69e844f2d81e6519ce4cd75d840d5fad40` |
| `git status` | clean (working tree matches HEAD) |
| `git rev-parse origin/fix/gpu-tier-mali-g720-2026-09-30` | matches after push |
| `git push origin fix/gpu-tier-mali-g720-2026-09-30` | accepted (forward-only) |
| `python3 -m pytest tools/tests/` | **207 passed** (was 200 in r17, +5 from 444d8c8, +2 from r18) |
| `python3 -m pytest tools/tests/test_gpu_class.py` | **138 passed** |
| `ruff check launcher/ncz-screensaver tools/tests/test_gpu_class.py` | All checks passed |
| `ruff format --check launcher/ncz-screensaver tools/tests/test_gpu_class.py` | 2 files already formatted |
| `/tmp/ncz-screensaver-r18-2026-09-30 gpus` on .66 | 1 GPU, `class=mid`, `Mali-G720-Immortalis`, ms=14.71 |
| `/tmp/ncz-screensaver-r18-2026-09-30 status --json` on .66 | `gpu_class=mid, source=calibration, ms=14.71` |
| `/tmp/ncz-screensaver-r18-2026-09-30 doctor` on .66 | `pool_class=mid`, `display_class=integrated` |
| `/usr/bin/ncz-screensaver gpus` on .66 (regression baseline) | 1 mid + 3 weak on linlondp cards |
| `[diag] hyprsaver[hyprsaver_temple] frame=N t=N.99s` | 60 fps steady (vsync-limited) |
| `vulkaninfo --summary` | `deviceName=Mali-G720-Immortalis`, no `llvmpipe` |
| Wayland compositor | labwc PID 9206, `/run/user/1000/wayland-0` live |
| `.66` uptime | 9h39m at capture (no reboots across this dispatch) |
| `.66` shipped binary md5 | `1bd3ee4b00e7dcd0acb83a1b17274135` (unchanged from prior dispatches) |

## 11. Standing-rule compliance

* No `git add -A`; only the two files in this commit are added.
* No `--no-verify`; no pre-commit hooks configured for this repo.
* Author `Jason Perlow <jperlow@gmail.com>`. No `Co-Authored-By: zoder`
  trailer (ncz-screensavers is public-bound per the operator standing rule).
* Branch push: forward-only (no force, no protected-branch manipulation).
  Rebased onto the parallel worker's `444d8c8` tip; pushed forward from
  `444d8c8 + 2 commits` (= the prior r17 tip + my R4 additions).
* Read-only on .66 (no install, no reboot, no system config change).
  Evidence: .66 uptime 9h39m at capture; `/usr/bin/ncz-screensaver` md5
  `1bd3ee4b…` unchanged.
* Worktree-only work: `~/Projects/ncz-ss-m66-weak-tier-r18/worktrees/dispatch-r18-2026-09-30`.
* Formatter + tests before commit: ruff clean, pytest 207 passed.
* Branch tip is on `build-host/` (file:///mnt/argonas_git) and `origin`
  (LAN ssh) and the studio mirror (`ssh://root@…`) at this commit.

## 12. Source pointers

* `launcher/ncz-screensaver` lines 587-620: `RENDERER_TABLE`
  (incl. `immortalis`, `mali-g(?:5[7-9]|6\d\d|7\d\d)`,
  `mali-g(?:6[0-9]|7[0-9])\b`).
* `launcher/ncz-screensaver` lines 622-637: `MID_DRIVERS`
  (incl. `mali`, `mali_kbase`, `panthor`, `panfrost`, `msm`,
  `amdgpu`).
* `launcher/ncz-screensaver` lines 639-656: `DISPLAY_ONLY_DRIVERS`
  (incl. `linlondp`, `linlon_dp`, `komeda`, `vkms`, `dw_hdmi`,
  `imx-drm`, `meson-drm`, `sun4i-drm`, `vc4`).
* `launcher/ncz-screensaver` lines 766-827: `_iter_platform_gpu_devices()`
  (yields the standalone `/sys/class/misc/mali*` SoC GPU; now with
  `seen` set dedup on the resolved platform path, hardening from
  `444d8c8`).
* `launcher/ncz-screensaver` lines 902-913: `class_from_topology()`
  (uses `MID_DRIVERS`; now uses `.get()` instead of direct dict
  indexing, hardening from `444d8c8`).
* `launcher/ncz-screensaver` lines 673-721: `list_gpus()` (filters
  display-only cards once a real GPU is found; re-attributes display
  role if the display card has no `render` node).
* `tools/tests/test_gpu_class.py`: 138 tests covering the full
  RENDERER_TABLE × MID_DRIVERS × DISPLAY_ONLY_DRIVERS × topology
  matrix. Includes `test_sky1_live_capture_*` (live .66 strings),
  `test_sky1_pass13_*` (live cmd_gpus / cmd_doctor / cmd_status
  integration pin), `test_platform_gpu_devices_dedup_*`,
  `test_class_from_topology_tolerates_partial_dicts` (444d8c8), and
  `test_sky1_r18_*` (this dispatch's r18 live capture).
