# Black Hole options

`blackhole_gles3` reads the same options from three places. Precedence, highest first:

1. command line `--option=value` (booleans also `--lensing` and `--no-lensing`)
2. environment `NCZ_BLACKHOLE_<OPTION>` (upper case, `-` written as `_`)
3. key file `$XDG_CONFIG_HOME/ncz-screensavers/blackhole.conf` (`~/.config/...` when unset), group `[blackhole]`, one `option=value` per line; `NCZ_BLACKHOLE_CONFIG` overrides the path
4. built-in defaults

An unknown option or an invalid value (wrong type, outside its range, not one of the choices) is reported once on stderr and ignored: the next lower source applies. `--help` and `--list-options` print everything with type, range and default. The declarative schema for settings UIs is `assets/screensaver-chooser/options/blackhole.tsv` (installed to `/usr/share/ncz-screensavers/options/`); a build test keeps it identical to what `blackhole_gles3 --dump-schema` prints.

Legacy names still work: `NCZ_BLACKHOLE_COLORS` sets `palette` (`stylised` is accepted), `NCZ_BLACKHOLE_FIXED_SEED` sets `seed`.

Examples:

```
blackhole_gles3 --palette=slingshot --flyby=slingshot --spin=0.95
blackhole_gles3 --palette=whitehole --cycle-palettes=slingshot,singularity --cycle-interval=60 --palette-transition=10
NCZ_BLACKHOLE_FLYBY=random NCZ_BLACKHOLE_DURATION=45 blackhole_gles3
printf '[blackhole]\npalette=singularity\nbloom=0.5\n' > ~/.config/ncz-screensavers/blackhole.conf
blackhole_gles3 --scene=merger --palette=synthwave
blackhole_gles3 --style=enhanced
NCZ_BLACKHOLE_SCENE=binary NCZ_BLACKHOLE_SEED=42 blackhole_gles3
```

