/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_hyprsaver.c — generic GLES3 wrapper for the 35 hyprsaver GLSL
 *                     fragment shaders vendored at vendor/hyprsaver/.
 *
 * Designed to plug into the gles3_harness.c init_cb / draw_cb / free_cb
 * triple. meson sets:
 *
 *   -DSHADER_FILE=<basename>      which .frag to load (relative to
 *                                 vendor/hyprsaver/shaders/)
 *   -DHACK_PREFIX=<ident>         symbol stem (init_<ident> / draw_<ident>
 *                                 / free_<ident> /
 *                                 <ident>_xscreensaver_function_table)
 *
 * The same .c file compiles 35 times — one per shader — with different
 * -D values, producing 35 independent executables. See the foreach
 * loop in meson.build.
 *
 * -----------------------------------------------------------------------
 * Hyprsaver's shaders all target #version 320 es (GLES 3.2). Confirmed
 * via `grep -h '^#version' vendor/hyprsaver/shaders/*.frag | sort -u`:
 *
 *   #version 320 es
 *   precision mediump float;
 *
 * Uniforms (from `grep -h '^uniform' ... | sort -u`):
 *
 *   uniform float u_time;          seconds since init
 *   uniform vec2  u_resolution;    viewport size in pixels
 *   uniform vec2  u_mouse;         mouse position in pixels
 *   uniform int   u_frame;         frame counter
 *   uniform float u_alpha;         (optional, used by some shaders)
 *   uniform float u_speed_scale;   (optional)
 *   uniform float u_zoom_scale;    (optional)
 *
 * -----------------------------------------------------------------------
 * Round 15 — preamble / palette-injection fix.
 *
 * Root cause: hyprsaver's upstream Rust runtime (`maravexa/hyprsaver`,
 * `src/shaders.rs`, `prepare_shader()`) NEVER compiles a vendored
 * `.frag` as-is. It always splits the raw source into a leading
 * `#version`/`precision` header and a body, then prepends a generated
 * preamble (uniform decls + a `vec3 palette(float t)` LUT-sampling
 * helper + a `void main()` wrapper that calls `_hyprsaver_main()` and
 * multiplies `fragColor *= u_alpha`) before compiling. The previous
 * version of this .c skipped the preamble entirely and just
 * speculatively *queried* uniform locations — which is why every
 * single one of the 35 binaries failed GLSL compile with
 * `u_speed_scale' undeclared` and `no function with name 'palette'`.
 *
 * Round 15 ports `prepare_shader()` verbatim (algorithm steps 1..7 in
 * the brief). The vendored `.frag` files themselves are unchanged —
 * they were correct; our port just wasn't inserting the preamble.
 * Palette injection is scoped down to a baked LUT (see PALETTE_LUT
 * block below) instead of upstream's full PNG/cosine/gradient +
 * hot-reload system; see PORTED.md §13.3.
 * -----------------------------------------------------------------------
 *
 * Shaders carry the same permissive MIT-ish license as hyprsaver itself,
 * which is preserved at vendor/hyprsaver-LICENSE.txt and credited in
 * PORTED.md alongside the notes below.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <GLES3/gl32.h>

#include "ncz_gl.h"
#include "ncz_hack_shim.h"

/* Resolved at compile time by -DSHADER_FILE=...frag / -DHACK_PREFIX=...
 * in meson.build's `hyprsaver_shaders` foreach. Defaults are picked so
 * the .c still compiles standalone for sanity-checking. */
#ifndef SHADER_FILE
#define SHADER_FILE "tunnel.frag"
#endif
#ifndef HACK_PREFIX
#define HACK_PREFIX hyprsaver_tunnel
#endif
#ifndef HACK_TABLE
#define HACK_TABLE hyprsaver_tunnel
#endif

/* HACK_PREFIX_str — the expanded token "hyprsaver_<shader>" with the
 * trailing underscore for clean symbol composition. We use a
 * two-stage token-paste (CAT_EXPAND expands its args before
 * pasting) to force the HACK_PREFIX macro to be expanded; without
 * it C99's preprocessor would emit the literal
 * "HACK_PREFIX_" because `##` suppresses macro expansion of its
 * operands.
 *
 * IMPORTANT C-preprocessor rule we rely on elsewhere: the lexer
 * does NOT do sub-token lookups. `init_HACK_PREFIX_STR` is one
 * identifier, and the preprocessor will only look THAT up as a
 * macro. So we ALSO define `init_HACK_PREFIX_STR`,
 * `draw_HACK_PREFIX_STR`, etc. as separate macros below — each
 * expands to the desired per-shader symbol name. */

/* Stringification of the per-shader prefix. The C preprocessor
 * suppresses macro expansion of the operand of `#`, so we indirect
 * through one extra macro layer — STR_EXPAND expands HACK_PREFIX
 * before STR sees it. */
#define STR(x) #x
#define STR_EXPAND(x) STR(x)
#define HACK_PREFIX_STR STR_EXPAND(HACK_PREFIX)

