# NVIDIA GLES3 full-matrix follow-up — build-host — 2026-09-25

This follow-up closes the previously untested NVIDIA vendor path identified in
[the full O6N/build-host matrix](FULL-MATRIX-2026-09-25.md). It records a complete
140/140 run on build-host using a real NVIDIA RTX 4500 Ada Generation and driver
595.58.03. The compositor and every client reported NVIDIA, not Mesa or llvmpipe.

## Outcome

The fail-closed harness result is **0 PASS / 140 FAIL**: 139 targets animated or
rendered long enough for both captures and then exited 139 during SIGTERM cleanup;
`splitflap_gles3` retained its distinct exit 134 behavior. Before shutdown, the
two real `grim` captures classify as **100 animated / 35 black / 5 static**.
These visual results are reported separately and are not promoted to PASS because
a target that crashes during normal harness shutdown does not satisfy the matrix bar.

The authoritative per-target machine-readable ledger is
[`results-with-visual.tsv`](../validation/full_matrix_2026-09-25/build-host/results-with-visual.tsv).
It retains the original harness status/failure columns and adds `visual_result`.

## Hardware and method

| Item | build-host result |
|---|---|
| Architecture | x86_64 |
| GPU | NVIDIA RTX 4500 Ada Generation |
| Driver / client GL | 595.58.03 / OpenGL ES 3.2 NVIDIA 595.58.03 |
| Client renderer | NVIDIA RTX 4500 Ada Generation/PCIe/SSE2 (140/140 logs and TSV rows) |
| Compositor | Debian labwc 0.8.3 / wlroots 0.18.2, user-local package extraction |
| Output | wlroots headless `HEADLESS-1`, 1280×720, `wayland-0` |
| Compositor renderer | NVIDIA EGL 1.5, NVIDIA GLES 3.2, RTX 4500 Ada |
| Source/build | native Meson/Ninja build from `9b617c0`; 140 targets |
| Capture | `validation/run-full-matrix.sh`; two real `grim` shots two seconds apart |
| Coverage | 140/140 targets, 280/280 captures |

There was no attached physical display. labwc was run with the wlroots headless
backend and GLES2 renderer pinned to `/dev/dri/renderD128` and GLVND's NVIDIA
vendor manifest. The login lacked `render` membership, so access to that single
render node was temporarily widened for the run and restored afterward. The
packaged Xwayland scratch directory was bind-mounted from `~/build-tmp/` in a
private mount namespace; no test/build data was placed on the host's `/tmp`.

## NVIDIA-specific evidence

This run exposes a broad NVIDIA-only shutdown failure. All 139 non-`splitflap`
targets reach both captures, then SIGSEGV during shared cleanup. A representative
GDB reproduction on animated `antinspect_gles3` resolves that target's stack to:

```text
#0  NVIDIA/GL dispatch target (no symbols)
#1  ncz_gles3_runtime_fini at src/gles3_compat.c:545
#2  app_fini at src/gles3_harness.c:478
#3  atexit_app_fini at src/gles3_harness.c:513
```

Line 545 is `glDeleteBuffers(1, &g_rt.scratch_vbo)`. This is materially different
from the prior Mesa-based matrix, where normal harness termination did not produce
a universal SIGSEGV. The evidence establishes the NVIDIA association and exact call
site; it does not by itself prove whether the defect belongs to the driver, GLVND
dispatch, or the harness's context/cleanup ordering.

Three targets are additionally black on build-host while animated on the prior
build-host/Mesa run: `cube21`, `fieldlines`, and `lattice`. `mapscroller` is also
black versus animated on build-host, but its stderr says `mapscroller.pl` is absent
from the current checkout, so it is categorized as a content/init issue rather
than NVIDIA-specific. `unicrud` likewise reports `no characters found`.

`flurry` is black and logs `glGenerateMipmap failed (0x501)` on NVIDIA. This is
concrete NVIDIA-path evidence, but the target was also black on both Mesa platforms,
so it is not claimed as NVIDIA-exclusive.

## Complete build-host ledger