## Look

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--preset` | string | `(empty)` |  | Named bundle of options (see --list-presets): applied above the saved config file and below environment and command line. Empty = none. |
| `--palette` | enum | `stylized` | stylized, kipthorne, faithful, singularity, slingshot, whitehole, eht, psychedelic, synthwave | Color model. stylized: original art-directed palette. kipthorne: symmetric amber blackbody (Interstellar). faithful: Doppler and redshift shifted blackbody. singularity: strict monochrome white on black (Singularity desktop). slingshot: rust disk with a gold-white beamed arc. whitehole: icy teal and steel blue. eht: Event Horizon Telescope false color (orange crescent). psychedelic: flowing rainbow bands that cycle through the spectrum. synthwave: indigo, magenta and hot pink with a cyan-white core. |
| `--palette-transition` | float | `8` | 0 to 120 | Seconds to cross-fade when the palette changes (palette cycling). 0 switches instantly. |
| `--cycle-palettes` | string | `(empty)` | stylized, kipthorne, faithful, singularity, slingshot, whitehole, eht, psychedelic, synthwave | Comma separated palettes to cycle through after the first one. Empty disables cycling. |
| `--cycle-interval` | float | `90` | 5 to 3600 | Seconds each palette stays before cross-fading to the next one in the cycle list. |
| `--exposure` | float | `1` | 0.2 to 4 | Overall brightness multiplier applied before tone mapping. |
| `--disk-brightness` | float | `1` | 0.2 to 3 | Surface brightness of the accretion disk relative to the per-launch value. |
| `--bloom` | float | `1` | 0 to 3 | Strength of the halo around the shadow, as a multiple of the palette's own halo (stylized, kipthorne and faithful have none). |
| `--fringe` | float | `0.15` | 0 to 1 | Cyan and magenta color fringing at the bright disk edges (whitehole palette). |
| `--beaming` | float | `1` | 0 to 2 | Strength of Doppler shift and relativistic beaming: 0 removes the bright/dim asymmetry, 2 doubles it. |
| `--star-density` | float | `1` | 0 to 3 | Multiplier on the number of background stars. |
| `--nebula` | float | `1` | 0 to 2 | Multiplier on the background nebula clouds. |
| `--jet` | float | `1` | 0 to 2 | Multiplier on the polar jets. 0 turns them off. |
| `--hot-sector` | float | `0.22` | 0 to 1 | Amplitude of the rotating bright sector in the stylized palette. |
| `--style` | enum | `classic` | classic, enhanced | classic: the original Black Hole look (default). enhanced: opt-in improvements from the 2026-09-29 review, each also settable on its own: the director camera for flyby=auto, luminance tone map, disk hot spots and spiral wave, photon ring, bright star layer, jet precession and occlusion, jet duty 0.75 and burn-in drift. Options set explicitly always win. |
| `--jet-duty` | float | `1` | 0 to 1 | Fraction of the time the jets are on; they fade in and out over a slow cycle (about 3 minutes). 1 = always on (classic default; enhanced uses 0.75). |
| `--drift` | bool | `false` |  | Slowly drift the composition (about 8 percent of the frame over several minutes) and add occasional dim passages, so no bright region stays on the same pixels (enhanced default: on). |
| `--photon-ring` | float | `0` | 0 to 2 | Brightness of a thin, pixel-filtered photon ring just outside the shadow (enhanced default: 1). |
| `--bright-stars` | float | `0` | 0 to 2 | Brightness of a sparse layer of bright stars that makes the lensing easy to follow (enhanced default: 1). |
| `--tonemap` | enum | `classic` | classic, luminance | classic: the original per-channel curve (default). luminance: compresses brightness and keeps the hue, so hot saturated regions stay colored with a pale core (enhanced default). |
| `--disk-structure` | float | `0` | 0 to 2 | Strength of the orbiting hot spots and the spiral density wave in the disk. 0 = the original disk (default); enhanced uses 1. |

## Scene

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--scene` | enum | `single` | single, binary, merger, tidal-disruption, galaxy-zoom, three-body | single: one black hole (default). binary: two black holes orbiting each other, each with its own disk, inside a circumbinary ring, with gravitational-wave ripples. merger: a binary spirals in and merges; the two shadows become one that rings down, and the shared disk falls in to feed the remnant, with no flash because a vacuum merger emits no light (repeats). tidal-disruption: a star is torn apart on a close pass and its debris wraps into a new, flaring disk (repeats). galaxy-zoom: a dive from a whole spiral galaxy down to the black hole in its nucleus (repeats). three-body: three black holes chasing each other along a figure eight. All scenes are artistic. |
| `--scene-quality` | enum | `auto` | auto, light, rich | light: thin-lens approximation for integrated GPUs; rich: every ray is integrated through the field of all holes. auto: light on weak GPUs, rich otherwise. |
| `--mass-ratio` | float | `0` | 0 to 1 | Mass of the lighter hole divided by the heavier one (binary and merger); 0 = random per launch or per merger. |
| `--separation` | float | `0` | 0 to 40 | Orbital separation in Schwarzschild radii of the heavier hole (binary, merger start, three-body size); 0 = scene default. |
| `--scene-cycle` | float | `0` | 0 to 3600 | Seconds per cycle of the repeating scenes (merger, tidal-disruption, galaxy-zoom); 0 = scene default (75, 110, 80). Values below 30 are raised to 30. |
| `--ripples` | float | `1` | 0 to 2 | Strength of the artistic gravitational-wave ripples (binary and merger). 0 turns them off. |

