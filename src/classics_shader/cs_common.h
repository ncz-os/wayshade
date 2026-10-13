/* cs_common.h - helpers shared by the shader-engine ports of the xscreensaver
 * classics (src/classics_shader/cs_<hack>.c).
 *
 * Every port keeps the original hack name, so the binary is still
 * <hack>_gles3 and plugs into src/gles3_harness.c through the same
 * xscreensaver_function_table symbol the GL1 version exported.  The port
 * itself contains no immediate mode and no fixed-function state: static
 * vertex buffers, GLSL ES 3.00 programs compiled once at init, a handful
 * of uniform updates per frame.
 *
 * The original algorithms are (c) Jamie Zawinski and the other xscreensaver
 * authors named in each file, MIT/X11 style permission notice; this glue is
 * part of ncz-screensavers (Apache-2.0).
 */
#ifndef CS_COMMON_H
#define CS_COMMON_H
#pragma GCC diagnostic ignored "-Wunused-function"

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <GLES3/gl32.h>
#include "ncz_gl.h"
#include "ncz_hack_shim.h"
#include "ncz_options.h"
#include "ncz_harness_cfg.h"
#include "ncz_platform.h"
#include "ncz_gpu_tier.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Shader sources are written as C token streams: no preprocessor lines
 * inside (the version and precision header is prepended by cs_program). */
#define CS_GLSL(...) #__VA_ARGS__

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

#define CS_OPT_STYLE \
    {"style", NCZ_OPT_ENUM, "auto", 0, 0, "auto,classic,enhanced", NULL, NULL, \
     "Style", "classic: faithful port of the xscreensaver original. enhanced: same hack with " \
     "anti-aliasing, soft lighting, glow and smooth palettes. auto: enhanced on GPUs that can " \
     "afford it, classic otherwise.", "Look"}
#define CS_OPT_SPEED \
    {"speed", NCZ_OPT_FLOAT, "1", 0.1, 4, NULL, NULL, NULL, "Animation speed", \
     "Time scale of the animation. 1 matches the original xscreensaver pace.", "Motion"}
#define CS_OPT_SEED \
    {"seed", NCZ_OPT_INT, "0", 0, 999999999, NULL, NULL, NULL, "Random seed", \
     "Seed for the random choices (0 = new every launch). The same seed repeats the same run.", "Motion"}
#define CS_OPT_BLOOM \
    {"bloom", NCZ_OPT_FLOAT, "1", 0, 3, NULL, NULL, NULL, "Glow", \
     "Strength of the soft glow around bright areas (enhanced style only). 0 turns the glow pass off.", "Look"}
