# Full GLES3 hardware matrix — 2026-09-25

> **Human-reviewed targeted follow-up:** See
> [FINAL-TARGETED-FIX-2026-09-25.md](FINAL-TARGETED-FIX-2026-09-25.md). Its
> manual findings supersede automated PASS classifications for those 13
> targets.

> **NVIDIA follow-up:** The complete 140-target build-host / RTX 4500 Ada run is
> documented in [NVIDIA-FULL-MATRIX-build-host-2026-09-25.md](NVIDIA-FULL-MATRIX-build-host-2026-09-25.md),
> including real two-shot `grim` evidence and a universal NVIDIA-path cleanup
> SIGSEGV that keeps the fail-closed result at 0 PASS / 140 FAIL.

This is the complete current build set: **140/140 targets on O6N arm64** and
**140/140 targets on build-host amd64**. After the focused fixes recorded below,
the current totals are 105 PASS / 35 FAIL on O6N and 110 PASS / 30 FAIL on
build-host. No target is marked N/A. PASS means two
real `grim` captures taken two seconds apart were non-black and differed by
more than 1,000 pixels while the process remained alive. Exit code alone is
not accepted. FAIL targets are set aside below with their observed mode.

## Hardware and method

| Host | Architecture | Renderer | Build | Coverage | Result |
|---|---|---|---|---:|---:|
| O6N | arm64 | Mali-G720-Immortalis | native the arm64 build host arm64 at `251bf22` + focused fixes | 140/140 | 105 PASS / 35 FAIL |
| build-host | amd64 | Mesa Intel UHD Graphics CML GT2 (the active compositor GPU; not RTX 2060) | fresh native Meson/Ninja build + focused fixes | 140/140 | 110 PASS / 30 FAIL |

Each target has stderr, an exit-code file, two 480px evidence thumbnails,
metrics, and SHA-256 hashes of the retained full-resolution captures under
`validation/full_matrix_2026-09-25/{o6n,build-host}/`. The full-resolution PNGs
remain on each named host at `~/gles3-validation/full-matrix-2026-09-25/shots/`
(176 MiB O6N; 129 MiB build-host); thumbnails are checked in to keep the repo
reviewable. Both runs dynamically discovered `/run/user/1000/wayland-0`.

The initial O6N attempt inherited an obsolete broad `LD_LIBRARY_PATH` from
the 2026-09-23 sweep and produced allocator corruption. That harness defect
was removed and the O6N matrix was restarted from target 1; only the clean
140-row rerun is reported here.

## Complete ledger

