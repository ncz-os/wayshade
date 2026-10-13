/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_leviathan.c - driver for leviathan_gles3.
 *
 * The shader (vendor/xshadertoy/glsl/leviathan.glsl) keeps the Shadertoy
 * mainImage() convention so the headless smoke tester can still render it
 * with default options. This driver adds what the generic xshadertoy driver
 * does not have: an option table (src/leviathan_opts.h, env NCZ_LEVIATHAN_*,
 * --preset, the key file), a reproducible run seed that is logged, and three
 * option uniforms (uLv0..uLv2) the shader reads when LV_OPTIONS is defined.
 *
 * Everything else follows src/gles3_xshadertoy.c: preamble, uniforms, 1x1
 * black iChannel textures, NCZ_QUALITY from the GPU class, one fullscreen quad
 * per frame. Options are uniforms, so nothing is recompiled after init.
 */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <GLES3/gl32.h>

#include "ncz_gl.h"
#include "ncz_hack_shim.h"
#include "ncz_harness_cfg.h"
#include "ncz_render.h"
#include "leviathan_opts.h"

#define LV_SHADER "leviathan.glsl"

const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = LV_NOPTS;
    *prefix = "NCZ_LEVIATHAN_";
    *group = "xshadertoy_leviathan";   /* = the launcher short name: presets/<group>/, config <group>.conf */
    return LV_OPTS;
}

typedef struct {
    GLuint program, vbo, tex;
    GLint l_res, l_time, l_dt, l_fps, l_frame, l_date, l_mouse, l_seed, l_ch[4];
    GLint l_lv0, l_lv1, l_lv2;
    double start, last;
    double offset;           /* start-time option, in wall seconds */
    unsigned long frame;
    float seed4[4];
    float lv0[4], lv1[4], lv2[4];
} LvState;

static const char *vert_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static const char *frag_head =
    "precision highp float;\n"
    "precision highp int;\n"
    "out vec4 frag_color;\n"
    "uniform vec3  iResolution;\n"
    "uniform float iTime;\n"
    "uniform float iTimeDelta;\n"
    "uniform float iFrameRate;\n"
    "uniform int   iFrame;\n"
    "uniform vec4  iDate;\n"
    "uniform vec4  iMouse;\n"
    "uniform vec4  iSeed;\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n";

static const char *frag_tail =
    "\nvoid main() {\n"
    "  vec4 col = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  mainImage(col, gl_FragCoord.xy);\n"
    "  frag_color = col;\n"
    "}\n";

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint32_t raw_seed(void);

/* A random seed is kept in 1..999999 so the logged value can be passed back
 * with --seed (the option's range) to replay the run. */
static uint32_t run_seed(uint32_t fixed) {
    if (fixed) return fixed;
    return 1u + raw_seed() % 999999u;
}
static uint32_t raw_seed(void) {
    uint32_t s = 0;
    int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (f >= 0) {
        ssize_t n = read(f, &s, 4);
        close(f);
        if (n == 4 && s) return s;
    }
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return ((uint32_t)t.tv_nsec ^ (uint32_t)t.tv_sec ^ (uint32_t)getpid()) | 1u;
}

/* splitmix32-style finaliser: four floats in [0,1) from the run seed. */
static uint32_t mix32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *b = malloc((size_t)len + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(b); return NULL; }
    b[len] = 0;
    return b;
}

static char *locate_shader(void) {
    const char *e = getenv("NCZ_SHADER_DIR");
    char p[1024];
    if (e && *e) {
        snprintf(p, sizeof p, "%s/%s", e, LV_SHADER);
        char *b = read_file(p);
        if (b) { fprintf(stderr, "[diag] leviathan shader=%s\n", p); return b; }
        fprintf(stderr, "leviathan: NCZ_SHADER_DIR=%s set but %s not found\n", e, p);
    }
    static const char *pre[] = {
        "vendor/xshadertoy/glsl/", "../vendor/xshadertoy/glsl/", "../../vendor/xshadertoy/glsl/",
        "/usr/share/ncz-screensavers/shaders/", NULL,
    };
    for (int i = 0; pre[i]; i++) {
        snprintf(p, sizeof p, "%s%s", pre[i], LV_SHADER);
        char *b = read_file(p);
        if (b) { fprintf(stderr, "[diag] leviathan shader=%s\n", p); return b; }
    }
    fprintf(stderr, "leviathan: cannot locate %s\n", LV_SHADER);
    return NULL;
}

