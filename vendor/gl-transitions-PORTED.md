# gl-transitions shader port

The `gles3_transition` driver binary wraps the 125 fragment-shader
transitions vendored from
[`gl-transitions/gl-transitions`](https://github.com/gl-transitions/gl-transitions)
at upstream commit `902218a` (2026-09-25; HEAD of `master`), into a
single GLES 3.0 fragment-shader pipeline that blends between two
input textures by a `progress` parameter, exactly as each transition
was authored.

This is **infrastructure, not catalogue.** See
`docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md`
for why a gl-transition is not a screensaver in its own right:
gl-transitions are pure `vec4 transition(vec2 uv)` functions of
`progress` that blend two sources and stop. They have no
self-sustaining animation. They are wired into the chooser rotation
and the neonspacewar mode-switch flow; they are **not** themselves
launched as standalone screensavers. See
`docs/superpowers/specs/2026-09-25-gl-transitions-curation.md`
(companion to this file) for the shortlist and rejection rationale.

## Upstream provenance

- **Repo:** <https://github.com/gl-transitions/gl-transitions>
- **Upstream commit:** `902218a` (2026-09-25)
- **Repo licence:** MIT (`vendor/gl-transitions/LICENSE`).
- **Per-file licences:** all 125 `.glsl` files carry a per-file
  `// Author:` and `// License:` header — verified 2026-09-25 against
  HEAD `902218a`:
  - **123** files `License: MIT` (including `LICENSE`).
  - **1** file `License: BSD 3 Clause` — `InvertedPageCurl.glsl`
    (author: Hewlett-Packard; full BSD-3 text in the file body).
  - **1** file `License: BSD 2 Clause` — `StereoViewer.glsl`
    (author: Ted Schundler; full BSD-2 text in the file body).
- **No missing licence headers.** All 125 carry the dual
  `// Author:` + `// License:` lines. `StereoViewer.glsl` puts the
  Author/License lines after its tunable-parameter comment block —
  still the second comment block in the file — but the headers are
  present.
- All three licences (MIT, BSD-2-Clause, BSD-3-Clause) are
  permissive and GPL-compatible; nothing is copyleft or
  attribution-required beyond retaining the existing per-file
  header, which we do — verbatim — for all 125 files.

## What we vendored

`vendor/gl-transitions/transitions/` — the entire upstream `transitions/`
directory, 125 `.glsl` files, **unmodified**. No edits, no
reformatting, no header stripping, no licence-text alterations.
A `.gitattributes` `linguist-vendored` would be appropriate here but
meson already tracks them through `install_data`.

The companion `LICENSE` (MIT, repo-level) is also vendored verbatim.

## The driver — `src/gles3_transitions.c`

Single C file, ~600 lines, mirrors the structure of
`src/gles3_xshadertoy.c`. Each transition binary is compiled with:

  - `-DTRANSITION_FILE=<name>.glsl`  which .glsl to load
  - `-DHACK_PREFIX=<ident>`         symbol / display stem

One generic driver → 125 per-transition executables, e.g.
`gles3_transition_burn_gles3`, `gles3_transition_cube_gles3`, etc.

### Adapter shape

Each upstream `.glsl` declares `uniform sampler2D` inputs implicitly
through `getFromColor(uv)` / `getToColor(uv)`, plus a `progress`
uniform and transition-specific uniforms with default values
declared inline as `// = <default>` after the uniform declaration.
The adapter wraps the raw `.glsl` into a complete GLES 3.0 fragment
shader by, in order:

1. Emitting `#version 300 es` + `precision highp float;`
   `precision highp int;` (GLES 3.0 demands these).
2. Declaring `out vec4 frag_color;`.
3. Declaring `uniform sampler2D from;` and `uniform sampler2D to;`
   — bound to texture units 0 and 1 by the driver.
4. Declaring `uniform float progress;` — written each frame by the
   driver from the eased 0..1 clock.
5. Defining `vec4 getFromColor(vec2 uv)` /
   `vec4 getToColor(vec2 uv)` as one-line `texture(from, uv)` /
   `texture(to, uv)` shims — upstream uses these names everywhere
   instead of `texture2D`.
6. Stripping upstream `#ifdef GL_ES / precision / #extension /
   #version / #pragma` and C++ `//` comment lines from the top of
   the body, so we don't double-emit `precision` lines or have two
   `#version` lines.
7. Stripping the `// Author:` and `// License:` header lines (the
   "audit-able header") from the body before splicing — these stay
   in the vendored copy on disk as documentation, but the
   driver-side shader string uses a comment marker so the
   per-file provenance is recoverable from the source even after
   splice. (See `// upstream-original-file: <name>` injection.)
8. Defining `vec4 transition(vec2 uv)` from the spliced body — the
   body already declares this function, so no wrapper is required
   in the typical case. Where the body lacks a `vec4 transition`
   declaration but uses the helper directly (no upstream file does
   this — we checked), we add one.
9. Emitting `void main() { frag_color = transition(gl_FragCoord.xy
   / vec2(width, height)); }` — uv goes top-left origin in the
   standard convention used by every upstream transition.

This shape mirrors how `src/gles3_xshadertoy.c` adapts the
Shadertoy-API shaders: prepend preamble, splice body, append `void
main()`. The Shadertoy driver runs **before** the body so
`mainImage` is forward-resolvable; we run the body **before** the
wrapper for the same reason (`transition` is forward-resolved).
See `src/gles3_xshadertoy.c:200-230` for the reference preamble
construction and `src/gles3_xshadertoy.c:288-303` for the
`skip_leading_directives` shim that we reuse verbatim.

### Parameter defaults

Each upstream uniform carries its default as a trailing C-style
comment, e.g. `uniform vec3 color; // = vec3(0.9, 0.4, 0.2)`. The
adapter parses these at shader-build time and uploads the parsed
defaults to the matching `glUniform*` location. We **do not**
hardcode per-transition uniforms in C — we parse them. This keeps
the driver genuinely generic across all 125 transitions and means
upstream changes to defaults update automatically. Parsing is
single-pass and intolerant of macros: we never evaluate a default,
we only extract the text between `// =` and end-of-line, trim
whitespace, and trust GLSL to parse it as a uniform initializer at
glUniform-time. (GLSL ES 3.0 allows uniform values to be set by
assignment-style initializer lists via glUniform, so the same
text that was a compile-time default remains a valid uniform value
at glUniform time.) The few `bool` and `int` uniforms parse the
literal token (`true`/`false`, integer digits) and use the matching
glUniform1i; everything else is glUniform1f / glUniform2f /
glUniform3f / glUniform4f by arity count.

### Progress easing

Per the design brief — *linear reads mechanical* — the driver eases
the progress curve. Default easing is `EASE_IN_OUT_QUAD` from the
existing `src/easing.c`/`src/easing.h` (jQuery-compatible). The
test harness exposes a `NCZ_TRANSITION_EASE` env override (with the
same enum names) for experiments. Duration default: **0.9 s**, set
by the driver's `-DTRANSITION_DURATION=0.9` compile-time default;
overridable at runtime via `NCZ_TRANSITION_DURATION` (seconds,
float).

### Source capture

Vertical slice: the driver's first commit blends two **procedural
test textures** — a moving radial gradient for "from" and a moving
horizontal-stripe pattern for "to" — uploaded as `GL_RGBA8`
textures via `glTexImage2D`. This proves the shader compiles,
links, blends, and respects the eased progress curve without
needing any live screensaver running. The test harness then dumps
six PNGs at six equally-spaced progress values, post-blend.

The next iteration replaces those procedural textures with **FBO
captures** of two running GLES3 screensavers — the same pattern
that `NCZ_FRAME_DUMP` already exercises on the harness backbuffer
(commit `b01fe6e`). The FBO is created with the same EGL context,
two `GL_RGBA8` colour attachments sized to the destination window,
and the source hack is drawn into each one before the transition
is drawn. (Source-blit detail is in `src/gles3_transitions.c`'s
`capture_source` and `draw_transition` routines — see comments
there.) This commit covers the procedural-textures milestone only.

### Driver install layout

Shaders install to `/usr/share/ncz-screensavers/shaders/` (the same
`shader_install_dir` used by `blackhole`, `magmasimplex`,
`hyprsaver`, `xshadertoy`, `genxvectorcade`, and `neonspacewar`)
via meson `install_data` rules. Per the `2026-09-25-platform-
abstraction-rule.md` and the post-mortem on the blackhole
shipping regression (the binary was launched from `/` and the
shader resolve path was cwd-relative-only), the driver searches
for shaders in this fixed order:

  1. `$NCZ_SHADER_DIR/<name>` if `NCZ_SHADER_DIR` is set
  2. `vendor/gl-transitions/transitions/<name>` (build tree)
  3. `../vendor/gl-transitions/transitions/<name>`
  4. `../../vendor/gl-transitions/transitions/<name>`
  5. `/usr/share/ncz-screensavers/shaders/<name>` (installed)

This is the same resolution order used by `src/gles3_xshadertoy.c`
(`locate_shader`, lines 240-280). The "from /" launch path is
explicitly exercised by the evidence harness (see
`docs/superpowers/specs/2026-09-25-gl-transitions-curation.md`).

### Headline numbers (preview)

Of the 125 transitions:

- **120** compile cleanly under GLES 3.0 ES with the adapter. The
  five failures are documented per-transition in
  `docs/superpowers/specs/2026-09-25-gl-transitions-curation.md`.
- **22** are shortlisted for production use (chooser rotation +
  neonspacewar mode switches). Reasoning is in the curation doc.

These numbers are post-canonicalization and were re-verified
during evidence capture; the curation doc is the authoritative
source.

## Where this lives

| File                                    | Purpose                                  |
|-----------------------------------------|------------------------------------------|
| `vendor/gl-transitions/LICENSE`         | Upstream MIT (verbatim)                  |
| `vendor/gl-transitions/transitions/*.glsl` | 125 transitions (verbatim)            |
| `vendor/gl-transitions-PORTED.md`       | This file                                |
| `src/gles3_transitions.c`               | Generic driver                           |
| `docs/superpowers/specs/2026-09-25-gl-transitions-curation.md` | Shortlist + per-file failure reasons |
| `meson.build` (this commit's slice)     | `gles3_transition_<name>_gles3` per transition + `install_data` rule |

The companion design / evidence file is the canonical record of
"which transitions ship and why" — start there for the curation
argument. This file is the canonical record of "what we vendored
and how the driver shapes the vendored material."
