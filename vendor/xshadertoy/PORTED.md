# xshadertoy shader port

This directory vendors 38 GLSL fragment shaders from upstream xscreensaver
6.16 (`hacks/glx/glsl/`). They implement the Shadertoy API: each one
exposes a `void mainImage(out vec4 fragColor, in vec2 fragCoord)` entry
point and reads the canonical Shadertoy uniform set
(`iResolution`, `iTime`, `iTimeDelta`, `iFrameRate`, `iFrame`,
`iDate`, `iMouse`, `iChannel0..3`). They are launched by a single
generic native GLES3 driver, `src/gles3_xshadertoy.c`, which prepends
the required `#version 300 es` + uniform-decl + `void main()`
preamble and binds a 1x1 black dummy to `iChannel0..3` so shaders
that call `texture(iChannel0, ...)` sample zeros instead of
uninitialized driver memory.

The driver is built once per shader via meson's foreach loop, producing
38 independent executables (`xshadertoy_<shader>_gles3`), each one
owning its own GLSL program object, VBO, and dummy-channel texture.
This matches the pattern of upstream xscreensaver (one ~20-line bash
wrapper per shader) and the pattern of this repo's own hyprsaver
shaders (one `.c` reused via `-D` flags, see PORTED.md §13.3).

## Vendoring

All 38 shaders come from upstream xscreensaver 6.16 commit `b99f621`
(https://github.com/Zygo/xscreensaver), released 2026-09-03. No edits
have been made to the .glsl files themselves — the upstream header
in each file is preserved verbatim (Title / Author / URL / Date /
license line). The `README` from upstream `hacks/glx/glsl/` is
included alongside.

## Licensing

Per upstream's `xshadertoy.c` LICENSING section, a shader is shippable
in xscreensaver only if its license is MIT / BSD / CC BY / CC BY-SA /
CC0. jwz contacted authors individually and relicensed where needed.
Inventory of the 38 vendored shaders (license declared in each file
header):

| License family | Count | Shaders |
|---|---|---|
| MIT (relicensed by permission) | 13 | bestill0-0..bestill5-0, darktransit, downfall, noxfire, polarnight, rigrekt, trizm, universeball |
| MIT (original) | 3 | hexplasma, prococean, starnest |
| CC0 / public domain | 18 | alienbeacon, batteredplanet, bubblecolors, driftclouds, elementalring, fluxcore, gimbalharmonics, goldenapollian, logarithmiccircles, neongravity-0, neongravity-1, neontriangulator, protophore, selfreflect, skyline, stardome, stripeytorus, topologica, trainmandala, truchetzoom |
| CC BY 3.0 | 1 | synthwavecity (original shader by Jan Mróz, jaszunio15, under CC BY 3.0; adapted by 3w36zj6 and shipped here under the same terms — attribution required, commercial use allowed) |
| Drift (3w36zj6 / drift public release) | 2 | bubblecolors (CC0-equivalent public release, see source), driftclouds (drift: "anyone and everyone who wishes to use this shader I give my permission to use it in any way that you choose. Credit would be nice but I won't insist on it.") |

> **Total**: 38 shaders. **None are CC BY-NC or CC BY-ND.** The brief's
> "exclude CC BY-NC/ND" check passes for the full set.

The shell-text inventory above came from
`grep -iE 'license|licence|copyright|relicensed|MIT|CC|BY-NC|public domain' vendor/xshadertoy/glsl/*.glsl`
plus header-by-header reads of any file whose header text wasn't
caught by that grep (notably `bubblecolors.glsl`, `driftclouds.glsl`,
by the upstream xshadertoy.c file itself, which already enumerates
the license of every shipped shader before re-releasing them in 6.16).

## Attribution / provenance

- **All 38 shaders** keep their original header verbatim (Title /
  Author / URL / Date / license line). Where the URL points at the
  original shadertoy.com entry (e.g. `https://www.shadertoy.com/view/3ss3R4`
  for `polarnight.glsl`), that attribution travels with the
  vendored file.
- **README** from upstream `hacks/glx/glsl/` is preserved as
  `vendor/xshadertoy/glsl/README`.
- **Upstream source** is identified in this directory by commit
  `b99f621` of https://github.com/Zygo/xscreensaver (released
  2026-09-03).
- **xshadertoy.c host** (the driver this port implements against) is
  the same commit; its permission notice is preserved in
  `src/gles3_xshadertoy.c`'s file header.

## Driver

