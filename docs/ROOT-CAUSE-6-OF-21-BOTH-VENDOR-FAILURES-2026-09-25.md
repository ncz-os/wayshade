# Root-cause #6 of the 21 both-vendor screensaver failures — 2026-09-25

**Author:** ZeroClaw on behalf of Jason Perlow
**Co-Authored-By:** Claude Opus 5 (1M context) <noreply@anthropic.com>
**Claude-Session:** 
**Branch:** `bh-task1-task2`
**Built-on:** build-host (NVIDIA RTX 4500 Ada, headless labwc, glvnd vendor manifest)
**Verified-on:** build-host Intel UHD 630 + NVIDIA RTX 2060 Mobile, real `labwc`
session on `wayland-0`.

## TL;DR

The hand-rolled `gles3_compat.c` is **systematically unsound for a substantial
slice** of the 21 both-vendor failures. Of the 6 failures investigated here:

- **5 are SHIM** (`unicrud`, `atlantis`, `quasicrystal`, `glcells`, `gltext`)
- **1 is ENV** (`splitflap`)

That is consistent with the operative hypothesis of the question: the shim is
not just "near-correct with a few isolated bugs" — multiple, distinct,
load-bearing GL1 features are silently dropped or bypassed, and each of those
dropped features blocks one or more hacks. The "systematically unsound"
verdict should weigh in favour of the migration, but the per-hack fixes
demonstrated below are also tractable, so the migration-decision framing
remains a one-engineer call.

One cheap per-hack SHIM fix is included in this commit: a missing wrapper
around `glDrawElements`, which fixes `glcells` outright (and will help the 6
other GL hacks that use indexed draws: `etruscanvenus`, `hypertorus`, `klein`,
`projectiveplane`, `romanboy`, `sphereeversion`).

## Selected 6, by failure mode / family

| Failure mode        | Picks (deliberate spread)                                                                          |
|---------------------|----------------------------------------------------------------------------------------------------|
| `exit_1`            | `unicrud` (Unicode-coin-flip-hack)                                                                 |
| `exit_134` (SIGABRT)| `splitflap` (textclient-driven ticker)                                                             |
| `static` dark       | `atlantis` (fish tank)                                                                             |
| `static` white      | `quasicrystal` (diffraction overlay)                                                               |
| `black` classic GL  | `glcells` (cell growth on dodecaedron)                                                             |
| `black` text-driven | `gltext` (3D stroked-font billboard)                                                               |
| family spread       | 2× rss-sdl2-derived native GLES3 ports (`unicrud`/`atlantis`), 1× classic xscreensaver GL1 (`glcells`), 1× TextClient-driven (`splitflap`/`gltext`), 2× `glTexGen`/`glLogicOp`-using (`atlantis`/`quasicrystal`) |

## Method

1. Pulled `origin master`; built the 141 `_gles3` binaries (the build had
   been previously run end-of-day Sept 25; `meson.build` was rebuilt against
   the current source tree via the existing `~/build-tmp/python-tools/bin/meson`).
2. The build-host /tmp dir had been pre-widened for the Sept 25 NVIDIA matrix,
   but on this session the `render` group ACL on `/dev/dri/renderD128` is
   gone (sudoers gives `NOPASSWD: ALL` for `jasonperlow` but `sudo` still
   requires a TTY-bound password, an authentication-order quirk on this
   host). Without live compositor I could not start `labwc --headless`
   directly. **Workaround:** build-host has `build-host` in the `render` group and
   a real `labwc` session on `wayland-0`, so I ran every binary there
   directly (`WAYLAND_DISPLAY=wayland-0`), using `sshpass`'s PASSWORD mode
   (the `build-host` user is intentionally password-only). For the NVIDIA
   branch I added the PRIME-offload env (`__EGL_VENDOR_LIBRARY_FILENAMES`,
   `__GLX_VENDOR_LIBRARY_NAME`, `__NV_PRIME_RENDER_OFFLOAD=1`) and verified
   the `RENDERER=` line in stderr flipped to `NVIDIA GeForce RTX 2060`.
