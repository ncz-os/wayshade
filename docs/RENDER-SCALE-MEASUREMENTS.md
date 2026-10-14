# Render size measurements (Mali-G720, Sky1)

## Correction

An earlier version of this document (package 0.4.0) measured on O6N and said that a 4K Sky1 panel costs the same as the 1080p case. O6N's attached monitor is a 1080p panel (QSA QiSi-AUGRA, DP-2, verified with `wlr-randr` and `/sys/class/drm`), so that 4K statement was an inference, not a measurement. The tables below are measured on the MS-R1 (same Cix Sky1 SoC, Mali-G720-Immortalis, mali_kbase) with its real ViewSonic VP2488-4K, 3840x2160 at 59.997 Hz, output scale 1.75. There the compositor configures 2194x1234 layer surfaces (logical pixels), and that is what a hack renders unless something reduces it.

## Method

Host MS-R1, package 0.5.3 binaries (unpacked, not installed) plus the test knob `NCZ_TEST_SURFACE_SIZE`, 72 shader hacks (Black Hole, 35 hyprsaver, 36 xshadertoy), each run 12 s under `timeout` with `--render-scale-mode=fixed`, under the host lock, in the operator's session. fps is the steady-state frame count over the last 7 s; p95 is the 95th percentile frame interval in ms (the `[stats]` line). Three operating points:

| Point | Surface | Render size at scale 1 | How |
|---|---|---|---|
| native 4K | 3840x2160 output pixels | 3840x2160 | `NCZ_TEST_SURFACE_SIZE=3840x2160 --max-render-height=0`; the buffer maps 1:1 to the physical pixels of the 4K panel, so compositor cost is the real one |
| default, uncapped | 2194x1234 (real) | 2194x1234 | `--max-render-height=0` |
| capped (the Mali default) | 2194x1234 (real) | 1919x1080 | nothing set; the platform cap of 1080 lines applies |

Slow hacks were re-run at scale 0.75, 0.5 and 0.35 (of the surface, then capped) until 55 fps was reached. Cells are fps (p95 ms); `-` means not measured because a larger size already reached 55 fps. Raw lines: `docs/render-scale-msr1-4k-sweep.txt`. The old 1080p O6N data stays in `docs/render-scale-o6n-sweep.txt` (a 1080p panel; it matches the capped table below within noise).

## Summary

* Capped (the shipped default): 55 of 72 hacks hold 55 fps or more at scale 1. This equals the 1080p O6N result, so the Mali cap makes a 4K panel cost the same as 1080p.
* Default uncapped 2194x1234: 51 of 72 (Black Hole 48 fps, p95 21 ms). Native 3840x2160: 37 of 72 (Black Hole 15 fps). The cap of 1080 lines is therefore the right Sky1 default; `max-render-height` stays 1080 on Mali.
* Black Hole capped: 57 fps, p95 18 ms (the adaptive step controller keeps it in the medium tier).
* alienbeacon is the heaviest shader: 5 fps capped, 26 fps at scale 0.35. No hint reaches 45 fps for it.
* `assets/screensaver-chooser/render-hints.tsv` was regenerated from the capped point (16 hacks). A hint seeds `NCZ_RENDER_SCALE`; adaptive mode may step lower.

Image quality: reduced renders are upscaled with the compositor's linear filter (wp_viewport); these shader hacks are smooth gradients, so the loss is softness; 0.35 is visibly soft.

## Tables (hacks that hold 55 fps at scale 1 are omitted, except Black Hole)
### native 3840x2160 (output pixels, scale 1): 37 of 72 hacks hold 55 fps or more at scale 1

| Hack | 1.0 | 0.75 | 0.5 | 0.35 |
|---|---|---|---|---|
| blackhole | 15 (67) | 28 (37) | 60 (17) | - |
| hyprsaver_aurora | 24 (43) | 41 (25) | 60 (17) | - |
| hyprsaver_bezier | 43 (24) | 60 (17) | - | - |
| hyprsaver_circuit | 34 (29) | 60 (17) | - | - |
| hyprsaver_geometry | 43 (29) | 60 (17) | - | - |
| hyprsaver_lissajous | 9 (112) | 16 (64) | 35 (29) | 60 (17) |
| hyprsaver_marble | 53 (19) | 60 (17) | - | - |
| hyprsaver_starfield | 43 (24) | 60 (17) | - | - |
| hyprsaver_voronoi | 17 (58) | 30 (33) | 60 (17) | - |
| xshadertoy_alienbeacon | 0 (0) | 2 (541) | 5 (249) | 10 (130) |
| xshadertoy_batteredplanet | 11 (93) | 19 (53) | 40 (26) | 60 (17) |
| xshadertoy_bestill0-0 | 6 (179) | 10 (102) | 22 (46) | 43 (24) |
| xshadertoy_bestill1-0 | 4 (273) | 7 (154) | 14 (70) | 28 (36) |
| xshadertoy_bestill2-0 | 3 (336) | 5 (190) | 12 (85) | 24 (43) |
| xshadertoy_bestill3-0 | 6 (174) | 10 (99) | 23 (45) | 44 (23) |
| xshadertoy_bestill4-0 | 3 (340) | 5 (192) | 12 (86) | 23 (43) |
| xshadertoy_bestill5-0 | 20 (51) | 35 (29) | 60 (17) | - |
| xshadertoy_darktransit | 31 (32) | 54 (19) | 60 (17) | - |
| xshadertoy_downfall | 4 (293) | 6 (166) | 14 (74) | 27 (38) |
| xshadertoy_fluxcore | 6 (227) | 11 (132) | 23 (63) | 42 (33) |
| xshadertoy_gimbalharmonics | 20 (53) | 34 (31) | 60 (17) | - |
| xshadertoy_neongravity-1 | 41 (24) | 60 (17) | - | - |
| xshadertoy_neonhorizon | 42 (24) | 60 (17) | - | - |
| xshadertoy_neontriangulator | 32 (43) | 52 (25) | 60 (17) | - |
| xshadertoy_noxfire | 5 (226) | 8 (128) | 18 (57) | 35 (29) |
| xshadertoy_polarnight | 4 (307) | 7 (175) | 14 (80) | 27 (44) |
| xshadertoy_prococean | 17 (60) | 29 (35) | 60 (17) | - |
| xshadertoy_protophore | 33 (32) | 54 (19) | 60 (17) | - |
| xshadertoy_rigrekt | 8 (132) | 13 (76) | 30 (34) | 57 (18) |
| xshadertoy_selfreflect | 25 (43) | 41 (25) | 60 (17) | - |
| xshadertoy_skyline | 4 (271) | 8 (156) | 17 (72) | 32 (39) |
| xshadertoy_starnest | 18 (57) | 31 (32) | 60 (17) | - |
| xshadertoy_topologica | 10 (105) | 17 (60) | 37 (27) | 60 (17) |
| xshadertoy_trizm | 14 (72) | 25 (41) | 54 (19) | 60 (17) |
| xshadertoy_universeball | 7 (161) | 11 (91) | 24 (42) | 47 (22) |

