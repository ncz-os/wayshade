/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_ephemeris.c - Ephemeris, a screensaver for NCZ-OS.
 *
 * Designed by Claude (Opus 5) on 2026-09-25 with Fable and Astra as
 * consultants; see docs/superpowers/specs/2026-09-25-ephemeris-chronicle.md
 * for the design record and docs/EPHEMERIS.md for what the program does.
 *
 * The mechanism, described plainly:
 *
 *   The surface is a texture on the GPU. It lasts one run.
 *
 *   Workers live on the CPU, in fixed slots. When a worker's time is up, the
 *   next occupant of the slot is built from the run's seed, the slot number
 *   and the slot's occupant count, and from nothing else. spawn() clears the
 *   slot before it fills it, so no position, heading, trail or intent crosses
 *   from one occupant to the next. That is enforced here, not depicted.
 *
 *   What a worker can see of the surface is a copy of one small mip level
 *   (at most 32x32), read back asynchronously a few times a second. It is
 *   blurred and it is late. Each worker also keeps a short private trail of
 *   where it has been, which is cleared with it.
 *
 *   An evaluator on the GPU checks each stroke against the real surface at
 *   full resolution and declines strokes that do not join what is actually
 *   there, plus some for no reason the worker could know. Declined strokes
 *   leave nothing behind. The CPU never reads the evaluator's output, so no
 *   worker ever learns what happened to its work.
 *
 *   The surface forgets a little every frame. Nothing is written to disk and
 *   nothing is fetched or sent anywhere. The next run begins from a new
 *   surface that is made to look as if it has a history, and does not.
 *
 *   On exit the program can write a plain numeric table of the run to
 *   standard error: the ephemeris of a sky that has already moved on. It is
 *   a description, not saved state, and nothing reads it back.
 *
 * Material: matte. Charcoal, limestone, a little iron oxide. No bloom, no
 * glow, no text, no flashing.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <GLES3/gl32.h>
#include "ncz_gl.h" /* GLES3/EGL includes, ncz_harness_die */
#include "ncz_options.h"
#include "ephemeris_opts.h"
#include "ncz_harness_cfg.h"
#include "ncz_hack_shim.h"
#include "ncz_render.h"

#define NW EPH_MAX_WORKERS
/* ephemeris.glsl sizes its u_seg/u_segx arrays and index clamps for 48. */
_Static_assert(EPH_MAX_WORKERS == 48, "ephemeris.glsl assumes 48 worker slots");
#define TRAIL 16
#define REC_ROWS 64
#define MIP_MAX 32
#define SURF_CAP 1280
#define SURF_CAP_BIG 1920    /* dramatic composition on mid and strong GPUs */
#define MAX_LIFTS 8          /* lifts that still clog the ground; ephemeris.glsl declares u_lifts[8] */
#define MAX_EVENTS 32        /* lifts and droughts kept for the record */
_Static_assert(MAX_LIFTS == 8, "ephemeris.glsl declares u_lifts[8]");

typedef struct {
    float x, y, heading;     /* position in uv, heading in aspect space */
    float age, life;         /* seconds */
    float speed, halfw;      /* height-uv per second, half width in height-uv */
    int kind;                /* 0 charcoal line, 1 iron oxide line, 2 charcoal wash */
    int tool;                /* dramatic: 0 hard point, 1 stick, 2 side of the stick, 3 wash */
    int idle;                /* dramatic: slot empty until the next occupant arrives */
    float wake;              /* dramatic: when the next occupant arrives (run seconds) */
    float flow, curv, cw, fw; /* pressure, 1/turn radius, trust in the blurred view, in the field */
    float swirl;             /* signed turning, fading with path length: keeps a line from closing */
    float hook;              /* the same over a short stretch of path: keeps a line from hooking */
    int mass;                /* home mass index, -1 none */
    uint32_t gen;            /* occupant count of this slot */
    float born_t, bx, by, inh_birth, path;
    float trail[TRAIL][2];
    int ntrail;
    float trail_t;
} Worker;

typedef struct {
    int slot;
    uint32_t gen;
    float born, life, bx, by, ex, ey, inh, path;
    int kind;
} Row;

typedef struct {
    GLuint vbo;
    GLuint p_init, p_eval, p_dep, p_decay, p_disp, p_lift;
    int cap;                 /* long side of the surface */
    GLuint surf, fbo_surf, fbo_rb, verdict, fbo_eval, pbo;
    GLsync fence;
    int sw, sh, levels, rb_level, rbw, rbh;
    int rb_pending;
    double rb_last;
    float mip[MIP_MAX * MIP_MAX];
    int mw, mh;
    Worker w[NW];
    int nw;
    float mass[4][4];
    int nmass;
    int loose;               /* 0 masses, 1 drawing */
    float field[3][4];       /* kx, ky, omega, phase */
    uint32_t seed, salt;
    double started, prev, t;
    uint32_t frame;
    float pace, tone;
    int record;
    Row rows[REC_ROWS];
    int nrows, rowpos;
    /* dramatic composition */
    int dram;
    float tide_period, tide_phase;
    float lift_gap, drought_gap, oxide_p, past_cap;   /* dramatic: event means (s, 0 = none), oxide share, init cap */
    float tool_w[4];                                  /* dramatic: cumulative tool weights */
    float lifts[MAX_LIFTS][4];       /* x, y, r, start time; clog decays over ~15 min */
    int nlifts;
    double next_lift, next_drought, drought_end, lift_step_t;
    int lift_steps_left;
    float lift_now[4];               /* the lift in progress: x, y, r, active */
    float ev_lift[MAX_EVENTS][4];    /* record: t, x, y, r */
    float ev_drought[MAX_EVENTS][2]; /* record: t0, t1 */
    int n_ev_lift, n_ev_drought;
    uint32_t ev_rng;
    unsigned long born_total, oxide_total;
    double life_total;
    double diag_last;
} State;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint32_t hash32(uint32_t a, uint32_t b) {
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u);
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
static float urand(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xffffffu) / 16777216.f;
}
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float sstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}

/* ---- options ------------------------------------------------------------ */
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = EPH_NOPTS; *prefix = "NCZ_EPHEMERIS_"; *group = "ephemeris";
    return EPH_OPTS;
}

static uint32_t run_seed(uint32_t fixed) {
    if (fixed) return fixed;
    uint32_t s = 0;
    int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (f >= 0) { ssize_t n = read(f, &s, 4); close(f); if (n == 4 && s) return s; }
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (uint32_t)(t.tv_nsec ^ t.tv_sec ^ getpid()) | 1u;
}

