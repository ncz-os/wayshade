# New screensaver tests (2026-09-29)

This is a visible-evidence test pass over every saver that the
`feat/new-screensavers-2026-09-29` branch builds but does not ship. For each saver it records:

- screenshots at t=5/20/45 s
- a 13 s screen recording
- frame-time statistics at three render scales
- behavior runs with an exit-on-input check
- a verdict

Tester: LEAD 21. LEAD 19 owns the saver code, and every defect below went to LEAD 19 with its evidence path.

## Where to look

- **Contact sheets:** `docs/new-saver-tests/<saver>.jpg`, one per saver. Each row is one host and build, showing t=5/20/45 s with a caption for renderer, fps/p95/p99 and gate.
- **Overview:** `docs/new-saver-tests/OVERVIEW-o6n-mali-t20.jpg` shows every saver on Mali at t=20 s on one page.
- **Ephemeris time-lapse:** `docs/new-saver-tests/ephemeris-seed11-600s-timelapse.jpg` covers 10 minutes with seed 11, one frame every 20 s.
- **Full evidence** (full-size PNGs, `clip.mp4` recordings, saver logs, per-run `result.json`, frame dumps):
  build-host `/mnt/datapool/projects/ncz-session-2026-09-29/new-saver-tests/<saver>/<host>/`

## Scope

The list comes from `meson.build` and `docs/round20/`: every saver that builds but is not in `ncz_ship_bins`.

- **Original pieces:** `genxvectorcade`, `neonspacewar`, `magmasimplex` (goo engine), `ephemeris`, `leviathan`.
- **xshadertoy round 20 and vendored extras:** `physarum`, `attractor`, `caustics`, `aurora`, `marble`, `wellcurve`, `caustictidepool`, `hexlattice`, `neongravity-0` (cut earlier for a hang).
- **Wave 2 materia:** `aurora-restrained`, `caustics-on-stone`, `foliage-plaster`, `frosted-glass`, `satin-wind`, `thin-film`.
- **Also captured, for completeness:** `jurassicaquarium_skeleton`.

## Method

**Builds.** Built in a `debian:forky` container (NCZ-OS is forky, glibc 2.43) on build-host (amd64) and an arm64 build host with `-Dinstall-extras=true`. The target binaries were copied from the build tree. The branch head moved during testing: `09b56be`, then `f97e11e`, `d126fc6` and `ace1c81`. Every table row names the build it used.

**Runner.** `tools/new-saver-test.py` runs on the host, inside the user's Wayland session. The saver environment comes from the branch launcher's `build_child_env()` with Auto settings. Offload and render scale are set per process only; nothing is ambient. The three render-scale configurations:

- `default`: the launcher's Auto settings.
- `native`: render scale 1.0 fixed, with no height cap. This is the logical surface size, not the physical panel: 1536x960 on the 2x MacBook panels, 1920x1080 on build-host at 2x, 2194x1234 on MS-R1 at 1.75.
- `weak`: render scale 0.5 fixed.

Each perf window is 5 s of warm-up and then 15 s of measurement. fps comes from the `[diag]` frame stamps; p95, p99 and max come from the harness `[stats]` steady window.

**Test conditions.** Before every run I stopped `ncz-screensaver-idled` and checked with pgrep that nothing else was fullscreen. The daemon was restored afterwards. Hosts were locked with `tools/host-lock.sh`.

**Gate.** The gate checks coverage, motion between frames 1 s apart, and difference from the desktop baseline, then runs the `host_test_image.visual_verdict` heuristics. On deliberately matte, single-ground pieces (ephemeris, thin-film), flat_fill and one_color are expected results; judge those by eye.

**Class rule.** From `tools/measure-tiers.py`: a run counts as ok at 30 fps or more with p95 at or under 40 ms.

**Software rendering.** A software renderer would invalidate a run. None occurred. Every run reported Mali-G720-Immortalis, AMD Radeon (radeonsi navi14) or NVIDIA RTX 2060.

## Verdicts

