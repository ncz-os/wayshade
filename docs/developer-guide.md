# Developer guide

Build the engine, work on a saver, add one, and package it.

## Build

Dependencies: a C99 compiler, Meson, Ninja, `wayland-scanner`, the Wayland
client libraries, EGL and GLESv2, GLib/GIO (launcher), libpng, and
pkg-config for all of them. The Singularity plugin additionally needs
`valac` and the Singularity SDK.

```
meson setup build
ninja -C build
meson test -C build
```

### Build options

| Option | Type | Default | Meaning |
|---|---|---|---|
| `install-extras` | boolean | `false` | Also install the shaders of the development-only savers. |
| `magma-audio` | boolean | `true` | Build audio capture into the Magmasimplex beat layer. Off, the saver still builds and runs on its procedural beat. Capture shells out to `pw-record` or `parec`, so there is no link-time dependency either way. |
| `gtk4-reference-app` | boolean | `false` | Also install the reference GTK4/libadwaita front end. The Debian packaging turns it on and splits it into `ncz-screensavers-gtk4-reference`. |
| `singularity-plugin` | feature | `disabled` | Build the Singularity settings plugin. |
| `singularity-vapidir` | string | `/opt/singularity/share/vala/vapi` | Where `singularity-1.0.vapi` lives. |
| `singularity-plugin-dir` | string | `/opt/singularity/lib/singularity/plugins` | Where Singularity loads plugins from. |

`gtk4-reference-app` is a boolean, so it takes `true`/`false`, while
`singularity-plugin` is a feature and takes `enabled`/`disabled`:

```
meson setup build -Dsingularity-plugin=enabled
meson setup build -Dgtk4-reference-app=true
```

The plugin declares its Vala project arguments before any target is
declared, so the flag has to be on the `meson setup` line.

## Layout

| Path | What lives there |
|---|---|
| `src/` | The GLES3 harness, the drivers, the GPU tier and guard modules, the option parser, and the in-house engines. |
| `src/classics_shader/` | The eight shader-engine ports of xscreensaver hacks. |
| `src/fractasia/` | The Fractasia engine. |
| `vendor/` | Vendored third-party and in-house shader sources, each with its licence text. |
| `launcher/` | `ncz-screensaver` (Python), `ncz-screensaver-idled` (C), `ncz-screensaver-calibrate` (C), the systemd user unit and their tests. |
| `plugin/screensaver/` | The Singularity settings plugin (Vala). |
| `contrib/gtk4-reference-app/` | The reference GTK4 + libadwaita front end. |
| `config/dev.ncz.screensaver.gschema.xml` | The settings schema. |
| `assets/screensaver-chooser/` | The catalogue and option schemas the launcher and the choosers read. |
| `protocols/` | The two Wayland protocol XML files the idle daemon uses. |
| `tools/` | Development, measurement and documentation tools. |
| `tests/`, `tools/tests/` | The C and Python test suites. |
| `debian/` | The Debian source package. |

## The harness contract

A saver is a set of callbacks in an `xscreensaver_function_table`:
`init_cb`, `draw_cb`, `reshape_cb`, `event_cb`, `free_cb`, `release_cb`,
plus an optional `opts` array of option definitions. The harness
(`src/gles3_harness.c`) owns Wayland, EGL, the layer surface, the frame
loop and the frame statistics; the saver owns its GL objects and its
uniforms.

The meson build instantiates one binary per saver by defining:

| Macro | Meaning |
|---|---|
| `HACK_PREFIX` | The saver id, used to compose symbol names. |
| `HACK_PREFIX_ID` | The same with hyphens replaced by underscores, for C identifiers. |
| `HACK_TABLE` | The name of the function table the saver exports. |
| `SHADER_FILE` | The vendored shader to compile in. |

Two C-preprocessor rules bite here and are worked around in the source: the
lexer does not do sub-token lookups, so `init_HACK_PREFIX_STR` is also
defined as a macro; and `##` suppresses expansion of its operands, so a
two-stage `CAT_EXPAND` token paste is used.

## Options

The shared option parser is `src/ncz_options.c` with declarations in
`include/ncz_gpu_tier.h`-style headers per saver. A saver's schema is
`assets/screensaver-chooser/options/<id>.tsv`, columns: name, type, default,
min, max, choices, label, description, group, environment name.

