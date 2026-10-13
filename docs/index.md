# ncz-screensavers documentation

End-user and developer documentation for the **ncz-screensavers** engine:
74 screensavers that run natively on Wayland. This file is the index; every
page below has its own purpose and links only to other pages in this folder
or to the source tree.

Start with the [user guide](user-guide.md) if you want to use it, or the
[developer guide](developer-guide.md) if you want to build or change it.

## Using it

- [user-guide.md](user-guide.md) — enable it, choose a saver, set the
  timeout, test one, troubleshoot.
- [savers.md](savers.md) — the catalogue index: how the 74 shipped savers
  are grouped and how to read a row.
- [cli-reference.md](cli-reference.md) — every `ncz-screensaver` command and
  every settings key, plus the data files the launcher and the choosers
  read.

## How it works

- [architecture.md](architecture.md) — the three processes and what each
  owns, the GLES3 harness, and the two shader pipeline patterns.
- [options-and-scenes.md](options-and-scenes.md) — per-saver options and
  presets, the randomize-everything mode, the seed streams, the palette
  systems and the tier system in brief.
- [idle-and-wayland.md](idle-and-wayland.md) — the idle daemon and its
  three timers, the lock and idle interaction, the Wayland integration
  (layer-shell, per-output, the fallback), the configuration files and the
  Singularity settings plugin.
- [packaging.md](packaging.md) — the packages, the architectures, how to
  install them, and the checklist for adding a saver.
- [gpu-classes.md](GPU-CLASSES.md) — the `weak`/`mid`/`strong` class system,
  what the startup calibration measures, its thresholds, how the renderer
  string and driver name map to a class for Intel, AMD, NVIDIA and Mali
  parts, the per-saver tier table, hybrid-GPU offload, and how to re-measure.
- [palettes.md](PALETTES.md) — the Black Hole palettes including Cygnus,
  the shader role palettes, and the palette options of Magmasimplex and
  Fractasia.

## The catalogue

One page per origin group, each with a row per saver: name, one-line
description, style category, minimum GPU class, options, and the author and
licence from the saver's own source header.

- [catalogue/in-house-engines.md](catalogue/in-house-engines.md) — Black
  Hole, Magmasimplex, Fractasia.
- [catalogue/in-house-shaders.md](catalogue/in-house-shaders.md) — Neon
  Horizon, and a note on the two different things called neongravity.
- [catalogue/ported-classics.md](catalogue/ported-classics.md) — the eleven
  ported xscreensaver hacks, what was ported and how, and that none of them
  uses OpenGL 1.
- [catalogue/hyprsaver.md](catalogue/hyprsaver.md) — the thirty hyprsaver
  shaders.
- [catalogue/xshadertoy.md](catalogue/xshadertoy.md) — the thirty
  Shadertoy-API shaders.
- [catalogue/xshadertoy-sources.md](catalogue/xshadertoy-sources.md) — one
  row per shader with its author, licence and Shadertoy link.
- [catalogue/classics-port-notes.md](catalogue/classics-port-notes.md) — for
  each ported classic, what the port kept and what it replaced.

## Credits and licence

- [credits.md](credits.md) — Apache-2.0 for our code, the third-party
  notices for everything else, the attribution lines a distributor must
  reproduce, and where the machine-readable record lives.

## Screenshots

- [screenshots/](screenshots/) — one thumbnail per saver and one contact
  sheet per style category.
- [screenshots/README.md](screenshots/README.md) — how they are produced,
  what the script cannot capture and why, and how to re-run it.
- [../tools/render-thumbnails.sh](../tools/render-thumbnails.sh) — the
  script itself.
