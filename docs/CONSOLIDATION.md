# Consolidation record (2026-09-29)

Branch `consolidated/ncz-os-mvp` (base: `master` 0db1ed1). Every stranded branch was
reviewed. "Taken" means its content is on the consolidated branch; "Skipped" means it
stays reachable on its original `origin/*` ref only.

| Branch | Head | Decision | Why |
|---|---|---|---|
| `bh-task1-task2` | a301b47 | Partly taken | Taken: `NCZ_BLACKHOLE_FIXED_SEED` env override (C driver only). Skipped: shader perf hacks (skip second fbm layer when `trans<.5`, early `trans<.04` break, gated `exp()` in the integrator). They lower image quality, and the shader was rewritten around it since (disk tilt, tier steps); blackhole is the visual bar for the set. |
| `recovered/build-host-gpu-tier-2026-09-29` (= `recovered/build-host-ncz-screensavers-...`) | b53596e | Taken (merged) | Shared GPU tier module (`ncz_gpu_tier.c`, `NCZ_GPU_TIER` override) wired into blackhole `max_steps`, plus the quality-ladder and audit docs. Already committed work; it is not the pending fidelity-tier design. |
| `recovered/build-host-ncz-screensavers-2026-09-29` | 3e4356e | Taken (merged) | `ridgeline.glsl` iGPU budget cut. Not in the ship set (calibration shader), harmless. |
| Recovered jwz reference branch (2026-09-29) | 42d5991 | Taken (merged) | jwz Apple API reference doc only. |
| Recovered presentation prototype branch (2026-09-29) | 0156158 | Skipped | Wayshade and adaptive presentation path (out of MVP scope). Its `iSeed` commit (5d512b0) competes with the `iSeed` already on master; master's version is kept and its duplicate declaration removed (see below). Float feedback buffers for xshadertoy are not needed by any ship-set shader. |
| `recovered/build-host-ncz-screensavers-root-2026-09-29`, `recovered/build-host-screensaver-smoke-2026-09-29` | 1cfcc9a / d80ee4f | Skipped | Wayshade automatic fidelity-tier design (pending operator approval) and a 10 s smoke copy. |
| `recovered/build-host-ncz-screensavers-2026-09-29` | 0db1ed1 | Nothing to take | Same as master. |
| Recovered screensaver branch (2026-09-29) | 584d2e2 | Nothing to take | Ancestor of master. |
| `wip/build-host-afterimage-2026-09-26` | b6da3d6 | Nothing to take | Already an ancestor of master (neonhorizon, stratus, iridescence clean-room ports). |
| Recovered screensaver and capture-report branches (2026-09-29) | 73b7079 | Skipped | 51 capture PNGs/reports (matte-plastic evidence on build-host). Evidence, not source. |
| Recovered lavafield branch (2026-09-29) | 11b4925 | Skipped | `lavafield` hack: not in the ratified ship set. Still on its ref if the operator wants it later. |
| NVIDIA linear-buffer proof-of-concept branches (2026-09-22) | 886d5c8 | Skipped | Proof of concept, result was negative (NVIDIA cannot import Intel-allocated LINEAR dma-buf); 2.8k lines of test tooling and logs. |
| `feat/launcher-ux` | (LEAD 2) | Separate | Launcher, idle daemon and settings live in `launcher/`; wired in through the optional `subdir('launcher')` hook in `meson.build`. |

## Changes made on the consolidated branch

- `xshadertoy`: `uniform vec4 iSeed;` was declared twice in the preamble (lines 199 and 212), which broke
  GLSL compile; the second declaration was deleted.
- `meson.build`: `ncz_ship_bins` is the single source of truth for the ship set. Only those executables
  get `install: true` (to `/usr/lib/ncz-screensavers`); only their shaders are installed to
  `/usr/share/ncz-screensavers/shaders`. Non-ship shaders install only with `-Dinstall-extras=true`.
  `wl-screenhack` now links libm.
- Ship set = 84 entries: Black Hole (1), hyprsaver (35), xshadertoy (36), classics (12). The older 85-entry
  catalog in cix-installer differed in two ways: it contained `neongravity-0` (cut by the operator, hangs)
  and the three shaders that were removed for licensing (`synthwavecity`, `driftclouds`, `bubblecolors`), whose
  clean-room replacements are `neonhorizon`, `stratus` and `iridescence`.
- `assets/screensaver-chooser/hacks.tsv` is generated from `ncz_ship_bins` by `tools/generate-catalog.py`
  (the Debian build re-runs the generator and fails on drift). It supersedes the copy in cix-installer.
- `config/dev.ncz.screensaver.gschema.xml`: the cix-installer schema plus `blackhole-color-mode`
  (`stylized`, `kipthorne`, `faithful`); the launcher exports the nick as `NCZ_BLACKHOLE_COLORS`.
- Black Hole: color modes restored (see `docs/blackhole/`), jet rewritten, star field re-hashed.
- `debian/`: package `ncz-screensavers` 0.2.0 for amd64 and arm64.

## Licensing (resolved 2026-10-05: Apache-2.0)

- The project licence is Apache-2.0. `LICENSE` is the full Apache License 2.0
  text and `meson.build` declares `license: 'Apache-2.0'`.
- `debian/copyright` is machine-readable DEP-5: `Files: *` is Apache-2.0 for
  the NCZ-authored code and each vendored component has its own stanza.
- Vendored and ported code keeps its own terms: Black Hole (MIT,
  Adriwin06/black-hole, `vendor/blackhole-LICENSE.txt`), hyprsaver shaders
  (MIT, `vendor/hyprsaver/LICENSE`), xshadertoy shaders (MIT, CC0 or CC BY 3.0
  per file header, plus original clean-room ports), and the 12 ported classics
  (xscreensaver permissive notice, jwz and other authors).
- There is no conflict: each screensaver is a separate program, and every
  upstream component in the shipped set is permissively licensed (MIT, CC0,
  CC-BY-3.0 or the MIT-style xscreensaver notice). The retired
  GPL-2.0-or-later declaration was the only copyleft term and is gone.

## Launcher requirement: SIGKILL fallback (for LEAD 2)

On O6N (Mali-G720, vendor EGL) while the session is locked, the compositor sends frame callbacks only to the lock surface. A hack's `eglSwapBuffers` then blocks inside the driver's `poll` and the process does not react to SIGTERM (observed: main thread in `poll_schedule_timeout`, no CPU use, `timeout -k2` needed SIGKILL, rc 137). The launcher must therefore stop a hack with SIGTERM, wait a short bounded time (about 1 second), then SIGKILL, and must never wait unboundedly on a hack exiting. It should also avoid starting a hack while the session is locked.