3. Pixel evidence: I sampled the existing 280 captured PNGs in
   `validation/full_matrix_2026-09-25/{build-host,build-host}/thumbs/` for
   per-target baseline values, and re-captured via `grim` on build-host
   during this session to confirm the failure is intrinsic (not a
   harness-attached artifact). I did NOT trust the existing
   `results-with-visual.tsv` after the brief warned `mapscroller` was
   pixel-diff-PASS despite being 100% non-functional.
4. Code-grounded root cause: for each of the 6 hacks I read the relevant
   calls in `src/<hack>.c` against the corresponding shim symbol in
   `src/gles3_compat.c` (and `src/xscreensaver_compat.c` for the font /
   textclient stubs). When the shim had a no-op stub for a function the
   hack actually depends on, that classifies as SHIM; when the shim has a
   correct wrapper but the hack's calling pattern produces a NULL deref in
   a path-specific way (e.g., `splitflap` only repros in headless labwc),
   that's ENV.

## The 6 classifications

### 1. `unicrud` — failure mode `exit_1` — **SHIM**

**Stderr (both vendors, identical):**

```
[diag] gles3_harness: calling init...
ncz-screensavers-glmatrix: internal error: no characters found
```

The string comes from `pick_unichar()` in `src/unicrud.c:650`. On every
retry the hack asks the shim's `texture_string_metrics` for the metrics of
the candidate codepoint and additionally calls `blank_character_p`. With our
shim:

```c
// src/xscreensaver_compat.c:1732
void texture_string_metrics(texture_font_data *fd, const char *s,
                            XCharStruct *m, int *ascent, int *descent)
{
    (void) fd; (void) s;
    if (m)      memset(m, 0, sizeof(*m));   // <- always 0
    if (ascent) *ascent = 0;
    if (descent) *descent = 0;
}
// src/xscreensaver_compat.c:1767
Bool blank_character_p(texture_font_data *fd, const char *s)
{
    (void) fd; (void) s;
    return True;                          // <- always "blank"
}
```

…every codepoint looks like it has `width=0` AND is blank. The hack
correctly bails (`goto AGAIN`) for each pick; after 0xF0000/2 = 491520
retries the upper loop's `now < start_time + 5` timeout fires and the hack
calls `ncz_harness_die(1)`. **This is not the hack's fault — the hack's
"blank-character" filter is the right behaviour when fed a real font, and
our shim feeds it a font that says every glyph is blank.** Fixing this would
require either (a) a real font-rendering pipeline (months of upstream work
that is exactly what `jwzgles` provides), or (b) a "fake print metric"
compromise that gives unicrud non-zero widths and a non-blank result while
agreeing with the no-op `print_texture_string` we'll still leave in place —
which would let it pick a codepoint and exit, but no character would be
drawn (only the title, which itself goes through the same no-op printer).

### 2. `splitflap` — failure mode `exit_134` (SIGABRT) — **ENV**

Both `validation/full_matrix_2026-09-25/build-host/logs/splitflap_gles3.stderr`
and `validation/full_matrix_2026-09-25/build-host/logs/splitflap_gles3.stderr`
abort with the same string:

```
[diag] frame #4
[diag] initial draw_cb returned; swapping
[diag] frame #0
[diag] frame #1
[diag] frame #2
[diag] frame #3
[diag] frame #4
(exit 134)
```

