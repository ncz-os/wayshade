/* cs_geodesic.c - shader-engine port of xscreensaver "geodesic".
 *
 * geodesic, Copyright (c) 2013-2014 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Kept from the original: the geodesic sphere built by recursively halving
 * the faces of an icosahedron, its frequency creeping up and down between 0
 * and count-1 with the new faces fading in and then morphing out onto the
 * sphere, the four modes (mesh: every face a triangular frame with a
 * triangular hole; solid; stellated; stellated2), the smooth random
 * colormap for the two frame colors, the spin and wander of the rotator, the
 * light from (1,1,1) with a sharp white specular.
 *
 * Changed: the GL1 version rebuilt up to 1280 faces (each 9 quads) with
 * glBegin every frame.  The faces of each frequency are generated once into
 * a static instance buffer, every face carrying its three corners both flat
 * and projected onto the sphere.  The vertex shader morphs between the two
 * with one uniform and builds the frame's nine quads itself, so a frame is
 * one or two instanced draw calls.  enhanced style adds rim light, depth cue,
 * softer specular, anti-aliasing and glow.
 */
#include "cs_common.h"
#include "cs_post.h"
#include "rotator.h"

#define STEP_DT (1.0 / 33.3)     /* original delay 30000 us */
#define MAXD 5

typedef struct { float x, y, z; } v3;
typedef struct { v3 a, b, c; } tri;

typedef struct {
    int style; float speed; int count, spin, wander, mode, random_p;   /* mode: 1 mesh 2 solid 3 stellated 4 stellated2 */
    float bloom; int aa;
    cs_rng rng; cs_clock clk;
    rotator *rot;
    float px, py, pz, rx, ry, rz;
    int ncolors; XColor *colors; int ccolor, ccolor2;
    float depth, delta, thickness;
    int w, h;
    cs_post post; int post_ok;
    GLuint prog, vao[2][MAXD + 1], vbo[2][MAXD + 1]; int ninst[2][MAXD + 1];
    float rot_state[3];
} gstate;
static gstate *G;

static const ncz_opt_def GOPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"mode", NCZ_OPT_ENUM, "mesh", 0, 0, "mesh,solid,stellated,stellated2,random", NULL, NULL, "Mode",
     "mesh: framed faces; solid; stellated: faces grow points; stellated2: faces dent inward; random: changes each cycle (xscreensaver -mode).", "Look"},
    {"count", NCZ_OPT_INT, "4", 1, 6, NULL, NULL, NULL, "Detail levels",
     "Number of frequencies to cycle through; the highest has 20 x 4^(count-1) faces (xscreensaver -count).", "Density"},
    {"spin", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Spin", "Rotate the sphere (xscreensaver -spin).", "Motion"},
    {"wander", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Wander", "Drift the sphere around the screen (xscreensaver -wander).", "Motion"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof GOPTS / sizeof GOPTS[0]; *prefix = "NCZ_GEODESIC_"; *group = "geodesic"; return GOPTS;
}

/* ---------- geometry generation ---------- */
static v3 vadd(v3 a, v3 b) { v3 r = {a.x + b.x, a.y + b.y, a.z + b.z}; return r; }
static v3 vscale(v3 a, float k) { v3 r = {a.x * k, a.y * k, a.z * k}; return r; }
static v3 vnorm(v3 a) { float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); return l > 1e-9f ? vscale(a, 1 / l) : a; }
static v3 ll(float lat, float lon) { v3 r = { cosf(lat) * cosf(lon), cosf(lat) * sinf(lon), sinf(lat) }; return r; }