static int quality(const ncz_opts *o) {
    const char *e = getenv("NCZ_SHADER_QUALITY");
    if (e && (e[0] == '0' || e[0] == '1' || e[0] == '2') && e[1] == 0) return e[0] - '0';
    const char *q = o ? ncz_opts_get(o, "quality") : "auto";
    if (q && !strcmp(q, "weak")) return 0;
    if (q && !strcmp(q, "mid")) return 1;
    if (q && !strcmp(q, "strong")) return 2;
    const char *c = ncz_gpu_class_current();
    if (c && !strcmp(c, "weak")) return 0;
    if (c && !strcmp(c, "mid")) return 1;
    return 2;
}

/* Index of an enum value in its choice list, minus one, so "auto" (always the
 * first choice) maps to -1 and the others to 0, 1, ... as the shader expects. */
static float enum_index(const ncz_opts *o, const char *name, const char *choices) {
    const char *v = o ? ncz_opts_get(o, name) : NULL;
    if (!v || !*v) return -1.f;
    int idx = 0;
    const char *p = choices;
    size_t vl = strlen(v);
    while (*p) {
        const char *c = strchr(p, ',');
        size_t l = c ? (size_t)(c - p) : strlen(p);
        if (l == vl && !strncmp(p, v, l)) return (float)idx - 1.f;
        idx++;
        if (!c) break;
        p = c + 1;
    }
    return -1.f;
}