| Target | O6N arm64 | build-host amd64 |
|---|---|---|
| `antinspect_gles3` | PASS (animated; 1,333,826 px changed) | PASS (animated; 1,316,849 px changed) |
| `antspotlight_gles3` | PASS (animated; 79,850 px changed) | PASS (animated; 79,255 px changed) |
| `atlantis_gles3` | FAIL (static; 0 px changed) | FAIL (static; 0 px changed) |
| `beats_gles3` | PASS (animated; 100,868 px changed) | PASS (animated; 100,800 px changed) |
| `blinkbox_gles3` | PASS (animated; 162,286 px changed) | PASS (animated; 202,911 px changed) |
| `blocktube_gles3` | PASS (animated; 2,294,142 px changed) | PASS (animated; 6,116,597 px changed) |
| `boing_gles3` | PASS (animated; 2,442,054 px changed) | PASS (animated; 1,610,901 px changed) |
| `bouncingcow_gles3` | PASS (animated after ModeInfo defaults fix; 52,546 px changed) | PASS (animated after ModeInfo defaults fix; 46,783 px changed) |
| `chompytower_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `cityflow_gles3` | PASS (animated; 4,233,938 px changed) | PASS (animated; 7,090,730 px changed) |
| `companion_gles3` | PASS (animated; 182,617 px changed) | PASS (animated; 1,072,528 px changed) |
| `covid19_gles3` | PASS (animated; 2,464,762 px changed) | PASS (animated; 2,026,162 px changed) |
| `crackberg_gles3` | PASS (animated; 3,680,577 px changed) | PASS (animated; 3,760,229 px changed) |
| `crumbler_gles3` | PASS (animated; 2,125,098 px changed) | PASS (animated; 2,261,140 px changed) |
| `cube21_gles3` | FAIL (black frames; render loop advanced) | PASS (animated; 80,988 px changed) |
| `cubenetic_gles3` | PASS (animated after ModeInfo defaults fix; 1,221,481 px changed) | PASS (animated after ModeInfo defaults fix; 1,035,024 px changed) |
| `cubestack_gles3` | PASS (animated; 727,682 px changed) | PASS (animated; 638,523 px changed) |
| `cubestorm_gles3` | PASS (animated; 876,685 px changed) | PASS (animated; 579,368 px changed) |
| `cubetwist_gles3` | PASS (animated; 1,268,718 px changed) | PASS (animated; 1,588,731 px changed) |
| `cubicgrid_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `cyclone_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `dangerball_gles3` | PASS (animated; 359,698 px changed) | PASS (animated; 422,650 px changed) |
| `discoball_gles3` | PASS (animated; 2,131,415 px changed) | PASS (animated; 4,138,946 px changed) |
| `energystream_gles3` | PASS (animated; 717,526 px changed) | PASS (animated; 452,276 px changed) |
| `etruscanvenus_gles3` | PASS (animated; 1,617,793 px changed) | PASS (animated; 1,666,136 px changed) |
| `euphoria_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `fieldlines_gles3` | FAIL (black frames; render loop advanced) | PASS (animated; 104,932 px changed) |
| `fliptext_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `flocks_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `flurry_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `flux_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `flyingtoasters_gles3` | PASS (animated; 106,464 px changed) | PASS (animated; 73,191 px changed) |
| `gears_gles3` | PASS (animated; 230,673 px changed) | PASS (animated; 277,700 px changed) |
| `geodesic_gles3` | PASS (animated; 2,963,899 px changed) | PASS (animated; 3,219,033 px changed) |
| `geodesicgears_gles3` | PASS (animated; 2,215,940 px changed) | PASS (animated; 3,676,124 px changed) |
| `gibson_gles3` | PASS (animated; 1,328,644 px changed) | PASS (animated; 6,779,975 px changed) |
| `glblur_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `glcells_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `glforestfire_gles3` | PASS (animated; 376,766 px changed) | PASS (animated; 477,082 px changed) |
| `glhanoi_gles3` | PASS (animated; 407,373 px changed) | PASS (animated; 348,484 px changed) |
| `glknots_gles3` | PASS (animated; 1,274,672 px changed) | PASS (animated; 833,671 px changed) |
| `glschool_gles3` | PASS (animated; 188,123 px changed) | PASS (animated; 577,714 px changed) |
| `glsnake_gles3` | PASS (animated; 786,712 px changed) | PASS (animated; 863,671 px changed) |
| `gltext_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `gravitywell_gles3` | PASS (animated; 1,280,139 px changed) | PASS (animated; 1,243,385 px changed) |
| `handsy_gles3` | PASS (animated; 2,686,813 px changed) | PASS (animated; 2,326,110 px changed) |
| `headroom_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `helios_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `hexstrut_gles3` | PASS (animated; 3,100,260 px changed) | PASS (animated; 3,358,067 px changed) |
| `hextrail_gles3` | PASS (animated after ModeInfo defaults fix; 357,395 px changed) | PASS (animated after ModeInfo defaults fix; 394,858 px changed) |
| `highvoltage_gles3` | FAIL (static; 0 px changed) | FAIL (static; 0 px changed) |
| `hilbert_gles3` | PASS (animated; 1,511,274 px changed) | PASS (animated; 1,497,789 px changed) |
| `hydrostat_gles3` | PASS (animated; 112,036 px changed) | PASS (animated; 115,464 px changed) |
| `hyperspace_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `hypertorus_gles3` | PASS (animated; 2,458,160 px changed) | PASS (animated; 2,939,405 px changed) |
| `hypnowheel_gles3` | PASS (animated; 3,310,237 px changed) | PASS (animated; 3,206,621 px changed) |
| `hyprsaver_attitude_gles3` | PASS (animated; 297,496 px changed) | PASS (animated; 272,945 px changed) |
| `hyprsaver_aurora_gles3` | PASS (animated; 7,390,354 px changed) | PASS (animated; 7,367,699 px changed) |
| `hyprsaver_bezier_gles3` | PASS (animated; 180,172 px changed) | PASS (animated; 186,837 px changed) |
| `hyprsaver_blob_gles3` | PASS (animated; 8,289,866 px changed) | PASS (animated; 8,293,902 px changed) |
| `hyprsaver_caustics_gles3` | PASS (animated; 3,631,578 px changed) | PASS (animated; 3,553,716 px changed) |
| `hyprsaver_circuit_gles3` | PASS (animated; 1,425,391 px changed) | PASS (animated; 1,407,249 px changed) |
| `hyprsaver_clouds_gles3` | PASS (animated; 7,990,130 px changed) | PASS (animated; 7,643,688 px changed) |
| `hyprsaver_donut_gles3` | PASS (animated; 2,412,511 px changed) | PASS (animated; 2,328,108 px changed) |
| `hyprsaver_fireflies_gles3` | PASS (animated; 2,001,379 px changed) | PASS (animated; 1,966,242 px changed) |
| `hyprsaver_flames_gles3` | PASS (animated; 3,568,628 px changed) | PASS (animated; 3,940,792 px changed) |
| `hyprsaver_fractaltrap_gles3` | PASS (animated; 5,591,885 px changed) | PASS (animated; 5,542,544 px changed) |
| `hyprsaver_geometry_gles3` | PASS (animated; 687,112 px changed) | PASS (animated; 703,847 px changed) |
| `hyprsaver_gridwave_gles3` | PASS (animated; 871,614 px changed) | PASS (animated; 814,438 px changed) |
| `hyprsaver_hypercube_gles3` | PASS (animated; 668,903 px changed) | PASS (animated; 774,474 px changed) |
| `hyprsaver_julia_gles3` | PASS (animated; 6,385,062 px changed) | PASS (animated; 6,240,227 px changed) |
| `hyprsaver_kaleidoscope_gles3` | PASS (animated; 7,346,853 px changed) | PASS (animated; 7,809,137 px changed) |
| `hyprsaver_lissajous_gles3` | PASS (animated; 787,007 px changed) | PASS (animated; 731,416 px changed) |
| `hyprsaver_marble_gles3` | PASS (animated; 8,169,214 px changed) | PASS (animated; 8,178,279 px changed) |
| `hyprsaver_matrix_gles3` | PASS (animated; 4,523,252 px changed) | PASS (animated; 4,493,969 px changed) |
| `hyprsaver_mobius_gles3` | PASS (animated; 2,241,667 px changed) | PASS (animated; 1,682,718 px changed) |
| `hyprsaver_oscilloscope_gles3` | PASS (animated; 2,564,333 px changed) | PASS (animated; 2,730,479 px changed) |
| `hyprsaver_planet_gles3` | PASS (animated; 1,705,499 px changed) | PASS (animated; 1,615,583 px changed) |
| `hyprsaver_plasma_gles3` | PASS (animated; 8,290,078 px changed) | PASS (animated; 8,294,135 px changed) |
| `hyprsaver_shipburn_gles3` | PASS (animated; 6,228,883 px changed) | PASS (animated; 6,071,416 px changed) |
| `hyprsaver_snowfall_gles3` | PASS (animated; 8,290,349 px changed) | PASS (animated; 8,294,400 px changed) |
| `hyprsaver_sonar_gles3` | PASS (animated; 504,038 px changed) | PASS (animated; 515,956 px changed) |
| `hyprsaver_starfield_gles3` | PASS (animated; 1,113,584 px changed) | PASS (animated; 1,222,266 px changed) |
| `hyprsaver_stonks_gles3` | PASS (animated; 802,273 px changed) | PASS (animated; 830,422 px changed) |
| `hyprsaver_temple_gles3` | PASS (animated; 3,323,951 px changed) | PASS (animated; 3,191,276 px changed) |
| `hyprsaver_terminal_gles3` | PASS (animated; 1,708,329 px changed) | PASS (animated; 1,873,158 px changed) |
| `hyprsaver_tesla_gles3` | PASS (animated; 2,810,099 px changed) | PASS (animated; 2,744,115 px changed) |
| `hyprsaver_tunnel_gles3` | PASS (animated; 6,064,613 px changed) | PASS (animated; 5,971,224 px changed) |
| `hyprsaver_voronoi_gles3` | PASS (animated; 8,290,351 px changed) | PASS (animated; 8,294,365 px changed) |
| `hyprsaver_waterfall_gles3` | PASS (animated; 5,509,359 px changed) | PASS (animated; 6,007,756 px changed) |
| `hyprsaver_wormhole_gles3` | PASS (animated; 8,257,870 px changed) | PASS (animated; 8,253,143 px changed) |
| `implicitdemo_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `jigsaw_gles3` | FAIL (black frames; render loop advanced) | PASS (animated; 3,229,758 px changed) |
| `juggler3d_gles3` | PASS (animated; 270,417 px changed) | PASS (animated; 267,735 px changed) |
| `kaleidocycle_gles3` | PASS (animated; 627,307 px changed) | PASS (animated; 716,358 px changed) |
| `kallisti_gles3` | PASS (animated; 159,257 px changed) | PASS (animated; 130,993 px changed) |
| `klein_gles3` | PASS (animated; 5,971,045 px changed) | PASS (animated; 5,358,790 px changed) |
| `lament_gles3` | PASS (animated; 517,573 px changed) | PASS (animated; 613,629 px changed) |
| `lattice_gles3` | FAIL (black frames; render loop advanced) | PASS (animated; 28,884 px changed) |
| `lavalite_gles3` | PASS (animated; 13,605 px changed) | PASS (animated; 101,426 px changed) |
| `lockward_gles3` | PASS (animated; 3,450,299 px changed) | PASS (animated; 3,444,336 px changed) |
| `mapscroller_gles3` | PASS (animated; 144,653 px changed) | PASS (animated; 259,086 px changed) |
| `menger_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `microcosm_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `moebiusgears_gles3` | PASS (animated; 1,964,642 px changed) | PASS (animated; 1,700,488 px changed) |
| `molecule_gles3` | PASS (animated; 877,358 px changed) | PASS (animated; 811,442 px changed) |
| `nakagin_gles3` | PASS (animated; 450,792 px changed) | PASS (animated; 594,657 px changed) |
| `noof_gles3` | PASS (animated; 3,973,302 px changed) | PASS (animated; 3,537,600 px changed) |
| `papercube_gles3` | PASS (animated; 1,826,840 px changed) | PASS (animated; 1,710,744 px changed) |
| `peepers_gles3` | PASS (animated; 406,477 px changed) | PASS (animated; 321,405 px changed) |
| `photopile_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `plasma_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `polyhedra-gl_gles3` | PASS (animated; 1,604,634 px changed) | PASS (animated; 1,184,478 px changed) |
| `projectiveplane_gles3` | PASS (animated; 2,380,085 px changed) | PASS (animated; 2,209,342 px changed) |
| `providence_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `quasicrystal_gles3` | FAIL (static; 0 px changed) | FAIL (static; 0 px changed) |
| `raverhoop_gles3` | PASS (animated; 2,427,147 px changed) | PASS (animated; 813,602 px changed) |
| `razzledazzle_gles3` | PASS (animated; 3,962,561 px changed) | PASS (animated; 401,779 px changed) |
| `romanboy_gles3` | PASS (animated; 1,092,378 px changed) | PASS (animated; 1,160,666 px changed) |
| `rubikblocks_gles3` | PASS (animated; 1,032,882 px changed) | PASS (animated; 1,042,040 px changed) |
| `sballs_gles3` | FAIL (static; 0 px changed) | FAIL (static; 0 px changed) |
| `skulloop_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `skyrocket_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `skytentacles_gles3` | PASS (animated after ModeInfo defaults fix; 989,230 px changed) | PASS (animated after ModeInfo defaults fix; 1,027,493 px changed) |
| `solarwinds_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `sphereeversion_gles3` | PASS (animated; 1,242,425 px changed) | PASS (animated; 1,228,437 px changed) |
| `spheremonics_gles3` | PASS (animated; 585,442 px changed) | PASS (animated; 637,898 px changed) |
| `splitflap_gles3` | FAIL (black frames; render loop advanced) | FAIL (SIGABRT after initial frames) |
| `splodesic_gles3` | FAIL (black frames; render loop advanced) | PASS (animated; 1,149,640 px changed) |
| `squirtorus_gles3` | PASS (animated; 190,170 px changed) | PASS (animated; 153,158 px changed) |
| `starwars_gles3` | PASS (animated; 1,189,304 px changed) | PASS (animated; 1,104,649 px changed) |
| `stonerview_gles3` | PASS (animated; 1,184,964 px changed) | PASS (animated; 1,045,938 px changed) |
| `tangram_gles3` | PASS (animated; 1,270,122 px changed) | PASS (animated; 1,175,058 px changed) |
| `timetunnel_gles3` | FAIL (black frames; render loop advanced) | FAIL (black frames; render loop advanced) |
| `topblock_gles3` | PASS (animated; 576,499 px changed) | PASS (animated; 699,276 px changed) |
| `tronbit_gles3` | PASS (animated; 1,258,261 px changed) | PASS (animated; 983,346 px changed) |
| `unicrud_gles3` | FAIL (exit 1: no characters found) | FAIL (exit 1: no characters found) |
| `unknownpleasures_gles3` | PASS (animated after resource-default fix; 4,116 px changed) | PASS (animated after resource-default fix; 5,488 px changed) |
| `voronoi_gles3` | PASS (animated; 2,632,337 px changed) | PASS (animated; 3,463,633 px changed) |
| `winduprobot_gles3` | PASS (animated after ModeInfo defaults fix; 665,655 px changed) | PASS (animated after ModeInfo defaults fix; 55,386 px changed) |

## Set aside for focused fixing

### O6N arm64

- **black** — process and frame diagnostics continued, but both real captures were black: `chompytower`, `cube21`, `cubicgrid`, `cyclone`, `euphoria`, `fieldlines`, `fliptext`, `flocks`, `flurry`, `flux`, `glblur`, `glcells`, `gltext`, `headroom`, `helios`, `hyperspace`, `implicitdemo`, `jigsaw`, `lattice`, `menger`, `microcosm`, `photopile`, `plasma`, `providence`, `skulloop`, `skyrocket`, `solarwinds`, `splitflap`, `splodesic`, `timetunnel`.
- **exit_1** — initialization rejected required content/configuration; see per-target stderr: `unicrud`.
- **static** — visible output was captured, but the two frames did not change materially: `atlantis`, `highvoltage`, `quasicrystal`, `sballs`.

### build-host amd64

- **black** — process and frame diagnostics continued, but both real captures were black: `chompytower`, `cubicgrid`, `cyclone`, `euphoria`, `fliptext`, `flocks`, `flurry`, `flux`, `glblur`, `glcells`, `gltext`, `headroom`, `helios`, `hyperspace`, `implicitdemo`, `menger`, `microcosm`, `photopile`, `plasma`, `providence`, `skulloop`, `skyrocket`, `solarwinds`, `timetunnel`.
- **exit_1** — initialization rejected required content/configuration; see per-target stderr: `unicrud`.
- **exit_134** — process aborted after initial frames; see splitflap stderr: `splitflap`.
- **static** — visible output was captured, but the two frames did not change materially: `atlantis`, `highvoltage`, `quasicrystal`, `sballs`.

## Interpretation constraints

- Black/static classification is deliberately fail-closed. Some savers can
  have slow or dark phases, but the directive requires confirmed animation;
  these targets therefore remain FAIL until a focused longer/manual run proves
  otherwise.
- build-host is genuine amd64 hardware, but its active labwc session renders on
  the Intel UHD 630-class iGPU. The installed RTX 2060 was not the compositor
  GPU, so this ledger does not claim NVIDIA coverage.
- build-host was attempted first but was unreachable (`No route to host`); build-host
  supplied the complete amd64 matrix instead.

## Focused fixes after the full run

- `unknownpleasures_gles3`: restored the application-wide XScreenSaver
  `Foreground`/`Background` resource defaults in the standalone compatibility
  layer. Real 2s/4s `grim` captures prove animation on both O6N and build-host;
  see `validation/full_matrix_fixes_2026-09-25/unknownpleasures/`.
- `bouncingcow_gles3`, `cubenetic_gles3`, `hextrail_gles3`,
  `skytentacles_gles3`, and `winduprobot_gles3`: populated standard xlockmore `ModeInfo` fields from
  each hack's parsed defaults. All four now pass real 2s/4s animation checks
  on both architectures; see
  `validation/full_matrix_fixes_2026-09-25/mode-defaults/`.

## Real NVIDIA follow-up (RTX 2060, PRIME offload)

This section supersedes the earlier build-host note above. The run used NVIDIA
615.71.09 through PRIME render offload and every target log reported
`RENDERER=NVIDIA GeForce RTX 2060/PCIe/SSE2`. Classification came from two
real `grim` captures and numpy pixel comparison, not process exit status.

### Fixes and evidence

- The GLES3 harness did not invoke the initial `reshape_cb`. Savers such as
  `gears` hid this by reshaping from their own init callback, while every RSS
  port relied on the harness lifecycle. Instrumentation showed submitted
  vertices and valid colors but an identity MVP. Calling reshape immediately
  after init fixes that lifecycle error.
- The compatibility runtime enabled depth testing by default, contrary to the
  OpenGL initial state. `flux` submitted projected, colored geometry without
  GL errors, but never disabled depth testing or cleared the initial depth
  buffer in its default mode. Restoring disabled depth testing fixed this
  shared state assumption.
- `glNewList(1, ...)` was silently ignored unless the name had first come from
  `glGenLists`. OpenGL permits explicit positive list names. Accepting those
  names restored the animated geometry in `flocks` and `solarwinds`.
- Three RSS HSL helpers failed to write their blue output. The undefined color
  channel was repaired in `hyperspace`, `microcosm`, and `skyrocket`.
- Xlockmore normally supplies `ModeInfo.colors` and `ModeInfo.pixels`.
  Allocating those framework arrays before saver init fixes the proven
  `juggler3d` NULL dereference; its targeted real run is now PASS.
- A real `splitflap` core showed SIGABRT in `read_unicode` because the Wayland
  text-client stub returned NULL. It now receives a non-NULL EOF client and
  exits cleanly, though its separate black-render failure remains.

### Post-fix full matrix

The complete 140-target run produced 119 PASS, 15 black, 4 static, one clean
expected exit (`unicrud`), and one temporary `fliptext` exit-137 caused by an
initial cycling fallback-text implementation. After changing that fallback to
EOF semantics, a targeted real rerun made `fliptext` exit cleanly and classify
black. The corrected final state is therefore:

- **119 PASS**
- **16 black:** `chompytower`, `cyclone`, `fieldlines`, `fliptext`, `flurry`,
  `glblur`, `glcells`, `headroom`, `hexstrut`, `hyperspace`, `photopile`,
  `providence`, `skulloop`, `splitflap`, `timetunnel`, `winduprobot`
- **4 static:** `atlantis`, `cityflow`, `quasicrystal`, `sballs`
- **0 crashes**
- **1 clean exit:** `unicrud` (exit 1, font/glyph lookup failure)

Artifacts are on build-host under `~/build-tmp/ncz-full-final/`; the targeted
correction is under `~/build-tmp/nczflip/`. Several formerly black targets now
pass, including all of `bouncingcow`, `cube21`, `cubicgrid`, `euphoria`,
`flocks`, `flux`, `gltext`, `helios`, `implicitdemo`, `lattice`, `menger`,
`microcosm`, `plasma`, `skyrocket`, and `solarwinds`. The remaining failures
above are deliberately not claimed fixed.