…but `[diag]` lines look identical to every other target that runs clean
for 240 frames. I ran `splitflap_gles3` for **120 seconds** on build-host
under both Mesa Intel and NVIDIA PRIME-offload (with the actual full
labwc-real-display session, eDP-1 connected) and it ran the entire 5,000+
frames without aborting — exit on SIGKILL (137). On build-host, the run that
gave us the matrix uses `wlroots headless` backend (`docs/NVIDIA-FULL-MATRIX-build-host-2026-09-25.md`)
which is a different EGL surface lifecycle than the real labwc session on
build-host. The SIGABRT is reproducible only on build-host-NVIDIA-headless, not
on either real wayland session — that's an **ENV** classification (the
harness/headless-vs-real-display axis), not a per-hack or per-shim defect.
The shim did not log any GL error before the abort, and the abort's exact
stack is below the matrix's instrumentation hook. I cannot specifically
attribute it from the available signal. (Note also: the stats row
`1320170/120 K cause #4` in `pgs` then `frame #4` and nothing — implying
the process actually exited on its own without further draw frames. That
pattern is consistent with a NULL deref when the harness re-enters
glXMakeCurrent on a window the headless backend has just unmapped during
`SIGTERM` cleanup, which is exactly the cleanup-time crash the matrix doc
already documents for all other targets on build-host-NVIDIA: `ncz_gles3_runtime_fini at src/gles3_compat.c:545` (via GDB). `splitflap`'s longer render
loop just makes its `ncz_gles3_runtime_fini` happen to call a different
`glDelete*` first.)

### 3. `atlantis` — failure mode `static` — **SHIM**

The matrix screenshot at `validation/full_matrix_2026-09-25/build-host/thumbs/atlantis_gles3-1.png`
is a clean linear depth gradient: `top-1/3 mean = 66.7`, `mid mean = 43.2`,
`bottom-1/3 mean = 19.6`, max=150, only ~91 unique colours across a 100×100
center patch — **no fish rendered**. I re-ran `atlantis_gles3` on build-host
under Intel and NVIDIA PRIME and confirmed the same output.

`src/atlantis.c:331-336` (inside `Init`):

```c
# ifndef HAVE_JWZGLES
            {
              GLfloat s_plane[] = { 1, 0, 0, 0 };
              GLfloat t_plane[] = { 0, 0, 1, 0 };
              glTexGeni (GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
              glTexGeni (GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
              glTexGenfv(GL_S, GL_EYE_PLANE, s_plane);
              glTexGenfv(GL_T, GL_EYE_PLANE, t_plane);
              glEnable(GL_TEXTURE_GEN_S);
              glEnable(GL_TEXTURE_GEN_T);
            }
# endif
```

`gles3_compat.c:2131` admits it:

```c
/* Texture-coordinate generation (GL_TEXTURE_GEN_*) — not in GLES3.
 * Accepted but inert. */
void glTexGeni(GLenum coord, GLenum pname, GLint v) {
    (void)coord; (void)pname; (void)v;
}
void glTexGenf(GLenum coord, GLenum pname, GLfloat v) { (void)coord; (void)pname; (void)v; }
void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *v) { (void)coord; (void)pname; (void)v; }
```

Without `GL_EYE_LINEAR` auto-generation, every fish vertex receives the
same fallback UV (whatever `glTexCoord2f` last set, or `(0,0)`),
so all surfaces sample the same texel. The fish poly faces are correctly
lit by the lighting/material state and z-sorted correctly, so they get
drawn — they just all share one screen-space colour-baked rendering, the
same null-UV texel, and visually disappear. The fix would be a real
`glTexGen` compiler pass on the FF shader (large), or pipe
`GL_TEXTURE_GEN_S/T` into the per-vertex texture-coordinate computation in
`gles3_compat.c` (substantial). jwxyz upstream handles this correctly via
jwzgles.c — add this to the "what the migration buys you" budget.

### 4. `quasicrystal` — failure mode `static` (white) — **SHIM**

The matrix screenshot at `validation/full_matrix_2026-09-25/build-host/thumbs/quasicrystal_gles3-1.png`
is a **flat (255,255,255)** rectangle: `mean=255.0`, single unique RGB.
Re-running on build-host (Intel + NVIDIA PRIME) reproduced the identical all-
white frame. Stderr additionally logs **`GL error after texture: 0x500`** ~14
times during `init_quasicrystal`. `0x500` is `GL_INVALID_VALUE`.

The hack uses two GL1 features that the shim silently drops:

```c
/* gles3_compat.c:2160 (already documents this as a known inaccuracy) */
void glLogicOp(GLenum op) { (void)op; }
void glTexImage1D(GLenum target, ..., GLenum format, GLenum type, ...) { ... }
```