| saver | verdict | recommended tier | why |
|---|---|---|---|
| caustics-on-stone | READY | weak | 60 fps, p95 16.7 on Mali at every scale. Sunlit caustic veins on pale stone. Key exit in 0.18 s |
| satin-wind | READY | weak | 60/16.7 on Mali at every scale. Slow satin drapes. Clean exit |
| hexlattice | READY | weak | 60/16.7 on Mali. Clean glowing lattice. Clean exit |
| caustics | READY | weak | 60/16.7 on Mali. Shards on dark water, with a mild fisheye vignette |
| marble | READY | mid | Mali 37 fps, p95 27 at 1080p; 60 at 0.5 |
| physarum | READY at 0.5 | mid | The best-looking piece of round 20. Mali 20 fps, p95 65 at 1080p; 60 at 0.5; 45/22.8 at the MS-R1 cap |
| magmasimplex (goo) | pre-redesign | weak (perf) | 60/16.7-16.9 on Mali, Navi14 and RTX. Reads well as a lava lamp. LEAD 25 is redoing it, so this is not a ship call |
| ephemeris (dramatic) | NEEDS WORK (small) | weak | 60 fps everywhere, including 59.1 over 600 s on the Mali 4K. No glyphs, no glow, no network, nothing persists. Problems: the opening frame is near-blank, the lift misses the densest knot, and the drawing turns heavy and dark after ~400 s |
| neonspacewar | pre-redesign (v2 Melee coming) | weak (perf) | 60/16.7 everywhere. Over 210 s the AI plays, waves advance and kills climb. Key exit in 0.2 s. The operator called it "blurry Asteroids"; v2 is in progress |
| genxvectorcade | pre-redesign | weak (perf) | Two washouts are fixed and verified: the Mali render-target feedback loop (b3c7b1f) and the seed washout (ed58c2c). The operator did not like the piece, and LEAD 19 is redirecting it |
| leviathan | pre-redesign | TBD | A sin()-of-large-argument slow path held it to 0.2-17 fps on Mali and 6.5 fps on RTX 2060, against 30 on Navi14. With the integer hash from ed58c2c it runs 60/16.7 on Mali for movement A. LEAD 24 owns the redesign and measurement |
| foliage-plaster | NEEDS WORK | mid | Nearly blank beige (contrast). Mali 17.5 fps, p95 66 at 1080p; 58 at 0.5 |
| frosted-glass | NEEDS WORK | weak (perf) | Stripes that barely move: motion 0.000 at 1 s, no visible change in 12 s |
| thin-film | NEEDS WORK | weak (perf) | Near-uniform pale gray that reads as a blank screen |
| aurora-restrained | NEEDS WORK | weak (perf) | Two flat teal bands with polygonal notch seams along the band edges |
| wellcurve | NEEDS WORK | weak (perf) | Faint polar grid on black that reads as a calibration pattern |
| attractor | NEEDS WORK | strong | Dim and murky, with no filaments. Mali 6 fps, p95 176; 22 at 0.5 |
| aurora (xshadertoy) | NEEDS WORK | strong | A purple smudge, not an aurora. Mali 7 fps, p95 145; 28 at 0.5 |
| caustictidepool | BROKEN | - | Static teal noise with zero motion and no caustics. Likely a missing iChannel/iMouse input |
| neongravity-0 | BROKEN | - | All-black frame on Mali (nonblack=0 from frame 4). No hang; exits 0 on SIGTERM. Stays cut |
| jurassicaquarium_skeleton | not content | - | A skeleton build by design, with a hard gray band at the top. 60 fps on Mali |