static int g_cap; static tri *g_tris; static int g_n;
static void push_tri(tri **arr, int *n, int *cap, tri t) {
    if (*n >= *cap) { *cap = *cap ? *cap * 2 : 64; *arr = realloc(*arr, sizeof(tri) * *cap); }
    (*arr)[(*n)++] = t;
}
/* the 20 icosahedron faces, in the order and winding make_geodesic() uses */
static void base_faces(tri **arr, int *n, int *cap) {
    float th0 = atanf(0.5f), s = (float)M_PI / 5;
    for (int i = 0; i < 10; i++) {
        float th1 = s * i, th2 = s * (i + 1), th3 = s * (i + 2);
        float a1 = th0, a2 = th0, a3 = -th0, ac = (float)M_PI / 2;
        if (i & 1) {
            v3 v1 = ll(a1, th1), v2 = ll(a2, th3), v3_ = ll(a3, th2), vc = ll(ac, th2);
            tri t1 = {v1, v2, vc}, t2 = {v2, v1, v3_};
            push_tri(arr, n, cap, t1); push_tri(arr, n, cap, t2);
        } else {
            v3 v1 = ll(-a1, th1), v2 = ll(-a2, th3), v3_ = ll(-a3, th2), vc = ll(-ac, th2);
            tri t1 = {v2, v1, vc}, t2 = {v1, v2, v3_};
            push_tri(arr, n, cap, t1); push_tri(arr, n, cap, t2);
        }
    }
}
static void subdivide(tri *in, int nin, tri **out, int *nout, int *cap) {
    for (int i = 0; i < nin; i++) {
        v3 p1 = in[i].a, p2 = in[i].b, p3 = in[i].c;
        v3 p12 = vnorm(vadd(p1, p2)), p23 = vnorm(vadd(p2, p3)), p13 = vnorm(vadd(p1, p3));
        tri t[4] = {{p1, p12, p13}, {p12, p2, p23}, {p13, p23, p3}, {p12, p23, p13}};
        for (int k = 0; k < 4; k++) push_tri(out, nout, cap, t[k]);
    }
}
typedef struct { float f[3], s[3]; } cpair;
static cpair pr(v3 flat, v3 sph) { cpair c = {{flat.x, flat.y, flat.z}, {sph.x, sph.y, sph.z}}; return c; }
/* instance = three corners, each (flat, sphere) : 18 floats */
static void emit(float **buf, int *n, int *cap, cpair a, cpair b, cpair c) {
    if (*n >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, sizeof(float) * 18 * *cap); }
    float *o = *buf + 18 * (*n)++;
    memcpy(o, a.f, 12); memcpy(o + 3, a.s, 12); memcpy(o + 6, b.f, 12); memcpy(o + 9, b.s, 12);
    memcpy(o + 12, c.f, 12); memcpy(o + 15, c.s, 12);
}
static void build_levels(gstate *s) {
    tri *lv[MAXD + 1]; int nl[MAXD + 1], cp[MAXD + 1];
    for (int d = 0; d <= MAXD; d++) { lv[d] = NULL; nl[d] = 0; cp[d] = 0; }
    base_faces(&lv[0], &nl[0], &cp[0]);
    int top = s->count - 1; if (top > MAXD) top = MAXD;
    for (int d = 1; d <= top; d++) subdivide(lv[d - 1], nl[d - 1], &lv[d], &nl[d], &cp[d]);
    for (int D = 0; D <= top; D++) {
        for (int fam = 0; fam < 2; fam++) {
            float *buf = NULL; int n = 0, cap = 0;
            if (D == 0 || fam == 0) {                       /* mesh/solid family */
                if (D == 0) {
                    for (int i = 0; i < nl[0]; i++) emit(&buf, &n, &cap, pr(lv[0][i].a, lv[0][i].a), pr(lv[0][i].b, lv[0][i].b), pr(lv[0][i].c, lv[0][i].c));
                } else {
                    for (int i = 0; i < nl[D - 1]; i++) {
                        v3 p1 = lv[D-1][i].a, p2 = lv[D-1][i].b, p3 = lv[D-1][i].c;
                        v3 f12 = vscale(vadd(p1, p2), 0.5f), f23 = vscale(vadd(p2, p3), 0.5f), f13 = vscale(vadd(p1, p3), 0.5f);
                        cpair c1 = pr(p1, p1), c2 = pr(p2, p2), c3 = pr(p3, p3);
                        cpair c12 = pr(f12, vnorm(f12)), c23 = pr(f23, vnorm(f23)), c13 = pr(f13, vnorm(f13));
                        emit(&buf, &n, &cap, c1, c12, c13); emit(&buf, &n, &cap, c12, c2, c23);
                        emit(&buf, &n, &cap, c13, c23, c3); emit(&buf, &n, &cap, c12, c23, c13);
                    }
                }
            } else {                                        /* stellated family (D >= 1) */
                for (int i = 0; i < nl[D - 1]; i++) {
                    v3 p1 = lv[D-1][i].a, p2 = lv[D-1][i].b, p3 = lv[D-1][i].c;
                    v3 ct = vscale(vadd(vadd(p1, p2), p3), 1.0f / 3.0f);
                    cpair c1 = pr(p1, p1), c2 = pr(p2, p2), c3 = pr(p3, p3), cc = pr(ct, vnorm(ct));
                    emit(&buf, &n, &cap, c1, c2, cc); emit(&buf, &n, &cap, c2, c3, cc); emit(&buf, &n, &cap, c3, c1, cc);
                }
            }
            s->ninst[fam][D] = n;
            glGenVertexArrays(1, &s->vao[fam][D]); glGenBuffers(1, &s->vbo[fam][D]);
            glBindVertexArray(s->vao[fam][D]);
            glBindBuffer(GL_ARRAY_BUFFER, s->vbo[fam][D]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 18 * n, buf, GL_STATIC_DRAW);
            for (int k = 0; k < 6; k++) {
                glEnableVertexAttribArray(k);
                glVertexAttribPointer(k, 3, GL_FLOAT, GL_FALSE, 72, (void *)(sizeof(float) * 3 * k));
                glVertexAttribDivisor(k, 1);
            }
            free(buf);
        }
    }
    glBindVertexArray(0);
    for (int d = 0; d <= MAXD; d++) free(lv[d]);
    (void)g_cap; (void)g_tris; (void)g_n;
}