/* ---- shaders ------------------------------------------------------------ */
static char *load_src(void) {
    const char *p[] = {"vendor/ephemeris/ephemeris.glsl", "../vendor/ephemeris/ephemeris.glsl",
                       "../../vendor/ephemeris/ephemeris.glsl",
                       "/usr/share/ncz-screensavers/shaders/ephemeris.glsl"};
    for (unsigned i = 0; i < sizeof p / sizeof p[0]; i++) {
        FILE *f = fopen(p[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        rewind(f);
        char *b = n > 0 ? malloc((size_t)n + 1) : NULL;
        if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
        fclose(f);
        b[n] = 0;
        fprintf(stderr, "[diag] ephemeris shader=%s\n", p[i]);
        return b;
    }
    fprintf(stderr, "ephemeris: cannot locate shader\n");
    return NULL;
}
static GLuint compile(GLenum type, const char *pass, const char *body) {
    char head[96];
    snprintf(head, sizeof head, "#version 300 es\n#define %s 1\n", pass);
    const char *src[2] = {head, body};
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char l[4096];
        glGetShaderInfoLog(s, sizeof l, NULL, l);
        fprintf(stderr, "ephemeris compile %s: %s\n", pass, l);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
static GLuint program(const char *body, const char *vs, const char *fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, body), f = compile(GL_FRAGMENT_SHADER, fs, body);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char l[4096];
        glGetProgramInfoLog(p, sizeof l, NULL, l);
        fprintf(stderr, "ephemeris link %s: %s\n", fs, l);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}
#define U(p, n) glGetUniformLocation(p, n)

/* ---- composition ---------------------------------------------------------- */
static void compose(State *s, const char *mode) {
    uint32_t z = hash32(s->seed, 0xC0FFEEu) | 1u;
    if (!strcmp(mode, "masses") || s->dram) s->loose = 0;
    else if (!strcmp(mode, "drawing")) s->loose = 1;
    else s->loose = urand(&z) < 0.5f ? 0 : 1;
    s->nmass = s->loose ? 4 : s->dram ? 2 + (urand(&z) < 0.5f) : 3 + (urand(&z) < 0.45f);
    /* Masses keep apart from each other and from the edges, so the open
     * space between them is part of the composition rather than leftover. */
    for (int k = 0; k < s->nmass; k++) {
        float best[4] = {0.5f, 0.5f, 0.2f, 0};
        float bestd = -1;
        for (int tries = 0; tries < 24; tries++) {
            float c[4] = {0.16f + 0.68f * urand(&z), 0.18f + 0.64f * urand(&z),
                          (s->loose ? 0.12f : 0.19f) + 0.10f * urand(&z), 6.2831853f * urand(&z)};
            float dmin = 9;
            for (int j = 0; j < k; j++) {
                float dx = (c[0] - s->mass[j][0]) * 1.6f, dy = c[1] - s->mass[j][1];
                float d = sqrtf(dx * dx + dy * dy) - s->mass[j][2] - c[2];
                if (d < dmin) dmin = d;
            }
            if (dmin > bestd) { bestd = dmin; memcpy(best, c, sizeof best); }
        }
        memcpy(s->mass[k], best, sizeof best);
    }
    for (int k = 0; k < 3; k++) {
        float a = 6.2831853f * urand(&z), f = 2.0f + 5.0f * urand(&z);
        s->field[k][0] = cosf(a) * f;
        s->field[k][1] = sinf(a) * f;
        s->field[k][2] = (0.01f + 0.03f * urand(&z)) * (urand(&z) < 0.5f ? -1.f : 1.f);
        s->field[k][3] = 6.2831853f * urand(&z);
    }
}

/* The correcting field: the curl of a slowly moving sum of waves. It is a
 * force, not an event; it pulls strokes back without saying why. */
static void field_dir(const State *s, float x, float y, float t, float *fx, float *fy) {
    float dpx = 0, dpy = 0;
    for (int k = 0; k < 3; k++) {
        float ph = s->field[k][0] * x + s->field[k][1] * y + s->field[k][2] * t * 6.2831853f + s->field[k][3];
        float c = cosf(ph);
        dpx += c * s->field[k][0];
        dpy += c * s->field[k][1];
    }
    float l = sqrtf(dpx * dpx + dpy * dpy) + 1e-6f;
    *fx = dpy / l;
    *fy = -dpx / l;
}

/* The worker's whole view of the surface: the last copy of the small mip. */
static float seen(const State *s, float u, float v) {
    if (s->mw < 1 || s->mh < 1 || s->mw > MIP_MAX || s->mh > MIP_MAX) return 0.f;
    float fx = clampf(u, 0.f, 1.f) * s->mw - 0.5f, fy = clampf(v, 0.f, 1.f) * s->mh - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = x0 < 0 ? 0 : x0 >= s->mw ? s->mw - 1 : x0;
    x1 = x1 < 0 ? 0 : x1 >= s->mw ? s->mw - 1 : x1;
    y0 = y0 < 0 ? 0 : y0 >= s->mh ? s->mh - 1 : y0;
    y1 = y1 < 0 ? 0 : y1 >= s->mh ? s->mh - 1 : y1;
    float a = s->mip[y0 * s->mw + x0], b = s->mip[y0 * s->mw + x1];
    float c = s->mip[y1 * s->mw + x0], d = s->mip[y1 * s->mw + x1];
    return (a + (b - a) * ax) + ((c + (d - c) * ax) - (a + (b - a) * ax)) * ay;
}

/* A new occupant for slot i. Everything it starts with comes from the run
 * seed, the slot number and the occupant count. The slot is cleared first. */
static void spawn(State *s, int i, float t_birth, float age) {
    uint32_t gen = s->w[i].gen + 1;
    Worker *w = &s->w[i];
    memset(w, 0, sizeof *w);
    w->gen = gen;
    uint32_t z = hash32(hash32(s->seed, (uint32_t)i * 7919u + 17u), gen) | 1u;
    float u = urand(&z);
    w->life = clampf(2.5f + 26.f * -logf(fmaxf(u, 1e-4f)) * 0.45f, 2.5f, 60.f);
    w->age = age < 0 ? 0 : age * w->life;
    w->born_t = t_birth - w->age;
    w->speed = 0.018f + 0.027f * urand(&z);
    float wr = urand(&z), kr = urand(&z);
    w->kind = kr < 0.06f ? 1 : kr < (s->loose ? 0.22f : 0.32f) ? 2 : 0;
    if (w->kind == 2) {
        /* A broad, dry wash: how a mass gets its tone. */
        w->halfw = 0.006f + 0.011f * wr;
        w->speed *= 0.6f;
    } else {
        w->halfw = 0.0011f + 0.0032f * wr * wr;
    }
    w->flow = 1.f;
    w->curv = 11.f;
    w->cw = s->loose ? 2.4f : 1.6f;
    w->fw = 0.9f;
    if (s->dram) {
        /* Hands that differ: lifetimes from seconds to minutes, speed, width,
         * and how tightly each one can turn. The long-lived ones follow the
         * contours they can make out in the blurred view, for many screen
         * heights, around whatever their predecessors left. */
        float lu = urand(&z);
        w->life = lu < 0.6f ? 3.f + 7.f * urand(&z) : lu < 0.9f ? 15.f + 30.f * urand(&z) : 60.f + 120.f * urand(&z);
        w->age = age < 0 ? 0 : age * w->life;
        w->born_t = t_birth - w->age;
        w->speed = 0.02f + 0.10f * urand(&z);
        float tu = urand(&z);
        int oxide = urand(&z) < s->oxide_p;
        w->tool = oxide ? (tu < 0.5f ? 2 : 3) : tu < s->tool_w[0] ? 0 : tu < s->tool_w[1] ? 1 : tu < s->tool_w[2] ? 2 : 3;
        static const float TOOL_W[4][2] = {{0.75f, 0.75f}, {1.5f, 5.f}, {10.f, 25.f}, {30.f, 70.f}};
        static const float TOOL_FLOW[4] = {0.5f, 0.45f, 0.35f, 0.10f};
        w->halfw = (TOOL_W[w->tool][0] + (TOOL_W[w->tool][1] - TOOL_W[w->tool][0]) * wr) / 1080.f;
        w->flow = TOOL_FLOW[w->tool];
        w->kind = oxide ? 1 : w->tool == 3 ? 2 : 0;
        w->curv = 1.f / (0.06f * powf(0.5f / 0.06f, urand(&z)));   /* turn radius 0.06..0.5 of the height */
        if (w->life >= 60.f) { w->cw = 4.f; w->fw = 0.25f; }
    }
    float asp = (float)s->sw / (float)(s->sh > 0 ? s->sh : 1);
    if (!s->loose && !(s->dram && urand(&z) < 0.4f)) {
        int k = (int)(urand(&z) * s->nmass) % s->nmass;
        float r = s->mass[k][2] * 0.9f * sqrtf(urand(&z)), a = 6.2831853f * urand(&z);
        w->mass = k;
        w->x = clampf(s->mass[k][0] + cosf(a) * r / asp, 0.04f, 0.96f);
        w->y = clampf(s->mass[k][1] + sinf(a) * r, 0.04f, 0.96f);
    } else {
        w->mass = -1;
        w->x = 0.06f + 0.88f * urand(&z);
        w->y = 0.08f + 0.84f * urand(&z);
    }
    w->heading = 6.2831853f * urand(&z);
    w->bx = w->x;
    w->by = w->y;
    w->inh_birth = seen(s, w->x, w->y);
    s->born_total++;
    s->oxide_total += (unsigned long)(w->kind == 1);
}

static void retire(State *s, int i) {
    const Worker *w = &s->w[i];
    if (w->gen == 0) return;
    Row *r = &s->rows[s->rowpos];
    r->slot = i; r->gen = w->gen; r->born = w->born_t; r->life = w->age;
    r->bx = w->bx; r->by = w->by; r->ex = w->x; r->ey = w->y;
    r->inh = w->inh_birth; r->path = w->path; r->kind = w->kind;
    s->rowpos = (s->rowpos + 1) % REC_ROWS;
    if (s->nrows < REC_ROWS) s->nrows++;
    s->life_total += w->age;
}

/* Advance one worker; fill its stroke segment for the GPU. */
static void step_worker(State *s, int i, float dt, float seg[4], float segx[4]) {
    Worker *w = &s->w[i];
    float asp = (float)s->sw / (float)(s->sh > 0 ? s->sh : 1);
    w->age += dt;
    float ex = 1.f / (float)(s->mw > 0 ? s->mw : 1), ey = 1.f / (float)(s->mh > 0 ? s->mh : 1);
    float gx = (seen(s, w->x + ex, w->y) - seen(s, w->x - ex, w->y)) / (2 * ex * asp);
    float gy = (seen(s, w->x, w->y + ey) - seen(s, w->x, w->y - ey)) / (2 * ey);
    float gm = sqrtf(gx * gx + gy * gy);
    float hx = cosf(w->heading), hy = sinf(w->heading);
    float cx = 0, cy = 0, wc = 0;
    if (gm > 1e-4f) {
        cx = -gy / gm; cy = gx / gm;
        if (cx * hx + cy * hy < 0) { cx = -cx; cy = -cy; }
        wc = clampf(gm / 12.f, 0.f, 1.f) * w->cw;
    }
    float fx, fy;
    field_dir(s, w->x * asp, w->y, (float)s->t, &fx, &fy);
    float hmx = 0, hmy = 0, outside = 0;
    if (w->mass >= 0) {
        float vx = (s->mass[w->mass][0] - w->x) * asp, vy = s->mass[w->mass][1] - w->y;
        float vl = sqrtf(vx * vx + vy * vy) + 1e-6f;
        float r = s->mass[w->mass][2];
        outside = sstep(0.8f * r, 1.4f * r, vl);
        hmx = vx / vl * outside * 0.5f;
        hmy = vy / vl * outside * 0.5f;
    }
    /* The margin is part of the plate: a smooth push inward, and the line
     * lightens as it nears the edge instead of bouncing along it. */
    float bx = sstep(0.12f, 0.03f, w->x) - sstep(0.88f, 0.97f, w->x);
    float by = sstep(0.12f, 0.03f, w->y) - sstep(0.88f, 0.97f, w->y);
    float edge = fminf(fminf(w->x, 1.f - w->x), fminf(w->y, 1.f - w->y));
    /* The private trail: the only full-resolution memory a worker has, and it
     * is cleared with the worker. It keeps a worker off its own recent line. */
    float rx = 0, ry = 0;
    for (int k = 0; k + 3 < w->ntrail; k++) {
        float dx = (w->x - w->trail[k][0]) * asp, dy = w->y - w->trail[k][1];
        float d2 = dx * dx + dy * dy;
        if (d2 < 0.0004f && d2 > 1e-9f) { float d = sqrtf(d2); rx += dx / d * (0.02f - d) * 50; ry += dy / d * (0.02f - d) * 50; }
    }
    float tx = hx + cx * wc + fx * w->fw + hmx + bx * 3 + rx * 1.2f;
    float ty = hy + cy * wc + fy * w->fw + hmy + by * 3 + ry * 1.2f;
    float want = atan2f(ty, tx), diff = want - w->heading;
    while (diff > 3.14159265f) diff -= 6.2831853f;
    while (diff < -3.14159265f) diff += 6.2831853f;
    /* A gentle turning limit keeps curves open: tight loops would start to
     * read as letters, and the plate carries no glyphs. */
    float turn = w->curv * w->speed * s->pace * dt;   /* curvature radius >= 1/curv of the height */
    float dh = clampf(diff, -turn, turn);
    /* A line that has already turned three quarters of the way round may not
     * keep turning the same way: closed loops and coils read as glyphs. It
     * can straighten or turn back instead. The memory of turning fades over
     * about 1.5 screen heights of path, so wide arcs are never affected. */
    /* Hands that can turn tightly (radius under an eighth of the height)
     * stop at a half turn, so they never draw the three-quarter arcs that
     * closed into round letter-sized rings (an o or a Q) where other strokes
     * crossed them. */
    float swirl_max = w->curv > 8.f ? 3.14159265f : 1.5f * 3.14159265f;
    if (s->dram && dh * w->swirl > 0.f && fabsf(w->swirl) > swirl_max) dh = 0.f;
    /* Over a short stretch (about 0.15 of the height of path) a line may not
     * turn more than about 115 degrees one way: a tight hook reads as a P or
     * a 9 at thumbnail size even when it never closes. At the tightest turn
     * radius a steady turn would reach about 145 degrees, so this binds. */
    if (s->dram && dh * w->hook > 0.f && fabsf(w->hook) > 2.0f) dh = 0.f;
    w->heading += dh;
    w->swirl = w->swirl * expf(-w->speed * s->pace * dt / 1.5f) + dh;
    if (s->dram) w->hook = w->hook * expf(-w->speed * s->pace * dt / 0.15f) + dh;
    float agree = wc > 0.05f ? fabsf(cx * fx + cy * fy) : 0.3f;
    float taper = sstep(0.f, 0.6f, w->age) * sstep(0.f, 1.0f, w->life - w->age);
    float weight = (0.3f + 0.7f * agree) * taper * (1.f - 0.6f * outside) * sstep(0.02f, 0.09f, edge);
    float px = w->x, py = w->y;
    float v = w->speed * s->pace * dt;
    w->x += cosf(w->heading) * v / asp;
    w->y += sinf(w->heading) * v;
    w->x = clampf(w->x, 0.01f, 0.99f);
    w->y = clampf(w->y, 0.01f, 0.99f);
    w->path += v;
    if (w->age - w->trail_t > 0.25f) {
        if (w->ntrail == TRAIL) { memmove(w->trail, w->trail + 1, sizeof(w->trail[0]) * (TRAIL - 1)); w->ntrail--; }
        w->trail[w->ntrail][0] = w->x; w->trail[w->ntrail][1] = w->y; w->ntrail++;
        w->trail_t = w->age;
    }
    seg[0] = px; seg[1] = py; seg[2] = w->x; seg[3] = w->y;
    segx[0] = seen(s, w->x, w->y);
    segx[1] = weight * w->flow;
    /* GPU pigment code: 0 charcoal line, 1 iron oxide line, 2 charcoal wash, 3 iron oxide wash */
    segx[2] = (w->kind == 1 && w->tool == 3) ? 3.f : (float)w->kind;
    segx[3] = w->halfw;
}

/* ---- GPU resources -------------------------------------------------------- */
static void full_quad(State *s) {
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static void read_mip_now(State *s) {
    if (s->rbw > MIP_MAX || s->rbh > MIP_MAX) return; /* make_surface keeps both <= MIP_MAX */
    unsigned char buf[MIP_MAX * MIP_MAX * 4];
    glBindTexture(GL_TEXTURE_2D, s->surf);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_rb);
    glReadPixels(0, 0, s->rbw, s->rbh, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    for (int k = 0; k < s->rbw * s->rbh; k++) {
        float d = (buf[k * 4] + buf[k * 4 + 1]) / 255.f;
        s->mip[k] = d > 1 ? 1 : d;
    }
    s->mw = s->rbw; s->mh = s->rbh;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* Surface size for a render size: the same aspect, long side capped. */
static void surf_dims(int cap, int w, int h, int *sw, int *sh) {
    int lng = w > h ? w : h;
    *sw = w; *sh = h;
    if (lng > cap) { *sw = (int)((long)w * cap / lng); *sh = (int)((long)h * cap / lng); }
    if (*sw < 64) *sw = 64;
    if (*sh < 64) *sh = 64;
}

static void make_surface(State *s, int w, int h, int sync_read) {
    int sw, sh;
    surf_dims(s->cap, w, h, &sw, &sh);
    int lng = sw > sh ? sw : sh;
    if (s->surf) { glDeleteTextures(1, &s->surf); s->surf = 0; }
    if (s->fence) { glDeleteSync(s->fence); s->fence = 0; s->rb_pending = 0; }
    s->sw = sw; s->sh = sh;
    int l = 1;
    while ((lng >> l) > 0) l++;
    s->levels = l;
    int rl = 0;
    while (((sw > sh ? sw : sh) >> rl) > MIP_MAX) rl++;
    s->rb_level = rl;
    s->rbw = sw >> rl; if (s->rbw < 1) s->rbw = 1;
    s->rbh = sh >> rl; if (s->rbh < 1) s->rbh = 1;
    glGenTextures(1, &s->surf);
    glBindTexture(GL_TEXTURE_2D, s->surf);
    glTexStorage2D(GL_TEXTURE_2D, s->levels, GL_RGBA8, sw, sh);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_surf);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->surf, 0);
    GLenum st1 = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_rb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->surf, s->rb_level);
    GLenum st2 = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st1 != GL_FRAMEBUFFER_COMPLETE || st2 != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "ephemeris: surface framebuffer incomplete (0x%x, 0x%x)\n", st1, st2);
        ncz_harness_die(1);
        return;
    }
    /* A surface that looks as if work has already happened on it. */
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_surf);
    glViewport(0, 0, sw, sh);
    glDisable(GL_BLEND);
    glUseProgram(s->p_init);
    glUniform2f(U(s->p_init, "u_size"), (float)sw, (float)sh);
    glUniform4fv(U(s->p_init, "u_mass"), 4, &s->mass[0][0]);
    glUniform1i(U(s->p_init, "u_nmass"), s->nmass);
    glUniform1ui(U(s->p_init, "u_salt"), s->salt);
    /* Dramatic: the apparent past covers the plate (loose contour marks
     * everywhere, and a faint worked ground under them) but stays faint
     * (capped below). */
    glUniform1f(U(s->p_init, "u_loose"), s->dram ? 1.f : (float)s->loose);
    glUniform1f(U(s->p_init, "u_plate"), s->dram ? 1.f : 0.f);
    glUniform1f(U(s->p_init, "u_cap"), s->dram ? s->past_cap : 1.f);
    full_quad(s);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* One synchronous read at start so the first workers are not blind. A
     * resize during the run skips it; the next asynchronous read fills in. */
    if (sync_read) {
        read_mip_now(s);
    } else {
        s->mw = s->mh = 0;
        s->rb_last = 0;
    }
    fprintf(stderr, "[diag] ephemeris surface=%dx%d levels=%d view=%dx%d (level %d)\n",
            sw, sh, s->levels, s->rbw, s->rbh, s->rb_level);
}

