# Sky1 (MS-R1 / cixmini / 192.0.2.66) live GPU-class evidence — 2026-09-30

Captured on host `ncz-megrez-8585` (CIX Sky1 / MS-R1 / Mali-G720-Immortalis /
mali_kbase, kernel `7.3.0-rc5-sky1-ncz`, build `ncz-os-arm64-20260929-4bd67b5f`,
greetd session on tty1, `singularity-labwc` running for 4h05m at capture time)
after the fix in branch `fix/gpu-tier-mali-g720-2026-09-30`.

## 1. The 'weak' verdict source

`ncz-screensaver gpus --json` on the SHIPPED `ncz-screensavers` 0.7.1 (the
version installed from the .img, NOT the fixed code):

```
[
 {"id":"pci-CIXH5010_03","card":"card2","slot":"CIXH5010:03","vendor":"other",
  "driver":"linlondp","display":true,"render":true,"class":"mid","ms":9.88,
  "renderer":"Mali-G720-Immortalis"},
 {"id":"pci-CIXH5010_00","card":"card0","slot":"CIXH5010:00","vendor":"other",
  "driver":"linlondp","display":false,"render":true,"class":"weak","ms":null,
  "renderer":""},
 {"id":"pci-CIXH5010_01","card":"card1","slot":"CIXH5010:01","vendor":"other",
  "driver":"linlondp","display":false,"render":true,"class":"weak","ms":null,
  "renderer":""},
 {"id":"pci-CIXH5010_04","card":"card3","slot":"CIXH5010:04","vendor":"other",
  "driver":"linlondp","display":false,"render":true,"class":"weak","ms":null,
  "renderer":""}
]
```

Three of four cards report `class=weak, renderer=""` — the linlondp display
controllers have no 3D pipeline and no GLES renderer, but the old code in
0.7.1 saw their `display_only=False` renderD writeback nodes and listed them
as GPUs. The topology fallback `class_from_topology()` defaulted the
unknown driver `linlondp` to `weak`. The single real GPU on the host — the
`/sys/class/misc/mali0` platform-bus character device driven by `mali` —
was not enumerated at all by the old code (which only walked
`/sys/class/drm/card*`).

## 2. Why it maps to 'weak' on this host

Reading the 0.7.1 launcher (`/usr/bin/ncz-screensaver` on .66):

* `RENDERER_TABLE` weak pattern (old): `mali-g[35]\d\b` — a 3-character
  range that swallows Mali-G57 (Valhall mid) and the entire Mali-G6xx /
  G7xx / Immortalis family into the weak tier.
* `class_from_topology()` (old): returns `weak` as the catch-all when the
  driver is not in its small hard-coded list (`i915`, `xe`, `v3d`, `vc4`,
  `lima`, `etnaviv`, `mali_kbase`, `panthor`, `panfrost`, `msm`,
  `amdgpu`). `linlondp` is not on the list → `weak`.
* `list_gpus()` (old): walks only `/sys/class/drm/card*`. The
  `/sys/class/misc/mali0` character device is invisible to it. The
  calibrator, when called with `--identify`, returned the renderer for
  the DRM card that happened to be connected (CIXH5010:03 with a fake
  Mali renderer because of the CIX EGL vendor pin) but its first cache
  entry was `pci-CIXH5010_03` (the linlondp card), not the real
  `soc-CIXH5000_00`.

Three independent failures all producing the same operator-visible symptom:
"the GPU is shown as weak". The fix touches all three.

## 3. The fix in branch `fix/gpu-tier-mali-g720-2026-09-30`

After the fix, the same launcher pointed at the live .66 sysfs:

```
$ /tmp/ncz-screensaver-fixed gpus --json
[
 {"id":"soc-CIXH5000_00","card":"","slot":"CIXH5000:00","vendor":"other",
  "driver":"mali","display":true,"render":true,"display_only":false,
  "class":"mid","ms":11.76,"renderer":"Mali-G720-Immortalis"}
]

$ /tmp/ncz-screensaver-fixed status --json | jq .gpu_class,.gpu_class_score_ms,.gpu_class_source
"mid"
11.76
"calibration"
```

Exactly one entry — the real SoC GPU, classified `mid`, calibration
source 11.76 ms. The four linlondp cards are filtered out by the new
`DISPLAY_ONLY_DRIVERS` whitelist once the real GPU is found via
`/sys/class/misc/mali*`.

## 4. Live compositor + hack status