/* ---------- shaders ---------- */
static const char *VS = CS_GLSL(
    layout(location = 0) in vec3 aF1; layout(location = 1) in vec3 aS1;
    layout(location = 2) in vec3 aF2; layout(location = 3) in vec3 aS2;
    layout(location = 4) in vec3 aF3; layout(location = 5) in vec3 aS3;
    uniform mat4 uP; uniform mat4 uMV; uniform mat3 uN;
    uniform float uMorph; uniform float uThick; uniform float uSolid;
    out vec3 vN; out vec3 vEye; flat out int vInside;
    const int QT[36] = int[36](
        0, 3, 5, 2,   0, 1, 4, 3,   1, 2, 5, 4,      /* codes: 0-2 P, 3-5 B, 6-8 d*P, 9-11 d*B; quads 0..2 outside */
        8, 11, 9, 6,  9, 10, 7, 6,  10, 11, 8, 7,    /* quads 3..5 inside */
        3, 4, 10, 9,  4, 5, 11, 10, 5, 3, 9, 11);    /* quads 6..8 connecting edges */
    void main() {
        vec3 P[3];
        P[0] = mix(aF1, aS1, uMorph); P[1] = mix(aF2, aS2, uMorph); P[2] = mix(aF3, aS3, uMorph);
        const float D = 0.98;
        vec3 c = (P[0] + P[1] + P[2]) / 3.0;
        float r = uThick;
        vec3 B[3]; B[0] = P[0] + r * (c - P[0]); B[1] = P[1] + r * (c - P[1]); B[2] = P[2] + r * (c - P[2]);
        int id = gl_VertexID; vec3 p; vec3 n; int inside = 0;
        if (uSolid > 0.5) {
            int k = id % 3;
            p = P[k]; n = cross(P[1] - P[0], P[2] - P[0]);
        } else {
            int q = id / 6; int m = id % 6;
            int corner = (m == 0) ? 0 : (m == 1) ? 1 : (m == 2) ? 2 : (m == 3) ? 0 : (m == 4) ? 2 : 3;
            int code = QT[q * 4 + corner];
            if (code < 3) p = P[code]; else if (code < 6) p = B[code - 3]; else if (code < 9) p = D * P[code - 6]; else p = D * B[code - 9];
            if (q < 3) n = cross(P[1] - P[0], P[2] - P[0]);
            else if (q < 6) { n = cross(B[2] - P[2], B[0] - P[2]); inside = 1; }
            else if (q == 6) n = cross(B[1] - B[0], D * B[1] - B[0]);
            else if (q == 7) n = cross(B[2] - B[1], D * B[2] - B[1]);
            else n = cross(B[0] - B[2], D * B[0] - B[2]);
        }
        vec4 e = uMV * vec4(p, 1.0);
        gl_Position = uP * e;
        vN = uN * n; vEye = e.xyz; vInside = inside;
    });