#define CS_OPT_AA \
    {"antialias", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Anti-aliasing", \
     "Smooth edges (enhanced style only).", "Look"}

static const ncz_opts *cs_opts(void) { return ncz_harness_opts(); }
static double cs_opt_f(const char *n, double dflt) {
    const ncz_opts *o = cs_opts();
    return (o && ncz_opts_get(o, n)) ? ncz_opts_get_float(o, n) : dflt;
}
static long cs_opt_i(const char *n, long dflt) {
    const ncz_opts *o = cs_opts();
    return (o && ncz_opts_get(o, n)) ? ncz_opts_get_int(o, n) : dflt;
}
static int cs_opt_b(const char *n, int dflt) {
    const ncz_opts *o = cs_opts();
    return (o && ncz_opts_get(o, n)) ? ncz_opts_get_bool(o, n) : dflt;
}
static const char *cs_opt_s(const char *n, const char *dflt) {
    const ncz_opts *o = cs_opts();
    const char *v = o ? ncz_opts_get(o, n) : NULL;
    return v ? v : dflt;
}

/* style option -> 0 classic, 1 enhanced.  auto: enhanced on medium and better
 * GPUs, classic on the weak class (ncz_gpu_tier LOW: Intel iGPUs and older
 * mobile parts; NCZ_GPU_TIER overrides for tests).  Call with a GL context
 * current. */
static int cs_style(void) {
    const char *s = cs_opt_s("style", "auto");
    if (!strcmp(s, "classic")) return 0;
    if (!strcmp(s, "enhanced")) return 1;
    ncz_gpu_tier_init();
    const ncz_gpu_tier_t *t = ncz_gpu_tier_current();
    int enh = t && t->tier >= NCZ_TIER_MEDIUM;
    fprintf(stderr, "[diag] classics style=auto -> %s (gpu tier %s: %s)\n", enh ? "enhanced" : "classic",
            t ? t->name : "?", t ? t->reason : "");
    return enh;
}

/* ------------------------------------------------------------------ */
/* Random numbers                                                      */
/* ------------------------------------------------------------------ */

typedef struct { uint64_t s; } cs_rng;
static uint32_t cs_seed_from_options(void) {
    long sd = cs_opt_i("seed", 0);
    if (sd > 0) return (uint32_t)sd;
    uint32_t v = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) { if (read(fd, &v, sizeof v) != (ssize_t)sizeof v) v = 0; close(fd); }
    if (!v) v = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    return v;
}
static void cs_rng_seed(cs_rng *r, uint32_t seed) {
    r->s = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed * 0xD1B54A32D192ED03ull);
    if (!r->s) r->s = 1;
}
static uint32_t cs_rand_u32(cs_rng *r) {           /* xorshift64* */
    uint64_t x = r->s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}
static float cs_rand(cs_rng *r) { return (cs_rand_u32(r) >> 8) * (1.0f / 16777216.0f); }
static inline float cs_rand_range(cs_rng *r, float a, float b) { return a + (b - a) * cs_rand(r); }
static int   cs_rand_int(cs_rng *r, int n) { return n <= 0 ? 0 : (int)(cs_rand_u32(r) % (uint32_t)n); }
static int   cs_rand_sign(cs_rng *r) { return (cs_rand_u32(r) & 1) ? 1 : -1; }
static inline float cs_bellrand(cs_rng *r, float n) { return (cs_rand(r) + cs_rand(r) + cs_rand(r)) * n / 3.0f; }

/* ------------------------------------------------------------------ */
/* Fixed-step clock: the originals advance their simulation per frame  */
/* at a nominal rate; we run the same steps at that rate in real time  */
/* so the motion is identical at 30, 60 or 144 fps.                    */
/* ------------------------------------------------------------------ */

typedef struct { double last, acc; int init; } cs_clock;
/* Returns the number of fixed steps of length `step` (seconds of nominal
 * time) to run now; `speed` scales real time.  At most `maxsteps`. */
static int cs_clock_steps(cs_clock *c, double step, double speed, int maxsteps) {
    double now = ncz_now();
    if (!c->init) { c->init = 1; c->last = now; c->acc = 0; }
    double dt = now - c->last;
    c->last = now;
    if (dt < 0) dt = 0;
    if (dt > 0.25) dt = 0.25;                       /* stall (suspend, drag): do not fast-forward */
    c->acc += dt * speed;
    int n = 0;
    while (c->acc >= step && n < maxsteps) { c->acc -= step; n++; }
    if (c->acc > step) c->acc = step;
    return n;
}
/* Fractional position inside the current step, for interpolation. */
static inline float cs_clock_alpha(const cs_clock *c, double step) { return (float)(c->acc / step); }

/* ------------------------------------------------------------------ */
/* 4x4 matrices, column-major, right-handed, GL clip space             */
/* ------------------------------------------------------------------ */