/* Per-shader function-name symbols. The function-table emit in
 * XSCREENSAVER_MODULE_2 uses `init_##PREFIX` (no underscore separator),
 * so the function names are `init_hyprsaver_<shader>` — NOT
 * `init_hyprsaver_<shader>_`. We define them as their own one-token
 * macros here because C preprocessor does NOT do sub-token lookups. */
#define CAT(a, b) a##b
#define CAT_EXPAND(a, b) CAT(a, b)
#define init_HACK_PREFIX_STR_    CAT_EXPAND(init_, HACK_PREFIX)
#define draw_HACK_PREFIX_STR_    CAT_EXPAND(draw_, HACK_PREFIX)
#define free_HACK_PREFIX_STR_    CAT_EXPAND(free_, HACK_PREFIX)
#define reshape_HACK_PREFIX_STR_ CAT_EXPAND(reshape_, HACK_PREFIX)
#define release_HACK_PREFIX_STR_ CAT_EXPAND(release_, HACK_PREFIX)

/* ------------------------------------------------------------------- */
/* Per-hack state                                                      */
/* ------------------------------------------------------------------- */

typedef struct {
    GLuint        program;
    GLuint        vbo;

    /* Uniform locations (cached once at init). A shader that doesn't
     * declare a given uniform will return -1 from glGetUniformLocation
     * — we still try to update it; GL silently no-ops. */
    GLint         loc_u_time;
    GLint         loc_u_resolution;
    GLint         loc_u_mouse;
    GLint         loc_u_frame;
    GLint         loc_u_alpha;
    GLint         loc_u_speed_scale;
    GLint         loc_u_zoom_scale;
    /* Palette LUT uniforms (Round 15 — preamble injection). -1 if
     * the shader doesn't reference them (e.g. one of the few that
     * define their own `vec3 palette(...)`). */
    GLint         loc_u_lut_a;
    GLint         loc_u_lut_b;
    GLint         loc_u_palette_blend;

    /* Palette LUT texture (Round 15). One 256x1 RGBA8 baked from a
     * cosine-gradient palette (a + b*cos(2*PI*(c*t+d))). Uploaded to
     * BOTH u_lut_a and u_lut_b texture units with u_palette_blend=0.0
     * — so the injected `palette()` helper resolves to a single,
     * stable palette with no per-frame blending machinery. The
     * GLuint is 0 if the shader has its own palette() (no LUT
     * needed). */
    GLuint        palette_lut_tex;
    GLenum        palette_lut_target;  /* GL_TEXTURE_2D or GL_TEXTURE_1D */

    double        start_time;
    unsigned long frame;
    /* Cached viewport size — taken from ModeInfo (filled in by the
     * harness on configure/reshape). */
    int           width;
    int           height;
} HyprsaverState;

/* ------------------------------------------------------------------- */
/* Vertex shader — trivial pass-through that emits a fullscreen quad.  */
/* ------------------------------------------------------------------- */
static const char *vert_src =
    "#version 320 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

/* ------------------------------------------------------------------- */
/* Trivial pass-through vertex shader + the .frag loaded at init.       */
/* ------------------------------------------------------------------- */

static double
now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static char *
read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr,
                "hyprsaver: cannot open shader %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

/* ------------------------------------------------------------------- */
/* prepare_shader() + palette LUT bake — Round 15 preamble fix.        */
/* ------------------------------------------------------------------- */

/* Palette LUT — Round 15 scope-down vs upstream parity.
 *
 * Upstream (`maravexa/hyprsaver`) loads user-configurable PNG, cosine,
 * or gradient palettes from a TOML config and supports hot-reload.
 * We have no config system (and don't need one for a screensaver
 * context). Instead we bake a small set of classic Inigo Quilez
 * cosine-gradient palettes directly into C constants here, sample 256
 * points along t∈[0,1) at init time on the CPU, and upload as a real
 * 256x1 RGBA8 texture bound to BOTH u_lut_a and u_lut_b with
 * u_palette_blend=0.0. Result: the injected `palette(t)` helper
 * resolves to a single stable palette per hack, no per-frame
 * blending, no hot-reload, no config system. This is a deliberate
 * scope-down from full upstream parity — see PORTED.md §13.3.
 *
 * The palette formula is the IQ cosine-gradient:
 *     color(t) = a + b * cos( 2*PI * (c*t + d) )
 * with a, b, c, d being vec3 parameters. The 6 palettes below are
 * hand-picked from https://iquilezles.org/articles/palettes/ for
 * "looks reasonable in a generic screensaver context". */

typedef struct {
    float a[3];
    float b[3];
    float c[3];
    float d[3];
} IQPalette;

