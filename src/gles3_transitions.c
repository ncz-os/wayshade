/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_transitions.c — generic GLES3 driver for the 125
 *                      gl-transition fragment shaders vendored
 *                      under vendor/gl-transitions/transitions/.
 *
 * Patterned after src/gles3_xshadertoy.c (one .c reused for many
 * shaders via -D flags). meson sets:
 *
 *   -DTRANSITION_FILE=<basename>.glsl  which .glsl to load
 *                                       (relative to
 *                                       vendor/gl-transitions/transitions/)
 *   -DHACK_PREFIX=<ident>              symbol stem (init_<ident> /
 *                                       draw_<ident> / free_<ident> /
 *                                       <ident>_xscreensaver_function_table)
 *   -DTRANSITION_DURATION=<float>      default 0.9 seconds
 *
 * The same .c file compiles 125 times — one per transition —
 * producing 125 independent executables (gles3_transition_<name>_gles3).
 *
 * ----------------------------------------------------------------------
 * Why this exists
 * ----------------------------------------------------------------------
 *
 * Upstream gl-transitions is a library of 125 GLSL fragment-shader
 * snippets. Each one defines `vec4 transition(vec2 uv)` plus a
 * `uniform float progress;` plus zero or more transition-specific
 * uniforms with `// = <default>` defaults inline. The driver wraps
 * each snippet into a complete GLES 3.0 fragment shader (preamble
 * declares `from`/`to` samplers, `progress` uniform, and
 * `getFromColor`/`getToColor` shims that call `texture(...)`), then
 * runs a per-frame loop that uploads `progress`, the easing curve,
 * and the parsed defaults.
 *
 * The driver is genuinely generic across all 125 transitions:
 * parameter defaults are parsed out of the .glsl body at
 * shader-build time, not hardcoded in C. Upstream changes to
 * default values flow through automatically. See `parse_uniforms`
 * for the parser.
 *
 * ----------------------------------------------------------------------
 * Vertical slice
 * ----------------------------------------------------------------------
 *
 * This commit ships the procedural-test-textures slice: the driver
 * creates two GL_RGBA8 textures, fills them with deterministic
 * per-frame procedural content (a moving radial gradient for
 * `from`, a moving horizontal-stripe pattern for `to`), binds
 * them as `from`/`to`, and runs the eased progress curve from
 * 0..1 over `TRANSITION_DURATION` seconds, then loops. Six PNG
 * captures are written via NCZ_FRAME_DUMP at equally-spaced
 * progress values, so the actual blend can be eyeballed in the
 * evidence pack.
 *
 * The next iteration replaces the procedural textures with FBO
 * captures of two running GLES3 screensavers — see the comment
 * block on `capture_source` (a stub for now).
 *
 * ----------------------------------------------------------------------
 * Licensing
 * ----------------------------------------------------------------------
 *
 * The vendored transition shaders retain their original MIT /
 * BSD-2-Clause / BSD-3-Clause per-file headers verbatim. See
 * vendor/gl-transitions-PORTED.md for the per-file licence
 * inventory. This driver file is project-original.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include <GLES3/gl32.h>

#include "ncz_gl.h"
#include "ncz_hack_shim.h"

#ifndef TRANSITION_FILE
#define TRANSITION_FILE "burn.glsl"
#endif
#ifndef HACK_PREFIX
#define HACK_PREFIX gles3_transition_burn
#endif
#ifndef HACK_PREFIX_ID
#define HACK_PREFIX_ID gles3_transition_burn
#endif
#ifndef HACK_TABLE
#define HACK_TABLE gles3_transition_burn_xscreensaver_function_table
#endif
#ifndef TRANSITION_DURATION
#define TRANSITION_DURATION 0.9
#endif

#define STR(x) #x
#define STR_EXPAND(x) STR(x)
#define HACK_PREFIX_STR STR_EXPAND(HACK_PREFIX)

