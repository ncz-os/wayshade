/* cs_hexstrut.c - shader-engine port of xscreensaver "hexstrut".
 *
 * hexstrut, Copyright (c) 2016-2017 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Kept from the original: the triangular lattice whose triangles each carry
 * three struts (a "Y") that rotate by a third of a turn, the propagation of a
 * spin from a random triangle to its neighbors after a short delay, the
 * smooth random colormap advancing while a triangle rotates, the slowly
 * spinning and wandering view (30 degree perspective, 30 units back).
 *
 * Changed: the struts are instanced (one instance per triangle, 18 vertices
 * generated in the vertex shader) instead of glBegin(GL_QUADS) per frame.
 * enhanced style adds anti-aliased strut edges, a glow that brightens the
 * struts while they turn, and a soft vignette.
 */
#include "cs_common.h"
#include "cs_post.h"
#include "rotator.h"

#define STEP_DT (1.0 / 33.3)    /* original delay 30000 us */

typedef struct triangle {
    float cx, cy;
    struct triangle *nb[6];
    float rot; int delay, odelay, ccolor;
} triangle;

typedef struct {
    int style; float speed, thickness; int count, spin, wander;
    cs_rng rng; cs_clock clk;
    triangle *tri; int ntri;
    int ncolors; XColor *colors;
    rotator *rot;
    float rx, ry, rz, px, py, pz;
    int w, h;
    float size;
    cs_post post; int post_ok;
    GLuint prog, vao, vbo_static, vbo_dyn[3]; int ring;
    float bloom; int aa;
    cs_mat4 P;
} hstate;
static hstate *H;

static const ncz_opt_def HOPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"count", NCZ_OPT_INT, "20", 4, 60, NULL, NULL, NULL, "Density",
     "Hexagons across the field (xscreensaver -count).", "Density"},
    {"thickness", NCZ_OPT_FLOAT, "0.2", 0.05, 1.7, NULL, NULL, NULL, "Strut thickness",
     "Thickness of the struts (xscreensaver -thickness).", "Look"},
    {"spin", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Spin", "Slowly rotate the whole field (xscreensaver -spin).", "Motion"},
    {"wander", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Wander", "Slowly drift the whole field (xscreensaver -wander).", "Motion"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof HOPTS / sizeof HOPTS[0]; *prefix = "NCZ_HEXSTRUT_"; *group = "hexstrut"; return HOPTS;
}

static void link_nb(triangle *a, triangle *b) {
    if (a == b) return;
    for (int k = 0; k < 6; k++) {
        if (a->nb[k] == b || a->nb[k] == NULL) { a->nb[k] = b; return; }
    }
}

static void make_plane(hstate *s) {
    int n = s->count * 2;
    float size = 2.0f / n, w = size, h = size * sqrtf(3.0f) / 2;
    s->size = size;
    triangle **grid = calloc((size_t)n * n, sizeof *grid);
    s->tri = calloc((size_t)n * n, sizeof *s->tri);
    s->ntri = 0;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        triangle *t = &s->tri[s->ntri++];
        float p0x = (x - n / 2) * w, p0y = (y - n / 2) * h;
        if (y & 1) p0x += w / 2;
        t->cx = p0x; t->cy = p0y + 2 * h / 3;
        if (x > 0) { triangle *t2 = grid[y * n + x - 1]; link_nb(t, t2); link_nb(t2, t); }
        if (y > 0) {
            triangle *t2 = grid[(y - 1) * n + x]; link_nb(t, t2); link_nb(t2, t);
            if (x < n - 1) { t2 = grid[(y - 1) * n + x + 1]; link_nb(t, t2); link_nb(t2, t); }
        }
        grid[y * n + x] = t;
    }
    free(grid);
}

static void tick(hstate *s) {
    const float step = 0.05f;                       /* 0.01 + 0.04 * speed(1.0) */
    if (cs_rand_int(&s->rng, 80) == 0) {
        triangle *t = &s->tri[cs_rand_int(&s->rng, s->ntri)];
        if (t->rot == 0) { t->rot += step * cs_rand_sign(&s->rng); t->odelay = t->delay = 4; }
    }
    for (int i = 0; i < s->ntri; i++) {
        triangle *t = &s->tri[i];
        if (t->rot != 0) {
            t->rot += step * (t->rot > 0 ? 1 : -1);
            if (++t->ccolor >= s->ncolors) t->ccolor = 0;
            if (t->rot > 1 || t->rot < -1) t->rot = 0;
        }
        if (t->delay) {
            if (--t->delay == 0) {
                for (int k = 0; k < 6; k++) {
                    triangle *nb = t->nb[k];
                    if (nb && nb->rot == 0) {
                        nb->rot += step * (t->rot > 0 ? 1 : -1);
                        nb->delay = nb->odelay = t->odelay;
                    }
                }
            }
        }
    }
    double x, y, z;
    get_position(s->rot, &x, &y, &z, True); s->px = (float)x; s->py = (float)y; s->pz = (float)z;
    get_rotation(s->rot, &x, &y, &z, True); s->rz = (float)z;
}