typedef struct { float m[16]; } cs_mat4;
static cs_mat4 cs_identity(void) { cs_mat4 r; memset(&r, 0, sizeof r); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
static cs_mat4 cs_mul(cs_mat4 a, cs_mat4 b) {
    cs_mat4 r;
    for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) {
        float s = 0;
        for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = s;
    }
    return r;
}
static cs_mat4 cs_translate(float x, float y, float z) { cs_mat4 r = cs_identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
static cs_mat4 cs_scale(float x, float y, float z) { cs_mat4 r = cs_identity(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
static cs_mat4 cs_rotate(float deg, float x, float y, float z) {
    float l = sqrtf(x * x + y * y + z * z);
    cs_mat4 r = cs_identity();
    if (l < 1e-8f) return r;
    x /= l; y /= l; z /= l;
    float a = deg * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a), t = 1 - c;
    r.m[0] = t * x * x + c;     r.m[4] = t * x * y - s * z; r.m[8]  = t * x * z + s * y;
    r.m[1] = t * x * y + s * z; r.m[5] = t * y * y + c;     r.m[9]  = t * y * z - s * x;
    r.m[2] = t * x * z - s * y; r.m[6] = t * y * z + s * x; r.m[10] = t * z * z + c;
    return r;
}
static cs_mat4 cs_perspective(float fovy_deg, float aspect, float zn, float zf) {
    cs_mat4 r; memset(&r, 0, sizeof r);
    float f = 1.0f / tanf(fovy_deg * (float)M_PI / 360.0f);
    r.m[0] = f / aspect; r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}
static inline cs_mat4 cs_ortho(float l, float r_, float b, float t, float n, float f) {
    cs_mat4 r = cs_identity();
    r.m[0] = 2 / (r_ - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
    return r;
}
/* Upper-left 3x3 as a column-major float[9] (rotation/uniform scale only). */
static inline void cs_mat3_of(const cs_mat4 *a, float out[9]) {
    for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) out[c * 3 + r] = a->m[c * 4 + r];
}

/* Debug aid: NCZ_CS_DEBUG=1 reports the first GL error at each call site once. */
static void cs_gl_check(const char *where) {
    static int on = -1, seen = 0;
    if (on < 0) on = getenv("NCZ_CS_DEBUG") != NULL;
    if (!on || seen > 20) return;
    GLenum e = glGetError();
    if (e) { fprintf(stderr, "[classics] GL error 0x%x at %s\n", e, where); seen++; }
}

/* ------------------------------------------------------------------ */
/* Shader helpers                                                      */
/* ------------------------------------------------------------------ */

static const char *CS_GLSL_HEAD = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";

static GLuint cs_compile(GLenum type, const char *src, const char *what) {
    GLuint sh = glCreateShader(type);
    const char *parts[2] = { CS_GLSL_HEAD, src };
    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei n = 0;
        glGetShaderInfoLog(sh, sizeof log - 1, &n, log); log[n] = 0;
        fprintf(stderr, "[classics] %s %s shader compile failed: %s\n", what,
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}
/* Compile and link once at init; returns 0 on failure (already logged). */
static GLuint cs_program(const char *vs, const char *fs, const char *what) {
    GLuint v = cs_compile(GL_VERTEX_SHADER, vs, what);
    GLuint f = cs_compile(GL_FRAGMENT_SHADER, fs, what);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof log - 1, &n, log); log[n] = 0;
        fprintf(stderr, "[classics] %s link failed: %s\n", what, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* A full-screen triangle needs no buffer: positions come from gl_VertexID. */
#define CS_FULLSCREEN_VS CS_GLSL( \
    out vec2 vUv; \
    void main() { \
        vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); \
        vUv = p; \
        gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); \
    })

/* Shared GLSL snippets (prepended to fragment sources by string concat). */
#define CS_GLSL_COMMON CS_GLSL( \
    float cs_hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); } \
    float cs_hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); } \
    vec3 cs_aces(vec3 x) { x *= 0.6; return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); } \
    vec3 cs_dither(vec3 c, vec2 fc) { return c + (cs_hash12(fc) - 0.5) / 255.0; } \
    vec3 cs_hsv(vec3 c) { vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0); return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y); })

#endif /* CS_COMMON_H */
