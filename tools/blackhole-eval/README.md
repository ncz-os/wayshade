# Black Hole evaluation tools

Scripts used to produce the measurements in `docs/PALETTES.md`, `docs/BLACKHOLE-STUTTER.md`, `docs/SPARSE-HACKS.md`, `docs/RENDER-SCALE-MEASUREMENTS.md` and the preset screenshots. They are a record and a starting point, not a supported interface.

* Run ON a host inside the user's Wayland session, under the host lock (`tools/host-lock.sh`), never on a CPU renderer:
  `bhm.sh` (palette x flyby matrix with frame dumps), `presets_run.sh` (all presets), `mali_run.sh` (palettes and presets on a Mali host), `o6sweep3.sh` (fps versus render size at the 4K operating points; needs a user session and the host lock; password auth only, SSHPASS from the environment), `classics.sh`, `chperf.sh` (frame pacing with the adaptive controller on and off).
* Run on a workstation with Python and Pillow on the collected frames: `pal_an.py` (pairwise per-pixel chroma distance and contact sheets), `cmp_sw.py` (luminance statistics and crops), `sheet.py`, `cov2.py` (tile coverage emulating the gate), `sweep_an.py`, `meas.py`, `hints.py` (writes `render-hints.tsv`), `docgen.py` (options table from the schema), `swatch.py`, `km.py` (k-means color sampling of a reference image), `metric_kip_faith.py`.