static GLuint compile(GLenum st, const char *src) {
    GLuint s = glCreateShader(st);
    if (!s) return 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof log, &n, log);
        fprintf(stderr, "leviathan: %s shader compile failed:\n%s\n",
                st == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static void init_leviathan(ModeInfo *mi) {
    LvState *st = calloc(1, sizeof *st);
    if (!st) { ncz_harness_die(1); return; }
    mi->data = st;
    const ncz_opts *o = ncz_harness_opts();

    uint32_t seed = run_seed(o ? (uint32_t)ncz_opts_get_int(o, "seed") : 0u);
    for (int i = 0; i < 4; i++)
        st->seed4[i] = (float)((mix32(seed + 0x9E3779B9u * (uint32_t)(i + 1)) >> 8) / 16777216.0);

    st->lv0[0] = enum_index(o, "hull", LV_HULLS);
    st->lv0[1] = enum_index(o, "landmark", LV_LANDMARKS);
    st->lv0[2] = enum_index(o, "palette", LV_PALETTES);
    st->lv0[3] = enum_index(o, "aperture-side", LV_SIDES);
    st->lv1[0] = enum_index(o, "planet", LV_PLANETS);
    st->lv1[1] = enum_index(o, "interior", LV_INTERIORS);
    st->lv1[2] = enum_index(o, "randomize", LV_RANDOMIZE) + 1.f;   /* 0 auto, 1 fixed */
    st->lv1[3] = o ? (float)ncz_opts_get_int(o, "chapter-length") : 0.f;
    st->lv2[0] = o ? (float)ncz_opts_get_float(o, "arc-scale") : 1.f;
    long hue = o ? ncz_opts_get_int(o, "gate-hue") : -1;
    st->lv2[1] = hue < 0 ? -9.f : (float)hue * 3.14159265f / 180.f;
    st->lv2[2] = o ? (float)ncz_opts_get_float(o, "gate-intensity") : 1.f;
    st->lv2[3] = o ? (float)ncz_opts_get_float(o, "star-density") : 1.f;
    st->offset = o ? ncz_opts_get_float(o, "start-time") * (double)st->lv2[0] : 0.0;
    int q = quality(o);
    fprintf(stderr, "[leviathan] seed=%u preset=%s quality=%d options=%g,%g,%g,%g %g,%g,%g,%g %g,%g,%g,%g\n",
            seed, o ? ncz_opts_get(o, "preset") : "", q,
            st->lv0[0], st->lv0[1], st->lv0[2], st->lv0[3], st->lv1[0], st->lv1[1], st->lv1[2], st->lv1[3],
            st->lv2[0], st->lv2[1], st->lv2[2], st->lv2[3]);

    char *body = locate_shader();
    if (!body) { ncz_harness_die(1); return; }
    char head[96];
    int hl = snprintf(head, sizeof head, "#version 300 es\n#define NCZ_QUALITY %d\n#define LV_OPTIONS 1\n", q);
    size_t n = (size_t)hl + strlen(frag_head) + strlen(body) + strlen(frag_tail) + 2;
    char *src = malloc(n);
    if (!src) { free(body); ncz_harness_die(1); return; }
    snprintf(src, n, "%s%s\n%s%s", head, frag_head, body, frag_tail);
    free(body);

    GLuint vs = compile(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, src);
    free(src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        ncz_harness_die(1);
        return;
    }
    st->program = glCreateProgram();
    glAttachShader(st->program, vs);
    glAttachShader(st->program, fs);
    glBindAttribLocation(st->program, 0, "a_pos");
    glLinkProgram(st->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(st->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        GLsizei got = 0;
        glGetProgramInfoLog(st->program, sizeof log, &got, log);
        fprintf(stderr, "leviathan: link failed:\n%s\n", log);
        ncz_harness_die(1);
        return;
    }
    GLuint p = st->program;
    st->l_res = glGetUniformLocation(p, "iResolution");
    st->l_time = glGetUniformLocation(p, "iTime");
    st->l_dt = glGetUniformLocation(p, "iTimeDelta");
    st->l_fps = glGetUniformLocation(p, "iFrameRate");
    st->l_frame = glGetUniformLocation(p, "iFrame");
    st->l_date = glGetUniformLocation(p, "iDate");
    st->l_mouse = glGetUniformLocation(p, "iMouse");
    st->l_seed = glGetUniformLocation(p, "iSeed");
    st->l_lv0 = glGetUniformLocation(p, "uLv0");
    st->l_lv1 = glGetUniformLocation(p, "uLv1");
    st->l_lv2 = glGetUniformLocation(p, "uLv2");
    for (int i = 0; i < 4; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "iChannel%d", i);
        st->l_ch[i] = glGetUniformLocation(p, nm);
    }

    static const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f};
    glGenBuffers(1, &st->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenTextures(1, &st->tex);
    glBindTexture(GL_TEXTURE_2D, st->tex);
    static const unsigned char black[4] = {0, 0, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    st->start = st->last = now_s();
    fprintf(stderr, "[diag] leviathan init ok: GL=%s iSeed=%f,%f,%f,%f\n",
            (const char *)glGetString(GL_VERSION), st->seed4[0], st->seed4[1], st->seed4[2], st->seed4[3]);
}

static void draw_leviathan(ModeInfo *mi) {
    LvState *st = (LvState *)mi->data;
    if (!st || !st->program) return;
    int w = mi->xgwa.width > 0 ? mi->xgwa.width : 1;
    int h = mi->xgwa.height > 0 ? mi->xgwa.height : 1;
    double now = now_s();
    float t = (float)(now - st->start + st->offset);
    float dt = (float)(now - st->last);
    st->last = now;
    st->frame++;

    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(st->program);
    glUniform3f(st->l_res, (float)w, (float)h, 1.f);
    glUniform1f(st->l_time, t);
    glUniform1f(st->l_dt, dt);
    glUniform1f(st->l_fps, t > 1e-3f ? (float)st->frame / t : 60.f);
    glUniform1i(st->l_frame, (GLint)st->frame);
    float zero4[4] = {0, 0, 0, 0};
    glUniform4fv(st->l_date, 1, zero4);
    glUniform4fv(st->l_mouse, 1, zero4);
    glUniform4fv(st->l_seed, 1, st->seed4);
    glUniform4fv(st->l_lv0, 1, st->lv0);
    glUniform4fv(st->l_lv1, 1, st->lv1);
    glUniform4fv(st->l_lv2, 1, st->lv2);
    for (int i = 0; i < 4; i++) {
        glActiveTexture(GL_TEXTURE0 + (GLenum)i);
        glBindTexture(GL_TEXTURE_2D, st->tex);
        glUniform1i(st->l_ch[i], i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

static void reshape_leviathan(ModeInfo *mi, int w, int h) { (void)mi; (void)w; (void)h; }
static Bool event_leviathan(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_leviathan(ModeInfo *mi) { (void)mi; }
static void free_leviathan(ModeInfo *mi) {
    LvState *st = (LvState *)mi->data;
    if (!st) return;
    if (st->program) glDeleteProgram(st->program);
    if (st->vbo) glDeleteBuffers(1, &st->vbo);
    if (st->tex) glDeleteTextures(1, &st->tex);
    free(st);
    mi->data = NULL;
}

static ModeSpecOpt leviathan_opts = {0, NULL, 0, NULL, NULL};

struct xscreensaver_function_table leviathan_xscreensaver_function_table = {
    .name = "leviathan",
    .class_ = "Leviathan",
    .init_cb = init_leviathan,
    .draw_cb = draw_leviathan,
    .reshape_cb = reshape_leviathan,
    .event_cb = event_leviathan,
    .free_cb = free_leviathan,
    .release_cb = release_leviathan,
    .opts = &leviathan_opts,
    .defaults_str = DEFAULTS,
};