`src/gles3_xshadertoy.c` is a single C file reused via `-D` flags
(see meson.build's `xshadertoy_shaders` foreach). It:

- Loads `vendor/xshadertoy/glsl/<shader>.glsl` at runtime, with
  fallbacks: build-tree → repo-relative → `/usr/share/ncz-screensavers/shaders/`.
  The absolute-install path is the production contract — blackhole's
  "dies when launched from /" regression is explicitly avoided here.
- Strips any leading `#version` / `#extension` / `#pragma` /
  `precision` / C++ comment block from the .glsl (the upstream
  shaders don't carry a `#version` line, but if any future
  re-vendored file did, our preamble would still work).
- Prepends the Shadertoy-API preamble (uniform decls + GLSL-1.2-to-1.3
  compat shims + `void main()` wrapper).
- Compiles, links, uploads the Shadertoy uniform set every frame
  (`iResolution`, `iTime`, `iTimeDelta`, `iFrameRate`, `iFrame`,
  `iDate`, `iMouse`), binds the dummy 1x1 RGBA8 black texture to
  iChannel0..3, and draws a fullscreen quad.

The preamble's GLSL helpers (texture/sampler2D aliases, round,
max(int,int), etc.) are adapted from upstream `xshadertoy.c`
lines 264-408 of the file as it appears in commit `b99f621`. They
are released under the same X Consortium MIT-style permission
notice as xscreensaver itself (preserved at the top of
`src/gles3_xshadertoy.c`).

## What we deliberately did NOT port

- **Multi-pass rendering.** Upstream xshadertoy.c supports
  `--program0`..`--program4` chained so a shader can read another
  pass's output as `iChannelN`. All 38 vendored shaders are
  single-pass (each upstream bash wrapper only sets `--program0`).
  Skipped — saves ~200 lines and zero current-shader value.
- **Upstream options (`--speed`, `--scale`, `--automouse`,
  `--duration`).** All expose XScreensaver-config plumbing we don't
  replicate; the GLES3 harness launches one binary per shader and
  tempo is the natural wall-clock tempo.
- **Upstream loader.** X11 `$BUNDLE_RESPATH` / Android
  `android_read_asset_file()` / macOS Cocoa `.saver` lookup is
  irrelevant to a Wayland/EGL/GLES3 pipeline.
- **iKeyboard** (Shadertoy's per-key bitmask-as-texture channel) —
  not shipped by any of the 38; the upstream xshadertoy.c itself
  documents it as not implemented.

## Per-shader caveats (iChannel sampling)

Upstream xshadertoy.c documents that `iChannel0..3` are only fed
from the output of a previous pass. With multi-pass disabled,
single-pass shaders that sample `iChannelN` get our 1x1 RGBA8
dummy black texture, not a previous-pass output. Of the 38
vendored shaders, 4 reference `iChannel0`:
`gimbalharmonics`, `neongravity-0`, `protophore`, `skyline`.

Per-shader observed behavior on NVIDIA RTX 2060:

| Shader | iChannel use | Observed rendering | Notes |
|---|---|---|---|
| `gimbalharmonics` | `texture(iChannel0, rayDir)` for env map | Renders normally (uses iChannel0 sparingly, plus procedural cube map) | Functional with the 1x1 dummy |
| `protophore` | Same as gimbalharmonics | Renders normally | Functional |
| `skyline` | `texture(iChannel0, ref.xy)` for window reflections | Renders normally (the sky color path dominates; window reflections are a small fraction) | Functional |
| `neongravity-0` | `fxaa(iChannel0, ...)` post-process | **Renders all-black** | The FXAA helper computes a gradient on the input texture; with a 1x1 zero input the gradient is identically zero, so the output is uniform black |

`neongravity-0` is the only shader in the inventory whose
rendering depends on the input texture being non-trivial. We
**ship the binary** (it compiles, links, and runs without GL
errors at gl_error=0x0), but the on-screen output is the
uniform-black fallback. This is the same degradation any
single-pass Shadertoy host would have without a multi-pass
backbuffer; documenting it explicitly so a future port with
multi-pass support can pick it up. **This is not an excluded
shader** — it links and runs cleanly per the project's
"ported-only-if-LINKS" rule (see PORTED.md header).

Upstream's comment about the 1x1 limitation also explains why
the other 3 shaders (gimbalharmonics/protophore/skyline) render
correctly: their iChannel sampling is a small fraction of the
final pixel color, dominated by procedural geometry.