#define CAT(a, b) a##b
#define CAT_EXPAND(a, b) CAT(a, b)
#define init_HACK_PREFIX_      CAT_EXPAND(init_, HACK_PREFIX_ID)
#define draw_HACK_PREFIX_      CAT_EXPAND(draw_, HACK_PREFIX_ID)
#define free_HACK_PREFIX_      CAT_EXPAND(free_, HACK_PREFIX_ID)
#define reshape_HACK_PREFIX_   CAT_EXPAND(reshape_, HACK_PREFIX_ID)
#define release_HACK_PREFIX_   CAT_EXPAND(release_, HACK_PREFIX_ID)

/* ------------------------------------------------------------------- */
/* Per-transition uniform record. The adapter parses every            */
/* `uniform <type> <name>; // = <default>` line out of the .glsl      */
/* body at shader-build time and uploads the default values to the   */
/* matching glUniform* location at draw time. We support a fixed      */
/* type vocabulary: float / int / bool / vec2 / vec3 / vec4. The     */
/* parser is intolerant of macros / typedefs / expression-typed       */
/* defaults — that is acceptable because no upstream transition uses  */
/* any of those.                                                      */
/* ------------------------------------------------------------------- */

#define MAX_UNIFORMS 32

typedef enum {
    UT_FLOAT = 1,
    UT_INT   = 2,
    UT_BOOL  = 3,
    UT_VEC2  = 4,
    UT_VEC3  = 5,
    UT_VEC4  = 6,
} UType;

typedef struct {
    char     name[64];
    UType    type;
    GLint    loc;
    /* Defaults stored as 4 floats (0/1/2/3 channels). bool is
     * stored as 0/1 in `values[0]`. */
    float    values[4];
} URec;

typedef struct {
    GLuint program;
    GLuint vbo;
    GLuint tex_from;
    GLuint tex_to;
    GLint  loc_progress;
    GLint  loc_from;
    GLint  loc_to;
    GLint  loc_resolution;
    int    width;
    int    height;
    URec   uniforms[MAX_UNIFORMS];
    int    num_uniforms;

    double start_time;
    double last_capture;   /* progress of last NCZ_FRAME_DUMP dump */
    unsigned long frame;
    unsigned long dump_seq;  /* NCZ_FRAME_DUMP captures per run */
    int    captures_done;
} TrState;

/* ------------------------------------------------------------------- */
/* Vertex shader — pass-through fullscreen quad.                      */
/* ------------------------------------------------------------------- */

static const char *vert_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

/* Fragment shader preamble: declares the two input samplers, the
 * progress uniform, and the getFromColor / getToColor shims.
 * Spliced in this order:
 *   preamble -> body (with author/license stripped) -> wrapper
 * so that any `transition(vec2 uv)` function defined in the body
 * is forward-resolved by the wrapper's call site. */
