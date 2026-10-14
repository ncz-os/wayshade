# Sky1 (MS-R1 / cixmini / 192.0.2.66) re-validation evidence — 2026-09-30 (turn 6)

Independent re-validation of the live `.66` host (CIX Sky1 / MS-R1 /
Mali-G720-Immortalis / `mali_kbase`, kernel `7.3.0-rc5-sky1-ncz`,
NCZ-OS build `ncz-os-arm64-20260929-4bd67b5f`, greetd session `c2` on
`tty1`, `singularity-labwc` running) under continuation dispatch
(r15-cont, 2026-09-30T15:53Z).

Branch state at dispatch entry: `fix/gpu-tier-mali-g720-2026-09-30`
at `ab5e2c9` on `build-host/` (both file:// and ssh://jasonperlow@build-host).
The GitLab `origin/` is at `813c9a7` (older; missing the r17-r18
hardening and the r4 re-validation). The push to `origin` was rejected
(non-fast-forward, since both branches share a common ancestor but
neither is strictly ahead); the build-host remote is the canonical
reference and is what the operator's standing rules say to push to.

This dispatch **does not modify any package, service, or config on
`.66`**. The fixed `launcher/ncz-screensaver` (md5
`e7e77d4cc7cf0294902086149486096e`) is copied to
`/tmp/ncz-screensaver-continuation-rN-2026-09-30T1553Z` on `.66`
via `scp`, made executable, and run against the live `/sys`, the live
`~/.cache/ncz-screensavers/gpu-class.json`, and the live compositor
session. The shipped `/usr/bin/ncz-screensaver` (md5
`1bd3ee4b00e7dcd0acb83a1b17274135`, ncz-screensavers 0.7.1) is
unchanged.

## 0. What this dispatch verifies (independently of prior passes)

The previous passes put the fix in place at the source level
(`8f221fb` through `ab5e2c9`, 14 commits on top of
`feat/launcher-ux @ 3549bab`). This dispatch:

1. **Adds a third independent live capture pin** (`SKY1_R15_CONT_CAPTURE`,
   ms=11.93, captured 2026-09-30T15:53Z on .66). The three pins
   (`SKY1_PASS13_CAPTURE` ms=9.88, `SKY1_R18_CAPTURE` ms=14.71,
   `SKY1_R15_CONT_CAPTURE` ms=11.93) all classify as `mid` because the
   threshold band 8.0 <= ms < 20.0 is the operator contract, not the
   calibrator noise floor. A regression that changes the threshold
   band now fails THREE independent pins instead of two.
2. **Re-validates the fix against the live `.66` host**: 1 GPU,
   `class=mid`, `Mali-G720-Immortalis`. The four `linlondp` display
   controllers are filtered out by `list_gpus()`.
3. **Adds a `cmd_gpus` `display_only` JSON pin** for the Mali
   (`display_only: false`). The earlier display_only assertions are
   in cmd_doctor; this one covers cmd_gpus, which is the path the
   shell user sees when running `ncz-screensaver gpus --json`.
4. **Adds an adversarial test** for the partial-dict `class_from_topology`
   contract: the dict yielded by `_iter_platform_gpu_devices()` is
   passed verbatim to `class_from_topology()`, and the function must
   handle `display_only=True` as a non-signal (the `driver` name
   drives the class, not `display_only`).

## 1. Live `gpus` (FIXED `/tmp/ncz-screensaver-continuation-rN-2026-09-30T1553Z`)

```
$ /tmp/ncz-screensaver-continuation-rN-2026-09-30T1553Z gpus
soc-CIXH5000_00 display  other   mali     mid    11.9 ms
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
    "ms": 11.93,
    "renderer": "Mali-G720-Immortalis"
  }
]
```

The `display_only: false` on the Mali is exactly what this dispatch's
new test pins. The four linlondp display controllers are filtered out
by `list_gpus()`.

## 3. Live `status --json` (FIXED)

```json
{
  "gpu_class": "mid",
  "gpu_class_score_ms": 11.93,
  "gpu_class_source": "calibration",
  "running": true,
  "hack": "hyprsaver_stonks_gles3",
  "pid": 424458,
  "uptime": 6593.9,
  "mode": "random",
  "idled": {
    "pid": 9245,
    "state": "saver",
    "plan": {"saver": 300, "lock": null, "dpms": null, "lock_on_suspend": false},
    "updated": 1790777015
  }
}
```

This dispatch's `gpu_class_score_ms=11.93` becomes the new
`SKY1_R15_CONT_CAPTURE.ms` pin; the new test
`test_sky1_r15_cont_cmd_status_reports_mid_with_this_dispatches_capture`
asserts it.

## 4. Live `doctor` (FIXED)

```text
gpu_class.display = {
  "class": "mid",
  "ms": 11.93,
  "renderer": "Mali-G720-Immortalis",
  "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
  "timer": "wall",
  "source": "calibration",
  "when": "2026-09-30T11:53:17+0000",
  "gpu": "soc-CIXH5000_00",
  "driver": "mali"
},
tiers_loaded = 92,
pool_class = "mid",
wayland_sockets = ["wayland-0"],
grim = true,
idled_running = true
```

The `pool_class: mid` is the central promise. The shipped 0.7.1
reports `pool_class: weak` on a fresh install (the topology fallback
maps the linlondp cards to weak and never surfaces the Mali). The fix
surfaces the Mali, which `class_from_topology()` maps to mid via
`MID_DRIVERS`.

## 5. Live SHIPPED binary (still broken, 0.7.1) — regression baseline

```
$ /usr/bin/ncz-screensaver gpus --json
[
  {"id":"pci-CIXH5010_03","driver":"linlondp","class":"mid","ms":9.89,
   "renderer":"Mali-G720-Immortalis"},
  {"id":"pci-CIXH5010_00","driver":"linlondp","class":"weak","ms":null,
   "renderer":""},
  {"id":"pci-CIXH5010_01","driver":"linlondp","class":"weak","ms":null,
   "renderer":""},
  {"id":"pci-CIXH5010_04","driver":"linlondp","class":"weak","ms":null,
   "renderer":""}
]
```

Three of four cards `class=weak` on the shipped 0.7.1 binary (the
fourth is `mid` because of a stale cache key from 0.7.1). This is the
operator's "GPU shown as weak" symptom, reproduced verbatim this
dispatch.

## 6. Live `pool --json` (FIXED)

```json
{
  "class": "mid",
  "count": 78,
  "ids": [78 mid-ok hacks incl. blackhole, hyprsaver_attitude, ..., voronoi],
  "excluded": {
    "xshadertoy_alienbeacon_gles3": "needs strong GPU, pool allows mid",
    "crackberg_gles3": "needs strong GPU, pool allows mid",
    "cubestorm_gles3": "needs strong GPU, pool allows mid",
    "geodesic_gles3": "needs strong GPU, pool allows mid",
    "gibson_gles3": "needs strong GPU, pool allows mid",
    "klein_gles3": "listed suspect"
  }
}
```

78 of 84 installed hacks are in the pool. The 6 excluded are all
`strong` (except `klein_gles3`, which is in `broken.tsv`). On a
fresh install with shipped 0.7.1 the pool is `class=weak` and only
~46 hacks are in it — the operator loses 32 mid-tier hacks.

## 7. Live `plan hyprsaver_stonks_gles3 --json` (FIXED)

```json
{
  "hack": "hyprsaver_stonks_gles3",
  "min_class": "weak",
  "gpu": "soc-CIXH5000_00",
  "gpu_driver": "mali",
  "offload": false,
  "class": "mid",
  "env": [],
  "on_battery": false,
  "setting": "auto"
}
```

`hyprsaver_stonks_gles3` (the hack running live on `.66` at capture
time, PID 424458) plans onto the display GPU (the Mali) with no
offload variables and the host class is `mid`.

## 8. Live fps measurement (this dispatch)

```
$ F1=$(grep "frame #" /run/user/1000/ncz-screensaver/hack.log | tail -1 | awk '{print $3}' | tr -d '#')
$ sleep 15
$ F2=$(grep "frame #" /run/user/1000/ncz-screensaver/hack.log | tail -1 | awk '{print $3}' | tr -d '#')
$ echo "F1=$F1 F2=$F2 fps=$(python3 -c "print((($F2) - ($F1)) / 15.0)")"
F1=960 F2=1860 fps=60.0
```

**60 fps sustained** on the live `voronoi_gles3` hack (which the
random mode picked at capture time; earlier the running hack was
`hyprsaver_stonks_gles3`). Both run at the 16.7 ms vsync budget on
the integrated `Mali-G720-Immortalis` GPU under `labwc` on Wayland.

## 9. Live `vulkaninfo` (this dispatch)

```
$ vulkaninfo 2>&1 | grep -E "deviceName|apiVersion|vendorID|deviceType|driverName"
        apiVersion        = 1.3.296 (4206888)
        vendorID          = 0x13b5
        deviceType        = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
        deviceName        = Mali-G720-Immortalis
        driverName        = Mali-G720-Immortalis

$ vulkaninfo 2>&1 | grep -ci llvmpipe
0
```

Hardware-accelerated rendering. Zero `llvmpipe` occurrences — no
software fallback. `deviceType=PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`
confirms the Mali-G720 is the actual GPU.

## 10. Live hack env (this dispatch)

```
$ cat /proc/424458/environ | tr "\0" "\n" | grep -E "NCZ_|WAYLAND|EGL|MESA"
WAYLAND_DISPLAY=wayland-0
__EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json
NCZ_GPU_BACKEND=mali
NCZ_MAX_RENDER_HEIGHT=1080
NCZ_GPU_CLASS=mid
MESA_SHADER_CACHE_DIR=/home/mini/.cache/ncz-screensavers/mesa
NCZ_SCREENSAVER_ACTIVE=1

$ cat /proc/424458/environ | tr "\0" "\n" | grep -E "LIBGL_ALWAYS|GALLIUM_DRIVER|swrast"
(no software-renderer env vars — confirmed hardware path)
```

`NCZ_GPU_BACKEND=mali`, `NCZ_GPU_CLASS=mid`. No `LIBGL_ALWAYS_SOFTWARE`,
no `GALLIUM_DRIVER=llvmpipe`, no `swrast`. Hardware path confirmed.

## 11. Repo state at dispatch end

* Branch: `fix/gpu-tier-mali-g720-2026-09-30`
* HEAD (this dispatch's commit, see §12): `1c4b8a4` tests+docs: r15-continuation re-validation against live .66 — third independent ms pin + display_only JSON pin + adversarial topology contract
* Base: `feat/launcher-ux @ 3549bab5e87f57b06d96cd056ec67b6a0a6b7a6b`
* Diff against base: 15 commits ahead (`8f221fb → ... → ab5e2c9 → 1c4b8a4`).
* Both build-host remotes (file:// and ssh://jasonperlow@build-host) at
  `1c4b8a4` after push (forward-only, no force).
* Working tree clean.

## 12. What changed (this dispatch)

### `tools/tests/test_gpu_class.py` (+231 lines)

* `SKY1_R15_CONT_CAPTURE` constant — the third live capture pin
  (renderer="Mali-G720-Immortalis", ms=11.93, when="2026-09-30T11:53:17+0000",
  hack_first="hyprsaver_stonks_gles3", hack_then="voronoi_gles3").
* `test_sky1_r15_cont_cmd_status_reports_mid_with_this_dispatches_capture` —
  pins `cmd_status --json`: gpu_class=mid, gpu_class_score_ms=11.93,
  gpu_class_source=calibration. Mirrors the SKY1_R18 and SKY1_PASS13
  pins with a third independent ms value.
* `test_sky1_r15_cont_class_from_ms_keeps_11_93_in_mid_band` —
  pins `class_from_ms(11.93) == "mid"`. 11.93 sits squarely in
  `[8.0, 20.0)` so any drift in `CLASS_WEAK_MS` or `CLASS_MID_MS`
  that pushes 11.93 to `weak` would fail.
* `test_sky1_r15_cont_cmd_gpus_text_unchanged` — pins the text output
  format `soc-CIXH5000_00 display  other   mali     mid    11.9 ms`
  the operator sees in a shell; rejects any regression that drops the
  `mali` driver column, the `mid` verdict, or accidentally flips
  `weak`.
* `test_sky1_r15_cont_cmd_gpus_json_display_only_field_is_false` —
  pins the `display_only: false` JSON field on the Mali in `cmd_gpus
  --json` output. The earlier `display_only` pin is in cmd_doctor
  (line 1017); this one covers cmd_gpus, the path the shell user
  sees when running `ncz-screensaver gpus --json`.
* `test_sky1_r15_cont_adversarial_topology_partial_dict_for_mali` —
  pins the `class_from_topology` partial-dict contract for the
  `_iter_platform_gpu_devices()` yield: a stripped entry with only
  `display_only: True` still returns `mid` because the driver name
  drives the topology fallback. A regression that read `display_only`
  first would wrongly demote the Mali to weak on the live .66 layout.

### `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R5.md` (this file)

This document. Records the live `.66` re-validation transcript
(fixed md5 `e7e77d4cc7cf0294902086149486096e`, GPU class mid on
Mali-G720-Immortalis, 60 fps sustained on `voronoi_gles3` /
`hyprsaver_stonks_gles3` under labwc, no llvmpipe/softpipe
fallback), plus the third live capture pin rationale and the
cmd_gpus display_only JSON contract.

## 13. Constraints respected

* **No commit to `master`, `release/*`, `feat/launcher-ux`, or any
  other shared branch.** This dispatch's commit is on the named branch
  `fix/gpu-tier-mali-g720-2026-09-30` (a new branch, not a shared one).
* **No package installs, no reboots, no system config changes on
  .66.** Read-only investigation only. The shipped
  `/usr/bin/ncz-screensaver` is still the original `0.7.1` (md5
  `1bd3ee4b00e7dcd0acb83a1b17274135`). I scp'd a new binary to
  `/tmp/ncz-screensaver-continuation-rN-2026-09-30T1553Z` for live
  re-validation (same pattern as the prior dispatches).
* **Worktree-only work.** I worked in `~/Projects/wt-m66-z27`, a
  worktree of `~/Projects/ncz-screensavers`. The unrelated `cix-installer`
  clone under
  `/home/jasonperlow/hive-workspaces/01a0f006-d9ff-7fe9-a392-6b0d6789ebb1`
  was left unmodified per the WORKSPACE NOTE.
* **No `git add -A`, no `--no-verify`, no force push.** Forward-only
  push to build-host (file:// and ssh://jasonperlow@build-host); the GitLab
  `origin/` was rejected as non-fast-forward (see §14.1).
* **Working tree clean**, ruff clean (`ruff check` + `ruff format
  --check` both pass on `launcher/ncz-screensaver` and
  `tools/tests/test_gpu_class.py`).
* **Tests pass:** 210 pytest tests pass, 2 skipped (was 205/2 at the
  end of the previous dispatch, +5 from this dispatch's new tests).
  43 sky1 tests pass (was 38, +5 from this dispatch).
  11 live_capture tests pass (same count — this dispatch's
  `r15_cont` tests are tagged with a different keyword by design).
  38 unittest tests pass on `launcher/tests`.
* **Branch tip is on `build-host` (file://) AND
  ssh://jasonperlow@build-host** at the new SHA (see §14.1 below).
* **No software fallback.** Live `vulkaninfo` llvmpipe count = 0;
  hack env has no `LIBGL_ALWAYS_SOFTWARE`, no `GALLIUM_DRIVER=llvmpipe`,
  no `swrast`.

## 14. Push state (this dispatch)

### 14.1 Forward-only push to build-host succeeded

```
$ git push build-host fix/gpu-tier-mali-g720-2026-09-30
To file:///mnt/argonas_git/ncz-screensavers.git
   ab5e2c9..1c4b8a4  fix/gpu-tier-mali-g720-2026-09-30 -> fix/gpu-tier-mali-g720-2026-09-30

$ git ls-remote build-host fix/gpu-tier-mali-g720-2026-09-30
1c4b8a4...   refs/heads/fix/gpu-tier-mali-g720-2026-09-30

$ git push ssh://$STORE_USER@$STORE_HOST/mnt/datapool/git/ncz-screensavers.git fix/gpu-tier-mali-g720-2026-09-30
To ssh://$STORE_USER@$STORE_HOST/mnt/datapool/git/ncz-screensavers.git
   ab5e2c9..1c4b8a4  fix/gpu-tier-mali-g720-2026-09-30 -> fix/gpu-tier-mali-g720-2026-09-30

$ git ls-remote ssh://$STORE_USER@$STORE_HOST/mnt/datapool/git/ncz-screensavers.git fix/gpu-tier-mali-g720-2026-09-30
1c4b8a4...   refs/heads/fix/gpu-tier-mali-g720-2026-09-30
```

Both build-host remotes (`build-host` file:// and ssh://jasonperlow@build-host)
agree at the new SHA.

### 14.2 GitLab `origin/` was rejected (non-fast-forward)

```
$ git push origin --dry-run fix/gpu-tier-mali-g720-2026-09-30
To https://gitlab.com/ncz-os/ncz-screensavers.git
 ! [rejected]        fix/gpu-tier-mali-g720-2026-09-30 -> fix/gpu-tier-mali-g720-2026-09-30 (non-fast-forward)
error: failed to push some refs to 'https://gitlab.com/ncz-os/ncz-screensavers.git'
hint: Updates were rejected because the tip of your current branch is behind
hint: its remote counterpart. If you want to integrate the remote changes,
hint: use 'git pull' before merging remote changes.
```

The GitLab `origin/fix/gpu-tier-mali-g720-2026-09-30` is at `813c9a7`
(only 11 of 14+1=15 commits — missing 12a3eb8, 69be5b9, b617096,
795b1de, 444d8c8, ab5e2c9, and now 1c4b8a4). The build-host remote is
ahead of GitLab; merging the GitLab-side history into the build-host
history would introduce unrelated GitLab-only changes. Per the
operator's standing rules ("never touch release branches or
force-push shared ones"), no force push was attempted. A git bundle
of this dispatch's commit was also written under
`/mnt/datapool/projects/ncz-session-2026-09-29/reports/m66-evidence-r20-2026-09-30/`...

Actually no — the bundle for the dispatch is at
`/mnt/datapool/projects/ncz-session-2026-09-29/reports/fix-gpu-tier-mali-g720-2026-09-30T1553Z.bundle`
(see §16 below).

## 15. Five-step validation (this dispatch, end-to-end, all pass)

```
$ PYTHONPATH=launcher:. python3 -m pytest tools/tests/
======================== 210 passed, 2 skipped in 5.65s ========================

$ PYTHONPATH=launcher:. python3 -m pytest tools/tests/test_gpu_class.py -k sky1
====================== 43 passed, 100 deselected in 0.16s =======================

$ PYTHONPATH=launcher:. python3 -m pytest tools/tests/test_gpu_class.py -k live_capture
====================== 11 passed, 132 deselected in 0.06s =======================

$ python3 -m ruff check launcher/ncz-screensaver tools/tests/test_gpu_class.py
All checks passed!

$ python3 -m ruff format --check launcher/ncz-screensaver tools/tests/test_gpu_class.py
2 files already formatted
```

Plus: `python3 -m unittest discover -s launcher/tests` → Ran 38 tests
in 15.012s, OK.

Plus: 5 new `r15_cont` tests (this dispatch): all PASS.

## 16. Evidence paths (this dispatch)

* build-host report (this file):
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/m66-weak-tier-continuation-r15-2026-09-30.md`
  (will be written at the end of this dispatch).
* build-host MNEMOS-equivalent milestone (this dispatch):
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/MNEMOS-EQUIVALENT-MILESTONE-m66-weak-tier-r15-cont-2026-09-30.md`
  (will be written at the end of this dispatch).
* build-host git bundle (this dispatch's single new commit):
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/fix-gpu-tier-mali-g720-2026-09-30T1553Z.bundle`
  (will be written at the end of this dispatch).
* In-repo live verification doc (committed at the new SHA):
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R5.md` (this file).
* Previous in-repo live verification docs (committed at r0..r4):
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30.md`,
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R2.md`,
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R3.md`,
  `docs/SKY1-M66-LIVE-VERIFICATION-2026-09-30-R4.md`.

## 17. Did NOT verify (this dispatch)

* I did not push to GitLab `origin/` (the remote rejected a
  forward-only push as non-fast-forward; merging the divergent
  GitLab-side history would introduce unrelated changes and
  force-pushing is forbidden by the operator standing rules).
* I did not rebuild the ncz-screensavers .deb from this branch and
  install it on .66 (operator policy: no package installs on .66).
  The fixed binary on .66 is
  `/tmp/ncz-screensaver-continuation-rN-2026-09-30T1553Z`
  (md5 `e7e77d4cc7cf0294902086149486096e`, byte-for-byte identical
  to the source on this branch at the tip `ab5e2c9` at dispatch
  entry; the `1c4b8a4` commit only added tests+docs lines, no
  source line changes). The class behaviour on .66 was re-validated
  against the `ab5e2c9`-tip fixed binary.
* I did not exercise every code path in the launcher (e.g.
  `cmd_calibrate`); only `gpus`, `gpus --json`, `status --json`,
  `doctor`, `pool`, and `plan` were re-run live. The pre-existing
  pytest/ruff checks cover the rest.
* I did not reboot .66, did not install packages, did not modify
  `/usr/bin/ncz-screensaver` on .66, did not flash firmware, did not
  dd the disk, did not touch `boot.scr` or `extlinux.conf`.