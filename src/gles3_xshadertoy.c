/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_xshadertoy.c — generic GLES3 driver for the 38 Shadertoy-API
 *                      fragment shaders vendored under
 *                      vendor/xshadertoy/glsl/.
 *
 * Patterned after gles3_hyprsaver.c (one .c reused for many shaders
 * via -D flags). meson.set:
 *
 *   -DSHADER_FILE=<basename>.glsl   which .glsl to load (relative to
 *                                   vendor/xshadertoy/glsl/)
 *   -DHACK_PREFIX=<ident>           symbol stem (init_<ident> /
 *                                   draw_<ident> / free_<ident> /
 *                                   <ident>_xscreensaver_function_table)
 *
 * The same .c file compiles 38 times — one per shader — producing 38
 * independent executables (xshadertoy_<shader>_gles3).
 *
 * ----------------------------------------------------------------------
 * Why this exists and why it isn't a port of upstream xshadertoy.c
 * ----------------------------------------------------------------------
 *
 * Upstream xscreensaver 6.16 ships `hacks/glx/xshadertoy.c` (1191 LOC)
 * plus 38 `.glsl` shaders under `hacks/glx/glsl/`. Each shader is
 * launched via a ~20-line bash wrapper that exec's xshadertoy with the
 * shader source piped on stdin. We need GLES3-native binaries that the
 * harness can launch from any cwd and that resolve their shader from
 * the installed /usr/share/... path (not cwd-relative — that was the
 * blackhole regression that bit shipping).
 *
 * A full port of xshadertoy.c is overkill for the 38 shipped shaders:
 *   - All 38 are single-pass (each wrapper only sets `--program0`,
 *     never `--program1`..`--program4`). Multi-pass support is
 *     required by upstream for full Shadertoy-API fidelity but
 *     unused here.
 *   - Upstream's options (--speed, --scale, --automouse, --duration,
 *     etc.) are screenhack config plumbing we don't replicate; the
 *     native GLES3 harness launches one binary per shader and the
 *     shader runs at native tempo.
 *   - Upstream's resource loader (read xrm / parse options / find
 *     shaders via $BUNDLE_RESPATH / android_read_asset_file) is
 *     purely an X11/Android concern.
 *
 * What we DO need is exactly what upstream `fragment_shader_head`
 * already provides: a GLSL preamble that declares the Shadertoy
 * uniform set (iResolution / iTime / iTimeDelta / iFrameRate /
 * iFrame / iDate / iMouse / iChannel0..3), the GLSL-1.2-to-1.3
 * compatibility shims (round, max(int,int), texture/sampler2D aliases,
 * etc.), and a `void main()` wrapper that calls the shader's
 * `mainImage(out vec4 fragColor, in vec2 fragCoord)`. That's the
 * minimum shader-source compatibility shim that lets a vendored
 * `xxx.glsl` compile on GLES 3.x without modification.
 *
 * The uniform shim is straight from the upstream file
 * `fragment_shader_head` (lines 264-408 in the source I read), with
 * small adjustments for our GLES 3.0 target: we always emit
 * `#version 300 es` and pin to precision highp. The whole preamble
 * is ~80 lines including helpers.
 *
 * Single-pass render loop: each frame uploads the Shadertoy uniform
 * set (iResolution, iTime, iTimeDelta, iFrameRate, iFrame, iDate,
 * iMouse), binds a dummy 1x1 black texture to each iChannel0..3 unit
 * so shaders that `texture(iChannel0, ...)` don't read driver garbage,
 * then draws a fullscreen quad. Mouse is a static no-op (the GLES3
 * harness doesn't pipe a Wayland pointer into this hack, and no
 * shipped shader depends on it being live — Shadertoy mouse is
 * "idiosyncratic" per upstream's own comment).
 *
 * ----------------------------------------------------------------------
 * Licensing
 * ----------------------------------------------------------------------
 *
 * The preamble's GLSL compatibility helpers are adapted from upstream
 * xscreensaver 6.16 xshadertoy.c, Copyright © 2026 Jamie Zawinski,
 * under the same X Consortium MIT-style permission notice as
 * xscreensaver itself. The vendored shaders preserve their original
 * MIT / CC0 / CC BY 3.0 / public-domain headers verbatim. See
 * `vendor/xshadertoy/PORTED.md` for the per-shader license inventory.
 */
/* ----------------------------------------------------------------------
 * Options, seed, palette and director (xshadertoy_opts, 2026-09-29)
 * ----------------------------------------------------------------------
 *
 * A shader that carries //@saver and //@option lines gets an option table
 * with no C of its own: ncz_hack_options() below parses the directives
 * (src/xst_core.c), the harness resolves them like any hack's options
 * (defaults < config file < preset < environment < command line), and the
 * values reach the shader as #defines (OPT_<NAME>) or bound uniforms. The run
 * seed is logged and replays the run exactly; the palette (uPal[4]) and the
 * slow director (uDir[4]) are pure functions of seed, options and time.
 * Shaders without //@saver behave as before. docs/XSHADERTOY-OPTIONS.md.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <unistd.h>

#include <GLES3/gl32.h>

#include "ncz_gl.h"
#include "ncz_hack_shim.h"
#include "ncz_render.h"
#include "ncz_harness_cfg.h"
#include "xst_core.h"

#ifndef SHADER_FILE
#define SHADER_FILE "polarnight.glsl"
#endif
#ifndef HACK_PREFIX
#define HACK_PREFIX xshadertoy_polarnight
#endif
#ifndef HACK_PREFIX_ID
#define HACK_PREFIX_ID xshadertoy_polarnight
#endif
#ifndef HACK_TABLE
#define HACK_TABLE xshadertoy_polarnight_xscreensaver_function_table
#endif

#define STR(x) #x
#define STR_EXPAND(x) STR(x)
#define HACK_PREFIX_STR STR_EXPAND(HACK_PREFIX)

typedef struct {
    GLuint program;
#ifdef NCZ_NEONGRAVITY_FXAA
    GLuint content_program;
    GLuint pass_fbo;
    GLuint pass_tex;
    int pass_w;
    int pass_h;
    GLint content_loc_iresolution;
    GLint content_loc_itime;
    GLint content_loc_itimedelta;
    GLint content_loc_ifps;
    GLint content_loc_iframe;
    GLint content_loc_idate;
    GLint content_loc_imouse;
    GLint content_loc_iseed;
#endif
    GLuint vbo;
    GLuint ichan_tex;        /* 1x1 RGBA8 dummy texture for iChannel0..3 */
    GLint loc_iresolution, loc_itime, loc_itimedelta, loc_ifps, loc_iframe;
    GLint loc_idate, loc_imouse, loc_iseed, loc_ichan[4];
    GLint loc_pal, loc_dir, loc_bind[XST_MAX_BINDS];
    double start_time, last_time;
    unsigned long frame;
    XstRun run;
    uint32_t src_hash;
    int logged;
    /* windowed frame-time statistics for sustained runs (NCZ_XST_PERF_WINDOW) */
    double perf_window, perf_t0;
    float *perf_ms;
    int perf_n, perf_cap;
} XSToyState;