static const IQPalette k_iq_palettes[6] = {
    /* Rainbow — full spectrum cycle, good general-purpose default. */
    { {0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.5f},
      {1.0f, 1.0f, 1.0f},
      {0.0f, 0.33f, 0.67f} },
    /* Sunset — warm orange/red/yellow, deep-sky feel. */
    { {0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.5f},
      {1.0f, 0.7f, 0.4f},
      {0.0f, 0.15f, 0.20f} },
    /* Ocean — blue/cyan/teal range, works well for aurora/clouds/caustics. */
    { {0.0f, 0.5f, 0.5f},
      {0.5f, 1.0f, 1.0f},
      {0.1f, 0.7f, 1.0f},
      {0.0f, 0.6f, 0.9f } },
    /* Forest — green/yellow range, calm / organic. */
    { {0.5f, 0.5f, 0.5f},
      {0.5f, 1.0f, 0.5f},
      {1.0f, 1.0f, 0.5f},
      {0.8f, 0.9f, 0.3f } },
    /* Fire — red/orange/yellow embers, hot/saturated. */
    { {0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.5f},
      {1.0f, 0.3f, 0.0f},
      {0.0f, 0.20f, 0.50f} },
    /* Violet — purple/pink/magenta range, calm / cosmic. */
    { {0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.5f},
      {1.0f, 0.5f, 1.0f},
      {0.25f, 0.50f, 0.75f} },
};

/* Bake one of the IQ palettes above into a 256-sample RGBA8 row and
 * upload as a 2D texture. `which` is the palette index in
 * k_iq_palettes[]. Returns the GL texture name (0 on failure). */