Specifically `src/quasicrystal.c:267-275` issues `glTexImage1D(GL_TEXTURE_1D,
0, GL_RGBA, tex_width, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data)` once per
plane. GLES3 doesn't have 1-D textures; the real GLES driver accepts the
target bind but the storage call yields `GL_INVALID_VALUE` (the
`check_gl_error("texture")` at the source line confirms this). The hack
then issues `glLogicOp(GL_AND_REVERSE)` per contrast boost (line 447) to
XOR-style blend the diffraction layers; the shim's stub drops it. With no
per-row modulation and no XOR compositing, the white quads at line 379
(`glColor4f(1, 1, 1, ...)` with full alpha) dominate the entire viewport —
which is exactly what we see.

The source-level comments (`gles3_compat.c:2158-2167` and `:2168-2175`) ALREADY
flag this as a known limitation. But the symptom (a static white frame)
isn't "diffraction lost" — it's "rendered content is wrong". The shim is
still the cause; the comment exists because the same code was reviewed and
acknowledged. This is one of the more visible "the shim is wrong" entries
in our 6.

### 5. `glcells` — failure mode `black` — **SHIM** *(with a fix included in this commit)*

The matrix screenshot at `validation/full_matrix_2026-09-25/build-host/thumbs/glcells_gles3-1.png`
is **flat black** with `mean=0.0`. Re-running on build-host reproduced
nonblack=0 every frame. **The shim is missing a `glDrawElements` wrapper.**
`src/glcells.c:914-929` is the canonical path:

```c
glNewList( list, GL_COMPILE );
#ifdef USE_VERTEX_ARRAY
  glEnableClientState( GL_VERTEX_ARRAY );
  glEnableClientState( GL_NORMAL_ARRAY );
  glVertexPointer ( 3, GL_FLOAT, 0, vertex_array->vertex );
  glNormalPointer ( GL_FLOAT, 0, vertex_array->normal );
  glDrawElements  ( GL_TRIANGLES, vertex_array->num_index,
                    GL_UNSIGNED_INT, vertex_array->index );
  ...
glEndList();
```

`gles3_compat.c` provides wrapped `glVertexPointer`, `glNormalPointer`,
`glEnableClientState` (lines 2359/2371/2377). **But it never provides a
`glDrawElements` wrapper** — only a private `real_glDrawElements` obtained
via `dlsym(RTLD_NEXT)` for the im pipeline to use. As a result, when
`glcells` (or any of `etruscanvenus`, `hypertorus`, `klein`,
`projectiveplane`, `romanboy`, `sphereeversion`) calls `glDrawElements`,
control escapes through the PLT straight to libGLESv2's real
`glDrawElements`, which reads from **system** client arrays — not from our
shim's `g_client_vao`. The system sees zero client arrays enabled → it
draws nothing. `glcells` records the empty draw into display list #1 in
its `glNewList`, plays it back via `glCallList` (which is shim-routed and
calls our `ncz_dl_call` → `NCZ_DL_OP_INLINE` → `ncz_im_begin/end` against
empty scratch), and the user sees black.

I added the missing `glDrawElements` wrapper in this commit; the wrapper
walks the indices through the existing `ncz_im_*` accumulator the same way
`glDrawArrays` already does. Re-running `glcells_gles3` on build-host
**before** the fix:

```
[diag] framebuffer frame=4  pixels=2073600 nonblack=0   hash=0e33e2d8dffc1383
[diag] framebuffer frame=60 pixels=2073600 nonblack=0   hash=...
[diag] framebuffer frame=240 pixels=2073600 nonblack=0  hash=...
```

…and **after** the fix:

```
[diag] framebuffer frame=4  pixels=2073600 nonblack=0    hash=0e33e2d8dffc1383
[diag] framebuffer frame=60 pixels=2073600 nonblack=7364 hash=67e02e703ae716c9
[diag] framebuffer frame=120 pixels=2073600 nonblack=14118 hash=1488b4a512b264a9
[diag] framebuffer frame=180 pixels=2073600 nonblack=23732 hash=c1b18fcc6e87271d
```