static const char *FS = CS_GLSL(
    in vec3 vN; in vec3 vEye; flat in int vInside; out vec4 o;
    uniform vec4 uCol1; uniform vec4 uCol2; uniform float uEnh;
    void main() {
        vec3 N = normalize(vN);
        vec4 base = vInside == 1 ? uCol2 : uCol1;
        vec3 L = normalize(vec3(1.0, 1.0, 1.0));
        float nl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
        float sp = pow(max(dot(N, H), 0.0), 128.0);
        vec3 col = base.rgb * (0.2 + nl) + vec3(0.0, 1.0, 1.0) * sp;
        if (uEnh > 0.5) {
            vec3 V = normalize(-vEye);
            float rim = pow(1.0 - max(dot(N, V), 0.0), 2.5);
            float hemi = 0.5 + 0.5 * N.y;
            col = base.rgb * (0.16 + 0.85 * nl + 0.22 * hemi) + vec3(0.7, 0.95, 1.0) * sp * 0.9 + base.rgb * rim * 0.45;
            if (vInside == 1) col *= 0.75;
            float fog = exp(-pow(max(length(vEye) - 14.0, 0.0) / 42.0, 2.0));
            col *= mix(0.55, 1.0, fog);
        }
        o = vec4(col, base.a);
    });

static void reshape_geodesic(ModeInfo *mi, int w, int h) {
    gstate *s = G; if (!s) return;
    s->w = w; s->h = h; glViewport(0, 0, w, h);
    if (s->post_ok) cs_post_resize(&s->post, w, h);
}

static int mode_from(const char *m, int *random_p) {
    *random_p = 0;
    if (!strcmp(m, "solid")) return 2;
    if (!strcmp(m, "stellated")) return 3;
    if (!strcmp(m, "stellated2")) return 4;
    if (!strcmp(m, "random")) { *random_p = 1; return 1 + (int)(random() % 4); }
    return 1;
}

static void init_geodesic(ModeInfo *mi) {
    gstate *s = calloc(1, sizeof *s); G = s;
    s->style = cs_style(); s->speed = (float)cs_opt_f("speed", 1);
    s->count = (int)cs_opt_i("count", 4); s->spin = cs_opt_b("spin", 1); s->wander = cs_opt_b("wander", 1);
    s->bloom = (float)cs_opt_f("bloom", 1); s->aa = cs_opt_b("antialias", 1);
    uint32_t seed = cs_seed_from_options(); cs_rng_seed(&s->rng, seed); srandom(seed);
    s->mode = mode_from(cs_opt_s("mode", "mesh"), &s->random_p);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    double spin = 0.25, wander = 0.01;
    s->rot = make_rotator(s->spin ? spin : 0, s->spin ? spin : 0, s->spin ? spin : 0, 0.2, s->wander ? wander : 0, True);
    s->ncolors = 1024; s->colors = calloc(s->ncolors, sizeof(XColor));
    make_smooth_colormap(0, 0, 0, s->colors, &s->ncolors, False, False, False);
    s->depth = 1; s->delta = 0.003f; s->thickness = 0.1f;
    s->prog = cs_program(VS, FS, "geodesic");
    if (!s->prog) return;
    build_levels(s);
    if (s->style) s->post_ok = cs_post_init(&s->post, s->w, s->h, 1);
    fprintf(stderr, "[diag] geodesic: style=%s seed=%u mode=%d count=%d\n", s->style ? "enhanced" : "classic", seed, s->mode, s->count);
}

