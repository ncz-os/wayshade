# Black Hole presets

A preset is a named bundle of Black Hole options. Run one with `blackhole_gles3 --preset=ID`; list them with `blackhole_gles3 --list-presets` (id, name, accuracy, description); see what a run will use with `--print-config`.

Precedence, lowest to highest: built-in defaults, `blackhole.conf`, **the preset**, `NCZ_BLACKHOLE_<OPTION>` environment, command line. An explicit preset beats the saved defaults, and anything you set on the command line or in the environment still wins (`--preset=kerr --spin=0.7` is Kerr with spin 0.7). An unknown preset id is reported and ignored.

## Files

* Sources of truth: `assets/screensaver-chooser/options/presets/blackhole/<id>.conf`, installed to `/usr/share/ncz-screensavers/presets/blackhole/`. Also searched: `$NCZ_PRESET_DIR`, `~/.local/share/ncz-screensavers/presets/blackhole/`, so users can add their own.
* Format (key file): a `[preset]` section with `name`, `description`, `accuracy` (`faithful` or `artistic`) and `sources`; then a `[blackhole]` section of option values. The generator validates every value against `options/blackhole.tsv` (type, range, choices).
* Catalog rows: `tools/generate-catalog.py` writes `assets/screensaver-chooser/presets.tsv` (installed to `/usr/share/ncz-screensavers/` and `/usr/share/ncz-screensaver-chooser/`), 9 tab-separated columns: `id` (row id, `blackhole_gles3--<preset>`), `title` ("Black Hole: M87*"), `group`, `hack` (binary id), `args` (`--preset=<id>`), `accuracy`, `description`, `min_gpu_class` (`weak`, `mid` or `strong`), `measured` (fps on the reference GPUs). `hacks.tsv` stays exactly four columns; a launcher or chooser lists these rows next to the hack rows and starts `hack args`, so each preset can be picked individually, put in the random pool or a playlist. A build test regenerates the file and fails on drift.

## Accuracy labels

`faithful`: geometry, viewing angle and colors follow published measurements (numbers below). `artistic`: physically motivated but scaled or stylized for viewing; the description says how. In every case the ray tracing is Schwarzschild; the `spin` option changes the disk edge and speed only (an approximation, see the design doc).

| Preset | Accuracy | What it shows | Numbers used |
|---|---|---|---|
| sagittarius-a-star | faithful | small hot flow, orange crescent, thin photon ring, seen 30 degrees from the pole | 4.3e6 solar masses, 8.28 kpc, shadow about 52 microarcseconds (EHT 2022, GRAVITY 2019); r_s = 2.95 km per solar mass = 1.27e7 km |
| m87-star | faithful | bright ring, brighter on one side, long jet, 17 degrees from the pole | 6.5e9 solar masses, 16.8 Mpc, shadow about 42 microarcseconds (EHT 2019); r_s about 130 AU |
| ton-618 | artistic | hyperluminous quasar: enormous disk, twin jets, dusty torus | about 4e10 solar masses (estimates 4 to 6.6e10), r_s about 790 AU, luminosity about 4e40 W; scene scaled |
| quasar | artistic | bright disk, dusty torus, twin jets, viewed 25 degrees off the pole | unified model (Antonucci 1993, Urry and Padovani 1995) |
| quasar-edge-on | artistic | the same nucleus seen through the torus plane: disk hidden, jets to the sides | same |
| blazar | artistic | jet pointing at us, strong beaming, disk face-on | Urry and Padovani 1995 |
| microquasar | artistic | stellar-mass hole, blue supergiant companion, gas stream into the disk | Cygnus X-1: 21.2 and 40.6 solar masses, inclination 27 degrees, 2.2 kpc, period 5.6 d (Miller-Jones 2021); the real system is wind fed, and the star and stream are drawn unlensed |
| kerr | artistic | near-maximal spin: disk edge close to the horizon, strongly beamed; since 2026-09-29 the shadow follows the analytic Kerr outline (Bardeen 1973) for the spin and viewing angle | Bardeen, Press and Teukolsky 1972 (ISCO 3 r_s at spin 0 to about 0.6 r_s at 0.998, prograde) |
| binary | artistic | two holes with minidisks inside a circumbinary ring, mutual lensing, gravitational-wave ripples | Kepler orbit; see `BLACKHOLE-SCENES-DESIGN.md` |
| binary-quasars | artistic | a feeding supermassive pair in faithful colors, mass ratio 0.35, seen from above | OJ 287 as the motivating candidate |
| merger | artistic | inspiral, coalescence of the shadows, ringdown, the disk refilling; no flash (vacuum mergers emit no light) | Peters 1964 inspiral, GW150914 energy budget |
| tidal-disruption | artistic | a star torn apart at pericenter; the debris wraps into a flaring disk | Barker's equation, fallback ~ t^(-5/3) |
| galaxy-zoom | artistic | a dive from a whole spiral galaxy to the hole in its nucleus | exponential disk, logarithmic arms, Gaussian bulge |
| three-body | artistic | three equal holes on the figure-eight choreography | Moore 1993, Chenciner and Montgomery 2000 |
| psychedelic | artistic | the psychedelic palette cross-fading to synthwave | art direction |

