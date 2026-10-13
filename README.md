# wayshade

Native Wayland screensavers on OpenGL ES 3. wayshade is the screensaver engine
of [NCZ-OS](https://github.com/ncz-os/cix-installer/releases/tag/v26.10-preview1):
74 screensavers that render directly through `wlr-layer-shell` and EGL, with no
X11, no Xwayland and no OpenGL 1 emulation, plus a launcher, an idle daemon, an
off-screen feed for lock screens and settings front ends.

The Debian packages built from this tree keep their historical names:
`ncz-screensavers` (engine, launcher, idle daemon), `ncz-screensavers-plugin`
(Singularity settings plugin) and `ncz-screensavers-gtk4-reference` (reference
GTK4 front end). This tree is the 0.8.12 release that ships in NCZ-OS 26.10
Preview 1.

| set | count | source |
|---|---|---|
| Black Hole | 1 | `src/gles3_blackhole.c`, `vendor/blackhole/` (MIT) |
| hyprsaver shaders | 30 | `src/gles3_hyprsaver.c`, `vendor/hyprsaver/` (MIT) |
| xshadertoy shaders | 30 | `src/gles3_xshadertoy.c`, `vendor/xshadertoy/` (per-shader licenses) |
| classics | 11 | xscreensaver classics on a shader engine (`src/classics_shader/`) and upstream GLSL hacks |
| Fractasia | 1 | `src/fractasia/`, `vendor/fractasia/` (original) |
| Magmasimplex | 1 | `src/gles3_magmasimplex.c`, `vendor/magmasimplex/` (original) |

Every screensaver is a separate `<id>_gles3` binary driven by one Wayland/EGL
harness (`src/gles3_harness.c`). Shaders are compiled once at startup and nothing
reads pixels back inside the frame loop. Each screensaver has a minimum GPU class
(weak, mid, strong) in `assets/screensaver-chooser/tiers.tsv`; the launcher only
picks savers the GPU can run.

## What runs it

* A Wayland compositor that offers `zwlr_layer_shell_v1` (rendering) and
  `ext_idle_notifier_v1` (idle timer), for example labwc, sway or Hyprland.
* A GPU with OpenGL ES 3.0 through EGL (Mesa, NVIDIA or Arm Mali drivers).
  Software rendering is refused on purpose.
* For the lock-hosted saver: a session-lock client that hosts an out-of-process
  frame source (NCZ-OS ships one for the Singularity desktop). The feed protocol
  is in `include/lock_framesrc_proto.h`.

## Build

Debian/Ubuntu build dependencies:

    sudo apt install meson ninja-build pkgconf libwayland-dev libwayland-bin \
        wayland-protocols libegl-dev libgles-dev libpng-dev libglib2.0-dev \
        libglib2.0-bin python3

Build and test:

    meson setup build
    ninja -C build
    meson test -C build
    python3 -m pytest tools/tests launcher/tests     # needs python3-pytest, python3-numpy

Options: `-Dsingularity-plugin=enabled` (Singularity settings plugin; needs valac
and the libsingularity SDK), `-Dgtk4-reference-app=true` (reference GTK4 front end),
`-Dinstall-extras=true` (install development-only shaders). Debian packaging is in
`debian/` (`dpkg-buildpackage -b`).

## Run

    ncz-screensaver list                 # catalog with GPU-class flags
    ncz-screensaver preview <id>         # run one screensaver now
    <id>_gles3 --help                    # options of that screensaver
    ncz-screensaver-feed --print-policy  # lock-hosted saver policy

## Lock-hosted saver

`ncz-screensaver-feed` renders a screensaver off-screen and hands frames to the
lock client over a socket. In 0.8.12 it runs a short intro at 8 fps and half
resolution, then freezes after 15 seconds and the lock keeps the last frame (only
with a lock that negotiates HOLD; older locks keep the low frame rate). A thermal
governor pauses rendering near 85 C.

## NCZ-OS and the Singularity desktop

NCZ-OS installs `ncz-screensavers` and `ncz-screensavers-plugin`; the plugin adds
Settings > Plugins > Screensaver and Lockscreen to the
[Singularity desktop](https://github.com/singularityos-lab). On other desktops use
the reference GTK4 front end or write one against `docs/CLI-CONTRACT.md`;
`docs/IMPLEMENTING-ON-ANOTHER-DISTRO.md` lists what the engine needs.

## Documents

* `docs/CLI-CONTRACT.md`, `docs/OPTIONS-SCHEMA.md` - the front-end contract
* `docs/GPU-CLASSES.md`, `docs/RENDER-AND-GUARD.md` - GPU classes, offload, render guards
* `docs/BLACKHOLE-OPTIONS.md`, `docs/BLACKHOLE-PRESETS.md`, `docs/SHADER-HACK-GUIDE.md`
* `docs/fractasia/` - Fractasia algorithm coverage and license analysis
* `CHANGELOG.md` - release history

## License

The original code in this repository is Apache-2.0 (`LICENSE`, `NOTICE`). Every
vendored or ported screensaver keeps its own license and notice (MIT, CC0-1.0,
CC-BY-3.0, BSD or the xscreensaver permission notice) and is not relicensed. The
component-by-component record is in `NOTICES.md`, `debian/copyright` and
`docs/PROVENANCE.tsv`.

## Contributing

See `CONTRIBUTING.md`. The canonical repository is
<https://gitlab.com/ncz-os/wayshade>.