This pass has not yet measured the weak class (UHD 630); see [Open items](#open-items). LEAD 19's own runs on build-host and build-host put ephemeris and magmasimplex goo at 60 fps (p95 16.8-17.1) on UHD 630, both native and at 0.5.

## Defects found and their status

1. **genxvectorcade on Mali (O6N), 09b56be: whole-frame washout plus a blocky yellow/cyan tile pattern.** Root cause (LEAD 19): the scene pass sampled the trail texture while it was the render target, and the trail textures were never cleared. FIXED in b3c7b1f; verified on O6N with NCZ_GVC_FIXED_SEED=2361279744.
2. **genxvectorcade, seed 2724644096, Navi14, d126fc6: near-white washout for the first 0-30 s.** Glow and pulse were fed back through the trail, pushing the loop gain above 1. FIXED in ed58c2c; verified on MS-R1 (gate PASS, 60 fps).
3. **leviathan: 0.2-17 fps on Mali and 6.5 fps on RTX 2060, against 30 fps on Navi14.** Cause: sin() of large arguments in hash3. With the integer hash from ed58c2c it runs 60 fps on Mali in movement A; the heavy movements are not re-measured yet (LEAD 24). It also ignored SIGTERM for more than 3 s while a 4.8 s frame was in flight.
4. **Candidate defects in the unshipped round 20 and wave 2 pieces.** LEAD 19 logged these for operator triage:
   - neongravity-0 renders all black on Mali.
   - caustictidepool is static.
   - thin-film reads as blank.
   - frosted-glass is static.
   - aurora-restrained shows seams.
   - attractor and aurora are heavy and murky.
   - foliage-plaster reads as blank.
   - wellcurve looks like a test pattern.
5. **ephemeris ace1c81, seed 11: three issues, sent to LEAD 19.**
   - The opening frame is near-blank.
   - The lift aimed at the lower-right arcs instead of the densest knot at upper left.
   - At t=540 a heavy hook reads as "P/9" at thumbnail size.
6. **Harness/launcher, not the savers:** the mouse cursor stays visible over saver surfaces in every capture.

## Measurements (Auto / native 1.0 / weak 0.5)

Each cell is fps / p95 ms / p99 ms. "ok" means the run meets the class rule.

| saver | host | renderer | GPU plan | Auto fps/p95/p99 | native 1.0 | weak 0.5 | gate | exit |
|---|---|---|---|---|---|---|---|---|
| ephemeris | build-host d126fc6 | AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64, 7.2.8-3-t2-trixie) | strong | 60 / 16.78 / 17.12 ok | 60 / 16.77 / 16.85 ok | 60 / 16.77 / 16.96 ok | FAIL flat_fill | 0 |
| ephemeris | MS-R1 ace1c81 | Mali-G720-Immortalis | weak | 60 / 16.7 / 16.74 ok | 60 / 16.72 / 16.96 ok | 60 / 16.72 / 16.76 ok | PASS | 0 |
| ephemeris | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.74 / 17.08 ok | 60 / 16.72 / 16.76 ok | 60 / 16.7 / 16.73 ok | FAIL flat_fill,one_color | 0 |
| ephemeris | build-host 09b56be | NVIDIA GeForce RTX 2060/PCIe/SSE2 | strong offload | 60 / 16.86 / 16.99 ok | - | - | FAIL flat_fill,one_color | 0 |
| ephemeris | build-host f97e11e | NVIDIA GeForce RTX 2060/PCIe/SSE2 | strong offload | 62.5 / 16.86 / 16.99 ok | - | - | FAIL flat_fill,one_color | 0 |
| genxvectorcade | build-host d126fc6 | AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64, 7.2.8-3-t2-trixie) | strong | 60 / 16.77 / 16.96 ok | 60 / 16.77 / 17.03 ok | 60 / 16.78 / 16.86 ok | PASS | 0 |
| genxvectorcade | MS-R1 ace1c81 | Mali-G720-Immortalis | weak | 60 / 16.7 / 16.78 ok | 60 / 16.7 / 16.8 ok | 60 / 16.71 / 16.74 ok | PASS | 0 |
| genxvectorcade | MS-R1 ace1c81@seed2724644096 | Mali-G720-Immortalis | weak | 60 / 16.7 / 16.77 ok | - | - | PASS | 0 |
| genxvectorcade | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.7 / 16.74 ok | 60 / 16.71 / 16.84 ok | 60 / 16.71 / 16.77 ok | PASS | 0 |
| genxvectorcade | build-host 09b56be | NVIDIA GeForce RTX 2060/PCIe/SSE2 | strong offload | 60 / 16.8 / 16.92 ok | - | - | PASS | 0 |
| magmasimplex | build-host d126fc6 | AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64, 7.2.8-3-t2-trixie) | strong | 60 / 16.79 / 16.99 ok | 60 / 16.76 / 16.84 ok | 60 / 16.79 / 16.99 ok | PASS | 0 |
| magmasimplex | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.72 / 16.78 ok | 60 / 16.72 / 16.79 ok | 60 / 16.72 / 16.81 ok | PASS | 0 |
| magmasimplex | build-host 09b56be | NVIDIA GeForce RTX 2060/PCIe/SSE2 | strong offload | 60 / 16.84 / 17.0 ok | - | - | FAIL flat_fill,one_color,tiny_motion | 0 |
| magmasimplex | build-host f97e11e | NVIDIA GeForce RTX 2060/PCIe/SSE2 | strong offload | 62.41 / 16.87 / 16.96 ok | - | - | PASS | 0 |
| neonspacewar | build-host d126fc6 | AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64, 7.2.8-3-t2-trixie) | strong | 60 / 16.77 / 16.83 ok | 60 / 16.76 / 16.98 ok | 60 / 16.8 / 16.92 ok | PASS | 0 |
| neonspacewar | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.71 / 16.75 ok | 60 / 16.7 / 16.71 ok | 60 / 16.69 / 16.7 ok | PASS | 0 |
| neonspacewar | build-host 09b56be | NVIDIA GeForce RTX 2060/PCIe/SSE2 | strong offload | 60 / 16.81 / 16.93 ok | - | - | PASS | 0 |
| attractor | O6N d126fc6 | Mali-G720-Immortalis | mid | 6 / 176.07 / 176.68 | 6 / 176.12 / 176.26 | 22 / 45.64 / 45.71 | PASS | 0 |
| aurora-restrained | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.73 / 16.79 ok | 60 / 16.71 / 16.87 ok | 60 / 16.71 / 16.82 ok | PASS | 0 |
| aurora | O6N d126fc6 | Mali-G720-Immortalis | mid | 7 / 144.7 / 145.2 | 7 / 145.42 / 145.78 | 27.7 / 37.9 / 38.55 | PASS | 0 |
| caustics-on-stone | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.7 / 16.75 ok | 60 / 16.7 / 16.8 ok | 60 / 16.72 / 16.79 ok | PASS | 0 |
| caustics | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.71 / 16.98 ok | 60 / 16.71 / 16.88 ok | 60 / 16.72 / 16.76 ok | PASS | 0 |
| caustictidepool | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.73 / 17.16 ok | 60 / 16.72 / 16.78 ok | 60 / 16.71 / 16.77 ok | FAIL (motion 0.0, cov 1.0) | 0 |
| foliage-plaster | O6N d126fc6 | Mali-G720-Immortalis | mid | 17.5 / 66.5 / 66.58 | 15.1 / 66.53 / 66.59 | 57.9 / 17.83 / 17.98 ok | PASS | 0 |
| frosted-glass | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.74 / 16.89 ok | 60 / 16.75 / 17.13 ok | 60 / 16.72 / 16.77 ok | FAIL (motion 0.0, cov 1.0) | 0 |
| hexlattice | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.75 / 16.82 ok | 60 / 16.72 / 16.81 ok | 60 / 16.69 / 16.7 ok | PASS | 0 |
| jurassicaquarium_skeleton | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.7 / 16.71 ok | 60 / 16.72 / 16.83 ok | 60 / 16.71 / 16.94 ok | PASS | 0 |
| leviathan | build-host d126fc6 | AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64, 7.2.8-3-t2-trixie) | strong | 30.3 / 33.46 / 33.66 ok | 30.8 / 33.42 / 33.49 ok | 60 / 16.77 / 17.09 ok | PASS | 0 |
| leviathan | MS-R1 ace1c81 | Mali-G720-Immortalis | weak | 60 / 16.72 / 16.9 ok | 60 / 16.71 / 16.81 ok | 60 / 16.72 / 16.82 ok | PASS | 0 |
| leviathan | O6N d126fc6 | Mali-G720-Immortalis | mid | 16.9 / 68.92 / 68.96 | 14.5 / 69.02 / 69.13 | 55.9 / 17.99 / 18.06 ok | PASS | 0 |
| leviathan | build-host 09b56be | NVIDIA GeForce RTX 2060/PCIe/SSE2 | strong offload | 6.5 / 175.32 / 178.72 | - | - | PASS | 0 |
| marble | O6N d126fc6 | Mali-G720-Immortalis | mid | 36.8 / 27.36 / 27.45 ok | 36.8 / 27.33 / 27.4 ok | 60 / 16.72 / 16.88 ok | PASS | 0 |
| neongravity-0 | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.7 / 16.72 ok | 60 / 16.71 / 16.77 ok | 60 / 16.71 / 16.73 ok | FAIL (motion 0.0, cov 0.0) | 0 |
| physarum | O6N d126fc6 | Mali-G720-Immortalis | mid | 19.8 / 64.53 / 64.74 | 15.7 / 63.84 / 64.04 | 60 / 16.72 / 17.23 ok | PASS | 0 |
| satin-wind | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.71 / 16.81 ok | 60 / 16.7 / 16.75 ok | 60 / 16.7 / 16.73 ok | PASS | 0 |
| thin-film | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.69 / 16.71 ok | 60 / 16.7 / 16.74 ok | 60 / 16.72 / 16.78 ok | FAIL flat_fill,one_color,tiny_motion | 0 |
| wellcurve | O6N d126fc6 | Mali-G720-Immortalis | mid | 60 / 16.72 / 16.75 ok | 60 / 16.72 / 16.79 ok | 60 / 16.71 / 16.74 ok | PASS | 0 |