static const char *vert_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* The shader source and its parsed spec, loaded once: the harness asks for
 * the option table before any EGL work, init compiles the same text. */
static XstSpec g_spec;
static char *g_body;            /* includes spliced */
static char g_used_path[1024];
static int g_loaded;

static void load_once(void) {
    if (g_loaded) return;
    g_loaded = 1;
    char *raw = xst_load_shader(SHADER_FILE, g_used_path, sizeof g_used_path);
    if (!raw) return;
    g_body = xst_splice_near(raw, g_used_path);
    free(raw);
    if (!g_body) return;
    xst_spec_parse(&g_spec, SHADER_FILE, g_body);
}

const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    load_once();
    if (!g_body || !g_spec.has_saver) { *n = 0; return NULL; }
    *n = g_spec.n;
    *prefix = g_spec.prefix;
    *group = g_spec.group;
    return g_spec.defs;
}

/* GPU-class quality 0..2 (weak, mid, strong); NCZ_SHADER_QUALITY overrides. */
static int class_quality(void) {
    const char *e = getenv("NCZ_SHADER_QUALITY");
    if (e && (e[0] == '0' || e[0] == '1' || e[0] == '2') && e[1] == 0) return e[0] - '0';
    const char *c = ncz_gpu_class_current();
    if (c && !strcmp(c, "weak")) return 0;
    if (c && !strcmp(c, "mid")) return 1;
    return 2;
}