static void set_col(float *c, XColor *x, float a) { c[0] = x->red / 65536.0f; c[1] = x->green / 65536.0f; c[2] = x->blue / 65536.0f; c[3] = a; }

static void draw_set(gstate *s, int level, int stell, float morph, float alpha, cs_mat4 P, cs_mat4 MV, float *n3) {
    int fam = (stell && level >= 1) ? 1 : 0;
    float c1[4], c2[4];
    set_col(c1, &s->colors[s->ccolor], alpha); set_col(c2, &s->colors[s->ccolor2], alpha);
    glUniform4fv(glGetUniformLocation(s->prog, "uCol1"), 1, c1);
    glUniform4fv(glGetUniformLocation(s->prog, "uCol2"), 1, c2);
    glUniform1f(glGetUniformLocation(s->prog, "uMorph"), morph);
    glUniform1f(glGetUniformLocation(s->prog, "uSolid"), (s->mode == 1) ? 0.f : 1.f);
    glBindVertexArray(s->vao[fam][level]);
    glDrawArraysInstanced(GL_TRIANGLES, 0, s->mode == 1 ? 54 : 3, s->ninst[fam][level]);
}

static void step_state(gstate *s) {
    double x, y, z;
    get_position(s->rot, &x, &y, &z, True); s->px = (float)x; s->py = (float)y; s->pz = (float)z;
    get_rotation(s->rot, &x, &y, &z, True); s->rx = (float)x; s->ry = (float)y; s->rz = (float)z;
    s->ccolor = (s->ccolor + 1) % s->ncolors;
    s->ccolor2 = (s->ccolor + s->ncolors / 2) % s->ncolors;
    s->depth += s->delta;
    if (s->depth > s->count - 1) { s->depth = (float)(s->count - 1); s->delta = -fabsf(s->delta); }
    else if (s->depth < 0) {
        s->depth = 0; s->delta = fabsf(s->delta);
        if (s->random_p) s->mode = 1 + (int)(random() % 4);
    }
}

