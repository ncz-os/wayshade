# Options, presets, seeds and palettes

How a saver's options are declared and resolved, what a preset is, how the
randomize-everything mode turns a seed into a look, and the palette systems.
The harness that reads all this is described in
[architecture.md](architecture.md); the classes and thresholds in
[GPU-CLASSES.md](GPU-CLASSES.md).

### Per-hack options

Every shader hack and every in-house engine has a schema in
`assets/screensaver-chooser/options/<hack>.tsv`. Columns:

| column | meaning |
|---|---|
| `name` | option name, used on the command line |
| `type` | `bool`, `int`, `float`, `enum`, `string` |
| `default` | default value |
| `min`, `max` | numeric bounds (omitted when the option is an enum or bool) |
| `choices` | comma-separated enum values |
| `label`, `desc` | UI labels |
| `group` | UI sub-grouping |
| env name | `NCZ_<HACK>_<OPTION>` |

Sources of values, lowest to highest precedence: built-in defaults <
`$XDG_CONFIG_HOME/ncz-screensavers/render.conf` (or
`~/.config/ncz-screensavers/render.conf`) < preset key file <
environment (`NCZ_<HACK>_<OPTION>`) < command line (`--option=value`).
Unknown options or out-of-range values produce a single warning on stderr
and are ignored — the next lower source applies.

The per-saver schema is the truth: a build test in `meson.build`
(`blackhole-schema-in-sync`, `xshadertoy-options-in-sync`,
`magmasimplex-schema-in-sync`, etc.) regenerates the schema from each
binary and fails when the file drifts.

### Scenes and presets

Scenes are key files: `assets/screensaver-chooser/options/presets/<hack>/<name>.conf`
with `[preset]` (name, description, accuracy, min-gpu-class) and
`[<hack>]` value blocks. A hack with a scene scheme takes `--preset=<name>`
or `<hack>_gles3--preset=NAME` from the launcher.

### Randomize-everything mode

`src/xst_core.c` is the option-parser / scene-driver / palette / director
shared between the xshadertoy and wave-2-materia shaders. The `randomize`
boolean (default `true`) is the central switch: when `true`, every shader
option that has a `rand` range and that nobody set (config file, scene,
environment or command line) is drawn from the seed inside that range;
when `false`, the built-in defaults apply and `palette=auto` means the
native palette. The seed splits into independent streams keyed by name
(geometry, palette, director, one per randomized option), so adding an
option never changes the palette or geometry of an existing seed.

`randomize=true` is the **shipped default** of every xshadertoy shader:
launch the picker and you'll see randomized, reproducible, within-coherent-
range looks. Pass `--randomize=false` for the native look.

### Palettes

Black Hole has ten palettes (see [`PALETTES.md`](PALETTES.md)): `stylized`, `kipthorne`,
`faithful`, `singularity`, `slingshot`, `whitehole`, `eht`, `psychedelic`,
`synthwave`, and `cygnus`. `cygnus` is the NCZ-OS 26.10 default; the
deprecated key `NCZ_BLACKHOLE_COLORS=cygnus` selects it for older
launchers that read the legacy `blackhole-color-mode` key.

The xshadertoy driver carries 45 role palettes in `src/xst_palettes.h`
grouped into three (high-contrast, mid-contrast, low-contrast) classes with
contrast constraints that `tests/test_xst_palettes.c` enforces.
`palette=auto` draws among palettes whose tags match the saver's `family`
(from the `//@saver` line in the shader source); `palette=native` uses
the `//@palette-native` line; `palette-cycle=N` cross-fades to another
palette of the family every N minutes; the fade runs in Oklab so
lightness moves monotonically.

## The tier system in brief

The full description of the classes, the calibration benchmark, the
thresholds and the per-saver tier table is on
[GPU-CLASSES.md](GPU-CLASSES.md). In short:

Each GPU is calibrated once (`ncz-screensaver calibrate`, about 3 seconds)
and cached per GPU id (`pci-0000_00_02_0`) in
`$XDG_CACHE_HOME/ncz-screensavers/gpu-class.json`. The reference benchmark
is a 160-step bent-ray marcher rendered into a 1920x1080 RGBA8 target,
timed with a GL timer query and a wall-clock fallback
(`launcher/ncz-screensaver-calibrate.c`). The run takes about two seconds,
creates no window and reads nothing back. It uses whatever GPU the
environment selects, and never a software renderer: llvmpipe, softpipe and
swrast exit with status 3.

### Thresholds

| Class | Reference time | Examples |
|---|---|---|
| weak | 20 ms or more | Intel UHD/HD/HD Graphics, VideoCore, Mali-G31 / Mali-G52, Adreno 3xx-5xx |
| mid | 8 to 20 ms | Mali-G57, Mali-G6xx/G7xx and Immortalis, Iris Xe, Radeon Graphics (Vega), Apple |
| strong | under 8 ms | discrete NVIDIA, discrete Radeon RX / Navi, Apple M-series, Intel Arc A5xx and A7xx |

The thresholds live in `CLASS_WEAK_MS` and `CLASS_MID_MS` in
`launcher/ncz-screensaver`. Detection uses a renderer-string table
(`RENDERER_TABLE`) first; the table spells out each class's first-match
regex, narrowing the Mali family so G31/G52 sit at weak and
G610/G615/G620/G720 and Immortalis sit at mid; on a renderer miss the
launcher falls back to driver + VRAM (a DRM card bound to `mali`,
`mali_kbase`, `panthor`, `panfrost`, `msm`, or `amdgpu` is mid; `i915`,
`xe`, `nouveau` go through the renderer string).

### How a saver learns its tier

`assets/screensaver-chooser/tiers.tsv` carries one row per hack with the
minimum class, fps/p95 ms on Intel UHD 630 at native resolution, on UHD
630 at the weak-class default (render scale 0.5 for shader hacks,
native for the others), on Mali-G720, on AMD Navi14, on RTX 2060 (`-`
when not measured), date and commit. The tier rule:

- **weak-ok**: at least 30 fps with p95 ≤ 40 ms on the UHD 630 at the
  weak-class default and on the Mali-G720.
- **mid-ok**: not weak-ok, but the same holds on the Mali-G720 at its
  platform defaults (1080 cap on Sky1, per-hack render hints from
  `render-hints.tsv`).
- **strong-only**: everything else.

### What the chooser does

A hack the system's best usable GPU cannot carry is `flagged` in
`ncz-screensaver list --json`. The chooser UI shows "Works well on this
graphics chip" first and lists flagged hacks under "May run poorly on
this graphics chip", with a warning badge, the measured expectation
("about 12 fps on Intel UHD 630") and a tooltip. Previewing or selecting
a flagged hack asks for a confirmation.

Random and playlist pools follow `pool-gpu-class`: `auto` (weak system
gets weak-ok only, mid gets weak-ok and mid-ok, strong gets everything),
`weak`, `mid`, `all` (opt in to flagged hacks). Hacks listed in
`broken.tsv` are excluded in every case.
## Where to read next

- [GPU-CLASSES.md](GPU-CLASSES.md) — the classes, thresholds and tier table.
- [PALETTES.md](PALETTES.md) — the palette list in full.
- [savers.md](savers.md) — the catalogue these options apply to.
