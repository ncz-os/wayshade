# Screenshots

One thumbnail per shipped saver, plus one contact sheet per style category.
Everything in this directory is generated: do not edit a PNG by hand, re-run
the script instead.

## Contents

| Path | What it is |
|---|---|
| `thumbs/<saver>.png` | One 320x180 frame of one saver, named after the binary minus its `_gles3` suffix. |
| `contact-sheet-<category>.jpg` | A grid of every captured thumbnail in one style category, each cell labelled with the saver id and its display title. The categories are the ones in `assets/screensaver-chooser/category-list.tsv`. |

The eight contact sheets are `space`, `fractal`, `neon`, `nature`, `fluid`,
`geometry`, `retro` and `scenes`.

![space](../screenshots/contact-sheet-space.jpg)

## How they are produced

```
tools/render-thumbnails.sh [--build DIR] [--out DIR] [--only ID[,ID...]]
                           [--timeout SECONDS] [--keep-work]
```

Defaults: `--build build --out docs/screenshots --timeout 25`. The script
is run from the source root and needs Python 3 with Pillow.

What it does, in order:

1. **Finds a compositor.** If `WAYLAND_DISPLAY` is not usable it starts a
   headless wlroots compositor (`sway` with `WLR_BACKENDS=headless`) on a
   private `XDG_RUNTIME_DIR` inside the output directory, records its PID
   and kills that exact PID when it finishes. Nothing is ever shown on a
   screen, and nothing is attached to a physical output.
2. **Reads the saver list** from `assets/screensaver-chooser/hacks.tsv`,
   which is generated from the shipped binary list in `meson.build`. Adding
   a saver to `meson.build` therefore adds it here with no change to this
   script.
3. **Runs each saver** with `NCZ_FRAME_DUMP` set, so the harness writes a
   real PNG of one known frame instead of the saver painting a live
   surface. The script stops each saver as soon as that frame exists, by
   exact PID.
4. **Resizes** the frame to a thumbnail (centre-cropped to the aspect
   ratio, LANCZOS) and writes `thumbs/<saver>.png`.
5. **Checks the frame** for emptiness and reports any saver whose frame
   carries no image content.
6. **Builds the contact sheets**, one per style category.

### Reproducibility

| Knob | Value | Where |
|---|---|---|
| Saver set and order | 74 ids from `hacks.tsv` | `--only` to narrow |
| Resolution | 1280x720 | `WIDTH`, `HEIGHT` |
| Frame | the 60th | `FRAME_SKIP` |
| Seed | 42 | `SEED` |
| Thumbnail size | 320x180 | `THUMB_W`, `THUMB_H` |

The seed is passed on the command line (`--seed=42`) to the savers that
declare a `seed` option, and as `NCZ_SEED=42` to every saver, so a run can
be replayed for the savers that support it.

The hyprsaver family declares no seed and derives its animation from the
wall clock, so its thumbnail is the frame the saver reached after
`FRAME_SKIP` warm-up frames rather than an instant in shader time. Two runs
of that family on different hardware therefore produce different
thumbnails. This is a property of those shaders, not of the script.

### Software rendering

The engine refuses to run on `llvmpipe`, `softpipe` or `swrast` when the
machine has a hardware GPU, and the script does not override that: a frame
captured through a software renderer would not be what a user sees. To
capture on a machine with no GPU at all, set `NCZ_ALLOW_SOFTWARE=1`.

## What the script cannot capture

Two cases, both handled explicitly rather than silently:

- **A saver that takes longer than `--timeout` to reach its first dumped
  frame.** Fractasia compiles 34 shader families at startup, so on a cold
  Mesa shader cache it is the slowest. If it still produced no frame, the
  script falls back to the in-tree GPU smoke test
  (`test_fractasia_smoke`, run with `NCZ_FRACTASIA_OUT`), which renders
  every family offscreen into its own target. `FRACTASIA_FAMILY` selects
  which family the thumbnail shows; the default is `orbit_trap`.
- **A saver with no capture mode.** `ncz-screensaver still` only works for
  Black Hole, which is the one saver with a capture path. The script does
  not use it: it uses the harness's own frame dump, which works for every
  saver because it does not depend on the saver cooperating.

The script exits non-zero if any saver failed to produce a frame and prints
the ids and the reason, so a partial run cannot be mistaken for a complete
one.

## What the thumbnails look like

Some of the frames are nearly black. That is what the saver renders, not a
capture failure: `starfield`, `sonar`, `fireflies` and `bezier` are dark
scenes by design, and the project's own archived capture of the starfield
saver measures the same mean luminance as the thumbnail here. The script
reports any frame with no content at all, and the 0.8.9 run reported none.

A few savers are brighter at a different moment: the shader families derive
their animation from the wall clock, so `FRAME_SKIP` decides how far into
the run the frame is taken. Raising it makes some frames busier and others
darker.

## Re-running it

```
meson setup build && ninja -C build
tools/render-thumbnails.sh --build build --out docs/screenshots
ruff check tools/render-thumbnails.py
```

To refresh one saver after changing it:

```
tools/render-thumbnails.sh --build build --out docs/screenshots --only hyprsaver_aurora_gles3
```

Keep the committed total under 15 MB: 74 thumbnails at 320x180 plus eight
contact sheets is a few megabytes. If a sheet grows large, lower `--columns`
or the thumbnail size rather than editing the image.