static const char *VS = CS_GLSL(
    layout(location = 0) in vec2 aCent;
    layout(location = 1) in vec4 aDyn;               /* angle, r, g, b */
    uniform mat4 uMVP; uniform float uW; uniform float uHalfW; uniform float uEnh;
    out vec3 vCol; out vec2 vEdge;                   /* across -1..1, along 0..1 */
    void main() {
        int id = gl_VertexID; int k = id / 6; int m = id % 6;
        float h = uW * 0.8660254;
        vec2 off = k == 0 ? vec2(0.0, -2.0 * h / 3.0) : (k == 1 ? vec2(-uW * 0.5, h / 3.0) : vec2(uW * 0.5, h / 3.0));
        float cr = cos(aDyn.x), sr = sin(aDyn.x);
        vec2 dv = vec2(cr * off.x - sr * off.y, cr * off.y + sr * off.x);
        vec2 nrm = normalize(vec2(-dv.y, dv.x)) * uHalfW;
        int q = (m == 0) ? 0 : (m == 1) ? 1 : (m == 2) ? 2 : (m == 3) ? 2 : (m == 4) ? 1 : 3;
        float side = (q == 0 || q == 2) ? -1.0 : 1.0;
        float along = q < 2 ? 0.0 : 1.0;
        vec2 p = aCent + dv * along + nrm * side;
        gl_Position = uMVP * vec4(p, 0.0, 1.0);
        float bright = 1.0 + uEnh * 0.6 * step(0.0001, abs(aDyn.x));
        vCol = aDyn.yzw * bright; vEdge = vec2(side, along);
    });
static const char *FS = CS_GLSL(
    in vec3 vCol; in vec2 vEdge; out vec4 o; uniform float uAA;
    void main() {
        float a = 1.0;
        if (uAA > 0.5) {
            vec2 fw = fwidth(vEdge);
            a = clamp((1.0 - abs(vEdge.x)) / max(fw.x, 1e-5), 0.0, 1.0);
            a *= clamp(vEdge.y / max(fw.y, 1e-5) + 0.5, 0.0, 1.0) * clamp((1.0 - vEdge.y) / max(fw.y, 1e-5) + 0.5, 0.0, 1.0);
        }
        o = vec4(vCol * a, a);
    });

static void init_hexstrut(ModeInfo *mi) {
    hstate *s = calloc(1, sizeof *s); H = s;
    s->style = cs_style(); s->speed = (float)cs_opt_f("speed", 1);
    s->count = (int)cs_opt_i("count", 20); s->thickness = (float)cs_opt_f("thickness", 0.2);
    s->spin = cs_opt_b("spin", 1); s->wander = cs_opt_b("wander", 1);
    s->bloom = (float)cs_opt_f("bloom", 1); s->aa = cs_opt_b("antialias", 1);
    uint32_t seed = cs_seed_from_options(); cs_rng_seed(&s->rng, seed); srandom(seed);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    s->rot = make_rotator(s->spin ? 0.002 : 0, s->spin ? 0.002 : 0, s->spin ? 0.002 : 0, 1.0,
                          s->wander ? 0.003 : 0, False);
    s->ncolors = 64; s->colors = calloc(s->ncolors, sizeof(XColor));
    make_smooth_colormap(0, 0, 0, s->colors, &s->ncolors, False, False, False);
    make_plane(s);
    s->prog = cs_program(VS, FS, "hexstrut");
    if (!s->prog) return;
    glGenVertexArrays(1, &s->vao);
    glBindVertexArray(s->vao);
    float *cent = malloc(sizeof(float) * 2 * s->ntri);
    for (int i = 0; i < s->ntri; i++) { cent[i * 2] = s->tri[i].cx; cent[i * 2 + 1] = s->tri[i].cy; }
    glGenBuffers(1, &s->vbo_static); glBindBuffer(GL_ARRAY_BUFFER, s->vbo_static);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * s->ntri, cent, GL_STATIC_DRAW); free(cent);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0); glVertexAttribDivisor(0, 1);
    glGenBuffers(3, s->vbo_dyn);
    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo_dyn[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * s->ntri, NULL, GL_STREAM_DRAW);
    }
    glBindVertexArray(0);
    if (s->style) s->post_ok = cs_post_init(&s->post, s->w, s->h, 0);
    fprintf(stderr, "[diag] hexstrut: style=%s seed=%u triangles=%d\n", s->style ? "enhanced" : "classic", seed, s->ntri);
}