static GLuint compile_shader(GLenum stage, const char *src) {
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
        fprintf(stderr, "xshadertoy: %s shader compile failed:\n%s\n",
                stage == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint
link_program(const char *frag_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "a_pos");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei got = 0;
        glGetProgramInfoLog(program, sizeof log, &got, log);
        fprintf(stderr, "xshadertoy: link failed:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

#ifdef NCZ_NEONGRAVITY_FXAA
static GLint
uniform_location(GLuint program, const char *name) {
    return glGetUniformLocation(program, name);
}

static int
ensure_neongravity_target(XSToyState *st, int w, int h) {
    if (st->pass_fbo && st->pass_w == w && st->pass_h == h) return 1;
    /* First allocation vs. resize. On a size change the old storage must
     * be fully released before new handles are created, otherwise every
     * window resize leaks a native-size RGBA8 texture plus FBO:
     *   1. Unbind first: the default framebuffer (0) is already bound by
     *      the caller before this runs, but detach the texture from the
     *      FBO we are about to destroy so no live FBO keeps it attached.
     *   2. Delete, then set the handles to 0 so a failed reallocation
     *      below leaves the state consistent (0 = not allocated) rather
     *      than pointing at objects we have already destroyed. */
    if (st->pass_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, st->pass_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &st->pass_fbo);
    }
    if (st->pass_tex) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &st->pass_tex);
    }
    st->pass_fbo = 0;
    st->pass_tex = 0;

    glGenFramebuffers(1, &st->pass_fbo);
    glGenTextures(1, &st->pass_tex);
    if (!st->pass_fbo || !st->pass_tex) {
        fprintf(stderr, "xshadertoy: failed to allocate neongravity content target\n");
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, st->pass_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, st->pass_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, st->pass_tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "xshadertoy: neongravity content FBO incomplete: 0x%x\n",
                (unsigned)status);
        glDeleteFramebuffers(1, &st->pass_fbo);
        glDeleteTextures(1, &st->pass_tex);
        st->pass_fbo = 0;
        st->pass_tex = 0;
        return 0;
    }
    st->pass_w = w;
    st->pass_h = h;
    return 1;
}
#endif

/* ------------------------------------------------------------------- */
/* XSCREENSAVER module                                                  */
/* ------------------------------------------------------------------- */

