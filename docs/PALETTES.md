# Palettes

Two palette systems ship in 0.8.9: the Black Hole colour models, and the
role palettes the xshadertoy family draws from.

## Black Hole

Ten colour models, selected with the `palette` option and listed in
`assets/screensaver-chooser/options/blackhole.tsv`:

| Palette | What it looks like |
|---|---|
| `stylized` | The original art-directed palette. The default. |
| `kipthorne` | Symmetric amber blackbody, the *Interstellar* look. |
| `faithful` | Blackbody shifted by Doppler and redshift. |
| `singularity` | Strict monochrome white on black. |
| `slingshot` | Rust disk with a gold-white beamed arc. |
| `whitehole` | Icy teal and steel blue, with cyan and magenta fringing at the bright disk edges. |
| `eht` | Event Horizon Telescope false colour: an orange crescent. |
| `psychedelic` | Flowing rainbow bands that cycle through the spectrum. |
| `synthwave` | Indigo, magenta and hot pink with a cyan-white core. |
| `cygnus` | The NCZ-OS 26.10 palette: magenta and cyan on violet-black. |

Related options:

| Option | Default | Meaning |
|---|---|---|
| `palette` | `stylized` | The colour model. |
| `cycle-palettes` | empty | Comma-separated list of palettes to cycle through after the first one. Empty disables cycling. |
| `palette-transition` | 8 | Seconds to cross-fade between palettes. 0 switches instantly. |
| `cycle-interval` | 90 | Seconds each palette stays before the cross-fade. |
| `exposure` | 1 | Overall brightness, applied before tone mapping. |
| `disk-brightness` | 1 | Surface brightness of the accretion disk relative to the per-launch value. |
| `temperature` | 1 | Multiplier on the disk heat, which shifts colours toward the hot end. |

The presets in `assets/screensaver-chooser/options/presets/blackhole/`
are named looks built from these options; `ncz-screensaver list` shows
them with their description, and `blackhole_gles3 --list-presets` prints
the same list on the command line. Preset rows carry their own minimum GPU
class, because some scenes are heavier than the default single-hole path.

The legacy key `blackhole-color-mode` in the `dev.ncz.screensaver`
schema still selects a Black Hole palette for older front ends; the
`palette` option is the supported path.

## xshadertoy role palettes

`src/xst_palettes.h` carries 45 role palettes in three contrast classes
(high, mid, low). Contrast constraints between them are enforced by
`tests/test_xst_palettes.c`.

| Option | Default | Meaning |
|---|---|---|
| `palette` | `auto` | `auto` draws a palette whose tags match the saver's `family`; `native` uses the `//@palette-native` declaration in the shader source; any other value names a table entry. |
| `palette-cycle` | 0 | Cross-fade to another palette of the same family every N minutes. 0 disables cycling. |

The cross-fade runs in Oklab, so lightness moves monotonically instead of
dipping through grey at the midpoint.

## Magmasimplex

Magmasimplex picks a palette era and a palette with two options:

| Option | Default | Meaning |
|---|---|---|
| `palette-era` | `auto` | Which era of the palette set to draw from. |
| `palette` | `auto` | The palette itself, or `auto` to choose one. |
| `colourway` | -1 | Pin one numbered colourway; -1 keeps the automatic choice. |
| `poster` | `false` | Poster-art treatment of the frame. |

## Fractasia

Fractasia selects a palette and can drift its hue:

| Option | Default | Meaning |
|---|---|---|
| `palette` | `random` | One of the named palettes, or `random` to pick per run. |
| `hue-drift` | 1 | How far the hue wanders over the run. |
| `exposure` | 1 | Overall brightness. |

## Hyprsaver shaders

The hyprsaver family carries its palette inside each vendored `.frag`
file: the driver injects a baked lookup table and the shader's own
`palette()` call resolves the colour for a given parameter. There is no
per-saver palette option; the look is whatever the shader draws. The
driver's notes on the injected lookup table are in
`src/gles3_hyprsaver.c`.