## Behavior runs (exit on input, persistence)

The key press is injected through `zwp_virtual_keyboard_v1` with `tools/wl_poke.py key --code 57`. "Files written" lists files under `$HOME` and `/tmp` that are newer than the run start. Session noise is excluded (labwc and desktop logs, `dconf/user`, the host-lock file); those files change even when no saver runs.

| saver | host | seconds | fps | p95 ms | max ms | alive throughout | key exit (s) | exit | files written by the saver |
|---|---|---|---|---|---|---|---|---|---|
| neonspacewar | MS-R1 | 210 | 59.91 | 16.71 | 80.55 | True | 0.2 | 0 | none |
| genxvectorcade | MS-R1 | 210 | 59.91 | 16.71 | 76.46 | True | 0.19 | 0 | ~/.local/share/ncz-screensavers/genxvectorcade.lineage |
| magmasimplex | MS-R1 | 210 | 59.9 | 16.71 | 70.43 | True | 0.19 | 0 | none |
| caustics-on-stone | MS-R1 | 90 | 59.89 | 16.71 | 70.32 | True | 0.18 | 0 | none |
| satin-wind | MS-R1 | 90 | 59.87 | 16.72 | 71.34 | True | 0.2 | 0 | none |
| hexlattice | MS-R1 | 90 | 59.9 | 16.71 | 70.14 | True | 0.19 | 0 | none |
| caustics | MS-R1 | 90 | 59.86 | 16.71 | 71.09 | True | 0.19 | 0 | none |
| marble | MS-R1 | 90 | 59.89 | 16.71 | 69.12 | True | 0.13 | 0 | none |
| physarum | MS-R1 | 90 | 45.43 | 22.8 | 79.72 | True | 0.2 | 0 | none |
| ephemeris@seed11-600s | MS-R1 | 600 | 59.08 | 16.71 | 386.1 | True | 0.14 | 0 | none |