`--dump-schema` prints a saver's options in that file format, and a build
test regenerates the file and fails when it has drifted — so the schema is
generated, never hand-maintained. The tests are named
`<saver>-schema-in-sync` and `xshadertoy-options-in-sync`; the latter also
fails when a scene names an option the driver does not know or a value the
driver rejects.

`tools/xshadertoy-gen-options.py` writes the xshadertoy schemas, their JSON
forms for the docs and the palette choices, and one JSON per scene.

## The GPU tier module

`include/ncz_gpu_tier.h` and `src/ncz_gpu_tier.c` expose four tiers
(`NCZ_TIER_LOW`, `MEDIUM`, `HIGH`, `ULTRA`) plus `AUTO`, a normalised
scalar, and a one-line `reason`. Two stages:

1. A static prior from `GL_RENDERER` / `GL_VENDOR` / `GL_VERSION` and a few
   cheap GL limits. It is a starting point, never the answer.
2. Runtime measurement: the saver calls `ncz_gpu_tier_record_frame_ms()`
   each frame and the module adapts with hysteresis — ignore the first
   about 30 frames, track a rolling median rather than a mean, downgrade
   readily and upgrade only when comfortably under budget, rate-limit
   changes, and never upgrade again after about a minute.

`NCZ_GPU_TIER=low|medium|high|ultra|auto` forces a tier, which is what the
reproducible captures and tier comparisons use.

The separate class system in the launcher (`weak`, `mid`, `strong`) is the
one a user sees; see [`gpu-classes.md`](GPU-CLASSES.md).

## The software-renderer guard

`src/ncz_gpu_guard.c` reads `GL_RENDERER` and refuses `llvmpipe`,
`softpipe`, `swrast` and `lavapipe`. If a hardware GPU is present
(a `/dev/mali*` node or a DRM node bound to a known driver) the saver exits
3 and says which driver it found; if no hardware GPU is present at all,
`NCZ_ALLOW_SOFTWARE=1` allows the CPU. Do not "fix" a guard refusal by
forcing the environment: the message names the driver, which is the hint.

## Tests and lint

```
meson test -C build                 # the whole suite
meson test -C build --list          # the test names
meson test -C build fractasia-gpu-smoke   # one test, by name
ruff check tools/                   # the Python tools
```

The C tests cover the option parser, the harness configuration, the xst
core and palettes, the Magmasimplex layers, the Fractasia families and
palette, the audio bus, the EGL colour space, the Black Hole fp32
accumulation and the GPU smoke tests. The Python tests cover the
launcher's GPU class logic and pools, the chooser data, the host-test
agent and several Magmasimplex measurement gates.

Two of the tests need a GPU: `fractasia-gpu-smoke` and
`build-artifact-gate`. The GPU smoke test renders every Fractasia family
into an offscreen target, so it runs with `EGL_PLATFORM=surfaceless` and
writes its frames to the directory named by `NCZ_FRACTASIA_OUT`.

## Measurement tools

| Tool | What it does |
|---|---|
| `tools/measure-tiers.py` | Rewrites `assets/screensaver-chooser/tiers.tsv` from measured runs. |
| `xshadertoy_render` | Headless renderer and GPU bench for the xshadertoy family: exact-time frames for contact sheets, and a sustained bench that prints p50/p95/p99 per window. |
| `tools/new-saver-test.py` | First-pass checks for a new saver. |
| `tools/build-artifact-gate.sh` | Packaging gate on the built artefacts. |
| `tools/generate-catalog.py` | Regenerates the catalogue files. |
| `tools/render-thumbnails.sh` | Reproduces the documentation thumbnails and contact sheets. |
| `tools/make-contact-sheet.py` | Builds contact sheets from host-test output. |
| `tools/xshadertoy-contact-sheet.py` | Before/after contact sheet for one xshadertoy shader. |
| `TOOLS.md` | Local notes and habits for working in this repository. |

## Writing documentation here

New pages go in `docs/`, one topic per page, with the catalogue split by
origin group under `docs/catalogue/`. Every command and every file path in
a page must be one you have actually run or grepped for; `README.md` is the
front door and `docs/index.md` is the index. Keep the page set
end-user-readable: no internal host names, no addresses, no credentials.

The thumbnails under `docs/screenshots/` are generated, never hand-edited.
Re-run the script rather than editing a PNG.
## Where to read next

- [packaging.md](packaging.md) — the packages and the checklist for adding a
  saver.
- [architecture.md](architecture.md) — the engine this builds.
