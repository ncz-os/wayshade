# xshadertoy options, seed, palette and director (xshadertoy_opts)

Every xshadertoy is one binary built from the generic driver `src/gles3_xshadertoy.c` and one
`.glsl` file. This mechanism gives each one settings, scenes, a reproducible seed, a palette and
slow structural evolution without a driver per saver. A shader opts in by carrying `//@` lines;
a shader without them runs exactly as before.

## Declaring options in the shader

```glsl
//@saver id=physarum version=2 family=dark|ink|botanical director=80
//@palette-native bg=#0e1526 body=#e9dcc6 accent=#f4e6cc accent2=#8a7058
//@option name=density type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Filament density" desc="How much of the frame the forms cover."
#include "xst_common.glsl"
```

| Directive | Keys |
|---|---|
| `//@saver` | `id` (defaults to the file name), `version`, `family` (palette tags, `\|` separated), `director` (seconds between keyframes, default 90) |
| `//@palette-native` | `bg`, `body`, `accent`, `accent2` as `#rrggbb`: the saver's own colors (`palette=native`) |
| `//@option` | `name`, `type` (`float`, `int`, `bool`, `enum`), `default`, `min`/`max` (numbers), `choices` (enum, comma separated), `rand` (see below), `group`, `label`, `desc`, `bind` |
| `//@+` | continues the previous directive |

`rand` is the coherent range randomize draws from: `lo:hi` for numbers, `a|b` for an enum
subset, a probability for a bool. It is narrower than the full range on purpose.

Every opted-in saver also gets the common options, in this order: `preset`, `palette`,
`palette-cycle`, `speed`, `evolution`, then the shader's own, then `randomize`, `quality`, `seed`.

## How values reach the shader

- `#define OPT_<NAME> value` for every shader option (`-` becomes `_`), placed right after
  `#version`, so an option costs nothing at run time and loop counts fold. Enums define the
  selected index plus one constant per choice (`OPT_MODE_STORM 1`); bools are `0`/`1`.
- `bind=uName.x` sends the value to component `x` of a `vec4` uniform instead (an enum sends its
  index). This matches drivers that pack options into uniforms (leviathan's `uLv0..uLv2`).
- `#define NCZ_QUALITY 0|1|2`: the `quality` option, or the GPU class when it is `auto`
  (`NCZ_SHADER_QUALITY` still overrides).
- `uniform vec3 uPal[4]`: palette roles in linear RGB (background, body, accent, accent2).
- `uniform vec4 uDir[4]`: the director (below).
- `iSeed`: four floats in [0,1) from the run seed (the geometry stream).
- `iTime` is motion time: wall time times `speed`.

## Resolution, presets and the schema

The driver implements the harness hook `ncz_hack_options()`, so the options go through the same
chain as every hack: defaults < `~/.config/ncz-screensavers/xshadertoy_<id>.conf` < preset <
environment (`NCZ_XSHADERTOY_<ID>_<OPTION>`) < command line (`--option=value`). Unknown options
exit with status 2. `--dump-schema`, `--list-presets`, `--print-config`, `--help` and
`--list-options` work as for Black Hole.

Scenes are key files, `assets/screensaver-chooser/options/presets/xshadertoy_<id>/<name>.conf`
(`[preset]` name, description, accuracy, min-gpu-class, then `[xshadertoy_<id>]` values), read by
the hack with `--preset=<name>`. `tools/xshadertoy-gen-options.py` writes
`options/xshadertoy_<id>.tsv` (the `--dump-schema` output), `options/xshadertoy_<id>.json`
(docs/OPTIONS-SCHEMA.md, with `choice_groups` and `choice_labels` for the palette) and one JSON
per scene; the meson test `xshadertoy-options-in-sync` fails when they are stale or when a scene
names an unknown option or a value the hack rejects.

## Seeds and randomize

`seed` 0 draws a new seed in 1..999999; the seed in use is always logged. The seed splits into
independent streams, each keyed by name (geometry, palette, director, and one per randomized
option), so adding an option never changes the palette or geometry of an existing seed.

With `randomize=true` (default) every shader option that has a `rand` range and that nobody set
(config file, scene, environment or command line) is drawn from the seed inside that range. With
`randomize=false` the defaults apply and `palette=auto` means the native palette.

## Palette

`src/xst_palettes.h` holds 45 role palettes in three classes with contrast constraints that
`tests/test_xst_palettes.c` enforces (see the header). `palette=auto` draws among palettes whose
tags match the saver's `family`; `native` uses `//@palette-native`; any table name is accepted.
`palette-cycle=N` cross-fades to another palette of the family every N minutes; the fade runs in
Oklab, so lightness moves monotonically instead of dipping through grey.

## Director

The director gives every saver structural change over minutes that is reproducible and never
jumps. It is computed on the CPU per frame from the director stream and uploaded as `uDir[4]`:

- `uDir[0..1]`, eight slow parameters in [0,1]: seeded keyframes, one every `director` seconds
  of motion time divided by `evolution` (each channel with its own period of 0.8 to 1.25 times
  that and its own offset, so channels never change together), joined by the quintic
  6u^5 - 15u^4 + 10u^3 (zero first and second derivative at the keys);
- `uDir[2..3]`, eight phases: each the integral over motion time of a rate that moves between
  0.5 and 1.5 through the same kind of keyframes. The integral is evaluated in closed form
  (the quintic integrates to u^6 - 3u^5 + 2.5u^4), so a changing frequency changes the phase
  speed without a jump, and phase(0) = 0. A shader scales a phase by its own constant.

Nothing depends on frame history: a frame at motion time t is the same whether it is reached
live or rendered directly, which is what makes the contact sheets and the seed exact.
`evolution=0` holds the parameters at their opening values.

## Shared shading (`vendor/xshadertoy/glsl/xst_common.glsl`)

Aspect-correct coordinates (`xst_uv`), integer-hash gradient noise with analytic derivatives
(`xst_noised`) and a curl from those derivatives (`xst_curl`, one noise evaluation instead of
four), fwidth coverage for contours (`xst_aastep`, `xst_aaband`), octave fading below the pixel
footprint (`xst_octave_fade`), and one output path (`xst_output`): a luminance highlight shoulder,
the sRGB transfer, and a static zero-mean dither. Shade in linear light and call `xst_output`
once.

## Logging and measurement

At the first frame the driver prints one line:

    [xshadertoy] saver=physarum version=2 src=8b4e4852 seed=4242 preset=calligraphy palette=navy-cream quality=2 res=1920x1080 options: ... density=0.42 ...

(`src` is a hash of the shader text; a `*` marks a randomized value.) With
`NCZ_XST_PERF_WINDOW=N` it also prints frame-interval percentiles for every N-second window.

`xshadertoy_render` (built, never installed) renders any family shader headless with the same
code: frames at exact times (`--out DIR --times 5,20,45`) and a sustained GPU bench
(`--bench SECONDS --window 60`). `tools/xshadertoy-contact-sheet.py` builds same-seed
before/after sheets from it; `tools/xshadertoy-perf-gate.py` runs the performance gates
(docs/XSHADERTOY-PERF.md).

## Honesty

These savers are stateless single-pass fragment shaders. Settings and a seed do not make a
simulation: where a shader is named after a physical process, its header and docs say whether it
is an analytic model (thin-film interference, membrane modes) or a procedural approximation of
the look (physarum, caustics).