static GLuint
bake_palette_lut(int which) {
    if (which < 0) which = 0;
    if (which >= (int)(sizeof(k_iq_palettes) / sizeof(k_iq_palettes[0]))) {
        which = 0;
    }
    const IQPalette *p = &k_iq_palettes[which];

    /* 256 samples, t in [0, 1). */
    enum { N = 256 };
    unsigned char px[N * 4];
    for (int i = 0; i < N; i++) {
        float t = (float)i / (float)N;
        /* color(t) = a + b * cos( 2*PI * (c*t + d) ). The 2*PI
         * factor is baked into a/b/c/d's conventional IQ form,
         * where the article writes `2*PI*(c*t+d)` directly. */
        const float TWO_PI = 6.28318530717958647692f;
        float r = p->a[0] + p->b[0] * cosf(TWO_PI * (p->c[0] * t + p->d[0]));
        float g = p->a[1] + p->b[1] * cosf(TWO_PI * (p->c[1] * t + p->d[1]));
        float b = p->a[2] + p->b[2] * cosf(TWO_PI * (p->c[2] * t + p->d[2]));
        /* Clamp to [0, 1] before quantising to 8-bit. IQ's formulas
         * are designed to stay in range for the canonical palettes,
         * but a hand-edit or third-party palette could overshoot. */
        if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
        if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
        if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;
        px[i * 4 + 0] = (unsigned char)(r * 255.0f + 0.5f);
        px[i * 4 + 1] = (unsigned char)(g * 255.0f + 0.5f);
        px[i * 4 + 2] = (unsigned char)(b * 255.0f + 0.5f);
        px[i * 4 + 3] = 255;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    /* GL_CLAMP_TO_EDGE so out-of-range t (which the shader clamps to
     * [0,1] anyway) reads the endpoint color rather than wrapping or
     * sampling black. Linear filter so the 256-sample row is smooth
     * under any u_palette_blend cross-fade even though we currently
     * keep blend=0.0. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* Use GL_RGBA as internalformat for maximum driver compatibility —
     * GL_RGBA8 is the size-only form but some embedded GLES drivers
     * (notably the Mali-G720 Panthor path on O6N with the CIX vendor
     * blob) have been observed to silently re-interpret a 1-pixel-tall
     * GL_RGBA8 texture as luminance when read back, which makes the
     * injected `vec3 palette()` return (intensity, intensity, intensity)
     * — observed on O6N 2026-09-22 with hyprsaver_aurora_gles3 et al.:
     * grim captured grayscale blobs instead of the expected rainbow.
     * Forcing GL_RGBA keeps all four channels distinct through the
     * upload. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 N, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

/* Stable, simple hash of the shader basename so each hack gets a
 * deterministic palette index from the 6-entry IQ set. Not
 * cryptographic — just a multiplicative string hash that maps
 * "aurora", "blob", "attitude", ... to 0..5. */
static int
pick_palette_for(const char *basename) {
    if (!basename) return 0;
    unsigned long h = 5381;
    for (const char *p = basename; *p; p++) {
        h = ((h << 5) + h) + (unsigned long)(unsigned char)*p;
    }
    int n = (int)(sizeof(k_iq_palettes) / sizeof(k_iq_palettes[0]));
    return (int)(h % (unsigned long)n);
}

/* Append a C string to a heap-allocated, growing buffer. The buffer
 * starts NULL; on first call we malloc a reasonable initial size and
 * grow on demand. Returns 0 on success, -1 on OOM (and leaves *buf
 * unchanged). The caller's ownership of *buf is preserved on success
 * (it may be reallocated; the pointer is updated). */
static int
strbuf_append(char **buf, size_t *len, size_t *cap, const char *s) {
    if (!s) return 0;
    size_t add = strlen(s);
    size_t need = *len + add + 1;
    if (need > *cap) {
        size_t new_cap = (*cap == 0) ? 1024 : *cap;
        while (new_cap < need) new_cap *= 2;
        char *nb = (char *)realloc(*buf, new_cap);
        if (!nb) return -1;
        *buf = nb;
        *cap = new_cap;
    }
    memcpy(*buf + *len, s, add);
    *len += add;
    (*buf)[*len] = '\0';
    return 0;
}

/* prepare_shader() — Round 15 preamble injection.
 *
 * Mirrors upstream `maravexa/hyprsaver` `src/shaders.rs::prepare_shader()`
 * verbatim. The vendored .frag is treated as an unprocessed source
 * body; we prepend a generated preamble before compile.
 *
 * Algorithm (from the operator's brief):
 *   1. Split raw source into header (leading #version / precision
 *      lines, including the blank line that usually follows) and body
 *      (the rest). If no #version header, default to
 *      `#version 320 es\nprecision highp float;\n`.
 *   2. Output starts with the header.
 *   3. For each (needle, decl) pair, if needle is NOT a substring of
 *      the ORIGINAL raw source, append decl. Needle granularity
 *      varies deliberately — copy the upstream rule verbatim.
 *   4. If "vec3 palette(" is NOT in raw, append the upstream palette
 *      helper (LUT-sampling version).
 *   5. Shadertoy: skip — none of our 35 use it. (Quick check:
 *      grep -l 'void mainImage' vendor/hyprsaver/shaders/ -r.)
 *   6. Append the body.
 *   7. Wrap main: in the body, rename `void main()` →
 *      `void _hyprsaver_main()`; then append
 *          void main() { _hyprsaver_main(); fragColor *= u_alpha; }
 *
 * Returns a freshly-malloc'd C string the caller must free(), or
 * NULL on allocation failure. */
static char *
prepare_shader(const char *raw) {
    if (!raw) return NULL;

    /* --- step 1: split header vs body. ---
     *
     * "Leading #version / precision lines" means: starting at byte 0,
     * consume any lines whose first non-whitespace token is `#version`
     * or `precision` (the C preprocessor form `precision highp float;`).
     * Stop at the first line that isn't either of those. The header
     * INCLUDES the trailing blank line that almost always follows
     * (the vendored files all have `#version 320 es\nprecision highp
     * float;\n\n`). If no leading `#version` exists, fall back to the
     * canonical GLES 3.2 preamble. */
    const char *p = raw;
    int has_version_header = 0;
    while (1) {
        /* Skip leading whitespace on this line. */
        while (*p == ' ' || *p == '\t') p++;
        const char *line_start = p;
        /* Find end of this line. */
        while (*p && *p != '\n') p++;
        size_t line_len = (size_t)(p - line_start);
        int is_version_or_precision = 0;
        if (line_len >= 8 && memcmp(line_start, "#version", 8) == 0) {
            is_version_or_precision = 1;
            has_version_header = 1;
        } else if (line_len >= 9 && memcmp(line_start, "precision", 9) == 0) {
            is_version_or_precision = 1;
        }
        if (!is_version_or_precision) {
            /* Roll p back to the start of this line — it's body. */
            p = line_start;
            break;
        }
        /* Consume the newline (if present) and keep scanning. */
        if (*p == '\n') p++;
    }
    const char *header_end = p;
    const char *body_start = p;

    /* Also consume ONE trailing blank line from the header (vendored
     * files all have `#version 320 es\nprecision highp float;\n\n`
     * with a blank line between header and body). This keeps the
     * GLSL line numbers stable so glGetShaderInfoLog output is
     * meaningful. */
    if (*body_start == '\n') {
        header_end = body_start + 1;
        body_start = body_start + 1;
    } else {
        header_end = body_start;
    }

    size_t header_len = (size_t)(header_end - raw);
    size_t body_len   = strlen(body_start);

    /* --- step 7 (split out): build the wrapped body by renaming
     * `void main()` → `void _hyprsaver_main()`. This is done on a
     * copy of the body so we can do exact-substring replacement
     * safely. The replacement target is the literal `void main()`
     * with no leading/trailing whitespace — the vendored shaders
     * all have `void main() {` at column 0 of the main function. */
    char *body_wrapped = (char *)malloc(body_len + 64);
    if (!body_wrapped) return NULL;
    /* Worst-case body_wrapped is body_len + strlen("void main()")
     * → "void _hyprsaver_main()" expansion, which is +14 chars. */
    {
        const char *needle_body = "void main()";
        size_t needle_len = strlen(needle_body);
        const char *repl_body  = "void _hyprsaver_main()";
        size_t repl_len   = strlen(repl_body);

        size_t wi = 0;
        size_t ri = 0;
        while (ri < body_len) {
            if (ri + needle_len <= body_len &&
                memcmp(body_start + ri, needle_body, needle_len) == 0) {
                memcpy(body_wrapped + wi, repl_body, repl_len);
                wi += repl_len;
                ri += needle_len;
            } else {
                body_wrapped[wi++] = body_start[ri++];
            }
        }
        body_wrapped[wi] = '\0';
    }

    /* --- steps 2..6: build the output string. --- */
    char  *out = NULL;
    size_t out_len = 0;
    size_t out_cap = 0;

    /* 2. Header. */
    if (has_version_header) {
        if (strbuf_append(&out, &out_len, &out_cap, raw) != 0) goto oom;
        /* Truncate to header_len only. */
        out_len = header_len;
        out[out_len] = '\0';
    } else {
        const char *dflt = "#version 320 es\nprecision highp float;\n\n";
        if (strbuf_append(&out, &out_len, &out_cap, dflt) != 0) goto oom;
    }

    /* 3. Uniform decls — needle vs decl pair, in upstream order.
     *    Each needle is checked against the ORIGINAL raw source
     *    (NOT the body or the in-progress output), per the brief's
     *    "needle granularity varies deliberately — copy exactly" rule. */
    struct needle_decl { const char *needle; const char *decl; };
    static const struct needle_decl pairs[] = {
        { "u_time",                                 "uniform float u_time;\n"         },
        { "u_resolution",                           "uniform vec2 u_resolution;\n"    },
        { "u_mouse",                                "uniform vec2 u_mouse;\n"         },
        { "u_frame",                                "uniform int u_frame;\n"          },
        { "out vec4 fragColor;",                    "out vec4 fragColor;\n"           },
        { "uniform float u_alpha",                  "uniform float u_alpha;\n"        },
        { "uniform float u_speed_scale",            "uniform float u_speed_scale;\n"  },
        { "uniform float u_zoom_scale",             "uniform float u_zoom_scale;\n"   },
        { "uniform sampler2D u_prev_frame",         "uniform sampler2D u_prev_frame;\n"},
    };
    for (size_t i = 0; i < sizeof(pairs)/sizeof(pairs[0]); i++) {
        if (!strstr(raw, pairs[i].needle)) {
            if (strbuf_append(&out, &out_len, &out_cap, pairs[i].decl) != 0)
                goto oom;
        }
    }

    /* 4. Palette helper — only inject if the raw source doesn't
     *    already define its own `vec3 palette(...)`. Our 35 vendored
     *    shaders all CALL palette() but NONE of them define it (it's
     *    upstream-runtime-injected), so this fires for every one. */
    if (!strstr(raw, "vec3 palette(")) {
        const char *palette_block =
            "uniform sampler2D u_lut_a;\n"
            "uniform sampler2D u_lut_b;\n"
            "uniform float u_palette_blend;\n"
            "vec3 palette(float t) {\n"
            "    float tc = clamp(t, 0.0, 1.0);\n"
            "    vec3 col_a = texture(u_lut_a, vec2(tc, 0.5)).rgb;\n"
            "    vec3 col_b = texture(u_lut_b, vec2(tc, 0.5)).rgb;\n"
            "    return mix(col_a, col_b, u_palette_blend);\n"
            "}\n";
        if (strbuf_append(&out, &out_len, &out_cap, palette_block) != 0)
            goto oom;
    }

    /* 5. Shadertoy compat — none of our 35 use `void mainImage`,
     *    skipped by design. If a future vendored shader does, the
     *    upstream branch (no main() wrap, add a `void mainImage(out
     *    vec4 fragColor, in vec2 fragCoord)` pass-through) would go
     *    here. */

    /* 6. Append the (renamed) body. */
    if (strbuf_append(&out, &out_len, &out_cap, body_wrapped) != 0) goto oom;

    /* 7. Wrap main(). The body has been renamed so its main is now
     *    `_hyprsaver_main`; append the real main() that calls it and
     *    applies u_alpha. */
    {
        const char *wrap =
            "void main() {\n"
            "    _hyprsaver_main();\n"
            "    fragColor *= u_alpha;\n"
            "}\n";
        if (strbuf_append(&out, &out_len, &out_cap, wrap) != 0) goto oom;
    }

    free(body_wrapped);
    return out;

oom:
    free(body_wrapped);
    free(out);
    return NULL;
}

static GLuint
compile_shader(GLenum stage, const char *src, const char *tag) {
    GLuint s = glCreateShader(stage);
    if (!s) {
        fprintf(stderr, "hyprsaver: glCreateShader(%u) failed for %s\n",
                (unsigned)stage, tag);
        return 0;
    }
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)malloc((size_t)(len > 0 ? len : 1));
        if (log) {
            glGetShaderInfoLog(s, len, NULL, log);
            fprintf(stderr,
                    "hyprsaver: %s compile failed (%s):\n%s\n",
                    tag, "shader", log);
            free(log);
        }
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint
link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    if (!p) return 0;
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)malloc((size_t)(len > 0 ? len : 1));
        if (log) {
            glGetProgramInfoLog(p, len, NULL, log);
            fprintf(stderr, "hyprsaver: program link failed:\n%s\n", log);
            free(log);
        }
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------- */
/* init_/draw_/free_                                                  */
/* ------------------------------------------------------------------- */

static void
hyprsaver_init(ModeInfo *mi) {
    HyprsaverState *st = (HyprsaverState *)calloc(1, sizeof(*st));
    if (!st) {
        fprintf(stderr, "hyprsaver: OOM at init\n");
        ncz_harness_die(1);
    }
    mi->data = st;
    st->start_time = now_seconds();
    st->frame = 0;

    /* Resolve shader path. The vendor tree lives at vendor/hyprsaver/shaders/
     * relative to the source root. We run from build/ — but the
     * cwd is whatever the compositor invocation set. Try a few roots
     * so this works both in `ninja -C builddir` dev runs and from a
     * package install. */
    char path[1024];
    const char *candidates[] = {
        "vendor/hyprsaver/shaders/" SHADER_FILE,
        "../vendor/hyprsaver/shaders/" SHADER_FILE,
        "../../vendor/hyprsaver/shaders/" SHADER_FILE,
        "/usr/share/ncz-screensavers/shaders/" SHADER_FILE,
    };
    FILE *probe = NULL;
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        probe = fopen(candidates[i], "rb");
        if (probe) { fclose(probe); snprintf(path, sizeof(path), "%s", candidates[i]); break; }
    }
    if (!probe) {
        fprintf(stderr,
                "hyprsaver[%s]: cannot locate vendor/hyprsaver/shaders/%s\n",
                HACK_PREFIX_STR, SHADER_FILE);
        ncz_harness_die(1);
    }

    size_t frag_len = 0;
    char *frag_src = read_entire_file(path, &frag_len);
    if (!frag_src) ncz_harness_die(1);

    /* Round 15 — run the raw .frag through prepare_shader() to inject
     * the upstream preamble (uniform decls + palette() helper + main()
     * wrapper). The raw file is unchanged on disk; the transformed
     * string only exists in memory and is freed right after
     * glCompileShader copies it into GL. */
    char *frag_prepared = prepare_shader(frag_src);
    if (!frag_prepared) {
        fprintf(stderr,
                "hyprsaver[%s]: prepare_shader() failed (OOM)\n",
                HACK_PREFIX_STR);
        free(frag_src);
        ncz_harness_die(1);
    }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src, "vertex");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_prepared, "fragment");
    /* `frag_prepared` is copied into GL by glShaderSource; safe to
     * free now. `frag_src` is the raw .frag as-read — also free. */
    free(frag_prepared);
    free(frag_src);
    if (!vs || !fs) ncz_harness_die(1);
    st->program = link_program(vs, fs);
    /* Shaders can be deleted now; they're attached to the program. */
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!st->program) ncz_harness_die(1);

    glGenBuffers(1, &st->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    /* Fullscreen NDC quad — 2 triangles covering [-1, 1] x [-1, 1]. */
    const float verts[] = {
        -1.f, -1.f,
         1.f, -1.f,
        -1.f,  1.f,
        -1.f,  1.f,
         1.f, -1.f,
         1.f,  1.f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    /* This quad uses the default VAO and reasserts its VBO attribute
     * pointer per draw. The compat runtime's scratch VAO belongs to its
     * immediate-mode helpers and has a different attribute layout. */
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Cache uniform locations — a shader that doesn't declare a given
     * uniform returns -1 and we silently skip the per-frame update. */
    st->loc_u_time         = glGetUniformLocation(st->program, "u_time");
    st->loc_u_resolution   = glGetUniformLocation(st->program, "u_resolution");
    st->loc_u_mouse        = glGetUniformLocation(st->program, "u_mouse");
    st->loc_u_frame        = glGetUniformLocation(st->program, "u_frame");
    st->loc_u_alpha        = glGetUniformLocation(st->program, "u_alpha");
    st->loc_u_speed_scale  = glGetUniformLocation(st->program, "u_speed_scale");
    st->loc_u_zoom_scale   = glGetUniformLocation(st->program, "u_zoom_scale");
    /* Palette LUT uniforms — only present if prepare_shader() injected
     * the palette() helper (i.e. raw source didn't already define one).
     * loc_u_palette_blend stays at -1 if the shader has no palette()
     * (e.g. one of the few that defines its own). */
    st->loc_u_lut_a        = glGetUniformLocation(st->program, "u_lut_a");
    st->loc_u_lut_b        = glGetUniformLocation(st->program, "u_lut_b");
    st->loc_u_palette_blend = glGetUniformLocation(st->program, "u_palette_blend");

    /* Bake + upload the palette LUT. Only meaningful if the shader
     * actually references u_lut_a / u_lut_b (i.e. prepare_shader()
     * injected the palette helper). We pick ONE IQ palette per hack
     * deterministically by hashing the basename; this is the
     * Round 15 scope-down from upstream's full PNG/cosine/gradient
     * palette system. The same texture is bound to BOTH u_lut_a and
     * u_lut_b with u_palette_blend=0.0, so the injected palette()
     * resolves to a single, stable palette with no per-frame cross-
     * fade machinery. */
    if (st->loc_u_lut_a >= 0 || st->loc_u_lut_b >= 0 ||
        st->loc_u_palette_blend >= 0) {
        /* Derive the basename (strip ".frag") from SHADER_FILE so the
         * hash is stable across runs. SHADER_FILE is set by meson at
         * compile time. */
        const char *base = SHADER_FILE;
        const char *dot  = strrchr(SHADER_FILE, '.');
        if (dot) {
            /* Local buffer is fine — SHADER_FILE is small. */
            static char basebuf[64];
            size_t blen = (size_t)(dot - SHADER_FILE);
            if (blen >= sizeof(basebuf)) blen = sizeof(basebuf) - 1;
            memcpy(basebuf, SHADER_FILE, blen);
            basebuf[blen] = '\0';
            base = basebuf;
        }
        int which = pick_palette_for(base);
        st->palette_lut_tex = bake_palette_lut(which);
        st->palette_lut_target = GL_TEXTURE_2D;
        if (!st->palette_lut_tex) {
            fprintf(stderr,
                    "hyprsaver[%s]: palette LUT bake/upload failed\n",
                    HACK_PREFIX_STR);
            ncz_harness_die(1);
        }
    } else {
        st->palette_lut_tex = 0;
        st->palette_lut_target = GL_TEXTURE_2D;
    }

    fprintf(stderr,
            "[diag] hyprsaver[%s] init: GL_VERSION=%s, frag=%s, locs=time:%d res:%d mouse:%d frame:%d alpha:%d speed:%d zoom:%d lutA:%d lutB:%d blend:%d palette_tex:%u\n",
            HACK_PREFIX_STR,
            (const char *)glGetString(GL_VERSION),
            SHADER_FILE,
            st->loc_u_time, st->loc_u_resolution, st->loc_u_mouse,
            st->loc_u_frame, st->loc_u_alpha, st->loc_u_speed_scale,
            st->loc_u_zoom_scale,
            st->loc_u_lut_a, st->loc_u_lut_b, st->loc_u_palette_blend,
            (unsigned)st->palette_lut_tex);
}

static void
hyprsaver_draw(ModeInfo *mi) {
    HyprsaverState *st = (HyprsaverState *)mi->data;
    if (!st || !st->program) return;

    int w = mi->xgwa.width;
    int h = mi->xgwa.height;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    st->width = w;
    st->height = h;

    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(st->program);
    if (st->loc_u_time        >= 0) glUniform1f(st->loc_u_time,        (float)(now_seconds() - st->start_time));
    if (st->loc_u_resolution  >= 0) glUniform2f(st->loc_u_resolution,  (float)w, (float)h);
    if (st->loc_u_mouse       >= 0) glUniform2f(st->loc_u_mouse,       (float)w * 0.5f, (float)h * 0.5f);
    if (st->loc_u_frame       >= 0) glUniform1i(st->loc_u_frame,       (int)st->frame);
    /* Scaling uniforms — fixed defaults; the operator's brief says
     * "headless/screensaver context has no real value" for these. */
    if (st->loc_u_alpha       >= 0) glUniform1f(st->loc_u_alpha,       1.0f);
    if (st->loc_u_speed_scale >= 0) glUniform1f(st->loc_u_speed_scale, 1.0f);
    if (st->loc_u_zoom_scale  >= 0) glUniform1f(st->loc_u_zoom_scale,  1.0f);

    /* Palette LUT binding (Round 15). Bind the same baked texture to
     * BOTH u_lut_a (texture unit 0) and u_lut_b (texture unit 1), and
     * set u_palette_blend = 0.0 so the injected palette() helper
     * resolves to a single, stable palette with no cross-fade.
     * sampler2D binding via glUniform1i() — GL interprets the int as
     * a texture-unit index. If a particular uniform isn't present
     * (loc < 0) we still bind the texture but no harm done. */
    if (st->palette_lut_tex) {
        if (st->loc_u_lut_a >= 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(st->palette_lut_target, st->palette_lut_tex);
            glUniform1i(st->loc_u_lut_a, 0);
        }
        if (st->loc_u_lut_b >= 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(st->palette_lut_target, st->palette_lut_tex);
            glUniform1i(st->loc_u_lut_b, 1);
        }
        if (st->loc_u_palette_blend >= 0) {
            glUniform1f(st->loc_u_palette_blend, 0.0f);
        }
        /* Restore texture unit 0 for any subsequent fixed-function
         * helper calls (none currently in this wrapper, but cheap
         * to be tidy). */
        glActiveTexture(GL_TEXTURE0);
    }

    /* Bind the VBO and reassert the quad's attribute pointer each draw. */
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    /* The compat layer's glDrawArrays shim handles GL1 client arrays and
     * silently ignores VBO draws. Use its native GLES entry point here. */
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    st->frame++;
    if ((st->frame % 60) == 0) {
        fprintf(stderr,
                "[diag] hyprsaver[%s] frame=%lu t=%.2fs\n",
                HACK_PREFIX_STR, st->frame,
                now_seconds() - st->start_time);
    }
}

static void
hyprsaver_free(ModeInfo *mi) {
    HyprsaverState *st = (HyprsaverState *)mi->data;
    if (!st) return;
    if (st->vbo) glDeleteBuffers(1, &st->vbo);
    if (st->program) glDeleteProgram(st->program);
    if (st->palette_lut_tex) glDeleteTextures(1, &st->palette_lut_tex);
    free(st);
    mi->data = NULL;
}

static void
hyprsaver_reshape(ModeInfo *mi, int w, int h) {
    /* The viewport size is grabbed fresh each draw from mi->xgwa —
     * no per-reshape work needed for a fullscreen-quad shader. */
    (void)mi; (void)w; (void)h;
}

static Bool
hyprsaver_event(ModeInfo *mi, XEvent *e) {
    (void)mi; (void)e;
    return False;
}

/* The harness expects init_<HACK_PREFIX> etc. We generate the right
 * symbols by stringifying HACK_PREFIX with two layers of macro
 * expansion so the per-binary symbol stem matches what meson declared
 * in -DHACK_PREFIX=hyprsaver_<name>. */
/* HACK_PREFIX_STR + PASTE_EXPAND are defined earlier (after the
 * HACK_PREFIX/#endif block) so the .c body below can use them
 * everywhere, not only after this comment. */

/* Initialize the per-binary symbols. We use a run-time indirection so
 * the .c compiles into 35 different executables with one -D per row. */

/* Public linkage so the xscreensaver_function_table can reference
 * these symbols from the harness. */
void init_HACK_PREFIX_STR_(ModeInfo *mi)    { hyprsaver_init(mi); }
void draw_HACK_PREFIX_STR_(ModeInfo *mi)    { hyprsaver_draw(mi); }
void free_HACK_PREFIX_STR_(ModeInfo *mi)    { hyprsaver_free(mi); }
void reshape_HACK_PREFIX_STR_(ModeInfo *mi, int w, int h)
                                          { hyprsaver_reshape(mi, w, h); }
/* Event-handler symbol has its own convention — XSCREENSAVER_MODULE_2
 * emits `PREFIX##_handle_event` (NOT `handle_##PREFIX`). With
 * PREFIX=HACK_PREFIX that produces the literal identifier
 * `HACK_PREFIX_handle_event`, which the preprocessor then looks up
 * as a macro. We define that macro to expand to the desired
 * per-shader symbol name `<hyprsaver_<shader>>_handle_event`.
 * Function body is just a thin wrapper around the static
 * `hyprsaver_event()` helper so the actual logic stays in one place.
 * Public linkage is required because the table global references it
 * from outside this translation unit. */
/* Event-handler symbol has its own convention — XSCREENSAVER_MODULE_2
 * emits `PREFIX##_handle_event` (NOT `handle_##PREFIX`). With
 * PREFIX=HACK_PREFIX that becomes the literal identifier
 * `HACK_PREFIX_handle_event`, which the preprocessor then looks up
 * as a macro. Define the macro FIRST so the function name below
 * expands to the right per-shader symbol; the XSCREENSAVER_MODULE_2
 * table emit at the bottom of the file references the same name.
 * The function body is a thin wrapper around the static
 * `hyprsaver_event()` helper so the actual logic stays in one place.
 * Public linkage is required because the table global references
 * it from outside this translation unit. */
#define HACK_PREFIX_handle_event CAT_EXPAND(HACK_PREFIX, _handle_event)
Bool HACK_PREFIX_handle_event(ModeInfo *mi, XEvent *e) { return hyprsaver_event(mi, e); }

/* Stubs the XSCREENSAVER_MODULE_2 macro expects to exist for every
 * PREFIX. The hyprsaver shaders don't carry their own option tables
 * (they're pure GL frag-shader file loads with all behavior driven by
 * uniforms), so release_<prefix> is a no-op and <prefix>_opts is a
 * single-element array terminated by an empty descriptor. These are
 * weak — if a future shader wants real options it can override by
 * providing its own. */
void release_HACK_PREFIX_STR_(ModeInfo *mi) { (void)mi; }
/* Per-shader opts symbol. The XSCREENSAVER_MODULE_2 macro emits
 * `&PREFIX##_opts` for the .opts field. We pass HACK_PREFIX as
 * the PREFIX arg, so the table references `&HACK_PREFIX_opts`,
 * which we also define as a macro alias for the expanded token
 * `hyprsaver_<shader>_opts`.
 *
 * The shape of the symbol is a `ModeSpecOpt` struct (a single
 * value, not an array). Initialized to all zeros so the table's
 * numopts/numvars/opts/vars fields stay 0/NULL — the harness
 * never reads them. */
#define HACK_PREFIX_opts CAT_EXPAND(HACK_PREFIX, _opts)
static ModeSpecOpt HACK_PREFIX_opts = { 0, NULL, 0, NULL, NULL };

/* Module-table emit. We bypass xscreensaver_compat.h's
 * XSCREENSAVER_MODULE_2 macro here because it bakes in the
 * `_xscreensaver_function_table` suffix, which our harness sets
 * HACK_TABLE to. We emit the struct directly with
 * HACK_TABLE as the symbol name — that matches what
 * `extern struct xscreensaver_function_table HACK_TABLE;` in
 * gles3_harness.c expects (where HACK_TABLE is -D'd to
 * `<shader>_xscreensaver_function_table`). */
struct xscreensaver_function_table HACK_TABLE = {
    .name        = HACK_PREFIX_STR,
    .class_      = "Hyprsaver",
    .init_cb     = init_HACK_PREFIX_STR_,
    .draw_cb     = draw_HACK_PREFIX_STR_,
    .reshape_cb  = reshape_HACK_PREFIX_STR_,
    .event_cb    = HACK_PREFIX_handle_event,
    .free_cb     = free_HACK_PREFIX_STR_,
    .release_cb  = release_HACK_PREFIX_STR_,
    .opts        = &HACK_PREFIX_opts,
    .defaults_str = DEFAULTS,
};
