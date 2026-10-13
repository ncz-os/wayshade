/* cs_gravitywell.c - shader-engine port of xscreensaver "gravitywell".
 *
 * gravitywell, Copyright (c) 2019 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Kept from the original: the stars (mass, radius, drift, respawn behind the
 * grid), the grid of lines whose height is the summed inverse-square pull of
 * the stars (flat "surface gravity" floor inside a star's radius, no pull
 * beyond the radius where it falls below MASS_EPSILON), the green-to-red
 * color ramp by depth, exponential-squared fog to black, the view (40 degree
 * perspective, camera 30 units back, grid rotated 90 degrees about X and
 * offset so the wells drop away from the viewer) and the foot circles at
 * the bottom of every well.
 *
 * Changed: the original evaluated the heights on the CPU and re-sent tens of
 * thousands of client-array vertices every frame.  Here the star table is a
 * uniform array and the vertex shader evaluates the height field itself, so
 * per frame the CPU uploads a few hundred bytes.  Lines are triangle strips
 * expanded in the shader with an anti-aliased edge (GLES has no wide or
 * smooth lines).  enhanced style adds additive neon lines with depth cue,
 * glow, a slow camera sway and palette cross-fades.
 */
#include "cs_common.h"
#include "cs_post.h"

#define GRID_SEG 16
#define MASS_EPSILON 0.03f
#define SLOPE_EPSILON 0.06f
#define MAX_MASS_COLOR 120.0f
#define SPEED_BASE 2.5f
#define MAXSTARS 32
#define YMAX 390.0f
#define STEP_DT (1.0 / 33.3)     /* original delay 30000 us */

typedef struct { float mass, ro2, rm2, ri2, ro, radius, x, y, dx, dy, sg, depth; } star;

typedef struct { float h1, s1, v1, h2, s2, v2; } pal;
static const pal PALS[] = {
    {120, 1.0f, 1.0f, 0, 1.0f, 1.0f},      /* classic: gridColor #00FF00 -> gridColor2 #FF0000 */
    {165, 0.85f, 1.0f, 290, 0.9f, 1.0f},   /* aurora */
    {45, 1.0f, 1.0f, 5, 1.0f, 1.0f},       /* amber */
    {190, 0.55f, 1.0f, 235, 0.95f, 1.0f},  /* ice */
    {58, 1.0f, 1.0f, 340, 1.0f, 1.0f},     /* ember */
};
#define NPAL 5
static const char *PALNAMES = "classic,aurora,amber,ice,ember,cycle";

typedef struct {
    int style; float speed, resolution, grid_size; int nstars;
    cs_rng rng; cs_clock clk;
    star st[MAXSTARS];
    int grid_w, grid_h;
    int w, h;
    cs_post post; int post_ok;
    GLuint prog, vao;
    GLint u_mvp, u_mv, u_res, u_mode, u_hw, u_n, u_s0, u_s1, u_pal, u_pal2, u_palmix, u_enh, u_step, u_lim, u_inst0, u_gm, u_bright;
    double t0; int palmode; float bloom; int aa;
    int rows_a, rows_b, gridmod, nsamp;
    float sway_phase;
} gstate;
static gstate *G;

static const ncz_opt_def GOPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"count", NCZ_OPT_INT, "15", 2, 30, NULL, NULL, NULL, "Stars", "Number of massive bodies (xscreensaver -count).", "Density"},
    {"resolution", NCZ_OPT_FLOAT, "1", 0.5, 2, NULL, NULL, NULL, "Resolution",
     "Size of the grid; larger means a bigger field with more room between the wells (xscreensaver -resolution).", "Density"},
    {"grid-size", NCZ_OPT_FLOAT, "1", 0.5, 4, NULL, NULL, NULL, "Grid spacing",
     "Spacing between grid lines; larger means fewer lines (xscreensaver -grid-size).", "Density"},
    {"palette", NCZ_OPT_ENUM, "classic", 0, 0, "classic,aurora,amber,ice,ember,cycle", NULL, NULL, "Palette",
     "Line colors by depth. classic is the original green to red. cycle cross-fades between the palettes every 45 seconds.", "Look"},
    {"fog", NCZ_OPT_FLOAT, "-1", -1, 4, NULL, NULL, NULL, "Fog",
     "Distance fog density as a multiple of the original (xscreensaver uses 1). -1 = automatic: none for classic (the look this catalog has always shown), half for enhanced.", "Look"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof GOPTS / sizeof GOPTS[0]; *prefix = "NCZ_GRAVITYWELL_"; *group = "gravitywell"; return GOPTS;
}

