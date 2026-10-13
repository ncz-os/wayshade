# Reference GTK4 application

A reference front end for the `ncz-screensaver` engine, written with GTK4 and libadwaita
(PyGObject). It is here for distributions and desktops that want to implement the
ncz-screensavers engine on Wayland: it shows how a settings UI drives the engine using only
the public interface, which is documented in `docs/CLI-CONTRACT.md`.

NCZ-OS does not use it. On NCZ-OS the settings are the Singularity plugin
(`plugin/`, package `ncz-screensavers-plugin`); this application is not installed by the
image, not recommended by the engine package and has no launcher entry there.

## What it uses

- the `ncz-screensaver` CLI (`list --json`, `status --json`, `preview`, `stop`, `calibrate`,
  `gpus --json`) and the `dev.ncz.screensaver` GSettings schema;
- the data files under `/usr/share/ncz-screensavers/` (`hacks.tsv`, `tiers.tsv`, `broken.tsv`,
  `options/*.tsv`, `presets.tsv`);
- nothing from Singularity, and no NCZ-OS specific path.

## Build and install

    meson setup build -Dgtk4-reference-app=true && ninja -C build && meson install -C build

Debian: `apt install ncz-screensavers ncz-screensavers-gtk4-reference` (both come from the same
source package and release; the reference package needs `python3-gi`, `gir1.2-gtk-4.0` and
`gir1.2-adw-1`).

## Try it without installing

    NCZ_SCREENSAVER_CATALOG=assets/screensaver-chooser/hacks.tsv ./ncz-screensaver-settings --dump

`--dump` prints what the window would show as JSON; `--self-test` builds the window and
checks the controls (needs a display). `NCZ_SCREENSAVER_CLI` points at another launcher.