static void init_xshadertoy(ModeInfo *mi) {
    XSToyState *st = (XSToyState *)calloc(1, sizeof(*st));
    if (!st) { ncz_harness_die(1); return; }
    mi->data = st;

    load_once();
    if (!g_body) { ncz_harness_die(1); return; }
    fprintf(stderr, "[diag] xshadertoy shader=%s\n", g_used_path);
    st->src_hash = xst_fnv1a(g_body);

    /* Legacy shaders (no //@saver) still get a fresh iSeed every run. The
     * quality option "auto" follows the GPU class; NCZ_SHADER_QUALITY wins. */
    xst_run_init(&st->run, &g_spec, g_spec.has_saver ? ncz_harness_opts() : NULL, class_quality());

    char *frag_src = xst_fragment_source(&st->run, g_body);
    if (!frag_src) { ncz_harness_die(1); return; }
    if (getenv("NCZ_XST_DUMP_SOURCE")) fputs(frag_src, stderr);

    st->program = link_program(frag_src);
    free(frag_src);
    if (!st->program) { ncz_harness_die(1); return; }

#ifdef NCZ_NEONGRAVITY_FXAA
    char content_path[1024] = "";
    char *content_body = xst_load_shader("neongravity-1.glsl",
                                         content_path, sizeof content_path);
    if (!content_body) { ncz_harness_die(1); return; }
    content_body = xst_splice_near(content_body, content_path);
    char *content_src = xst_fragment_source(&st->run, content_body);
    free(content_body);
    if (!content_src) { ncz_harness_die(1); return; }
    st->content_program = link_program(content_src);
    free(content_src);
    if (!st->content_program) { ncz_harness_die(1); return; }
    fprintf(stderr, "[diag] xshadertoy neongravity multipass content=%s fxaa=%s\n",
            content_path, g_used_path);
#endif

    st->loc_iresolution = glGetUniformLocation(st->program, "iResolution");
    st->loc_itime       = glGetUniformLocation(st->program, "iTime");
    st->loc_itimedelta  = glGetUniformLocation(st->program, "iTimeDelta");
    st->loc_ifps        = glGetUniformLocation(st->program, "iFrameRate");
    st->loc_iframe      = glGetUniformLocation(st->program, "iFrame");
    st->loc_idate       = glGetUniformLocation(st->program, "iDate");
    st->loc_imouse      = glGetUniformLocation(st->program, "iMouse");
    st->loc_iseed       = glGetUniformLocation(st->program, "iSeed");
    st->loc_ichan[0]    = glGetUniformLocation(st->program, "iChannel0");
    st->loc_ichan[1]    = glGetUniformLocation(st->program, "iChannel1");
    st->loc_ichan[2]    = glGetUniformLocation(st->program, "iChannel2");
    st->loc_ichan[3]    = glGetUniformLocation(st->program, "iChannel3");
    st->loc_pal         = glGetUniformLocation(st->program, "uPal");
    st->loc_dir         = glGetUniformLocation(st->program, "uDir");
    for (int b = 0; b < st->run.nbind; b++)
        st->loc_bind[b] = glGetUniformLocation(st->program, st->run.bind_uniform[b]);

#ifdef NCZ_NEONGRAVITY_FXAA
    st->content_loc_iresolution = uniform_location(st->content_program, "iResolution");
    st->content_loc_itime       = uniform_location(st->content_program, "iTime");
    st->content_loc_itimedelta  = uniform_location(st->content_program, "iTimeDelta");
    st->content_loc_ifps        = uniform_location(st->content_program, "iFrameRate");
    st->content_loc_iframe      = uniform_location(st->content_program, "iFrame");
    st->content_loc_idate       = uniform_location(st->content_program, "iDate");
    st->content_loc_imouse      = uniform_location(st->content_program, "iMouse");
    st->content_loc_iseed       = uniform_location(st->content_program, "iSeed");
#endif

    static const float quad[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };
    glGenBuffers(1, &st->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenTextures(1, &st->ichan_tex);
    glBindTexture(GL_TEXTURE_2D, st->ichan_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    static const unsigned char black[4] = {0, 0, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    glBindTexture(GL_TEXTURE_2D, 0);

    const char *pw = getenv("NCZ_XST_PERF_WINDOW");
    double pwv = pw ? atof(pw) : 0.0;
    if (isfinite(pwv) && pwv >= 1.0 && pwv <= 86400.0) {
        st->perf_window = pwv;
        st->perf_cap = 16384;
        st->perf_ms = malloc(sizeof(float) * (size_t)st->perf_cap);
        if (!st->perf_ms) st->perf_cap = 0;
    } else if (pw && *pw) {
        fprintf(stderr, "[xshadertoy] NCZ_XST_PERF_WINDOW=%s ignored (seconds, 1..86400)\n", pw);
    }

    st->start_time = now_seconds();
    st->last_time  = st->start_time;
    st->perf_t0 = st->start_time;
    fprintf(stderr,
            "[diag] xshadertoy init ok: program=%u GL=%s options=%s iSeed=%f,%f,%f,%f\n",
            st->program, (const char *)glGetString(GL_VERSION),
            g_spec.has_saver ? "yes" : "no",
            st->run.iseed[0], st->run.iseed[1], st->run.iseed[2], st->run.iseed[3]);
}

static int cmp_f(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/* Sustained-run gate support: with NCZ_XST_PERF_WINDOW=N, one [xst-perf]
 * line every N seconds with the frame-interval percentiles of that window
 * (tools/xshadertoy-perf-gate.py reads them). */
static void perf_sample(XSToyState *st, double now, double dt_s) {
    if (!st->perf_ms) return;
    if (st->frame > 1 && st->perf_n < st->perf_cap) st->perf_ms[st->perf_n++] = (float)(dt_s * 1000.0);
    if (now - st->perf_t0 < st->perf_window) return;
    if (st->perf_n > 0) {
        qsort(st->perf_ms, (size_t)st->perf_n, sizeof(float), cmp_f);
        int n = st->perf_n;
        fprintf(stderr, "[xst-perf] t=%.0f window=%.0f n=%d fps=%.1f p50=%.2f p95=%.2f p99=%.2f max=%.2f\n",
                now - st->start_time, now - st->perf_t0, n, (double)n / (now - st->perf_t0),
                st->perf_ms[n / 2], st->perf_ms[(int)(n * 0.95)], st->perf_ms[(int)(n * 0.99)], st->perf_ms[n - 1]);
    }
    st->perf_n = 0;
    st->perf_t0 = now;
}

/* Bind all four iChannel units and leave unit 0 active. The samplers are
 * contiguous by construction: loc_ichan[u] is the location of iChannel<u>
 * (see the init above), so unit u is the only one a given sampler can
 * read. In the FXAA build unit 0 carries the content-pass texture and the
 * rest get the 1x1 dummy; in the single-pass build every unit gets the
 * dummy. Binding all of them either way means a sampler can never be left
 * reading whatever the driver happened to have on that unit. Locations
 * are guarded because a shader that declares no iChannel2/3 gets -1 from
 * glGetUniformLocation, and glUniform1i(-1, ...) is a spec no-op only by
 * accident of driver goodwill. Returns with GL_TEXTURE0 active so the
 * caller's next state change is predictable. */
static void bind_ichan_units(XSToyState *st, GLuint unit0_tex)
{
    for (int u = 0; u < 4; u++) {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, u == 0 ? unit0_tex : st->ichan_tex);
        if (st->loc_ichan[u] >= 0) glUniform1i(st->loc_ichan[u], u);
    }
    glActiveTexture(GL_TEXTURE0);
}

static void draw_xshadertoy(ModeInfo *mi) {
    XSToyState *st = (XSToyState *)mi->data;
    if (!st || !st->program) return;

    int w = mi->xgwa.width;
    int h = mi->xgwa.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    double now = now_seconds();
    double wall = now - st->start_time;
    double motion = wall * (double)st->run.speed;   /* speed option (1 for legacy) */
    float idelta = (float)(now - st->last_time);
    perf_sample(st, now, now - st->last_time);
    st->last_time = now;
    st->frame++;

    if (!st->logged && g_spec.has_saver) {
        char line[4096];
        xst_run_describe(&st->run, line, sizeof line, w, h, st->src_hash);
        fprintf(stderr, "%s\n", line);
        st->logged = 1;
    }

    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    float zero4[4] = {0.f, 0.f, 0.f, 0.f};
    /* iTime/iTimeDelta feed the saver's motion clock; iFrameRate uses the
     * upstream total_frames / elapsed formula with max(elapsed, 1e-6) so the
     * first frame never divides by zero.  Do NOT upload iTime/iTimeDelta
     * here: the pass-specific glUseProgram() happens further down, and a
     * glUniform* with no program bound is GL_INVALID_OPERATION (0x502).  Both
     * the FXAA and single-pass branches re-upload them after glUseProgram. */
    float fps = (float)st->frame /
                (float)((now - st->start_time) > 1e-6
                        ? (now - st->start_time) : 1e-6);

    static double last_date_update = -1.0;
    static float date_v[4] = {0, 0, 0, 0};
    if (last_date_update < 0.0 || now - last_date_update > 1.0) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        date_v[0] = (float)(tm.tm_year + 1900);
        date_v[1] = (float)(tm.tm_mon + 1);
        date_v[2] = (float)(tm.tm_mday);
        date_v[3] = (float)(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
        last_date_update = now;
    }
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

#ifdef NCZ_NEONGRAVITY_FXAA
    if (!ensure_neongravity_target(st, w, h)) return;
    glBindFramebuffer(GL_FRAMEBUFFER, st->pass_fbo);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(st->content_program);
    glUniform3f(st->content_loc_iresolution, (float)w, (float)h, 1.f);
    glUniform1f(st->content_loc_itime, (float)motion);
    glUniform1f(st->content_loc_itimedelta, idelta * st->run.speed);
    glUniform1f(st->content_loc_ifps, fps);
    glUniform1i(st->content_loc_iframe, (GLint)st->frame);
    glUniform4fv(st->content_loc_idate, 1, date_v);
    glUniform4fv(st->content_loc_imouse, 1, zero4);
    glUniform4fv(st->content_loc_iseed, 1, st->run.iseed);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    /* (viewport/clear are repeated in both branches of the
     * NCZ_NEONGRAVITY_FXAA split; the cost is one cheap state write
     * per frame and is left for readability of each path). */
    glUseProgram(st->program);
    glUniform3f(st->loc_iresolution, (float)w, (float)h, 1.f);
    glUniform1f(st->loc_itime, (float)motion);
    glUniform1f(st->loc_itimedelta, idelta * st->run.speed);
    glUniform1f(st->loc_ifps, fps);
    glUniform1i(st->loc_iframe, (GLint)st->frame);
    glUniform4fv(st->loc_idate, 1, date_v);
    glUniform4fv(st->loc_imouse, 1, zero4);
    glUniform4fv(st->loc_iseed, 1, st->run.iseed);
    if (g_spec.has_saver) {
        float dir[16], pal[12];
        xst_director(&st->run, motion, dir);
        xst_palette_at(&st->run, wall, pal);
        glUniform4fv(st->loc_dir, 4, dir);
        glUniform3fv(st->loc_pal, 4, pal);
        for (int b = 0; b < st->run.nbind; b++) glUniform4fv(st->loc_bind[b], 1, st->run.bind_val[b]);
    }
    /* The FXAA pass samples the content-pass texture, so unit 0 carries
     * st->pass_tex and units 1..3 get the 1x1 black dummy. */
    bind_ichan_units(st, st->pass_tex);
#else
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(st->program);
    glUniform3f(st->loc_iresolution, (float)w, (float)h, 1.f);
    glUniform1f(st->loc_itime, (float)motion);
    glUniform1f(st->loc_itimedelta, idelta * st->run.speed);
    glUniform1f(st->loc_ifps, fps);
    glUniform1i(st->loc_iframe, (GLint)st->frame);
    glUniform4fv(st->loc_idate, 1, date_v);
    glUniform4fv(st->loc_imouse, 1, zero4);
    glUniform4fv(st->loc_iseed, 1, st->run.iseed);
    if (g_spec.has_saver) {
        float dir[16], pal[12];
        xst_director(&st->run, motion, dir);
        xst_palette_at(&st->run, wall, pal);
        glUniform4fv(st->loc_dir, 4, dir);
        glUniform3fv(st->loc_pal, 4, pal);
        for (int b = 0; b < st->run.nbind; b++) glUniform4fv(st->loc_bind[b], 1, st->run.bind_val[b]);
    }
    /* Single-pass build: no FBO in the chain, so all four iChannel
     * units read the 1x1 black dummy (same contract as the FXAA
     * build, just with no real source on unit 0). */
    bind_ichan_units(st, st->ichan_tex);
#endif

    /* Single final draw for this frame — both the FXAA build and the
     * plain one-pass build funnel through this same call. */
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    if (ncz_diag_sample_frame(st->frame, 60)) {
        unsigned char px[16] = {0};
        glReadPixels(w/2, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glReadPixels(w/8, h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+4);
        glReadPixels(7*w/8, h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+8);
        glReadPixels(w/8, 7*h/8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+12);
        GLenum e = glGetError();
        fprintf(stderr,
                "[diag] xshadertoy frame=%lu gl_error=0x%x "
                "samples=%u,%u,%u; %u,%u,%u; %u,%u,%u; %u,%u,%u\n",
                st->frame, (unsigned)e,
                px[0],px[1],px[2], px[4],px[5],px[6],
                px[8],px[9],px[10], px[12],px[13],px[14]);
    }
}

static void reshape_xshadertoy(ModeInfo *mi, int w, int h) { (void)mi; (void)w; (void)h; }
static Bool xshadertoy_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_xshadertoy(ModeInfo *mi) { (void)mi; }

static void free_xshadertoy(ModeInfo *mi) {
    XSToyState *st = (XSToyState *)mi->data;
    if (!st) return;
    if (st->program) glDeleteProgram(st->program);
#ifdef NCZ_NEONGRAVITY_FXAA
    if (st->content_program) glDeleteProgram(st->content_program);
    if (st->pass_fbo) glDeleteFramebuffers(1, &st->pass_fbo);
    if (st->pass_tex) glDeleteTextures(1, &st->pass_tex);
#endif
    if (st->vbo) glDeleteBuffers(1, &st->vbo);
    if (st->ichan_tex) glDeleteTextures(1, &st->ichan_tex);
    free(st->perf_ms);
    free(st);
    mi->data = NULL;
}

static ModeSpecOpt xshadertoy_opts = {0, NULL, 0, NULL, NULL};

struct xscreensaver_function_table HACK_TABLE = {
    .name       = HACK_PREFIX_STR,
    .class_     = "XShadertoy",
    .init_cb    = init_xshadertoy,
    .draw_cb    = draw_xshadertoy,
    .reshape_cb = reshape_xshadertoy,
    .event_cb   = xshadertoy_event,
    .free_cb    = free_xshadertoy,
    .release_cb = release_xshadertoy,
    .opts       = &xshadertoy_opts,
    .defaults_str = DEFAULTS,
};