static void new_star(gstate *s, star *t, int first) {
    int w = s->grid_w * GRID_SEG;
    t->radius = 2 * (2 + cs_rand(&s->rng) * 3 + cs_rand(&s->rng) * 3 + cs_rand(&s->rng) * 3);
    t->mass = t->radius * 150 * (2 + cs_rand(&s->rng) * 3 + cs_rand(&s->rng) * 3 + cs_rand(&s->rng) * 3);
    t->ro2 = t->mass / MASS_EPSILON;
    t->ro = sqrtf(t->ro2);
    t->rm2 = powf(t->mass * (2.0f / SLOPE_EPSILON), 2.0f / 3.0f);
    t->ri2 = t->radius * t->radius;
    if (t->rm2 < t->ri2) t->rm2 = t->ri2;
    if (t->ro2 < t->rm2) t->ro2 = t->rm2;
    t->x = w * (first ? 0.5f : (0.35f + cs_rand(&s->rng) * 0.3f));
    t->dx = ((cs_rand(&s->rng) - 0.5f) * 0.1f) / s->resolution;
    t->dy = (0.1f + cs_rand(&s->rng) * 0.6f) / s->resolution;
    t->sg = t->mass / t->ri2;
    t->depth = t->sg;
}

static void step_stars(gstate *s) {
    int w = s->grid_w * GRID_SEG, h = s->grid_h * GRID_SEG;
    float off = SPEED_BASE * s->resolution;
    for (int i = 0; i < s->nstars; i++) {
        star *t = &s->st[i];
        t->x += t->dx * off; t->y += t->dy * off;
        if (t->x < -t->ro || t->y < -t->ro || t->x >= w + t->ro || t->y >= h + t->ro) {
            new_star(s, t, 0);
            t->y = -t->ro;
        }
    }
}