## Camera

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--flyby` | enum | `auto` | auto, orbit, slow-orbit, equatorial, polar, plunge, slingshot, drift, random, director, legacy | auto: the original per-launch path for the single hole in the classic style; the director for the scenes and the enhanced style (the galaxy zoom keeps its own camera). director: seeded observation chapters (establish, approach, reveal as the camera rises out of the disk plane, dwell, withdraw), never cutting, no roll. legacy: always the original per-launch random zoom-whirl or hyperbolic path with a tilted, precessing disk. orbit and slow-orbit: circular. equatorial: fly past along the disk plane. polar: over the poles. plunge: dive toward the horizon and out. slingshot: swoop in close on a hyperbolic arc and leave. drift: very slow wandering. random: pick one per cycle. |
| `--speed` | float | `1` | 0.1 to 4 | Time scale of the whole animation (camera, disk rotation, palette drift). |
| `--distance-min` | float | `0` | 0 to 60 | Closest camera distance in units of the Schwarzschild radius; 0 = per-path default. Values below 3.2 are raised to 3.2 so the camera never crosses the horizon. |
| `--distance-max` | float | `0` | 0 to 120 | Farthest camera distance; 0 = per-path default. |
| `--inclination` | float | `-1` | -1 to 90 | Elevation of the view above the disk plane in degrees; -1 = automatic. |
| `--duration` | float | `0` | 0 to 3600 | Seconds per flyby before it restarts with a new variation (with a short fade); 0 = the path's natural length. |

## Physics

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--spin` | float | `0` | 0 to 0.998 | Dimensionless spin a/M. Approximation: moves the inner disk edge inward and speeds up the disk (not full Kerr geodesics). |
| `--rotation-speed` | float | `1` | 0 to 3 | Multiplier on the disk rotation rate. |
| `--precession` | float | `1` | 0 to 4 | Multiplier on the disk's slow precession (auto camera path only). |
| `--temperature` | float | `1` | 0.3 to 1.6 | Multiplier on the disk heat, which shifts colors toward the hot end. |
| `--disk-outer` | float | `12` | 6 to 80 | Outer radius of the accretion disk in Schwarzschild radii. Presets use large values for quasars. |
| `--jet-length` | float | `22.2` | 5 to 250 | Length scale of the polar jets in Schwarzschild radii (brightness falls off exponentially). |
| `--torus` | float | `0` | 0 to 1 | Opacity of a dusty torus ring around the disk (quasar look). Artistic straight-ray approximation. |
| `--companion` | float | `0` | 0 to 1 | Show a blue companion star and the gas stream it feeds the disk with (microquasar look). Artistic. |
| `--lensing` | bool | `true` |  | Turn off to draw straight rays (debugging). |

## System

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--adaptive` | bool | `true` |  | Measure frame times and lower the ray-step budget when frames miss the display refresh, with hysteresis (only when quality is auto). Resolution is controlled by the shared render-scale options. |
| `--quality` | enum | `auto` | auto, low, medium, high, ultra | Ray-march step budget tier. auto picks by GPU and adapts to frame time. |
| `--seed` | int | `0` | 0 to 4.29497e+09 | Fixes the per-launch randomness so a run can be reproduced; 0 = random. |

## Performance notes

`quality=auto` (default) with `adaptive=true` adapts the ray-step budget to the display refresh; see `BLACKHOLE-STUTTER.md`. `NCZ_BLACKHOLE_PERF_LOG=2` prints frame-time statistics. Palette and flyby settings never recompile the shader: they are uniforms, and palette changes cross-fade over `palette-transition` seconds. Render resolution is a shared, generic option (`--render-scale`, `--max-render-height`, `--render-scale-mode`, see `RENDER-AND-GUARD.md`).

## Flyby camera paths

`auto` is the original per-launch path for the single hole in the classic style (default), and the director for the scenes and for `--style=enhanced`. The director (see `BLACKHOLE-SCENES-DESIGN.md`) plays seeded observation chapters (establish, approach, reveal as the camera rises out of the disk plane, dwell, withdraw) that never cut, with the disk normal as up (no roll) and distances derived from the disk size. The galaxy zoom keeps its own camera. `legacy` is the earlier per-launch random path (zoom-whirl or hyperbolic flyby, tilted and precessing disk). The other types are deterministic functions of time and the seed: `orbit`/`slow-orbit` (circular, one revolution per cycle of 60/180 s), `equatorial` (hyperbolic pass in the disk plane), `polar` (pass over the poles), `plunge` (dive to `distance-min` and out), `slingshot` (eccentricity 1.18, swings through about 285 degrees), `drift` (slow closed wandering), `random` (a type per cycle). Types that do not close on themselves fade through black for 1.2 s at each cycle boundary. The camera never goes closer than 3.2 Schwarzschild radii, and each path has its own default distance range (orbit 11 to 16, equatorial 6.5 to 30, polar 7.5 to 30, plunge 3.8 to 26, slingshot 4.6 to 34, drift 9 to 20).