static void draw_geodesic(ModeInfo *mi) {
    gstate *s = G; if (!s || !s->prog) return;
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, 6);
    for (int i = 0; i < steps; i++) step_state(s);

    float aspect = (float)s->w / (float)s->h;
    float sc = (s->w < s->h) ? (float)s->w / (float)s->h : 1.0f;
    cs_mat4 P = cs_perspective(30, aspect, 1, 100);
    cs_mat4 M = cs_mul(cs_translate(0, 0, -30), cs_scale(sc, sc, sc));
    M = cs_mul(M, cs_translate((s->px - 0.5f) * 8, (s->py - 0.5f) * 8, (s->pz - 0.5f) * 15));
    M = cs_mul(M, cs_rotate(s->rx * 360.0f, 1, 0, 0));
    M = cs_mul(M, cs_rotate(s->ry * 360.0f, 0, 1, 0));
    M = cs_mul(M, cs_rotate(s->rz * 360.0f, 0, 0, 1));
    M = cs_mul(M, cs_scale(10, 10, 10));
    float n3[9]; cs_mat3_of(&M, n3); for (int i = 0; i < 9; i++) n3[i] /= 10.0f * sc;   /* rotation only */

    float r = s->depth - floorf(s->depth);
    float range = 0.15f, min1 = (0.5f - range) / 2, max1 = 0.5f - min1, min2 = 0.5f + min1, max2 = 0.5f + max1;
    float alpha, morph1, morph2; int d1, d2;
    int stell = (s->mode == 3 || s->mode == 4);
    if (r < min1) { d1 = d2 = (int)floorf(s->depth); morph1 = morph2 = 1; alpha = 1; }
    else if (r < max1 && (s->mode == 1 || stell)) {
        d1 = (int)floorf(s->depth); d2 = (int)ceilf(s->depth); morph1 = 1; morph2 = 0;
        alpha = (r - min1) / (max1 - min1);
        if (stell) { morph1 = 1 - alpha; morph1 = 2 * (morph1 - 0.5f); if (morph1 < 0) morph1 = 0; }
    }
    else if (r < min2) { d1 = d2 = (int)ceilf(s->depth); morph1 = morph2 = 0; alpha = 1; }
    else if (r < max2) { d1 = d2 = (int)ceilf(s->depth); morph1 = morph2 = (r - min2) / (max2 - min2); alpha = 1; }
    else { d1 = d2 = (int)ceilf(s->depth); morph1 = morph2 = 1; alpha = 1; }
    if (s->mode == 4) { morph1 = -morph1; morph2 = -morph2; }
    if (d1 > s->count - 1) d1 = s->count - 1;
    if (d2 > s->count - 1) d2 = s->count - 1;

    int enh = s->style && s->post_ok;
    if (enh) cs_post_begin(&s->post); else glViewport(0, 0, s->w, s->h);
    glClearColor(s->style ? 0.006f : 0, s->style ? 0.006f : 0, s->style ? 0.012f : 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s->prog);
    cs_mat4 MVm = M;
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uP"), 1, GL_FALSE, P.m);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uMV"), 1, GL_FALSE, MVm.m);
    glUniformMatrix3fv(glGetUniformLocation(s->prog, "uN"), 1, GL_FALSE, n3);
    glUniform1f(glGetUniformLocation(s->prog, "uThick"), s->thickness);
    glUniform1f(glGetUniformLocation(s->prog, "uEnh"), s->style ? 1.f : 0.f);
    if (d1 != d2) {
        if (alpha > 0.5f) { int t = d1; d1 = d2; d2 = t; float m = morph1; morph1 = morph2; morph2 = m; alpha = 1 - alpha; }
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(0.0f, 0.0f);
        draw_set(s, d1, stell, morph1, 1 - alpha, P, MVm, n3);
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.0f, 1.0f);
    }
    draw_set(s, d2, stell, morph2, alpha, P, MVm, n3);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    if (enh) cs_post_end(&s->post, s->bloom * 0.3f, 1.0f, 0.4f, s->aa, 0);
}

static void free_geodesic(ModeInfo *mi) {
    gstate *s = G; if (!s) return;
    if (s->prog) glDeleteProgram(s->prog);
    for (int f = 0; f < 2; f++) for (int d = 0; d <= MAXD; d++) {
        if (s->vao[f][d]) { glDeleteVertexArrays(1, &s->vao[f][d]); glDeleteBuffers(1, &s->vbo[f][d]); }
    }
    if (s->post_ok) cs_post_free(&s->post);
    if (s->rot) free_rotator(s->rot);
    free(s->colors); free(s); G = NULL;
}
static Bool geodesic_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_geodesic(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt geodesic_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table geodesic_xscreensaver_function_table = {
    .name = "geodesic", .class_ = "Geodesic",
    .init_cb = init_geodesic, .draw_cb = draw_geodesic, .reshape_cb = reshape_geodesic,
    .event_cb = geodesic_handle_event, .free_cb = free_geodesic, .release_cb = release_geodesic,
    .opts = &geodesic_opts, .defaults_str = "",
};