/* ------------------------------------------------------------------ */
static const char *VS = CS_GLSL(
    uniform mat4 uMVP; uniform mat4 uMV; uniform vec2 uRes;
    uniform int uMode;            /* 0 grid lines along x, 1 grid lines along y, 2 foot circles */
    uniform float uHalfW; uniform float uFogK; uniform int uN; uniform float uStep; uniform float uLimY; uniform float uLimX; uniform float uGrid;
    uniform vec4 uS0[32];         /* x, y, mass, surface gravity */
    uniform vec4 uS1[32];         /* ri2, ro2, radius, depth */
    uniform vec3 uPalA; uniform vec3 uPalB; uniform vec3 uPalA2; uniform vec3 uPalB2; uniform float uPalMix;
    uniform float uEnh; uniform float uBright;
    out float vDist; out vec3 vCol; out float vFog;
    float height(vec2 p) {
        float z = 0.0;
        for (int i = 0; i < uN; i++) {
            vec2 d = p - uS0[i].xy; float d2 = dot(d, d);
            if (d2 < uS1[i].y) z += (d2 < uS1[i].x) ? uS0[i].w : uS0[i].z / d2;
        }
        return z;
    }
    vec3 hsv(vec3 c) { vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0); return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y); }
    vec3 ramp(float z) {
        float t = clamp(sin(clamp(z / 120.0, 0.0, 1.0) * 1.5707963), 0.0, 1.0);
        vec3 a = mix(uPalA, uPalA2, uPalMix), b = mix(uPalB, uPalB2, uPalMix);
        vec3 hsvv = mix(a, b, t);
        return hsv(vec3(hsvv.x / 360.0, hsvv.y, hsvv.z));
    }
    vec3 pos(int inst, int j) {
        if (uMode == 2) {
            vec4 a = uS0[inst], b = uS1[inst];
            float th = float(j) * 0.19634954;                 /* pi/16 */
            return vec3(a.x + b.z * cos(th), a.y + b.z * sin(th), b.w);
        }
        float u = min(float(j) * uStep, uMode == 1 ? uLimY : uLimX);
        float v = float(inst) * uGrid;
        vec2 p = uMode == 0 ? vec2(u, v) : vec2(v, u);
        return vec3(p, height(p));
    }
    vec4 clipOf(vec3 m) { return uMVP * vec4(m, 1.0); }
    vec2 scr(vec4 c) { return c.xy / c.w * uRes * 0.5; }
    void main() {
        int j = gl_VertexID >> 1; float side = ((gl_VertexID & 1) == 0) ? -1.0 : 1.0;
        int inst = gl_InstanceID;
        int jm = uMode == 2 ? j - 1 : max(j - 1, 0);
        vec3 m0 = pos(inst, jm), m1 = pos(inst, j), m2 = pos(inst, j + 1);
        vec4 c1 = clipOf(m1);
        vec2 s0 = scr(clipOf(m0)), s1 = scr(c1), s2 = scr(clipOf(m2));
        vec2 dir = s2 - s0; float l = length(dir);
        dir = l > 1e-4 ? dir / l : vec2(1.0, 0.0);
        vec2 n = vec2(-dir.y, dir.x);
        float hw = uHalfW + 0.9;
        vec2 sp = s1 + n * side * hw;
        gl_Position = vec4(sp / (uRes * 0.5) * c1.w, c1.z, c1.w);
        vDist = side * hw;
        float eyeD = abs((uMV * vec4(m1, 1.0)).z);
        vFog = exp(-pow(0.005 * uFogK * eyeD, 2.0));
        vec3 col = ramp(m1.z);
        float hn = clamp(m1.z / 120.0, 0.0, 1.0);
        if (uEnh > 0.5) col *= (0.55 + 1.1 * hn) * uBright;
        vCol = col;
    });
static const char *FS = CS_GLSL(
    in float vDist; in vec3 vCol; in float vFog; out vec4 o;
    uniform float uHalfW; uniform float uEnh; uniform float uAA;
    void main() {
        float a = uAA > 0.5 ? clamp(uHalfW + 0.5 - abs(vDist), 0.0, 1.0) : (abs(vDist) <= uHalfW + 0.5 ? 1.0 : 0.0);
        if (a <= 0.0) discard;
        o = vec4(vCol * vFog, a * (uEnh > 0.5 ? 0.9 : 1.0));
    });

static void init_gw(ModeInfo *mi) {
    gstate *s = calloc(1, sizeof *s); G = s;
    s->style = cs_style();
    s->speed = (float)cs_opt_f("speed", 1);
    s->resolution = (float)cs_opt_f("resolution", 1);
    s->grid_size = (float)cs_opt_f("grid-size", 1);
    s->nstars = (int)cs_opt_i("count", 15); if (s->nstars > MAXSTARS) s->nstars = MAXSTARS;
    s->bloom = (float)cs_opt_f("bloom", 1); s->aa = cs_opt_b("antialias", 1);
    const char *pn = cs_opt_s("palette", "classic");
    s->palmode = 0;
    { char list[128]; snprintf(list, sizeof list, "%s", PALNAMES); int k = 0;
      for (char *t = strtok(list, ","); t; t = strtok(NULL, ","), k++) if (!strcmp(t, pn)) s->palmode = k; }
    if (s->style == 0) s->palmode = 0;             /* classic style keeps the original colors */
    uint32_t seed = cs_seed_from_options(); cs_rng_seed(&s->rng, seed);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    s->grid_w = (int)((512 * s->resolution) / GRID_SEG); if (s->grid_w < 2) s->grid_w = 2;
    s->grid_h = s->grid_w;
    s->gridmod = (int)(s->grid_size * 7); if (s->gridmod < 1) s->gridmod = 1;
    s->rows_a = (int)(YMAX / s->gridmod) + 1;
    s->rows_b = ((s->grid_w - 1) * GRID_SEG) / s->gridmod + 1;
    s->nsamp = (s->grid_w - 1) * GRID_SEG + 1;
    for (int i = 0; i < s->nstars; i++) {
        star *t = &s->st[i];
        new_star(s, t, i == 0);
        t->y = cs_rand(&s->rng) * (t->ro * 2 + s->grid_h * GRID_SEG) - t->ro;
    }
    s->prog = cs_program(VS, FS, "gravitywell");
    if (!s->prog) return;
    glGenVertexArrays(1, &s->vao);
    if (s->style) s->post_ok = cs_post_init(&s->post, s->w, s->h, 0);
    fprintf(stderr, "[diag] gravitywell: style=%s seed=%u stars=%d lines=%d+%d\n", s->style ? "enhanced" : "classic", seed, s->nstars, s->rows_a, s->rows_b);
}