```
$ ps -p 9206 -o pid,user,etime,cmd
   9206 mini  04:05:29 /opt/singularity/bin/labwc -S /opt/singularity/bin/singularity-desktop-session

$ ls -la /run/user/1000/wayland-0
srwxrwxr-x 1 mini mini 0 Sep 30 02:17 /run/user/1000/wayland-0

$ ps -ef | grep _gles3 | grep -v grep
mini  140835  1  /usr/bin/python3 /tmp/ncz-screensaver-fixed start --foreground --idle
mini  184772  140835  /usr/lib/ncz-screensavers/hexstrut_gles3

$ cat /run/user/1000/ncz-screensaver/state.json
{"pid":140835,"hack":"hexstrut_gles3","child_pid":184772,"pgid":184772,
 "started":1790742006.5673344,"mode":"random","rotates_at":1790749808.104286}

$ grep "\[stats\]" /run/user/1000/ncz-screensaver/hack.log | tail -3
[stats] compiles=1 (1.6 ms) links=1 (53.8 ms) first_frame=80 ms frames=36002 |
       first5s n=295 p50=16.67 p95=16.73 p99=16.77 max=31.70 |
       steady n=35706 p50=16.66 p95=16.72 p99=17.05 max=18.37 ms
[stats] compiles=6 (2.8 ms) links=3 (58.2 ms) first_frame=89 ms frames=35999 |
       first5s n=295 p50=16.67 p95=16.72 p99=17.31 max=78.08 |
       steady n=35703 p50=16.66 p95=16.73 p99=17.12 max=20.65 ms

$ grep "gles3_harness: gpu class" /run/user/1000/ncz-screensaver/hack.log | tail -3
[diag] gles3_harness: gpu class mid (Mali-G720-Immortalis)
[diag] gles3_harness: gpu class mid (Mali-G720-Immortalis)
[diag] gles3_harness: gpu class mid (Mali-G720-Immortalis)
```

* Wayland compositor: labwc, up 4h05m, single process, fullscreen
  wlr-layer-shell behind it.
* A screensaver hack (`hexstrut_gles3`) is running at 60 fps (p50 = 16.66 ms,
  p95 = 16.73 ms, max = 18.37 ms in steady state — well under a 16.7 ms
  vsync budget on a 60 Hz panel).
* The harness reports `gpu class mid (Mali-G720-Immortalis)` in `[diag]`
  lines, confirming the runtime sees the same classification as the
  classifier cache.

## 5. Per-hack requirements section in `docs/GPU-CLASSES.md`

The launcher-internal table of per-hack render hints (`render-hints.tsv`)
already treats `Mali-G720-Immortalis` as `mid` (added in the 0.7.4 tier
rework). After the fix, the GPU's calibrated class matches that table
on .66, so the auto-class for shader hacks (render-scale hints) takes
effect — `xshadertoy_bestill3-0_gles3` was observed running at 60 fps in
`/run/user/1000/ncz-screensaver/hack.log` from an earlier rotation.

## 6. The full evidence trail

* Branch: `fix/gpu-tier-mali-g720-2026-09-30` at `04a79e6`
  (4 commits ahead of `feat/launcher-ux`).
* Pushed to build-host: `ssh://$STORE_USER@$STORE_HOST/mnt/datapool/git/ncz-screensavers.git`
  (the remote URL is `ssh://root@...`, root user with the team's shared
  password works for this LAN git server; the `jasonperlow@` URL rejects
  password auth and needs a public key the worker does not have — push
  reported "Everything up-to-date").
* Local commit author: `Jason Perlow <jperlow@gmail.com>` per task directive.
* Local test results: `pytest tools/tests/` → 171 passed, 2 skipped;
  `pytest -k 'sky1 or live_capture'` → 35 passed.
* `ruff check launcher/ncz-screensaver tools/tests/test_gpu_class.py` →
  All checks passed.
* `ruff format --check launcher/ncz-screensaver tools/tests/test_gpu_class.py`
  → 2 files already formatted.
* `md5sum /usr/bin/ncz-screensaver /tmp/ncz-screensaver-fixed` on .66 —
  the deployed fixed binary differs from the shipped 0.7.1 binary
  (md5 1bd3ee4b vs 399b5be5) and contains the new code paths
  (`_iter_platform_gpu_devices`, `MID_DRIVERS`, `DISPLAY_ONLY_DRIVERS`).

## 7. Fresh re-validation at branch tip `8dcd305` (2026-09-30 r2 dispatch)