/* The workers' view is refreshed a few times a second through a pixel pack
 * buffer and a fence: glReadPixels into the PBO only queues the copy, and
 * the data is mapped on a later frame once the fence has signalled, so this
 * never drains the GPU pipeline (the stall that tools/tests/
 * test_no_periodic_readback.py guards against is a synchronous read). The
 * copy is at most 32x32x4 bytes. The only synchronous read is the one at
 * surface creation. */
static void readback_poll(State *s, double tn) {
    if (s->rb_pending && s->fence) {
        GLenum r = glClientWaitSync(s->fence, 0, 0);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
            const unsigned char *b = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)s->rbw * s->rbh * 4, GL_MAP_READ_BIT);
            if (b) {
                for (int k = 0; k < s->rbw * s->rbh; k++) {
                    float d = (b[k * 4] + b[k * 4 + 1]) / 255.f;
                    s->mip[k] = d > 1 ? 1 : d;
                }
                s->mw = s->rbw; s->mh = s->rbh;
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glDeleteSync(s->fence);
            s->fence = 0;
            s->rb_pending = 0;
        } else if (r == GL_WAIT_FAILED) {
            fprintf(stderr, "[diag] ephemeris readback fence wait failed; retrying\n");
            glDeleteSync(s->fence);
            s->fence = 0;
            s->rb_pending = 0;
        }
    }
    if (!s->rb_pending && tn - s->rb_last > 0.4) {
        glBindTexture(GL_TEXTURE_2D, s->surf);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_rb);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
        glReadPixels(0, 0, s->rbw, s->rbh, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        s->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        s->rb_pending = s->fence != 0;
        s->rb_last = tn;
    }
}

