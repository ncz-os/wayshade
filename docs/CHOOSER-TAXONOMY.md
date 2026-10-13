# Chooser taxonomy and simple modes

NCZ-OS 26.10 "Cygnus". How the Settings page ("Screensaver and Lockscreen", the
Singularity plugin in `plugin/screensaver/`) groups the 74 shipped screensavers,
how the Black Hole simple mode maps six sliders onto its 55 options, and how to
add a screensaver or a category. Everything here is data under
`assets/screensaver-chooser/`; `tools/generate-catalog.py` validates it at build
time and `tools/tests/test_chooser_data.py` pins the invariants.

## Why a style taxonomy and an engine filter

Before 0.8.3 the page was one long scroll: every hack as a row, grouped only by
engine (30 xshadertoy, 30 hyprsaver, 11 Classics, Black Hole, Fractasia), with
every hack's option groups appended below. People choose a screensaver by what it
looks like, not by which renderer draws it, so the primary grouping is now a
**style category** and the engine is a **secondary filter**. The two are
orthogonal: a Classics port can be "Geometry", a hyprsaver shader can be
"Nature", and the engine chips (All / In-house / Shader / hyprsaver / Classics)
narrow any category.

Layout choice: the Singularity settings panel is a narrow sidebar (about 450
logical pixels on the lab boards), so a two-pane category list + grid would
leave no room for either pane. The chooser is a single column of
collapsed-by-default category expanders, with a search entry and the engine
segmented control above them, and Favourites and Recently used pinned (expanded)
at the top. Search expands the sections that match and collapses them again when
cleared.

Each hack row carries, left to right: a check (the one screensaver, in "One
screensaver" mode), the title and engine, a GPU badge from `tiers.tsv` (Light /
Mid / Heavy, with the tooltip spelling out the class it needs), a star
(favourite), a gear (options, only when the hack has a schema), a play button
(preview, 30 s) and a switch (part of the random rotation, in "Random rotation"
mode). The row subtitle still carries the flagged warning and known issues. Every
icon button has an accessible label and a tooltip; everything is keyboard
reachable. A category's "more" button adds the whole group to the rotation,
removes it, or makes it the only group that rotates.

## Categories

`assets/screensaver-chooser/category-list.tsv` (id, label, description; the file
order is the display order):

| id | label | description |
|---|---|---|
| `space` | Space & Cosmos | Black holes, planets, star fields and deep space |
| `fractal` | Fractals & Math | Self-similar structure, curves and surfaces from mathematics |
| `neon` | Neon & Synthwave | Glowing lines, electric arcs and saturated colour on black |
| `nature` | Nature & Weather | Water, clouds, fire, snow, aurora and landscapes |
| `fluid` | Abstract & Fluid | Flowing colour, soft shapes and slow abstract motion |
| `geometry` | Geometry & Wireframe | Polyhedra, lattices, cells and clean geometric form |
| `retro` | Retro, Terminal & Data | Terminals, scopes, instruments and streams of data |
| `scenes` | Cities & Scenes | Skylines, interiors and journeys through a place |

`assets/screensaver-chooser/categories.tsv` (id, category, origin, tags) places
every shipped hack in exactly one primary category. `origin` is `ncz` for
in-house work and `ported` for shaders and hacks adapted from other projects
(the "In-house" engine chip shows the `ncz` rows). The validator refuses an
`xshadertoy_*` row marked `ncz` whose shader header does not say "original
work", and any `hyprsaver_*` or Classics row marked `ncz`. Black Hole is marked
`ncz`: it started from a Schwarzschild raytracer (`vendor/blackhole-PORTED.md`)
but the shipped program (scenes, director camera, palettes, 55 options) is this
project's work; the attribution stays in PORTED.md.

| Category | Screensavers (74) |
|---|---|
| Space & Cosmos (8) | Black Hole (ncz), Planet, Starfield, Wormhole, Alienbeacon, Batteredplanet, Darktransit, Starnest |
| Fractals & Math (10) | Fractasia (ncz), Goldenapollian, Logarithmiccircles, Truchetzoom, Protophore, Hypertorus, Klein, Projectiveplane, Lissajous, Bezier |
| Neon & Synthwave (5) | Neongravity 1, Neontriangulator, Hexplasma, Tunnel, Arc Coil |
| Nature & Weather (15) | Aurora, Clouds, Fireflies, Flames, Snowfall, Waterfall, Caustics, Bestill 2.0, Bestill 3.0, Bestill 4.0, Bestill 5.0, Prococean, Polarnight, Downfall, Crackberg |
| Abstract & Fluid (14) | Blob, Plasma, Marble, Kaleidoscope, Bestill 0.0, Bestill 1.0, Noxfire, Rigrekt, Trizm, Topologica, Elementalring, Trainmandala, Noof, Magmasimplex (ncz) |
| Geometry & Wireframe (9) | Geometry, Hypercube, Donut, Voronoi (hyprsaver), Voronoi (Classics), Hexstrut, Cubestorm, Geodesic, Gimbalharmonics |
| Retro, Terminal & Data (8) | Digital Rain, Terminal, Ticker, Oscilloscope, Sonar, Attitude, Circuit, Data Towers |
| Cities & Scenes (5) | Skyline, Temple, Cityflow, Neonhorizon (ncz), Fluxcore |

The exact ids are in `categories.tsv`; the tags column feeds the search box
(title, id, category label, engine and tags all match).

A hack the installed catalog knows but the categories file does not lands in an
"Other" section instead of disappearing, so an older or newer engine package
still shows everything.