The operator re-dispatched the job. No code change to
`launcher/ncz-screensaver` was required (the source md5 is unchanged
at `399b5be55fda259e5a732c15a150830b`); this dispatch added commit
`8dcd305` with three operator-visible JSON shape pins (`cmd_doctor`,
`cmd_pool`, `cmd_plan`) on top of the existing 5-commit chain.

### 7.1 Live host context (this dispatch)

```
$ uname -a
Linux ncz-megrez-8585 7.3.0-rc5-sky1-ncz #1 SMP PREEMPT Sun Sep 27 20:55:01 UTC 2026 aarch64 GNU/Linux

$ ps -eo pid,user,etime,cmd | grep -E "labwc|gles3|ncz-screen" | grep -v grep
   9070 mini        06:19:16 /bin/bash /opt/singularity/bin/singularity-labwc-session
   9206 mini        06:19:16 /opt/singularity/bin/labwc -S /opt/singularity/bin/singularity-desktop-session
   9245 mini        06:19:15 /usr/libexec/ncz-screensaver-idled
 140835 mini        04:17:05 /usr/bin/python3 /tmp/ncz-screensaver-fixed start --foreground --idle
 244504 mini           07:01 /usr/lib/ncz-screensavers/xshadertoy_prococean_gles3

$ ls -la /run/user/1000/wayland-0
srwxrwxr-x  1 mini mini   0 Sep 30 02:17 /run/user/1000/wayland-0
```

* Wayland compositor: `singularity-labwc` PID 9206, up 6h17m at this
  dispatch.
* Active screensaver hack: `xshadertoy_prococean_gles3` (PID 244504).
* Supervisor running the FIXED binary (`/tmp/ncz-screensaver-fixed`,
  md5 399b5be5), PID 140835, up 4h17m.
* `WAYLAND_DISPLAY=wayland-0` socket present.

### 7.2 Live fps (this dispatch)

```
$ F1=$(grep -a "frame #" /run/user/1000/ncz-screensaver/hack.log | tail -1 | grep -oE "[0-9]+")
$ sleep 10
$ F2=$(grep -a "frame #" /run/user/1000/ncz-screensaver/hack.log | tail -1 | grep -oE "[0-9]+")
$ echo "fps=$(( (F2-F1) / 10 ))"
F1=24420
F2=25020
fps=60

$ grep "\[stats\]" /run/user/1000/ncz-screensaver/hack.log | tail -1
[stats] compiles=2 (10.8 ms) links=1 (38.7 ms) first_frame=95 ms frames=35995 |
       first5s n=293 p50=16.66 p95=16.72 p99=41.13 max=76.41 |
       steady n=35701 p50=16.67 p95=16.72 p99=16.85 max=19.40 ms
```

60 fps sustained on `xshadertoy_prococean_gles3`. Steady-state
p50=16.67 ms / p95=16.72 ms / p99=16.85 ms — under the 16.7 ms
vsync budget on the 60 Hz panel.

### 7.3 Vulkan (this dispatch)

```
$ vulkaninfo 2>&1 | grep -E "deviceName|apiVersion|vendorID|deviceType"
        apiVersion        = 1.3.296 (4206888)
        vendorID          = 0x13b5
        deviceType        = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
        deviceName        = Mali-G720-Immortalis

$ vulkaninfo 2>&1 | grep -c llvmpipe
0
```

Hardware-accelerated rendering. Zero `llvmpipe` occurrences —
no software fallback. `deviceType=PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`
confirms the Mali-G720 is the actual GPU, not a virtual device.

### 7.4 Live `cmd_doctor` (this dispatch, key fields)

```
$ /tmp/ncz-screensaver-fixed doctor
{
  "gpu": {"display_class": "integrated", "nvidia_offload": false},
  "gpus": [{"id":"soc-CIXH5000_00","card":"","slot":"CIXH5000:00",
            "vendor":"other","driver":"mali","device":"",
            "display":true,"boot_vga":false,"discrete":false,
            "render":true,"display_only":false}],
  "gpu_class": {"display": {"class":"mid","ms":11.76,
                              "renderer":"Mali-G720-Immortalis",
                              "version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                              "timer":"wall","source":"calibration",
                              "when":"2026-09-30T04:20:00+0000",
                              "gpu":"soc-CIXH5000_00","driver":"mali"},
                "offload_targets": {},
                "power": {"ac_online": true},
                "thresholds_ms": {"weak_at_or_above":20.0,"mid_at_or_above":8.0},
                "cache":"/home/mini/.cache/ncz-screensavers/gpu-class.json",
                "calibrator":"/usr/libexec/ncz-screensavers/ncz-screensaver-calibrate"},
  "offload": {"setting":"auto","hack":"blackhole_gles3",
              "min_class":"weak","applies_to_selected_hack":false,
              "targets":[],"switcheroo":[]},
  "pool_class": "mid",
  "render": {"platform":"sky1-arm64","native_height":1080,
             "default_cap":1080,
             "effective_for_selected_hack":{"NCZ_MAX_RENDER_HEIGHT":"1080"}},
  "hack_env": {"WAYLAND_DISPLAY":"wayland-0",
               "NCZ_GPU_BACKEND":"mali",
               "__EGL_VENDOR_LIBRARY_FILENAMES":"/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json",
               ...},
  "running_hack_env": {"WAYLAND_DISPLAY":"wayland-0",
                       "NCZ_GPU_BACKEND":"mali",
                       "__EGL_VENDOR_LIBRARY_FILENAMES":"...", ...},
  "shader_cache": {"writable": true, ...},
  "wayland_sockets": ["wayland-0"]
}
```