/* Dramatic options. With randomize=auto each one nobody set (not the user,
 * not the scene) is drawn from the run's seed; the draw is logged. */
static void dramatic_options(State *s, const ncz_opts *o, float tide_r) {
    static const float TOOLS[4][3] = {{0.25f, 0.65f, 0.85f}, {0.55f, 0.80f, 0.92f}, {0.10f, 0.70f, 0.88f}, {0.10f, 0.35f, 0.60f}};
    static const char *const TOOL_ID[4] = {"mix", "points", "sticks", "washes"};
    int rnd = o && !strcmp(ncz_opts_get(o, "randomize"), "auto");
    uint32_t z = hash32(s->seed, 0xB0A7u) | 1u;
    /* Every draw is made whether or not it is used, so setting one option
     * never changes what the seed gives the others. */
    float u_lift = urand(&z), u_dry = urand(&z), u_ox = urand(&z), u_past = urand(&z), u_tool = urand(&z);
    float u_nw = urand(&z), u_tone = urand(&z);
#define SET(name) (!rnd || (o && ncz_opts_is_set(o, name)))
    s->tide_period = SET("tide") ? (o ? (float)ncz_opts_get_float(o, "tide") : 330.f) : 240.f + 180.f * tide_r;
    s->lift_gap = SET("lift-every") ? (o ? (float)ncz_opts_get_float(o, "lift-every") : 300.f) : 200.f + 200.f * u_lift;
    s->drought_gap = SET("drought-every") ? (o ? (float)ncz_opts_get_float(o, "drought-every") : 360.f) : 240.f + 240.f * u_dry;
    s->oxide_p = SET("oxide") ? (o ? (float)ncz_opts_get_float(o, "oxide") : 0.04f) : 0.02f + 0.06f * u_ox;
    s->past_cap = SET("past") ? (o ? (float)ncz_opts_get_float(o, "past") : 0.35f) : 0.25f + 0.2f * u_past;
    int tool = 0;
    if (SET("tools")) {
        const char *t = o ? ncz_opts_get(o, "tools") : "mix";
        for (int k = 0; k < 4; k++) if (!strcmp(t, TOOL_ID[k])) tool = k;
    } else {
        tool = u_tool < 0.6f ? 0 : u_tool < 0.73f ? 1 : u_tool < 0.87f ? 2 : 3;
    }
    for (int k = 0; k < 3; k++) s->tool_w[k] = TOOLS[tool][k];
    /* Dramatic keeps 4-24 workers (see the workers option); the draw is 6-16. */
    if (!SET("workers")) s->nw = 6 + (int)(11.f * u_nw);
    if (!SET("tone")) s->tone = 0.62f + 0.16f * u_tone;
#undef SET
    fprintf(stderr, "[diag] ephemeris dramatic: workers=%d tone=%.2f tide=%.0f lift-every=%.0f drought-every=%.0f "
            "tools=%s oxide=%.3f past=%.2f (randomize=%s)\n", s->nw, s->tone, s->tide_period, s->lift_gap,
            s->drought_gap, TOOL_ID[tool], s->oxide_p, s->past_cap, rnd ? "auto" : "off");
}