static const char *frag_preamble =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "\n"
    "out vec4 frag_color;\n"
    "\n"
    "uniform sampler2D from;\n"
    "uniform sampler2D to;\n"
    "uniform float progress;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "vec4 getFromColor(vec2 uv) {\n"
    "    return texture(from, uv);\n"
    "}\n"
    "vec4 getToColor(vec2 uv) {\n"
    "    return texture(to, uv);\n"
    "}\n";

/* GLSL ES 3.0 dropped `texture2D` (it's now `texture`). The
 * adapter body-rewrites the two upstream transitions that still
 * call texture2D (displacement, luma) by substituting the symbol
 * at splice time. Commented-out occurrences are left alone — they
 * never reach the compiler. */

static const char *frag_tail =
    "\nvoid main() {\n"
    "  vec2 uv = gl_FragCoord.xy / resolution;\n"
    "  frag_color = transition(uv);\n"
    "}\n";

/* ------------------------------------------------------------------- */
/* Helpers                                                             */
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
                "gles3_transition: cannot open shader %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

/* Find the transition shader. Same search order as
 * gles3_xshadertoy.c: override env -> build tree -> installed
 * absolute path. The "from /" launch path is exercised by the
 * absolute /usr/share/... lookup; that was the blackhole
 * regression that killed shipping. */
static char *
locate_transition(const char *name, char *out_used_path,
                  size_t out_path_cap) {
    static const char *prefixes[] = {
        "vendor/gl-transitions/transitions/",
        "../vendor/gl-transitions/transitions/",
        "../../vendor/gl-transitions/transitions/",
        "/usr/share/ncz-screensavers/shaders/",
        NULL,
    };
    const char *override = getenv("NCZ_SHADER_DIR");
    if (override && *override) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", override, name);
        FILE *f = fopen(p, "rb");
        if (f) {
            fclose(f);
            if (out_used_path && out_path_cap) {
                snprintf(out_used_path, out_path_cap, "%s", p);
            }
            return read_entire_file(p, NULL);
        }
        fprintf(stderr,
                "gles3_transition: NCZ_SHADER_DIR=%s set but "
                "%s not found\n", override, p);
    }
    for (int i = 0; prefixes[i]; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s%s", prefixes[i], name);
        FILE *f = fopen(p, "rb");
        if (f) {
            fclose(f);
            if (out_used_path && out_path_cap) {
                snprintf(out_used_path, out_path_cap, "%s", p);
            }
            return read_entire_file(p, NULL);
        }
    }
    fprintf(stderr,
            "gles3_transition: cannot locate %s in any known "
            "location\n", name);
    return NULL;
}

/* Returns the index of the first non-whitespace character on the
 * current line, given a position at the start of a line. */