### 2194x1234 (4K at output scale 1.75, no cap): 51 of 72 hacks hold 55 fps or more at scale 1

| Hack | 1.0 | 0.75 | 0.5 | 0.35 |
|---|---|---|---|---|
| blackhole | 48 (21) | 60 (17) | - | - |
| hyprsaver_lissajous | 27 (37) | 47 (22) | 60 (17) | - |
| hyprsaver_voronoi | 51 (20) | 60 (17) | - | - |
| xshadertoy_alienbeacon | 4 (311) | 7 (190) | 14 (87) | 26 (49) |
| xshadertoy_batteredplanet | 32 (32) | 52 (20) | 60 (17) | - |
| xshadertoy_bestill0-0 | 17 (60) | 29 (36) | 60 (19) | - |
| xshadertoy_bestill1-0 | 11 (91) | 19 (54) | 41 (26) | 60 (17) |
| xshadertoy_bestill2-0 | 9 (111) | 16 (64) | 35 (30) | 60 (17) |
| xshadertoy_bestill3-0 | 17 (59) | 30 (35) | 60 (17) | - |
| xshadertoy_bestill4-0 | 9 (112) | 16 (64) | 34 (30) | 60 (17) |
| xshadertoy_downfall | 11 (97) | 18 (56) | 39 (26) | 60 (17) |
| xshadertoy_fluxcore | 18 (80) | 30 (48) | 50 (24) | 60 (17) |
| xshadertoy_noxfire | 14 (75) | 24 (43) | 51 (20) | 60 (17) |
| xshadertoy_polarnight | 11 (104) | 19 (61) | 38 (31) | 60 (17) |
| xshadertoy_prococean | 48 (22) | 60 (17) | - | - |
| xshadertoy_rigrekt | 23 (44) | 40 (26) | 60 (17) | - |
| xshadertoy_skyline | 13 (93) | 22 (56) | 45 (27) | 60 (17) |
| xshadertoy_starnest | 52 (19) | 60 (17) | - | - |
| xshadertoy_topologica | 29 (35) | 50 (20) | 60 (17) | - |
| xshadertoy_trizm | 42 (24) | 60 (17) | - | - |
| xshadertoy_universeball | 19 (54) | 32 (32) | 60 (17) | - |

### capped to 1080 lines (Mali default, 1919x1080): 55 of 72 hacks hold 55 fps or more at scale 1

| Hack | 1.0 | 0.75 | 0.5 | 0.35 |
|---|---|---|---|---|
| blackhole | 57 (18) | - | - | - |
| hyprsaver_lissajous | 35 (29) | 47 (22) | 60 (17) | - |
| xshadertoy_alienbeacon | 5 (243) | 7 (188) | 14 (91) | 26 (48) |
| xshadertoy_batteredplanet | 40 (26) | 52 (20) | 60 (17) | - |
| xshadertoy_bestill0-0 | 22 (46) | 29 (36) | 60 (20) | - |
| xshadertoy_bestill1-0 | 14 (70) | 19 (54) | 41 (26) | 60 (17) |
| xshadertoy_bestill2-0 | 12 (85) | 16 (64) | 35 (30) | 60 (17) |
| xshadertoy_bestill3-0 | 22 (45) | 30 (35) | 60 (17) | - |
| xshadertoy_bestill4-0 | 12 (86) | 16 (64) | 34 (30) | 60 (17) |
| xshadertoy_downfall | 14 (74) | 18 (56) | 39 (26) | 60 (17) |
| xshadertoy_fluxcore | 23 (63) | 30 (47) | 50 (25) | 60 (17) |
| xshadertoy_noxfire | 18 (57) | 24 (43) | 51 (20) | 60 (17) |
| xshadertoy_polarnight | 14 (80) | 19 (61) | 39 (30) | 60 (17) |
| xshadertoy_rigrekt | 30 (34) | 39 (26) | 60 (17) | - |
| xshadertoy_skyline | 17 (72) | 22 (56) | 45 (27) | 60 (17) |
| xshadertoy_topologica | 37 (27) | 50 (20) | 60 (17) | - |
| xshadertoy_trizm | 54 (19) | 60 (17) | - | - |
| xshadertoy_universeball | 24 (42) | 32 (32) | 60 (17) | - |