static void init_ephemeris(ModeInfo *m) {
    State *s = calloc(1, sizeof *s);
    if (!s) { ncz_harness_die(1); return; }
    m->data = s;
    const ncz_opts *o = ncz_harness_opts();
    const char *comp = o ? ncz_opts_get(o, "composition") : "auto";
    s->dram = !comp || !strcmp(comp, "dramatic");
    s->nw = o ? (int)ncz_opts_get_int(o, "workers") : 32;
    if (s->dram) {
        if (!o || !ncz_opts_is_set(o, "workers")) s->nw = 10;
        if (s->nw < 4) s->nw = 4;
        if (s->nw > 24) s->nw = 24;
    }
    if (s->nw < 1) s->nw = 1;
    if (s->nw > NW) s->nw = NW;
    s->pace = o ? (float)ncz_opts_get_float(o, "pace") : 1.f;
    s->tone = o ? (float)ncz_opts_get_float(o, "tone") : 0.5f;
    if (s->dram && (!o || !ncz_opts_is_set(o, "tone"))) s->tone = 0.70f;
    const char *gpu_class = ncz_gpu_class_current();
    s->cap = (s->dram && gpu_class && strcmp(gpu_class, "weak") != 0) ? SURF_CAP_BIG : SURF_CAP;
    s->record = o ? ncz_opts_get_bool(o, "record") : 1;
    s->seed = run_seed(o ? (uint32_t)strtoul(ncz_opts_get(o, "seed"), NULL, 0) : 0);
    s->salt = hash32(s->seed, 0x5a17u);
    compose(s, comp);
    if (s->dram) {
        uint32_t z = hash32(s->seed, 0x71DEu) | 1u;
        s->ev_rng = z;
        float tide_r = urand(&z);
        s->tide_phase = 6.2831853f * urand(&z);
        dramatic_options(s, o, tide_r);
        /* Rare events, a Poisson process each: some runs have none. Lifts
         * start at the earliest two minutes in (the brief's first >= 120 s);
         * after that the gaps are exponential with a mean of lift-every
         * (default 300 s); droughts, drought-every (default 360 s). */
        double ul = urand(&s->ev_rng), ud = urand(&s->ev_rng);
        s->next_lift = s->lift_gap > 0 ? 120.0 - s->lift_gap * log(fmax(1e-6, 1.0 - ul)) : INFINITY;
        s->next_drought = s->drought_gap > 0 ? -s->drought_gap * log(fmax(1e-6, 1.0 - ud)) : INFINITY;
    }

    char *body = load_src();
    if (!body) { ncz_harness_die(1); return; }
    s->p_init = program(body, "PASS_VS_FULL", "PASS_INIT");
    s->p_eval = program(body, "PASS_VS_FULL", "PASS_EVAL");
    s->p_dep = program(body, "PASS_VS_DEPOSIT", "PASS_DEPOSIT");
    s->p_decay = program(body, "PASS_VS_FULL", "PASS_DECAY");
    s->p_disp = program(body, "PASS_VS_FULL", "PASS_DISPLAY");
    s->p_lift = program(body, "PASS_VS_FULL", "PASS_LIFT");
    free(body);
    if (!s->p_init || !s->p_eval || !s->p_dep || !s->p_decay || !s->p_disp || !s->p_lift) { ncz_harness_die(1); return; }

    static const float q[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glGenFramebuffers(1, &s->fbo_surf);
    glGenFramebuffers(1, &s->fbo_rb);
    glGenFramebuffers(1, &s->fbo_eval);
    glGenTextures(1, &s->verdict);
    glBindTexture(GL_TEXTURE_2D, s->verdict);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, NW, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_eval);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->verdict, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "ephemeris: evaluator framebuffer incomplete\n");
        ncz_harness_die(1);
        return;
    }
    glGenBuffers(1, &s->pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, MIP_MAX * MIP_MAX * 4, NULL, GL_STREAM_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int w = m->xgwa.width > 0 ? m->xgwa.width : 1280, h = m->xgwa.height > 0 ? m->xgwa.height : 720;
    make_surface(s, w, h, 1);
    /* Workers are already mid-life when the run begins: no global start. */
    for (int i = 0; i < s->nw; i++) {
        uint32_t z = hash32(s->seed, (uint32_t)i + 0x51u) | 1u;
        spawn(s, i, 0.f, urand(&z));
        if (s->dram && urand(&z) < 0.4f) {
            s->w[i].idle = 1;
            s->w[i].wake = 20.f * urand(&z);
        }
    }
    s->started = s->prev = s->rb_last = s->diag_last = now();
    const char *glv = (const char *)glGetString(GL_VERSION);
    fprintf(stderr, "[diag] ephemeris composition=%s masses=%d workers=%d pace=%.2f tone=%.2f surface_cap=%d GL=%s\n",
            s->dram ? "dramatic" : s->loose ? "drawing" : "masses", s->nmass, s->nw, s->pace, s->tone, s->cap,
            glv ? glv : "unknown");
    if (s->dram)
        fprintf(stderr, "[diag] ephemeris first lift at t=%.0f, first drought at t=%.0f\n", s->next_lift, s->next_drought);
}