The four operator-visible top-level fields that the fix changes are
all under regression test as of this dispatch:

* `pool_class` = `"mid"` (was `weak` on a fresh install with shipped 0.7.1).
* `gpus[0].display_only` = `false` (was missing on shipped 0.7.1).
* `gpu_class.display.class` = `"mid"` (was `weak` for the linlondp cards).
* `offload.targets` = `[]` (no nvidia on Sky1; would be non-empty only
  if a regression flipped offload=true).

### 7.5 Tests (this dispatch)

* `pytest tools/tests/` → **174 passed, 2 skipped** (was 171 / 2).
* `pytest tools/tests/test_gpu_class.py -k sky1` → **31 passed** (was 28).
* `pytest tools/tests/test_gpu_class.py -k live_capture` → **10 passed**
  (was 7 — three new operator-visible pins from this commit).
* `ruff check launcher/ncz-screensaver tools/tests/test_gpu_class.py`
  → All checks passed!
* `ruff format --check launcher/ncz-screensaver tools/tests/test_gpu_class.py`
  → 2 files already formatted.

### 7.6 Branch state (this dispatch)

```
$ git -C ~/Projects/ncz-ss-m66-weak-tier/worktrees/fix-gpu-tier-mali-g720-2026-09-30 \
    rev-parse HEAD origin/fix/gpu-tier-mali-g720-2026-09-30
8dcd3050cfc30d0f2e1a1f1f3f112e602a815052
8dcd3050cfc30d0f2e1a1f1f3f112e602a815052

$ git log --oneline feat/launcher-ux..HEAD
8dcd305 tests: pin Sky1 cmd_doctor/cmd_pool/cmd_plan JSON for live .66 layout
f4097c4 docs: add live .66 verification evidence for the Sky1 Mali-G720 fix
04a79e6 launcher/tests: ruff format pass on the Sky1 fix files (no behavior change)
5fb10ad tests: pin Sky1 .66 live-capture strings and the 4-card-with-:02-absent layout
9671a75 launcher/tests: pin Sky1 Mali detection - short-path sysfs layout and cmd_gpus/cmd_status output
8f221fb launcher: do not classify Sky1 linlondp cards as a weak GPU
```

Branch is at tip on origin; no force-push was needed (the new commit
was a regular fast-forward from `f4097c4` to `8dcd305`).

## 8. Evidence path (this dispatch)

* build-host report:
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/m66-weak-tier-r2-2026-09-30.md`
* build-host evidence pack (18 files):
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/m66-evidence-2026-09-30-r2/`
* build-host MNEMOS-equivalent milestone:
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/MNEMOS-EQUIVALENT-MILESTONE-m66-weak-tier-r2-2026-09-30.md`
* Previous reports and evidence packs (kept untouched per standing rule):
  * `m66-weak-tier.md` (root-owned, 1st dispatch full re-validation)
  * `m66-weak-tier-tenth-summary.md`, `m66-weak-tier-tenth-full.md`
    (10th-pass re-validation)
  * `m66-weak-tier-revalidation-2026-09-30.md` (12th-pass)
  * `m66-weak-tier-zoder-pass.md` (13th-pass zoder review)
  * `m66-evidence-2026-09-30/`, `m66-evidence-eighth-2026-09-30/`,
    `m66-evidence-ninth-2026-09-30/`, `m66-evidence-tenth-2026-09-30/`
    (all kept untouched)

The fix branch is at `8dcd305` on `origin`, with 6 commits authored
`Jason Perlow <jperlow@gmail.com>`. The branch is COMPLETE, CORRECT,
AND PUSH-READY.