Notes:

- The ~70-80 ms max frame in these runs lines up with the grim screenshot taken every 15 s, not with the saver.
- genxvectorcade's `.lineage` file is its intended cross-run lineage state.
- **Ephemeris spec contract, all verified:**
  - No text or glyphs: checked by eye across 30 frames.
  - No network: the binary imports no socket, connect or getaddrinfo.
  - Matte pigment, no glow or bloom.
  - Nothing persists across runs: the only mkdir in the binary is the harness frame-dump path, used only with NCZ_FRAME_DUMP.
  - The numeric record goes to stderr only (`[ephemeris] run_seconds=... lift ... drought ...`).
- **Ephemeris, seed 11, 600 s:**
  - The drought (201.5-250.7 s) is visible.
  - The lift at t=225.7 s reads as a gray smear over the lower-right arcs.
  - No closed letter-size rings.
  - Frame 28800 (t=480 s) is not frozen.
- **neonspacewar AI over 210 s on the 4K MS-R1:** waves 3 through 9, 178 kills, about 5 deaths per minute. The longest lull was 12 s spent chasing the last rock.

## Open items

- **build-host pass, UHD 630 (offload off, weak class) and RTX 2060 (offload on), every saver at d126fc6 or later.** It was queued but never ran. An orphaned remote `host-lock.sh acquire --wait` left over from a cancelled driver took the lock with nothing behind it. The locks have been released; see the harness caveat below.
- **build-host Navi14 for the xshadertoy/wave2 set.** The run was stopped when build-host and build-host were reserved for the 7.3 kernel boot tests. The headline pieces and physarum were measured before that.
- **neonspacewar v2 (Melee):** test on Mali, on UHD 630 at 0.5, and on the 4K MS-R1 once LEAD 19 lands it. The runner's 30-minute soak (`--phases soak`, telemetry every 60 s) is ready.

## Tools

- `tools/new-saver-test.py`: the runner. Phases: capture, default, native, weak, long, soak. Options: `--env`, `--args=`, `--offload`, `--tag`.
- `tools/new-saver-sheet.py`: builds the contact sheets.
- `tools/new-saver-report.py`: builds the measurement table.

**Harness caveat.** Cancelling a driver that is waiting in `host-lock.sh acquire --wait` over ssh does not stop the remote waiter. Kill the remote acquire too, or it takes the lock later with nothing behind it.