`inclination` in the files is the elevation above the disk plane, so "30 degrees from the pole" is 60. New extras used by presets: `disk-outer` (disk size in r_s), `jet-length`, `torus` (opacity of the dusty ring, a straight-ray approximation) and `companion` (star and stream, artistic). The `eht` palette (Event Horizon Telescope false color) is the seventh palette.

## Screenshots

`docs/blackhole/presets-*.jpg`: all eight presets at four times each.

## GPU classes and measured performance

Every preset declares `min-gpu-class` (the weakest GPU class that still runs it at the bar of 30 fps with a 95th percentile frame time under 40 ms) and `measured`. Classes: `weak` (Intel UHD 630 reference), `mid` (Mali-G720 reference), `strong` (Radeon Pro 5500M, RTX 2060 class). The class comes from `NCZ_GPU_CLASS` when the launcher sets it (from its calibration), otherwise from GL_RENDERER (Intel and CPU renderers weak, Mali mid, everything else strong).

Weak-class defaults (used unless the user sets the options): render scale 0.5 (960x540 upscaled to 1080p by the compositor), the ray-step budget at its lowest, no bloom, half the nebula and star density, torus opacity x0.6. Measured on a UHD 630 + RTX 2060 laptop (Intel UHD 630, Mesa iris, 1920x1080, clean timing, 26 s per run, no readbacks):

| Preset | weak (UHD 630) fps, p95 | mid (Mali-G720, 1080p) | strong (Navi14) | min-gpu-class |
|---|---|---|---|---|
| default (no preset) | 49, 21.5 ms | 59 | 60 | weak |
| sagittarius-a-star | 54, 18.6 ms | 60 | 60 | weak |
| m87-star | 54, 18.7 ms | 60 | 60 | weak |
| ton-618 | 49, 20.6 ms | 60 | 60 | weak |
| quasar | 48, 20.9 ms | 60 | 60 | weak |
| quasar-edge-on | 50, 20.1 ms | 60 | 60 | weak |
| blazar | 49, 20.7 ms | 60 | 60 | weak |
| microquasar | 52, 19.6 ms | 60 | 60 | weak |
| kerr | 53, 19.2 ms | 60 | 60 | weak |

The eight original presets and Black Hole itself clear the bar on the weakest reference GPU at the weak-class defaults, so all are `weak`. The RTX 2060 was not measured here. Scene presets (2026-09-29, `--scene`). on a UHD 630 + RTX 2060 laptop these were measured with the weak-class light mode; on Mali-G720 (Orion O6N, mali_kbase) with the rich mode that the mid class uses, with light mode for comparison; and on the RTX 2060 via per-process PRIME offload. Runs were 26 s with seed 42 and no readbacks. The merger, TDE and galaxy runs start 35, 40 and 30 s into their cycle, so they cover the heavy phases:

| Preset | weak (UHD 630, 960x540, light) fps, p95 | mid (Mali-G720, 1080p, rich) fps, p95 | strong (RTX 2060) | min-gpu-class |
|---|---|---|---|---|
| binary | 60, 16.8 ms | 52, 19.9 ms (light 60) | 60 | weak |
| binary-quasars | 60, 16.8 ms | 53, 19.5 ms (light 60) | 60 | weak |
| merger | 60, 16.9 ms | 56, 19.7 ms (light 60) | 60 | weak |
| tidal-disruption | 37, 33.5 ms | 60, 16.7 ms | 60 | weak |
| galaxy-zoom | 45, 33.5 ms | 58, 18.7 ms | 60 | weak |
| three-body | 60, 16.8 ms | 46, 22.4 ms (light 60) | 60 | weak |
| psychedelic | 52, 33.5 ms | 60, 16.7 ms | 60 | weak |

Every scene clears the weak bar at the weak-class defaults, so none is strong-only (the design-doc estimate was strong-only). Forcing the rich mode on the UHD 630 fails for the multi-hole scenes: about 42 fps, but p95 is 50 ms. At native 1080p on the UHD 630 the tidal disruption (10 fps) and the galaxy zoom (18 fps) fail; the weak class always renders at scale 0.5. `tiers.tsv` carries the preset rows (`tools/measure-tiers.py`, merged per id).