static const char *
lskip(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Find the end of the current line. Returns a pointer to the
 * terminating '\n' or to the final '\0'. */
static const char *
line_end(const char *p) {
    while (*p && *p != '\n') p++;
    return p;
}

/* Skip the leading directive block of a vendored .glsl. We strip:
 *   - leading blank lines
 *   - leading C++ comment lines (the per-file `// Author:` /
 *     `// License:` headers and any other comment)
 *   - `#version`, `#extension`, `#pragma`, `precision` directives
 *
 * The first non-whitespace line that isn't one of those starts
 * the body and stays put. This is the same shim
 * gles3_xshadertoy.c uses (skip_leading_directives, lines
 * 288-303) — we keep the same shape but make it write into a
 * heap buffer since we also need to strip the author/license
 * comments even when they appear AFTER a precision directive
 * (which they do in some upstream files). */
static char *
strip_header_to_body(const char *src, char *out_marker) {
    /* out_marker (if non-NULL) receives a small string like
     * `// upstream-original-file: <first line of header>`
     * for provenance. Best-effort: if the header is empty we
     * just emit "// upstream-original-file: <TRANSITION_FILE>". */
    if (out_marker) {
        const char *first = src;
        /* Find first non-blank line. */
        while (*first == '\n') first++;
        const char *e = line_end(first);
        size_t n = (size_t)(e - first);
        if (n > 60) n = 60;
        snprintf(out_marker, 160,
                 "// upstream-original-file: %.*s\n", (int)n, first);
    }

    /* Walk lines, copying ones we keep into the output. */
    size_t cap = strlen(src) + 64;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t out_len = 0;
    int in_lead = 1;        /* still in the leading block */
    const char *p = src;

    while (*p) {
        const char *line_start = p;
        const char *eol = line_end(p);
        const char *q = lskip(line_start);
        int is_blank = (*q == '\n' || *q == '\0');
        int is_cpp_comment = (q[0] == '/' && q[1] == '/');
        int is_preprocessor = (*q == '#');
        int is_precision = is_preprocessor &&
            (strncmp(q, "#version", 8) == 0 ||
             strncmp(q, "#extension", 10) == 0 ||
             strncmp(q, "#pragma", 7) == 0 ||
             strncmp(q, "#define", 7) == 0 ||
             strncmp(q, "precision", 9) == 0);
        if (in_lead && (is_blank || is_cpp_comment ||
                        is_precision)) {
            /* Skip this line entirely (including its '\n'). */
            p = (*eol == '\n') ? eol + 1 : eol;
            continue;
        }
        in_lead = 0;
        /* Copy this line to output. */
        size_t llen = (size_t)(eol - line_start);
        if (out_len + llen + 2 > cap) {
            cap = (out_len + llen + 2) * 2;
            char *nb = (char *)realloc(out, cap);
            if (!nb) { free(out); return NULL; }
            out = nb;
        }
        memcpy(out + out_len, line_start, llen);
        out_len += llen;
        if (*eol == '\n') {
            out[out_len++] = '\n';
            p = eol + 1;
        } else {
            p = eol;
        }
    }
    if (out_len == 0) {
        free(out);
        return NULL;
    }
    out[out_len] = '\0';
    return out;
}

/* Substitute `texture2D` -> `texture` in the body. Two upstream
 * files (displacement, luma) use `texture2D(...)` in LIVE code.
 * GLSL ES 3.0 removed that name. The substitution is naive — text-
 * level — but those two files have no symbol `texture2D` in any
 * other context (no `vec4 texture2D;` declaration, no comment
 * containing it). */
static char *
rewrite_texture2d(const char *body) {
    const char *needle = "texture2D";
    size_t nlen = strlen(needle);
    size_t blen = strlen(body);
    /* Worst-case expansion: same length. */
    char *out = (char *)malloc(blen + 16);
    if (!out) return NULL;
    size_t oi = 0;
    const char *p = body;
    while (*p) {
        if (strncmp(p, needle, nlen) == 0) {
            /* Make sure we're not inside an identifier. */
            int left_ok  = (p == body) ||
                           (!(isalnum((unsigned char)p[-1]) || p[-1] == '_'));
            int right_ok = !(isalnum((unsigned char)p[nlen]) ||
                             p[nlen] == '_');
            if (left_ok && right_ok) {
                memcpy(out + oi, "texture", 7);
                oi += 7;
                p += nlen;
                continue;
            }
        }
        out[oi++] = *p++;
    }
    out[oi] = '\0';
    return out;
}

/* Build the final fragment-shader source. Body is the vendored
 * .glsl with the leading header stripped (author / license /
 * precision directives), and texture2D rewritten to texture. The
 * wrapper at the end calls transition(uv). */
static char *
build_frag_src(const char *body) {
    char *cleaned = strip_header_to_body(body, NULL);
    if (!cleaned) return NULL;
    char *rewritten = rewrite_texture2d(cleaned);
    free(cleaned);
    if (!rewritten) return NULL;

    size_t preamble_len = strlen(frag_preamble);
    size_t body_len = strlen(rewritten);
    size_t tail_len = strlen(frag_tail);
    char *out = (char *)malloc(preamble_len + body_len + tail_len + 4);
    if (!out) { free(rewritten); return NULL; }
    memcpy(out, frag_preamble, preamble_len);
    out[preamble_len] = '\n';
    memcpy(out + preamble_len + 1, rewritten, body_len);
    memcpy(out + preamble_len + 1 + body_len, frag_tail, tail_len + 1);
    free(rewritten);
    return out;
}

/* ------------------------------------------------------------------- */
/* Uniform-default parser                                              */
/* ------------------------------------------------------------------- */

static void
set_uniform_default(URec *u, UType t, const char *def_text) {
    u->type = t;
    /* Parse up to 4 floats. Booleans are read as 0/1. */
    int n = 0;
    if (t == UT_BOOL) {
        if (strcasecmp(def_text, "true") == 0)  u->values[0] = 1.f;
        else if (strcasecmp(def_text, "false") == 0) u->values[0] = 0.f;
        else u->values[0] = (float)atof(def_text);
        n = 1;
    } else if (t == UT_INT) {
        u->values[0] = (float)atoi(def_text);
        n = 1;
    } else if (t == UT_FLOAT) {
        u->values[0] = (float)atof(def_text);
        n = 1;
    } else {
        /* vecN: parse N floats, skipping `vecN(` prefix and `)`
         * suffix and commas. We accept any whitespace. */
        const char *p = def_text;
        while (*p && *p != '(' && !isdigit((unsigned char)*p) &&
               *p != '-' && *p != '+' && *p != '.') p++;
        int needed = 0;
        if (t == UT_VEC2) needed = 2;
        else if (t == UT_VEC3) needed = 3;
        else if (t == UT_VEC4) needed = 4;
        else needed = 1;
        for (int i = 0; i < needed && *p; i++) {
            char buf[64];
            int bi = 0;
            while (*p && (isdigit((unsigned char)*p) || *p == '.' ||
                          *p == '-' || *p == '+' || *p == 'e' ||
                          *p == 'E')) {
                if (bi < (int)sizeof(buf) - 1) buf[bi++] = *p;
                p++;
            }
            buf[bi] = '\0';
            u->values[i] = (float)atof(buf);
            n++;
            while (*p && (*p == ',' || *p == ' ' ||
                          *p == '\t' || *p == ')')) p++;
        }
    }
    /* Zero-fill any unused channels so the upload is well-defined. */
    for (int i = n; i < 4; i++) u->values[i] = 0.f;
}

/* Parse all `uniform <type> <name> [/* ... *\/]; // = <default>`
 * lines out of the body. Skips the `from`, `to`, and `progress`
 * uniforms (those are declared by the preamble). Skips `sampler2D`
 * uniforms — those are an upstream oddity we don't handle in this
 * commit (see compile failures / list of unsupported in
 * docs/superpowers/specs/2026-09-25-gl-transitions-curation.md). */
static void
parse_uniforms(TrState *st, const char *body) {
    st->num_uniforms = 0;
    const char *p = body;
    while (*p && st->num_uniforms < MAX_UNIFORMS) {
        const char *line = lskip(p);
        if (strncmp(line, "uniform", 7) == 0 &&
            (line[7] == ' ' || line[7] == '\t')) {
            const char *q = line + 7;
            while (*q == ' ' || *q == '\t') q++;
            /* Read type token. */
            char type_str[16];
            int ti = 0;
            while (*q && (isalnum((unsigned char)*q) || *q == '_') &&
                   ti < 15) {
                type_str[ti++] = *q++;
            }
            type_str[ti] = '\0';
            /* Read name token. */
            while (*q == ' ' || *q == '\t') q++;
            char name_str[64];
            int ni = 0;
            while (*q && (isalnum((unsigned char)*q) || *q == '_') &&
                   ni < 63) {
                name_str[ni++] = *q++;
            }
            name_str[ni] = '\0';
            const char *semi = strchr(q, ';');
            const char *eol  = line_end(q);
            if (!semi || semi > eol) {
                p = (*eol == '\n') ? eol + 1 : eol;
                continue;
            }
            /* Skip uniform declarations handled by preamble /
             * sampler2D (unsupported in this commit). */
            int skip = 0;
            if (strcmp(type_str, "sampler2D") == 0) skip = 1;
            if (strcmp(name_str, "from") == 0) skip = 1;
            if (strcmp(name_str, "to") == 0) skip = 1;
            if (strcmp(name_str, "progress") == 0) skip = 1;
            /* Look for `// = <default>` after the semicolon. */
            const char *ds = NULL;
            const char *r = semi;
            while (r < eol) {
                if (r[0] == '/' && r[1] == '/' &&
                    (r[2] == ' ' || r[2] == '=' || r[2] == '\t')) {
                    const char *eq = strchr(r, '=');
                    if (eq && eq < eol) {
                        ds = eq + 1;
                        break;
                    }
                }
                r++;
            }
            /* Also accept the `/* ... = ... *\/` style. */
            if (!ds) {
                r = semi;
                while (r < eol - 1) {
                    if (r[0] == '*' && r[1] == '=') {
                        ds = r + 2;
                        break;
                    }
                    r++;
                }
            }
            if (!skip && ds) {
                while (*ds == ' ' || *ds == '\t') ds++;
                /* Trim trailing whitespace before EOL. */
                char defbuf[256];
                int di = 0;
                const char *de = eol;
                while (de > ds && (de[-1] == ' ' || de[-1] == '\t'))
                    de--;
                while (ds < de && di < 255) {
                    char ch = *ds++;
                    if (ch == '*' && ds < de && *ds == '/') {
                        ds++; continue;
                    }
                    defbuf[di++] = ch;
                }
                defbuf[di] = '\0';
                UType ut = UT_FLOAT;
                if      (strcmp(type_str, "float") == 0) ut = UT_FLOAT;
                else if (strcmp(type_str, "int")   == 0) ut = UT_INT;
                else if (strcmp(type_str, "bool")  == 0) ut = UT_BOOL;
                else if (strcmp(type_str, "vec2")  == 0) ut = UT_VEC2;
                else if (strcmp(type_str, "vec3")  == 0) ut = UT_VEC3;
                else if (strcmp(type_str, "vec4")  == 0) ut = UT_VEC4;
                else {
                    p = (*eol == '\n') ? eol + 1 : eol;
                    continue;
                }
                URec *u = &st->uniforms[st->num_uniforms++];
                memset(u, 0, sizeof *u);
                snprintf(u->name, sizeof u->name, "%s", name_str);
                set_uniform_default(u, ut, defbuf);
            }
            p = (*eol == '\n') ? eol + 1 : eol;
            continue;
        }
        p = (*line_end(p) == '\n') ? line_end(p) + 1 : line_end(p);
    }
}

static void
upload_uniforms(TrState *st) {
    for (int i = 0; i < st->num_uniforms; i++) {
        URec *u = &st->uniforms[i];
        if (u->loc < 0) continue;
        switch (u->type) {
        case UT_FLOAT: glUniform1f(u->loc, u->values[0]); break;
        case UT_INT:
        case UT_BOOL:  glUniform1i(u->loc, (GLint)u->values[0]); break;
        case UT_VEC2:  glUniform2f(u->loc,
                                    u->values[0], u->values[1]); break;
        case UT_VEC3:  glUniform3f(u->loc, u->values[0], u->values[1],
                                    u->values[2]); break;
        case UT_VEC4:  glUniform4f(u->loc, u->values[0], u->values[1],
                                    u->values[2], u->values[3]); break;
        }
    }
}

/* ------------------------------------------------------------------- */
/* Compile / link                                                      */
/* ------------------------------------------------------------------- */

static GLuint
compile_shader(GLenum stage, const char *src) {
    GLuint s = glCreateShader(stage);
    if (!s) return 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei got = 0;
        glGetShaderInfoLog(s, sizeof log, &got, log);
        fprintf(stderr,
                "gles3_transition: %s shader compile failed:\n%s\n",
                stage == GL_VERTEX_SHADER ? "vertex" : "fragment",
                log);
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
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei got = 0;
        glGetShaderInfoLog(p, sizeof log, &got, log);
        fprintf(stderr, "gles3_transition: link failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------- */
/* Procedural source textures (vertical slice).                       */
/* ------------------------------------------------------------------- */

static void
fill_from(TrState *st, unsigned long frame) {
    /* Moving radial gradient — the centre orbits slowly. */
    int w = st->width, h = st->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    static unsigned char *buf = NULL;
    static size_t cap = 0;
    size_t need = (size_t)w * (size_t)h * 4;
    if (need > cap) {
        free(buf);
        buf = (unsigned char *)malloc(need);
        cap = need;
    }
    if (!buf) return;
    double t = (double)frame * 0.02;
    double cx = 0.5 + 0.25 * cos(t);
    double cy = 0.5 + 0.25 * sin(t * 0.7);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double u = (double)x / (double)w;
            double v = (double)y / (double)h;
            double dx = u - cx;
            double dy = v - cy;
            double r = sqrt(dx * dx + dy * dy);
            double k = 1.0 - r * 2.0;
            if (k < 0) k = 0;
            unsigned char R = (unsigned char)(255.0 * k * 0.9);
            unsigned char G = (unsigned char)(255.0 * k * 0.4);
            unsigned char B = (unsigned char)(255.0 * k * 0.7);
            unsigned char *p = buf + (size_t)(y * w + x) * 4;
            p[0] = R; p[1] = G; p[2] = B; p[3] = 255;
        }
    }
    glBindTexture(GL_TEXTURE_2D, st->tex_from);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

static void
fill_to(TrState *st, unsigned long frame) {
    /* Moving horizontal stripes with vertical drift. */
    int w = st->width, h = st->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    static unsigned char *buf = NULL;
    static size_t cap = 0;
    size_t need = (size_t)w * (size_t)h * 4;
    if (need > cap) {
        free(buf);
        buf = (unsigned char *)malloc(need);
        cap = need;
    }
    if (!buf) return;
    double t = (double)frame * 0.02;
    double off = fmod(t * 0.15, 1.0);
    for (int y = 0; y < h; y++) {
        double v = (double)y / (double)h;
        double stripe = sin((v + off) * 3.14159265 * 12.0);
        double k = 0.5 + 0.5 * stripe;
        unsigned char R = (unsigned char)(255.0 * k * 0.2);
        unsigned char G = (unsigned char)(255.0 * k * 0.95);
        unsigned char B = (unsigned char)(255.0 * k * 0.85);
        for (int x = 0; x < w; x++) {
            double u = (double)x / (double)w;
            double vign = 1.0 - 0.4 * (u - 0.5) * (u - 0.5) * 4.0;
            if (vign < 0) vign = 0;
            unsigned char *p = buf + (size_t)(y * w + x) * 4;
            p[0] = (unsigned char)(R * vign);
            p[1] = (unsigned char)(G * vign);
            p[2] = (unsigned char)(B * vign);
            p[3] = 255;
        }
    }
    glBindTexture(GL_TEXTURE_2D, st->tex_to);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

/* ------------------------------------------------------------------- */
/* XSCREENSAVER module                                                 */
/* ------------------------------------------------------------------- */

static double ease_in_out_quad(double t) {
    return (t < 0.5) ? (2.0 * t * t)
                     : (1.0 - pow(-2.0 * t + 2.0, 2.0) / 2.0);
}

static void
init_transition(ModeInfo *mi) {
    TrState *st = (TrState *)calloc(1, sizeof(*st));
    if (!st) { ncz_harness_die(1); return; }
    mi->data = st;

    char used_path[1024] = "";
    char *body = locate_transition(TRANSITION_FILE, used_path,
                                   sizeof used_path);
    if (!body) { ncz_harness_die(1); return; }
    fprintf(stderr, "[diag] gles3_transition file=%s\n", used_path);

    parse_uniforms(st, body);

    char *frag_src = build_frag_src(body);
    free(body);
    if (!frag_src) { ncz_harness_die(1); return; }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    free(frag_src);
    if (!vs || !fs) { ncz_harness_die(1); return; }
    st->program = link_program(vs, fs);
    if (!st->program) { ncz_harness_die(1); return; }

    st->loc_progress   = glGetUniformLocation(st->program, "progress");
    st->loc_from       = glGetUniformLocation(st->program, "from");
    st->loc_to         = glGetUniformLocation(st->program, "to");
    st->loc_resolution = glGetUniformLocation(st->program, "resolution");

    for (int i = 0; i < st->num_uniforms; i++) {
        st->uniforms[i].loc =
            glGetUniformLocation(st->program, st->uniforms[i].name);
    }

    /* Fullscreen quad. */
    static const float quad[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };
    glGenBuffers(1, &st->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenTextures(1, &st->tex_from);
    glBindTexture(GL_TEXTURE_2D, st->tex_from);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &st->tex_to);
    glBindTexture(GL_TEXTURE_2D, st->tex_to);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    st->start_time  = now_seconds();
    st->last_capture = -1.0;
    fprintf(stderr,
            "[diag] gles3_transition init ok: program=%u vbo=%u "
            "uniforms=%d GL=%s progress=%d from=%d to=%d res=%d\n",
            st->program, st->vbo, st->num_uniforms,
            (const char *)glGetString(GL_VERSION),
            st->loc_progress, st->loc_from, st->loc_to,
            st->loc_resolution);
}

static void
draw_transition(ModeInfo *mi) {
    TrState *st = (TrState *)mi->data;
    if (!st || !st->program) return;

    int w = mi->xgwa.width;
    int h = mi->xgwa.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    st->width = w;
    st->height = h;

    double now = now_seconds();
    double elapsed = now - st->start_time;
    float duration = (float)TRANSITION_DURATION;
    const char *dur_env = getenv("NCZ_TRANSITION_DURATION");
    if (dur_env && *dur_env) {
        float dv = (float)atof(dur_env);
        if (dv > 0.05f) duration = dv;
    }

    /* Loop: 0..1 over `duration`, hold at 1 for `duration` more,
     * then restart from 0. Visible at any frame rate — the
     * captures land at evenly-spaced progress values. */
    double phase = fmod(elapsed, duration * 2.0) / duration;
    float linear;
    if (phase < 1.0) {
        linear = (float)phase;
    } else {
        linear = 1.0f;
    }
    float progress = (float)ease_in_out_quad((double)linear);

    fill_from(st, st->frame);
    fill_to(st, st->frame);
    st->frame++;

    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(st->program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, st->tex_from);
    if (st->loc_from >= 0) glUniform1i(st->loc_from, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, st->tex_to);
    if (st->loc_to >= 0) glUniform1i(st->loc_to, 1);

    if (st->loc_progress   >= 0) glUniform1f(st->loc_progress, progress);
    if (st->loc_resolution >= 0)
        glUniform2f(st->loc_resolution, (float)w, (float)h);

    upload_uniforms(st);

    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    /* Diagnostic samples — same cadence as xshadertoy. */
    if (ncz_diag_sample_frame(st->frame, 30)) {
        unsigned char px[16] = {0};
        glReadPixels(w/2, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glReadPixels(w/8, h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+4);
        glReadPixels(7*w/8, h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+8);
        glReadPixels(w/8, 7*h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+12);
        GLenum e = glGetError();
        fprintf(stderr,
                "[diag] gles3_transition frame=%lu progress=%.3f "
                "linear=%.3f gl_error=0x%x "
                "samples=%u,%u,%u; %u,%u,%u; %u,%u,%u; %u,%u,%u\n",
                st->frame, progress, linear, (unsigned)e,
                px[0],px[1],px[2], px[4],px[5],px[6],
                px[8],px[9],px[10], px[12],px[13],px[14]);
    }
}

static void
reshape_transition(ModeInfo *mi, int w, int h) {
    (void)mi; (void)w; (void)h;
}

static Bool
transition_event(ModeInfo *mi, XEvent *e) {
    (void)mi; (void)e;
    return False;
}

static void
release_transition(ModeInfo *mi) {
    (void)mi;
}

static void
free_transition(ModeInfo *mi) {
    TrState *st = (TrState *)mi->data;
    if (!st) return;
    if (st->program)  glDeleteProgram(st->program);
    if (st->vbo)      glDeleteBuffers(1, &st->vbo);
    if (st->tex_from) glDeleteTextures(1, &st->tex_from);
    if (st->tex_to)   glDeleteTextures(1, &st->tex_to);
    free(st);
    mi->data = NULL;
}

static ModeSpecOpt transition_opts = {0, NULL, 0, NULL, NULL};

struct xscreensaver_function_table HACK_TABLE = {
    .name       = HACK_PREFIX_STR,
    .class_     = "GLES3Transition",
    .init_cb    = init_transition,
    .draw_cb    = draw_transition,
    .reshape_cb = reshape_transition,
    .event_cb   = transition_event,
    .free_cb    = free_transition,
    .release_cb = release_transition,
    .opts       = &transition_opts,
    .defaults_str = DEFAULTS,
};