static void reshape_hexstrut(ModeInfo *mi, int w, int h) {
    if (!H) return;
    H->w = w; H->h = h; glViewport(0, 0, w, h);
    if (H->post_ok) cs_post_resize(&H->post, w, h);
}

static void draw_hexstrut(ModeInfo *mi) {
    hstate *s = H; if (!s || !s->prog) return;
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, 6);
    for (int i = 0; i < steps; i++) tick(s);

    float *dyn = malloc(sizeof(float) * 4 * s->ntri);
    for (int i = 0; i < s->ntri; i++) {
        triangle *t = &s->tri[i]; float *q = dyn + i * 4;
        q[0] = (float)(2 * M_PI / 3) * t->rot;
        int c = t->ccolor;
        q[1] = s->colors[c].red / 65535.0f * 0.75f + 0.25f;
        q[2] = s->colors[c].green / 65535.0f * 0.75f + 0.25f;
        q[3] = s->colors[c].blue / 65535.0f * 0.75f + 0.25f;
    }
    s->ring = (s->ring + 1) % 3;
    glBindVertexArray(s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_dyn[s->ring]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * 4 * s->ntri, dyn); free(dyn);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, (void *)0); glVertexAttribDivisor(1, 1);

    float aspect = (float)s->w / (float)s->h;
    float sc = (s->w < s->h) ? (float)s->w / (float)s->h : 1.0f;
    cs_mat4 P = cs_perspective(30, aspect, 1, 100);
    cs_mat4 M = cs_mul(cs_translate(0, 0, -30), cs_scale(sc, sc, sc));
    M = cs_mul(M, cs_translate((s->px - 0.5f) * 6, (s->py - 0.5f) * 6, (s->pz - 0.5f) * 12));
    M = cs_mul(M, cs_rotate(s->rz * 360.0f, 0, 0, 1));
    M = cs_mul(M, cs_scale(30, 30, 30));
    cs_mat4 MVP = cs_mul(P, M);

    int enh = s->style && s->post_ok;
    if (enh) cs_post_begin(&s->post); else glViewport(0, 0, s->w, s->h);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s->prog);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uMVP"), 1, GL_FALSE, MVP.m);
    glUniform1f(glGetUniformLocation(s->prog, "uW"), s->size);
    float halfw = (sqrtf(3.0f) / 3.0f) * s->thickness / 2 * s->size;
    glUniform1f(glGetUniformLocation(s->prog, "uHalfW"), halfw);
    glUniform1f(glGetUniformLocation(s->prog, "uEnh"), s->style ? 1.f : 0.f);
    glUniform1f(glGetUniformLocation(s->prog, "uAA"), (s->style && s->aa) ? 1.f : 0.f);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 18, s->ntri);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    if (enh) cs_post_end(&s->post, s->bloom * 0.8f, 1.0f, 0.5f, 0, 0);
}

static void free_hexstrut(ModeInfo *mi) {
    hstate *s = H; if (!s) return;
    if (s->prog) glDeleteProgram(s->prog);
    if (s->vao) glDeleteVertexArrays(1, &s->vao);
    glDeleteBuffers(1, &s->vbo_static); glDeleteBuffers(3, s->vbo_dyn);
    if (s->post_ok) cs_post_free(&s->post);
    if (s->rot) free_rotator(s->rot);
    free(s->colors); free(s->tri); free(s); H = NULL;
}
static Bool hexstrut_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_hexstrut(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt hexstrut_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table hexstrut_xscreensaver_function_table = {
    .name = "hexstrut", .class_ = "Hexstrut",
    .init_cb = init_hexstrut, .draw_cb = draw_hexstrut, .reshape_cb = reshape_hexstrut,
    .event_cb = hexstrut_handle_event, .free_cb = free_hexstrut, .release_cb = release_hexstrut,
    .opts = &hexstrut_opts, .defaults_str = "",
};