static void reshape_gw(ModeInfo *mi, int w, int h) {
    if (!G) return;
    G->w = w; G->h = h; glViewport(0, 0, w, h);
    if (G->post_ok) cs_post_resize(&G->post, w, h);
}

static void pal_vec(int k, float *a, float *b) {
    const pal *p = &PALS[k];
    a[0] = p->h1; a[1] = p->s1; a[2] = p->v1; b[0] = p->h2; b[1] = p->s2; b[2] = p->v2;
}

static void draw_gw(ModeInfo *mi) {
    gstate *s = G; if (!s || !s->prog) return;
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, 6);
    for (int i = 0; i < steps; i++) step_stars(s);
    double t = ncz_now(); if (!s->t0) s->t0 = t;
    float tt = (float)(t - s->t0);

    /* depth of each foot circle: own surface gravity plus the pull of the others */
    float S0[MAXSTARS * 4], S1[MAXSTARS * 4];
    for (int i = 0; i < s->nstars; i++) {
        star *a = &s->st[i]; a->depth = a->sg;
        for (int j = 0; j < s->nstars; j++) {
            if (i == j) continue;
            float dx = s->st[j].x - a->x, dy = s->st[j].y - a->y;
            a->depth += s->st[j].mass / (dx * dx + dy * dy + 1e-3f);   /* coincident stars must not give inf */
        }
        S0[i * 4] = a->x; S0[i * 4 + 1] = a->y; S0[i * 4 + 2] = a->mass; S0[i * 4 + 3] = a->sg;
        S1[i * 4] = a->ri2; S1[i * 4 + 1] = a->ro2; S1[i * 4 + 2] = a->radius; S1[i * 4 + 3] = a->depth;
    }
    /* view: perspective(40) * lookAt(0,0,30) * Rx(90) * T(-gw*8, -gh*12, 3) */
    float aspect = (float)s->w / (float)s->h;
    cs_mat4 P = cs_perspective(40, aspect, 10, 1000);
    cs_mat4 V = cs_translate(0, 0, -30);
    cs_mat4 M = cs_mul(cs_rotate(90, 1, 0, 0), cs_translate(-s->grid_w * (GRID_SEG / 2.0f), -s->grid_h * (GRID_SEG * 0.75f), 3));
    if (s->style) {   /* slow sway of the whole field, about a degree */
        cs_mat4 sw = cs_mul(cs_rotate(1.4f * sinf(tt * 0.11f), 1, 0, 0), cs_rotate(1.1f * sinf(tt * 0.07f + 1.0f), 0, 1, 0));
        V = cs_mul(V, sw);
    }
    cs_mat4 MV = cs_mul(V, M), MVP = cs_mul(P, MV);

    float pa[3], pb[3], pa2[3], pb2[3], pmix = 0;
    if (s->palmode < NPAL) { pal_vec(s->palmode, pa, pb); memcpy(pa2, pa, sizeof pa); memcpy(pb2, pb, sizeof pb); }
    else {              /* cycle: 45 s per palette, 8 s cross-fade */
        float ph = tt / 45.0f; int k = (int)ph % NPAL, k2 = (k + 1) % NPAL; float f = ph - floorf(ph);
        pmix = f < 0.82f ? 0 : (f - 0.82f) / 0.18f; pmix = pmix * pmix * (3 - 2 * pmix);
        pal_vec(k, pa, pb); pal_vec(k2, pa2, pb2);
    }

    int enh = s->style && s->post_ok;
    if (enh) cs_post_begin(&s->post); else glViewport(0, 0, s->w, s->h);
    if (s->style) glClearColor(0.004f, 0.006f, 0.014f, 1); else glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    if (s->style) glBlendFunc(GL_SRC_ALPHA, GL_ONE); else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s->prog);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uMVP"), 1, GL_FALSE, MVP.m);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uMV"), 1, GL_FALSE, MV.m);
    glUniform2f(glGetUniformLocation(s->prog, "uRes"), (float)s->w, (float)s->h);
    glUniform1i(glGetUniformLocation(s->prog, "uN"), s->nstars);
    glUniform1f(glGetUniformLocation(s->prog, "uStep"), 1.0f);
    glUniform1f(glGetUniformLocation(s->prog, "uLimY"), YMAX);
    glUniform1f(glGetUniformLocation(s->prog, "uLimX"), (float)(s->nsamp - 1));
    glUniform1f(glGetUniformLocation(s->prog, "uGrid"), (float)s->gridmod);
    glUniform4fv(glGetUniformLocation(s->prog, "uS0"), s->nstars, S0);
    glUniform4fv(glGetUniformLocation(s->prog, "uS1"), s->nstars, S1);
    glUniform3fv(glGetUniformLocation(s->prog, "uPalA"), 1, pa);
    glUniform3fv(glGetUniformLocation(s->prog, "uPalB"), 1, pb);
    glUniform3fv(glGetUniformLocation(s->prog, "uPalA2"), 1, pa2);
    glUniform3fv(glGetUniformLocation(s->prog, "uPalB2"), 1, pb2);
    glUniform1f(glGetUniformLocation(s->prog, "uPalMix"), pmix);
    glUniform1f(glGetUniformLocation(s->prog, "uEnh"), s->style ? 1.f : 0.f);
    { float fg = (float)cs_opt_f("fog", -1); if (fg < 0) fg = s->style ? 0.5f : 0.0f; glUniform1f(glGetUniformLocation(s->prog, "uFogK"), fg); }
    glUniform1f(glGetUniformLocation(s->prog, "uBright"), 1.45f);
    glUniform1f(glGetUniformLocation(s->prog, "uAA"), (s->style ? s->aa : 1) ? 1.f : 0.f);
    float lw = (s->style ? 1.5f : 2.0f) * (s->h / 1080.0f); if (lw < 1.2f) lw = 1.2f;
    glUniform1f(glGetUniformLocation(s->prog, "uHalfW"), lw * 0.5f);
    glBindVertexArray(s->vao);
    /* grid lines along x (rows at constant y), then along y */
    int nv_a = s->nsamp * 2;
    int nsamp_y = (int)YMAX + 1; int nv_b = nsamp_y * 2;
    glUniform1i(glGetUniformLocation(s->prog, "uMode"), 0);
    glUniform1f(glGetUniformLocation(s->prog, "uLimY"), YMAX);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, nv_a, s->rows_a);
    glUniform1i(glGetUniformLocation(s->prog, "uMode"), 1);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, nv_b, s->rows_b);
    glUniform1i(glGetUniformLocation(s->prog, "uMode"), 2);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 33 * 2, s->nstars);      /* 32 segments close the loop */
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    if (enh) cs_post_end(&s->post, s->bloom * 0.9f, 1.2f, 0.55f, s->aa, 0);
}

static void free_gw(ModeInfo *mi) {
    gstate *s = G; if (!s) return;
    if (s->prog) glDeleteProgram(s->prog);
    if (s->vao) glDeleteVertexArrays(1, &s->vao);
    if (s->post_ok) cs_post_free(&s->post);
    free(s); G = NULL;
}
static Bool gw_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_gw(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt gw_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table gravitywell_xscreensaver_function_table = {
    .name = "gravitywell", .class_ = "GravityWell",
    .init_cb = init_gw, .draw_cb = draw_gw, .reshape_cb = reshape_gw,
    .event_cb = gw_handle_event, .free_cb = free_gw, .release_cb = release_gw,
    .opts = &gw_opts, .defaults_str = "",
};