| Target | Pre-shutdown visual result | Harness result |
|---|---|---|
| `antinspect_gles3` | animated (139,226 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `antspotlight_gles3` | animated (9,448 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `atlantis_gles3` | static (0 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `beats_gles3` | animated (11,532 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `blinkbox_gles3` | animated (31,901 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `blocktube_gles3` | animated (692,282 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `boing_gles3` | animated (151,824 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `bouncingcow_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `chompytower_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cityflow_gles3` | animated (691,693 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `companion_gles3` | animated (67,854 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `covid19_gles3` | animated (209,354 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `crackberg_gles3` | animated (425,801 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `crumbler_gles3` | animated (251,902 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cube21_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cubenetic_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cubestack_gles3` | animated (64,269 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cubestorm_gles3` | animated (46,844 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cubetwist_gles3` | animated (178,915 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cubicgrid_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `cyclone_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `dangerball_gles3` | animated (55,920 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `discoball_gles3` | animated (491,572 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `energystream_gles3` | animated (26,117 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `etruscanvenus_gles3` | animated (163,445 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `euphoria_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `fieldlines_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `fliptext_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `flocks_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `flurry_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `flux_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `flyingtoasters_gles3` | animated (7,758 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `gears_gles3` | animated (40,375 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `geodesic_gles3` | animated (356,010 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `geodesicgears_gles3` | animated (397,035 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `gibson_gles3` | animated (746,472 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `glblur_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `glcells_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `glforestfire_gles3` | animated (50,876 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `glhanoi_gles3` | animated (36,996 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `glknots_gles3` | animated (90,726 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `glschool_gles3` | animated (21,273 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `glsnake_gles3` | animated (97,286 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `gltext_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `gravitywell_gles3` | animated (114,018 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `handsy_gles3` | animated (257,990 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `headroom_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `helios_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hexstrut_gles3` | animated (359,555 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hextrail_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `highvoltage_gles3` | static (0 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hilbert_gles3` | animated (164,258 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hydrostat_gles3` | animated (13,177 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyperspace_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hypertorus_gles3` | animated (324,696 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hypnowheel_gles3` | animated (336,024 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_attitude_gles3` | animated (28,382 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_aurora_gles3` | animated (813,515 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_bezier_gles3` | animated (16,431 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_blob_gles3` | animated (921,427 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_caustics_gles3` | animated (385,114 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_circuit_gles3` | animated (146,629 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_clouds_gles3` | animated (865,417 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_donut_gles3` | animated (249,805 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_fireflies_gles3` | animated (207,685 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_flames_gles3` | animated (458,361 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_fractaltrap_gles3` | animated (619,213 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_geometry_gles3` | animated (76,432 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_gridwave_gles3` | animated (80,054 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_hypercube_gles3` | animated (79,978 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_julia_gles3` | animated (669,359 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_kaleidoscope_gles3` | animated (867,212 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_lissajous_gles3` | animated (86,250 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_marble_gles3` | animated (904,137 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_matrix_gles3` | animated (551,689 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_mobius_gles3` | animated (142,813 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_oscilloscope_gles3` | animated (305,747 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_planet_gles3` | animated (190,038 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_plasma_gles3` | animated (921,471 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_shipburn_gles3` | animated (660,296 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_snowfall_gles3` | animated (921,478 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_sonar_gles3` | animated (54,455 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_starfield_gles3` | animated (106,600 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_stonks_gles3` | animated (92,504 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_temple_gles3` | animated (241,876 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_terminal_gles3` | animated (213,850 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_tesla_gles3` | animated (582,975 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_tunnel_gles3` | animated (619,208 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_voronoi_gles3` | animated (921,505 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_waterfall_gles3` | animated (664,979 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `hyprsaver_wormhole_gles3` | animated (921,199 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `implicitdemo_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `jigsaw_gles3` | animated (339,999 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `juggler3d_gles3` | animated (28,739 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `kaleidocycle_gles3` | animated (83,108 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `kallisti_gles3` | animated (9,812 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `klein_gles3` | animated (293,670 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `lament_gles3` | animated (169,308 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `lattice_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `lavalite_gles3` | animated (9,839 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `lockward_gles3` | animated (380,428 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `mapscroller_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `menger_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `microcosm_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `moebiusgears_gles3` | animated (183,246 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `molecule_gles3` | animated (99,089 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `nakagin_gles3` | animated (76,618 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `noof_gles3` | animated (237,143 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `papercube_gles3` | animated (190,040 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `peepers_gles3` | animated (43,946 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `photopile_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `plasma_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `polyhedra-gl_gles3` | animated (119,288 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `projectiveplane_gles3` | animated (250,336 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `providence_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `quasicrystal_gles3` | static (0 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `raverhoop_gles3` | animated (228,945 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `razzledazzle_gles3` | animated (42,961 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `romanboy_gles3` | animated (135,336 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `rubikblocks_gles3` | animated (113,445 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `sballs_gles3` | static (0 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `skulloop_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `skyrocket_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `skytentacles_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `solarwinds_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `sphereeversion_gles3` | animated (114,423 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `spheremonics_gles3` | animated (58,307 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `splitflap_gles3` | black | FAIL (SIGABRT / exit 134) |
| `splodesic_gles3` | animated (112,813 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `squirtorus_gles3` | animated (18,576 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `starwars_gles3` | animated (152,688 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `stonerview_gles3` | animated (115,014 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `tangram_gles3` | animated (114,014 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `timetunnel_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `topblock_gles3` | animated (70,464 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `tronbit_gles3` | animated (92,750 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `unicrud_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |
| `unknownpleasures_gles3` | static (457 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `voronoi_gles3` | animated (532,248 px changed) | FAIL (SIGSEGV / exit 139 during cleanup) |
| `winduprobot_gles3` | black | FAIL (SIGSEGV / exit 139 during cleanup) |

## Visual failure sets

- **Black (35):** `bouncingcow`, `chompytower`, `cube21`, `cubenetic`, `cubicgrid`, `cyclone`, `euphoria`, `fieldlines`, `fliptext`, `flocks`, `flurry`, `flux`, `glblur`, `glcells`, `gltext`, `headroom`, `helios`, `hextrail`, `hyperspace`, `implicitdemo`, `lattice`, `mapscroller`, `menger`, `microcosm`, `photopile`, `plasma`, `providence`, `skulloop`, `skyrocket`, `skytentacles`, `solarwinds`, `splitflap`, `timetunnel`, `unicrud`, `winduprobot`.
- **Static (5):** `atlantis`, `highvoltage`, `quasicrystal`, `sballs`, `unknownpleasures`.

## Evidence

Checked-in evidence is under
[`validation/full_matrix_2026-09-25/build-host/`](../validation/full_matrix_2026-09-25/build-host/):

- `results.tsv` — unmodified harness ledger.
- `results-with-visual.tsv` — harness ledger plus the pre-shutdown visual category.
- `logs/` — 140 stderr logs and 140 exit-code files.
- `thumbs/` — both 480px evidence captures for all 140 targets.
- `screenshots.sha256` — hashes for the 280 retained full-resolution captures.
- `labwc-nvidia.log` — compositor EGL/GLES vendor and renderer evidence.
- `antinspect-gdb-crash.txt` — representative cleanup SIGSEGV backtrace.

The 26 MiB full-resolution capture set remains on build-host at
`~/build-tmp/full-matrix-2026-09-25-build-host-final/shots/`.