/* Dramatic composition: rare events, each a Poisson process.
 * A lift chooses a mass-sized region where there is work, reduces it to a
 * smear over eight seconds and leaves ground that takes marks badly for a
 * long time. A drought is a stretch in which the evaluator accepts almost
 * nothing: workers keep moving and the plate stops changing, and nothing
 * announces it or its end. */
static void events_step(State *s) {
    /* A lift due while one is still being applied waits for it to finish. */
    if (s->t >= s->next_lift && s->lift_steps_left == 0) {
        /* The lift goes where there is most work to lose: every spot on a
         * 16 x 16 grid is scored by the mean density over a disc the size of
         * the lift (a centre sample, six at 0.4 r and twelve at 0.8 r, so
         * each covers a similar area; aspect-corrected), not by one texel: the
         * view is clamped at 1, so any single dark stroke would tie with the
         * densest knot. A small jitter breaks ties. Both the
         * view and the lift use GL coordinates (y up). */
        float r = 0.12f + 0.13f * urand(&s->ev_rng);
        float asp = (float)s->sw / (float)(s->sh > 0 ? s->sh : 1);
        float bx = 0.5f, by = 0.5f, best = -1.f;
        for (int gy = 0; gy < 16; gy++)
            for (int gx = 0; gx < 16; gx++) {
                float x = 0.15f + 0.7f * (gx + 0.5f) / 16.f, y = 0.15f + 0.7f * (gy + 0.5f) / 16.f;
                float d = seen(s, x, y);
                for (int k = 0; k < 6; k++)
                    d += seen(s, x + 0.4f * r * cosf(1.0472f * k) / asp, y + 0.4f * r * sinf(1.0472f * k));
                for (int k = 0; k < 12; k++)
                    d += seen(s, x + 0.8f * r * cosf(0.5236f * k + 0.26f) / asp, y + 0.8f * r * sinf(0.5236f * k + 0.26f));
                d = d / 19.f + 0.01f * urand(&s->ev_rng);
                if (d > best) { best = d; bx = x; by = y; }
            }
        s->lift_now[0] = bx; s->lift_now[1] = by; s->lift_now[2] = r; s->lift_now[3] = 1.f;
        s->lift_steps_left = 8;
        s->lift_step_t = s->t;
        if (s->nlifts == MAX_LIFTS) {
            memmove(s->lifts, s->lifts + 1, sizeof(s->lifts[0]) * (MAX_LIFTS - 1));
            s->nlifts--;
        }
        s->lifts[s->nlifts][0] = bx; s->lifts[s->nlifts][1] = by;
        s->lifts[s->nlifts][2] = r; s->lifts[s->nlifts][3] = (float)s->t;
        s->nlifts++;
        if (s->n_ev_lift < MAX_EVENTS) {
            float *e = s->ev_lift[s->n_ev_lift++];
            e[0] = (float)s->t; e[1] = bx; e[2] = by; e[3] = r;
        }
        s->next_lift = s->t - s->lift_gap * log(fmax(1e-6, 1.0 - urand(&s->ev_rng)));
    }
    /* Drought starts are a Poisson process (mean gap 360 s); one that starts
     * inside another just extends it. */
    if (s->t >= s->next_drought) {
        double len = 20.0 + 40.0 * urand(&s->ev_rng);
        double end = s->t + len;
        if (s->t < s->drought_end && s->n_ev_drought > 0) {
            if (end > s->drought_end) s->ev_drought[s->n_ev_drought - 1][1] = (float)end;
        } else if (s->n_ev_drought < MAX_EVENTS) {
            s->ev_drought[s->n_ev_drought][0] = (float)s->t;
            s->ev_drought[s->n_ev_drought][1] = (float)end;
            s->n_ev_drought++;
        }
        if (end > s->drought_end) s->drought_end = end;
        s->next_drought = s->t - s->drought_gap * log(fmax(1e-6, 1.0 - urand(&s->ev_rng)));
    }
}

