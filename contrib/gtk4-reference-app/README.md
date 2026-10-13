# ncz-screensaver-settings (reference GTK4 front end)

A small settings window for the `ncz-screensaver` engine. It is written as a
**reference implementation**: it shows how a distribution's own settings panel
can drive the engine using nothing but the documented public interface, and it
is deliberately kept small and dependency-light so it can be read in one sitting
and copied into a larger settings application.

It is published as the `ncz-screensavers-gtk4-reference` binary package
(`Architecture: all`), released from the same source package as the engine.

## What it is not

It is **not** the settings UI used on the image this engine was developed for.
That image has its own settings plugin and its image build refuses to install
this package; nothing here is required to use the engine. If you are packaging
the engine for your own distribution, this app is optional.

## What it needs

| | |
|---|---|
| Required | `python3`, `python3-gi` (PyGObject), GTK 4 (`gir1.2-gtk-4.0`) |
| Optional | libadwaita (`gir1.2-adw-1`) — nicer widgets when present |
| Data | the engine package: `ncz-screensaver` CLI, `/usr/share/ncz-screensavers/`, the `dev.ncz.screensaver` GSettings schema |

libadwaita is genuinely optional. With it the window uses the Adwaita widget
set; without it the same window is built from plain GTK4 widgets and edits the
same settings keys. Either way it runs on GNOME, KDE Plasma, Wayland and X11
sessions.

## What it does

* **Lists the engine's own catalogue.** The list comes from the shipped
  `hacks.tsv`, the same file the launcher reads, so it can never drift from the
  engine's ship set.
* **Shows a GPU-tier badge per screensaver** — *Light*, *Mid* or *Heavy* —
  taken from `ncz-screensaver list --json` (the engine's own `tiers.tsv`),
  plus a "may stutter" warning when the screensaver is heavier than this
  machine's measured graphics class.
* **Edits the settings schema**: mode (off / one screensaver / random rotation
  / playlist), the idle timeout, rotation interval, lock and display-power-off
  delays, GPU selection, render quality, and the random-rotation pool.
  The available values for each list are read from the installed schema, so a
  new engine setting shows up without touching this app.
* **Per-screensaver options dialog** generated from the engine's
  `options/<screensaver>.tsv` schema files, with **Randomize all** (a fresh
  in-range value for every option), **Preview**, **Reset to defaults** and
  **Apply**.
* **Preview** a screensaver with its staged options for 30 seconds, and stop it.

## Install

Debian / Ubuntu:

```sh
apt install ncz-screensavers ncz-screensavers-gtk4-reference
```

Fedora:

```sh
dnf install ncz-screensavers ncz-screensavers-gtk4-reference
```

Arch:

```sh
pacman -S ncz-screensavers ncz-screensavers-gtk4-reference
```

Then start it from your desktop's application menu ("Screensaver & Lock"), or:

```sh
ncz-screensaver-settings
```

## Build from source

```sh
meson setup build -Dgtk4-reference-app=true
ninja -C build
meson install -C build
```

Run the headless checks (they skip where PyGObject, GTK 4 or a display is
missing):

```sh
meson test -C build --suite gtk4-reference-app
```

## Try it without installing

```sh
NCZ_SCREENSAVER_CATALOG=assets/screensaver-chooser/hacks.tsv \
NCZ_SCREENSAVER_OPTIONS=assets/screensaver-chooser/options \
    contrib/gtk4-reference-app/ncz-screensaver-settings --dump
```

`--dump` prints exactly what the window would show, as JSON, and needs no
display. `--gtk-smoke` builds the window and the options dialog on a throw-away
display and asserts they came up. `--self-test` drives the live window and
checks that changes reach GSettings.

## Environment overrides

Useful when testing against an engine built somewhere else:

| Variable | Meaning |
|---|---|
| `NCZ_SCREENSAVER_CLI` | path to the `ncz-screensaver` launcher |
| `NCZ_SCREENSAVER_CATALOG` | path to `hacks.tsv` |
| `NCZ_SCREENSAVER_OPTIONS` | directory holding `options/*.tsv` |
| `NCZ_SCREENSAVER_DIRS` | colon-separated list of screensaver binary directories |
| `NCZ_SCREENSAVER_BROKEN` | path to `broken.tsv` |

## Relationship to the engine

* The engine's command line is the only control surface; see
  `docs/CLI-CONTRACT.md` for the exact subcommands and JSON fields.
* The schema, the catalogue and the option files are the engine's; see
  `docs/OPTIONS-SCHEMA.md` for the option file format and
  `docs/IMPLEMENTING-ON-ANOTHER-DISTRO.md` for a full porting guide.
* The window never keeps its own list of screensavers, modes or options, so it
  cannot fall out of step with the engine. Adding a screensaver, a mode or an
  option to the engine makes it appear here.

Licensed under Apache-2.0, like the rest of this source package.