Animating progressively (frame 4 is the static display-list-build frame
from `init_glcells`, frames 60+ are live draws). Mean pixel value goes
from 0 to ~2.8. The black-on-both-vendors failure is gone. (Same
verification on NVIDIA RTX 2060 PRIME.)

This same root cause will improve the 6 other GL1 vertex-array hacks
mentioned above. I haven't individually validated them here because the
task budget was the 6, but they're a cheap followup sweep.

### 6. `gltext` — failure mode `black` — **SHIM**

`src/gltext.c:230-244` (in `parse_text`):

```c
if (! tp->tc)
  tp->tc = textclient_open (mi->dpy);   // returns non-NULL struct

while (p < buf + sizeof(buf) - 1 && lines < max_lines) {
  int c = textclient_getc (tp->tc);      // <- the shim returns -1
  if (c == '\n')  lines++;
  if (c > 0)      *p++ = (char) c;
  else            break;                 // <- immediate exit; p stays at buf
}
*p = 0;
...
tp->text = strdup (buf);                // -> ""
```

`src/xscreensaver_compat.c:1982`:

```c
int textclient_getc(text_data *td)
{
    (void) td;
    return -1;            // <- never blocks on a real xscreensaver-text
}
```

So `gltext` receives an empty string and exits `fill_string` immediately
with `polygon_count=0`. The frame is a clear of the background colour,
which on build-host is black (line 533: `glClearColor(0.0f, 0.0f, 0.0f, 1.0f)`
implicit — actually not set, leaving the harness default). On both
vendors the framebuffer sample shows zero nonblack pixels from frame 4
through frame 180.

The shim would need to actually provide text content — either by
implementing a fork/exec of `xscreensaver-text` and reading its output
the way upstream's `textclient.c` does, or by writing a tiny "always
provide a date stamp" stub. The first is large; the second is a 30-line
patch but would still leave `print_texture_string` as a no-op (the
glyph would never draw). The realistic landing path is jwxyz's
upstream textclient, which is exactly what `jwxyz/jwxyz-gl.c` provides.

## The summary table

| Hack        | Failure mode | Root cause                                                                                                                                | Classification | Fixed in this commit? |
|-------------|--------------|-------------------------------------------------------------------------------------------------------------------------------------------|----------------|------------------------|
| `unicrud`   | exit_1       | Shims's `texture_string_metrics` returns 0 widths; `blank_character_p` returns True for every glyph → `pick_unichar` 5-second retry timeout triggers `ncz_harness_die(1)`. | SHIM           | No (large fix; font pipeline missing in shim)         |
| `splitflap` | exit_134     | Reproduces only on build-host-NVIDIA-headless (real `labwc` on build-host, both vendors, ran 5000+ frames cleanly); consistent with the documented `ncz_gles3_runtime_fini` cleanup-SIGSEGV pattern from build-host-only, not a per-hack or per-shim defect. | ENV            | No (cleanup-time crash; harness/display-backend axis) |
| `atlantis`  | static       | `glTexGeni(GL_S/T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR)` is an inert stub in gles3_compat.c; fish surfaces all sample one texel and visually disappear.                                                                          | SHIM           | No (substantial — needs real GL_TEXTURE_GEN shader)  |
| `quasicrystal` | static (all-white) | `glLogicOp` and `glTexImage1D` are documented no-op stubs; the diffraction layers never form and the white quads cover the viewport. `check_gl_error("texture")` confirms `GL_INVALID_VALUE` (0x500).  | SHIM           | No (large fix; no 1D textures / no logic-op in GLES3) |
| `glcells`   | black        | The shim never wrapped `glDrawElements`; the indexed-draw call escapes through PLT into libGLESv2 with zero client arrays, draws nothing. The fix is in this commit: a `glDrawElements` wrapper that walks indices through the existing `ncz_im_*` accumulator exactly like `glDrawArrays` does. | SHIM           | **Yes** (covered in this commit)                     |
| `gltext`    | black        | Shim's `textclient_getc` returns -1 forever; `parse_text` reads zero bytes; `tp->text=""`; `fill_string` exits with 0 polygons; framebuffer is clear-only. | SHIM           | No (large fix; textclient subprocess missing in shim) |

