# Packaging and adding a saver

The packages, the architectures and how to install them; the Singularity
plugin package; and the checklist for adding a new saver. The build itself
is on [developer-guide.md](developer-guide.md); the command surface is on
[cli-reference.md](cli-reference.md).

## Packaging

`ncz-screensaver` is the user-side CLI. See [`cli-reference.md`](cli-reference.md)
for the full command and key table. Top-level entry points:

```
ncz-screensaver list                  # catalogue
ncz-screensaver list --json           # machine-readable catalogue
ncz-screensaver status                # running hack, GPU class, offload, idled
ncz-screensaver preview HACK          # run HACK now
ncz-screensaver still HACK            # render one frame to PNG (Black Hole today)
ncz-screensaver stop                  # stop the running saver or preview
ncz-screensaver list-options HACK     # per-hack options
ncz-screensaver gpus                  # GPUs with class
ncz-screensaver calibrate             # measure the GPU class
ncz-screensaver pool                  # pool and why each excluded hack is out
ncz-screensaver plan HACK             # which GPU would render HACK
ncz-screensaver doctor                # environment report
ncz-screensaver config get|set|dump   # raw settings
ncz-screensaver enable|disable [HACK] # random/off, or add/drop one pool entry
ncz-screensaver set-timeout N         # hack-idle-delay
ncz-screensaver set-hack HACK         # mode one
ncz-screensaver set-mode M            # off|one|random|playlist
ncz-screensaver set-gpu G             # auto|off|prime|GPU id
ncz-screensaver set-pool C            # auto|weak|mid|all
ncz-screensaver set-color PALETTE     # Black Hole color
ncz-screensaver ensure-plugin         # Singularity: enable the plugin once
```

The compat shim `ncz-screensaver-run --start|--stop|--preview ID` is
kept for older `ncz-lock` and `ncz-idle-manager` callers; it forwards
to the new CLI.

| Distribution | How to package |
|---|---|
| NCZ-OS | `debian/` builds `ncz-screensavers` (engines, CLI, idle daemon, data files) for `amd64` and `arm64`. The settings UI is the Singularity plugin, packaged separately with `tools/make-plugin-deb.sh ARCH BUILD_DIR OUT.deb [VERSION]` into `/opt/singularity/lib/singularity/plugins/screensaver/`. |
| Another GTK/libadwaita distribution | The same `debian/` build also produces `ncz-screensavers-gtk4-reference` (architecture `all`, PyGObject, GTK4 + libadwaita, no Singularity code). |
| Another desktop | `meson setup build && ninja -C build`. Write a front end against [`cli-reference.md`](cli-reference.md); everything the engine does is exposed through that contract. |

`debian/control` declares the two binary packages the source package
builds. Architectures: `amd64` and `arm64` for the engine,
`all` for the reference front end. The source package is `3.0 (native)`
and the build uses `dh $@` with these overrides:

- `override_dh_auto_configure`: `-Dinstall-extras=false -Dgtk4-reference-app=true`.
- `override_dh_auto_install`: install everything into the engine package,
  then move `ncz-screensaver-settings` and its desktop entry into the GTK4
  reference package.
- `override_dh_auto_test`: regenerate the catalogue with
  `tools/generate-catalog.py`, diff it against `assets/screensaver-chooser/hacks.tsv`
  and `presets.tsv`, then run the test suite. The screensavers need a live
  Wayland compositor and a GPU, so the packaging gate is that the ship set
  links, installs and keeps its catalogue in sync.

On a distribution with `apt`, installing the engine is:

```
sudo apt install ncz-screensavers
sudo apt install ncz-screensavers-gtk4-reference
```

The engine package `Recommends: grim` (the one-time black-frame check) and
`Suggests: ncz-screensavers-plugin`.

Hard runtime, the user's compositor must advertise
`zwlr_layer_shell_v1` (or `xdg_wm_base` for the fullscreen fallback);
otherwise the saver will not draw.

## How to add a saver

1. Drop the saver source under `src/` (driver) and any vendored shader
   under `vendor/<engine>/`. Each in-house engine is a fresh driver and
   one or more `.frag` / `.glsl` files; each shader hack is one
   `<hack>.frag` or `<hack>.glsl` plus the shared driver.
2. Add the binary to `ncz_ship_bins` in `meson.build`. This is the
   single source of truth: it controls what the build links, what the
   Debian package installs and what the catalogue generates.
3. Add a schema at `assets/screensaver-chooser/options/<hack>.tsv`. The
   build test (`<hack>-schema-in-sync`) regenerates the schema from the
   binary's `--dump-schema` output and fails on drift.
5. Add one line to `assets/screensaver-chooser/categories.tsv`: id, one
   category id from `category-list.tsv`, `ncz` or `ported`, a few
   search tags. `tools/generate-catalog.py` fails the build until every
   shipped id has exactly one line.
6. Regenerate the catalogue with `tools/generate-catalog.py meson.build`
   and commit the updated `assets/screensaver-chooser/hacks.tsv`,
   `categories.tsv` and `presets.tsv` it writes.
7. If the saver has more than about eight options, add a
   `simple.tsv` line naming the ones to show in the chooser simple
   view.
8. Add a row to `docs/PROVENANCE.tsv` with the per-saver origin and
   licence.
9. Add a page in `docs/catalogue/<category>.md` with the per-saver
   description, options, tier, origin and credits.

Build, test, commit, push.
## Where to read next

- [developer-guide.md](developer-guide.md) — the build, the harness
  contract and the tests.
- [credits.md](credits.md) — the provenance row a new saver needs.
- [savers.md](savers.md) — the catalogue and where a new saver lands in it.