static void draw_ephemeris(ModeInfo *m) {
    State *s = m->data;
    if (!s || !s->p_disp) return;
    int w = m->xgwa.width, h = m->xgwa.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    {
        int sw, sh;
        surf_dims(s->cap, w, h, &sw, &sh);
        if (sw != s->sw || sh != s->sh) make_surface(s, w, h, 0);
    }
    double tn = now();
    float dt = (float)(tn - s->prev);
    s->prev = tn;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    s->t += dt * s->pace;
    s->frame++;

    readback_poll(s, tn);

    float seg[NW][4], segx[NW][4];
    memset(seg, 0, sizeof seg);
    memset(segx, 0, sizeof segx);
    if (s->dram) events_step(s);
    for (int i = 0; i < s->nw; i++) {
        Worker *wk = &s->w[i];
        if (!s->dram) {
            if (wk->age >= wk->life) {
                retire(s, i);
                spawn(s, i, (float)s->t, -1.f);
            }
        } else if (!wk->idle && wk->age >= wk->life) {
            /* The slot stays empty for a while: a lognormal gap (median 6 s,
             * 95th percentile 90 s) drawn from the slot and its occupant count,
             * stretched when the tide is out. The tide is the run clock, the
             * same for every slot; nothing about the departing occupant enters
             * the gap. */
            retire(s, i);
            uint32_t z = hash32(hash32(s->seed, (uint32_t)i * 104729u + 5u), wk->gen) | 1u;
            float u1 = fmaxf(urand(&z), 1e-6f), u2 = urand(&z);
            float n = sqrtf(-2.f * logf(u1)) * cosf(6.2831853f * u2);
            float tide = 1.f - 0.65f * (0.5f + 0.5f * sinf(6.2831853f * (float)s->t / s->tide_period + s->tide_phase));
            wk->idle = 1;
            wk->wake = (float)s->t + 6.f * expf(1.646f * n) / tide;
        } else if (wk->idle && s->t >= wk->wake) {
            spawn(s, i, (float)s->t, -1.f);
        }
        if (!wk->idle) step_worker(s, i, dt, seg[i], segx[i]);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    full_quad(s);

    /* Evaluator: reads the real surface, writes one verdict per slot. */
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_eval);
    glViewport(0, 0, NW, 1);
    glDisable(GL_BLEND);
    glUseProgram(s->p_eval);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->surf);
    glUniform1i(U(s->p_eval, "u_surface"), 0);
    glUniform2f(U(s->p_eval, "u_size"), (float)s->sw, (float)s->sh);
    glUniform4fv(U(s->p_eval, "u_seg"), NW, &seg[0][0]);
    glUniform4fv(U(s->p_eval, "u_segx"), NW, &segx[0][0]);
    glUniform1f(U(s->p_eval, "u_tol"), 0.3f);
    int drought = s->dram && s->t < s->drought_end;
    glUniform1f(U(s->p_eval, "u_outage"), drought ? 0.95f : 0.07f);
    /* Dramatic: a ground that is already full refuses most new work. */
    glUniform2f(U(s->p_eval, "u_full"), 0.8f, s->dram ? 0.7f : 0.f);
    glUniform1ui(U(s->p_eval, "u_salt"), s->salt);
    glUniform1ui(U(s->p_eval, "u_epoch"), (uint32_t)(s->t / 1.7));
    glUniform1ui(U(s->p_eval, "u_frame"), s->frame);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* Deposit: saturating pigment, dst + src * (1 - dst). */
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_surf);
    glViewport(0, 0, s->sw, s->sh);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);
    glUseProgram(s->p_dep);
    glBindTexture(GL_TEXTURE_2D, s->verdict);
    glUniform1i(U(s->p_dep, "u_verdict"), 0);
    glUniform2f(U(s->p_dep, "u_size"), (float)s->sw, (float)s->sh);
    glUniform4fv(U(s->p_dep, "u_seg"), NW, &seg[0][0]);
    glUniform4fv(U(s->p_dep, "u_segx"), NW, &segx[0][0]);
    /* Dramatic: per-tool pressure is in the stroke weight, so the global flow is
     * higher; a stick crossing a spot once leaves close to full charcoal. */
    glUniform1f(U(s->p_dep, "u_flow"), clampf((s->dram ? 1.0f : 0.3f) * dt * 60.f, 0.f, s->dram ? 2.f : 0.8f));
    glUniform1ui(U(s->p_dep, "u_salt"), s->salt);
    glUniform1f(U(s->p_dep, "u_now"), (float)s->t);
    glUniform1i(U(s->p_dep, "u_nlifts"), s->nlifts);
    glUniform4fv(U(s->p_dep, "u_lifts"), MAX_LIFTS, &s->lifts[0][0]);
    /* One 4-vertex strip per worker. The vertex shader builds the stroke quad
     * from gl_VertexID and gl_InstanceID; the bound full-screen buffer only
     * keeps attribute 0 enabled for strict drivers and is not read. */
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, s->nw);

    /* Weathering: the surface forgets, one 8-bit step at a time. */
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(s->p_decay);
    float k = 255.f * dt * s->pace;
    if (s->dram)
        glUniform3f(U(s->p_decay, "u_rate"), clampf(k / 1200.f, 0, 1), clampf(k / 1800.f, 0, 1), clampf(k / 12.f, 0, 1));
    else
        glUniform3f(U(s->p_decay, "u_rate"), clampf(k / 420.f, 0, 1), clampf(k / 700.f, 0, 1), clampf(k / 12.f, 0, 1));
    glUniform1ui(U(s->p_decay, "u_frame"), s->frame);
    glUniform1ui(U(s->p_decay, "u_salt"), s->salt);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBlendEquation(GL_FUNC_ADD);
    /* A lift in progress: eight steps a second apart, charcoal in the region
     * multiplied down (to about a quarter in all) and iron oxide to about half.
     * What is left is a dull grey smear, not clean paper. */
    if (s->lift_steps_left > 0 && s->t >= s->lift_step_t) {
        glViewport(0, 0, s->sw, s->sh);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        glUseProgram(s->p_lift);
        glUniform2f(U(s->p_lift, "u_size"), (float)s->sw, (float)s->sh);
        glUniform4fv(U(s->p_lift, "u_lift"), 1, s->lift_now);
        glUniform1ui(U(s->p_lift, "u_salt"), s->salt);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        s->lift_steps_left--;
        s->lift_step_t = s->t + 1.0;
        if (s->lift_steps_left == 0) s->lift_now[3] = 0.f;
    }
    glDisable(GL_BLEND);

    /* Display. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glUseProgram(s->p_disp);
    glBindTexture(GL_TEXTURE_2D, s->surf);
    glUniform1i(U(s->p_disp, "u_surface"), 0);
    glUniform2f(U(s->p_disp, "u_res"), (float)w, (float)h);
    glUniform1f(U(s->p_disp, "u_tone"), s->tone);
    glUniform1f(U(s->p_disp, "u_dram"), s->dram ? 1.f : 0.f);
    glUniform1ui(U(s->p_disp, "u_salt"), s->salt);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    if (tn - s->diag_last > 30.0) {
        double mean = 0;
        for (int k2 = 0; k2 < s->mw * s->mh; k2++) mean += s->mip[k2];
        if (s->mw * s->mh > 0) mean /= s->mw * s->mh;
        fprintf(stderr, "[diag] ephemeris t=%.0f mean_density=%.3f born=%lu\n", s->t, mean, s->born_total);
        s->diag_last = tn;
    }
}

/* The ephemeris proper: numbers describing a run that no longer exists.
 * Plain data only; it carries no seed, no host detail and no path. */