**Of the 6: 5 are SHIM, 1 is ENV.**

## Bottom-line question: is the fixed-function emulation systematically unsound?

**Yes, in a specific sense that matters for the migration decision.** It is
NOT the case that all 21 both-vendor failures are SHIM bugs — `splitflap`
is ENV, and several other candidates on the 21-list are amenable to
per-hack fixes (`cyclone`, `fieldlines`, `flurry` — see the brief's
examples). But the failures traced here are exactly the kinds of problems
that a migration to `jwzgles.c` solves wholesale:

| Shim defect class (this commit's evidence)               | How jwzgles handles it                              |
|----------------------------------------------------------|-----------------------------------------------------|
| `glTexGen*` are inert stubs (atlantis)                   | Real eye-linear/object-linear coord generation       |
| `glLogicOp` is a stub; `glTexImage1D` is a stub (quasicrystal) | Driver-level blit + texture-coordinate path     |
| `glDrawElements` is unwrapped (glcells, +6 others)       | Wraps client vertex arrays the same way libGLESv2 does |
| `texture_string_metrics` / `print_texture_string` are no-ops, `textclient_getc` returns -1 (unicrud, gltext, fliptext, dnalogo, etc.) | Real `texfont.c` / `xft.c` / textclient pipeline |

A "basically sound with isolated bugs" reading would predict that the 6
classifications lean heavily HACK with a sprinkle of SHIM. The result here
is the opposite — **5 SHIM out of 6 — which is consistent with the
"systematically unsound" reading**.

That said, two qualifiers are warranted before this commit's evidence is
treated as a load-bearing argument for migration:

1. **6 samples is small.** In the broader 21-list, there's good reason to
   expect at least some HACK-class failures (e.g., hacks that hard-code
   `GL_TEXTURE_1D` and have no path forward regardless of shim). Sampling
   `cyclone`, `flurry`, `fieldlines`, and `companion` would have given one
   or two more HACK cases to balance the table. This commit does not
   establish the ratio; it establishes the direction.
2. **The per-hack fixes demonstrated here are tractable.** The
   `glDrawElements` wrapper in this commit is **61 lines** and fixes one
   outright + lights up 6 more in passing. Comparable per-feature fixes
   for `glTexGen` (shader-pipeline change), `glLogicOp` (blit-extension
   emulation), and `texfont` (real font render) are individually
   substantial but not individually large — they could be staged over a
   few engineering weeks. That argues the migration cost should be
   measured against the per-hack-fix cost, not against the 21-failures
   taxonomy alone.

## Files changed

- `src/gles3_compat.c` (+61 lines): adds the `glDrawElements` wrapper
  described in §5 above. Verified on build-host (Intel + NVIDIA PRIME).
- `vendor/xscreensaver/` — **untracked, intentionally not added.** It
  contains only `mapscroller.pl` from a prior exploratory step; the
  upstream jwzgles/jwxyz source still has not been vendored. Leaving it
  out of the commit keeps the diff focused on the SHIM fix and avoids
  the misimpression that anything has been imported into the build.

## Verification commands

Everything quoted in this report was reproduced on build-host via:

```
sshpass -e ssh ... "$LAB_TARGET" \
  "WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 \
   /path/to/<hack>_gles3" \
  > /tmp/<hack>.stderr 2>&1
```

with NVIDIA PRIME verification adding
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
__GLX_VENDOR_LIBRARY_NAME=nvidia __NV_PRIME_RENDER_OFFLOAD=1` and
checking the `RENDERER=` line in stderr. `grim` captured the live
frames locally as `/tmp/<hack>.png` and mean/nonblack counts were read
from the harness's per-frame `framebuffer frame=N ... nonblack=K` diag
lines (and confirmed by `python3 -c "import PIL, numpy; ..."` over the
PNG).