## Black Hole simple mode

`assets/screensaver-chooser/options/blackhole-simple.tsv` defines six sliders.
Each slider is 0..100; **the centre (50) is the shipped default of every option
it moves**, so a fresh install shows every slider in the middle and "Reset"
(per axis: the undo button; overall: "Reset all") simply forgets the stored
values. The validator checks that the value at 50 equals the schema default,
that every stop is inside the option's range or choices, that numeric curves
are monotone and have stops at 0 and 100, and that no option is moved by two
axes. `tools/tests/test_chooser_data.py` also evaluates every axis at 0, 25,
50, 75 and 100 and asserts the results stay in bounds.

| Axis | Words | Options moved (value at 0 / 50 / 100) |
|---|---|---|
| Glow | Dim .. Blazing | `exposure` 0.55 / 1 / 1.8; `bloom` 0.3 / 1 / 2.2; `disk-brightness` 0.7 / 1 / 1.6; `photon-ring` 0 / 0 / 1.2 |
| Drama | Serene .. Violent | `beaming` 0.3 / 1 / 1.8; `jet` 0.25 / 1 / 1.7; `jet-length` 12 / 22.2 / 70; `hot-sector` 0.06 / 0.22 / 0.6 |
| Motion | Calm .. Dynamic | `speed` 0.35 / 1 / 2.2; `rotation-speed` 0.4 / 1 / 2; `precession` 0.3 / 1 / 2; `flyby` (step) drift at 0, slow-orbit from 25, auto from 50, random from 80 |
| Cosmos | Empty .. Crowded | `star-density` 0.2 / 1 / 2.4; `nebula` 0.1 / 1 / 1.8; `bright-stars` 0 / 0 / 1.3 |
| Disk | Compact .. Vast | `disk-outer` 7 / 12 / 34; `temperature` 0.75 / 1 / 1.35; `disk-structure` 0 / 0 / 1.2; `spin` 0 / 0 / 0.9 |
| Detail | Fast .. Finest | `quality` (step) low, medium from 25, auto from 50, high from 75, ultra at 100; `scene-quality` (step) light, auto from 50, rich from 75 |

Numeric options use the `linear` curve (piecewise linear between the stops);
enums use `step` (the value of the last stop at or below the slider). `lens` is
deliberately not on the Drama axis: its default `0` means "per-launch
automatic", which is not a point on a numeric curve, so moving a slider across
it would jump rather than glide. The Detail slider shows a warning above the
centre when the display GPU is calibrated as weak.

Above the sliders the panel shows the Look row (the ten palettes as swatches),
the Scene drop-down (single, binary, merger, tidal disruption, galaxy zoom,
three black holes), the presets as one-click pills, and a still frame of the
black hole rendered with the current settings (`ncz-screensaver still`, see
`docs/CLI-CONTRACT.md`; the harness runs headless, so nothing flashes on
screen). Below them "Advanced" holds every other option grouped as the schema
groups them; the Capture group is shown only when "Developer options" is on.

Slider positions are derived from the stored value of the first option of each
axis (the inverse of its curve), so a value typed into Advanced or applied by a
preset is reflected approximately; moving the slider rewrites all of the axis's
options in one settings write.

### Mapping to settings and the engine

Nothing new is stored: the sliders write the same `dev.ncz.screensaver
hack-options` entries the Advanced rows write, and the engine receives them as
`NCZ_BLACKHOLE_*` environment variables as before. The Look row also mirrors
the palette into the legacy `blackhole-color-mode` key for older launchers.

## Simple views of other screensavers

`assets/screensaver-chooser/options/simple.tsv` lists, per hack, the options the
simple view shows (at most eight); the rest fold into "Advanced", one
sub-expander per schema group. A hack that is not listed shows its basic-level
options (`preset`, `seed`, `palette`, `style`, `speed`, as the launcher reports
`level`). Only the hack being configured (the chosen one, or the one whose
gear was pressed) has its options on the page.

## Per-user chooser state

Favourites, the recently used list (last six: previewed, chosen or configured),
the developer toggle and the engine filter are UI state that no daemon reads,
so they live in `$XDG_CONFIG_HOME/ncz-screensavers/chooser.conf` (group
`[chooser]`), not in the GSettings schema.

## Data file lookup

The plugin reads `category-list.tsv`, `categories.tsv`, `options/simple.tsv`
and `options/<hack>-simple.tsv` from `$XDG_DATA_HOME/ncz-screensavers/` first
and then from `/usr/share/ncz-screensavers/` (where the engine package installs
them). The user path lets a person override the grouping and lets a newer
plugin run against an older engine package during testing.

## How to add a screensaver

1. Add the binary to `ncz_ship_bins` in `meson.build` as before.
2. Add one line to `categories.tsv`: id, one category id, `ncz` or `ported`,
   a few tags. `tools/generate-catalog.py` fails the build until every shipped
   id has exactly one line.
3. Optional: if the hack has more than about eight options, add a `simple.tsv`
   line naming the ones worth showing first.
4. Optional: a hack with its own simple mode ships
   `options/<stem>-simple.tsv` in the format above; the plugin picks it up by
   name. Only options that exist in the hack's schema are used, so the same
   file can be shipped ahead of an engine that lacks some of them.

## How to add a category

Add a line to `category-list.tsv` (id, label, description) in the position it
should appear, then move hacks to it in `categories.tsv`. Keep the list between
six and ten entries (`test_category_list_is_small_and_ordered`); a category
nobody uses fails `test_every_category_is_used`.