static void emit_record(const State *s) {
    fprintf(stderr, "[ephemeris] run_seconds=%.1f composition=%s masses=%d slots=%d workers_born=%lu oxide_workers=%lu mean_lifetime_s=%.2f\n",
            s->t, s->dram ? "dramatic" : s->loose ? "drawing" : "masses", s->nmass, s->nw, s->born_total, s->oxide_total,
            s->born_total > (unsigned long)s->nw ? s->life_total / (double)(s->born_total - (unsigned long)s->nw) : 0.0);
    for (int k = 0; k < s->nmass; k++)
        fprintf(stderr, "[ephemeris] mass=%d x=%.3f y=%.3f r=%.3f\n", k, s->mass[k][0], s->mass[k][1], s->mass[k][2]);
    for (int k = 0; k < s->n_ev_lift; k++)
        fprintf(stderr, "[ephemeris] lift t=%.1f x=%.3f y=%.3f r=%.3f\n", s->ev_lift[k][0], s->ev_lift[k][1],
                s->ev_lift[k][2], s->ev_lift[k][3]);
    for (int k = 0; k < s->n_ev_drought; k++)
        fprintf(stderr, "[ephemeris] drought t0=%.1f t1=%.1f\n", s->ev_drought[k][0],
                fminf(s->ev_drought[k][1], (float)s->t));
    fprintf(stderr, "[ephemeris] columns: slot occupant born_s lifetime_s birth_x birth_y end_x end_y seen_at_birth pigment path\n");
    int start = s->nrows < REC_ROWS ? 0 : s->rowpos;
    for (int n = 0; n < s->nrows; n++) {
        const Row *r = &s->rows[(start + n) % REC_ROWS];
        fprintf(stderr, "[ephemeris] %d %u %.2f %.2f %.3f %.3f %.3f %.3f %.3f %d %.3f\n", r->slot, r->gen, r->born, r->life,
                r->bx, r->by, r->ex, r->ey, r->inh, r->kind, r->path);
    }
    for (int i = 0; i < s->nw; i++) {
        const Worker *w = &s->w[i];
        fprintf(stderr, "[ephemeris] %d %u %.2f %.2f %.3f %.3f %.3f %.3f %.3f %d %.3f unfinished\n", i, w->gen, w->born_t, w->age,
                w->bx, w->by, w->x, w->y, w->inh_birth, w->kind, w->path);
    }
}

static void free_ephemeris(ModeInfo *m) {
    State *s = m->data;
    if (!s) return;
    if (s->record) emit_record(s);
    if (s->fence) glDeleteSync(s->fence);
    if (s->pbo) glDeleteBuffers(1, &s->pbo);
    if (s->vbo) glDeleteBuffers(1, &s->vbo);
    if (s->surf) glDeleteTextures(1, &s->surf);
    if (s->verdict) glDeleteTextures(1, &s->verdict);
    if (s->fbo_surf) glDeleteFramebuffers(1, &s->fbo_surf);
    if (s->fbo_rb) glDeleteFramebuffers(1, &s->fbo_rb);
    if (s->fbo_eval) glDeleteFramebuffers(1, &s->fbo_eval);
    GLuint p[] = {s->p_init, s->p_eval, s->p_dep, s->p_decay, s->p_disp, s->p_lift};
    for (unsigned i = 0; i < sizeof p / sizeof p[0]; i++)
        if (p[i]) glDeleteProgram(p[i]);
    free(s);
    m->data = NULL;
}

/* Size changes are picked up in draw_ephemeris from the frame size the
 * harness publishes, which is the render size, not the output size. */
static void reshape_ephemeris(ModeInfo *m, int w, int h) { (void)m; (void)w; (void)h; }
static Bool ephemeris_handle_event(ModeInfo *m, XEvent *e) { (void)m; (void)e; return False; }
static void release_ephemeris(ModeInfo *m) { (void)m; }
static ModeSpecOpt ephemeris_opts = {0, NULL, 0, NULL, NULL};
struct xscreensaver_function_table ephemeris_xscreensaver_function_table = {
    .name = "ephemeris", .class_ = "Ephemeris", .init_cb = init_ephemeris, .draw_cb = draw_ephemeris,
    .reshape_cb = reshape_ephemeris, .event_cb = ephemeris_handle_event, .free_cb = free_ephemeris,
    .release_cb = release_ephemeris, .opts = &ephemeris_opts, .defaults_str = DEFAULTS};
