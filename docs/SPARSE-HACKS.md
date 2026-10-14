# Sparse hacks: root-cause investigation (2026-09-29)

Six hacks miss the host-test tile rule (25 percent of 8x8 tiles lit) on every host:
`hyprsaver_geometry`, `hyprsaver_hypercube`, `hyprsaver_stonks`, `hyprsaver_tesla`,
`hyprsaver_gridwave` (O6N only) and `cubestorm_gles3` (build-host, O6N).

## How the gate measures

`tools/host-test-agent.py` captures the compositor with `grim -s 0.125`, so a 1536x960 frame
becomes 192x120, and then counts 8x8 tiles that contain a pixel above 16. Each tile therefore
stands for 64x64 native pixels, and thin lines are area-averaged before the threshold (a 3 px
line at full resolution becomes a value below 16 after the 1/8 box filter unless it is bright).
I reproduced the gate numbers offline from raw frames (`NCZ_FRAME_DUMP` on build-host, radeonsi,
package 0.3.3, 13 frames per hack, box downscale by 8): they match the reported matrix
(geometry 0.23 vs 23 percent, gridwave 0.32 vs 33, tesla 0.19 vs 20, stonks 0.22 vs 23,
hypercube 0.26 vs 22, cubestorm 0.22 vs 23). Frame-to-frame variation is large (per-hack
min to max below), so a single sample lands on either side of 25 percent.

```
cubestorm            gate-style (1/8 downscale): tiles 0.22 (min 0.11 max 0.35) lit-px 0.167 | native: tiles 0.17 lit-px 0.129
hyprsaver_geometry   gate-style (1/8 downscale): tiles 0.23 (min 0.15 max 0.27) lit-px 0.071 | native: tiles 0.09 lit-px 0.066
hyprsaver_gridwave   gate-style (1/8 downscale): tiles 0.32 (min 0.24 max 0.38) lit-px 0.086 | native: tiles 0.12 lit-px 0.048
hyprsaver_hypercube  gate-style (1/8 downscale): tiles 0.26 (min 0.22 max 0.31) lit-px 0.073 | native: tiles 0.08 lit-px 0.051
hyprsaver_stonks     gate-style (1/8 downscale): tiles 0.22 (min 0.22 max 0.23) lit-px 0.061 | native: tiles 0.14 lit-px 0.056
hyprsaver_tesla      gate-style (1/8 downscale): tiles 0.19 (min 0.17 max 0.21) lit-px 0.070 | native: tiles 0.09 lit-px 0.067
```

`native` is the same computation on the full-resolution frame (8x8 native pixels per tile).

## Findings per hack

| Hack | Host(s) failing | Root cause | Verdict | Fix |
|---|---|---|---|---|
| hyprsaver_geometry | all four (tiles 17-23 percent) | Lines-only wireframe. Was also genuinely too dim (Reinhard capped a lone edge at 50 percent), fixed in 0.3.2 (tiles 13 to 18 percent before, 23 after on build-host). Remaining miss is sparsity. | fixed bug; now legitimately sparse | done in 0.3.2 |
| hyprsaver_hypercube | all | Tesseract wireframe, 6 px lines, already tone-mapped and gamma corrected. Correct output. | legitimately sparse | annotate |
| hyprsaver_stonks | all | Candlestick chart on black; candles cover a strip of the screen. Correct output. | legitimately sparse | annotate |
| hyprsaver_tesla | all | Three electrodes and thin arcs; a glow halo but most of the frame is dark. Correct output. | legitimately sparse | annotate |
| hyprsaver_gridwave | O6N (24 percent) | Floor grid under a black sky (the top half is empty by design). Passes on x86 hosts at 27-33 percent, misses on O6N by one point; renders identically run to run. Shader unchanged by 0.3.2. | legitimately sparse | annotate |
| cubestorm_gles3 | build-host, O6N | Upstream xscreensaver cubestorm: a tube of wire cubes, perspective 30 degrees at distance 45, matching the original code (`gluPerspective (30.0, 1/h, ...)`); the tube wanders and covers 11 to 35 percent of tiles over time. | legitimately sparse, time varying | annotate |

Checked and ruled out as causes: wrong uniforms, resolution scale (line widths are in
screen-height units and scale with the output), aspect ratio (cubestorm projection matches
upstream), missing clear (frames are clean black), alpha/blend state (all write opaque alpha 1),
and GPU differences (the scenes look the same on radeonsi, iris and Mali; the per-host
spread in the matrix is the panel resolution and the moment of the capture).

## Proposed gate rule

`assets/screensaver-chooser/sparse.tsv` lists the six ids with a tile floor of 0.10. A listed
hack passes when it is healthy, moving, non-black and its lit-tile coverage is at least its
floor. Measured minima over a run are 0.11 to 0.24 (cubestorm is lowest at 0.11, just above the floor), an empty or
black frame scores 0, so the floor still separates a working sparse scene from a broken one.
`hacks.tsv` stays at four columns because the Vala chooser plugin requires exactly four.
The gate change itself belongs to `tools/host-test-agent.py` (LEAD 2).

Screenshots: `docs/hyprsaver-shots/sparse-hacks-build-host-2026-09-29.jpg`.
