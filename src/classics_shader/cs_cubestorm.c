/* cs_cubestorm.c - shader-engine port of xscreensaver "cubestorm".
 *
 * cubestorm, Copyright (c) 2003-2018 Jamie Zawinski <jwz@jwz.org>
 * (see the xscreensaver source for the full list of contributors)
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Kept from the original: a string of hollow, beveled cube frames trailing
 * behind wandering, tumbling leaders (count of them, each following the
 * first with its own spin), a history of up to `length` frames that is
 * re-drawn every frame, colors walking through a smooth random colormap,
 * the periodic short "no trails" resets with a fresh colormap, the light from
 * (1,1,1) with a sharp white specular, and the 30 degree view from 45 units.
 *
 * Changed: the GL1 version issued one display-list call per history cube
 * (about 800 per frame) through immediate mode.  The cube frame is now one
 * static vertex buffer drawn with a single instanced call; the per-cube pose
 * and color live in a small streamed instance buffer.  enhanced style adds
 * per-pixel lighting with rim light and depth cue, fading of the oldest
 * trail cubes, anti-aliasing and a soft glow.
 */
#include "cs_common.h"
#include "cs_post.h"
#include "rotator.h"

#define STEP_DT (1.0 / 33.3)       /* original delay 30000 us */
#define MAXHIST 1400

typedef struct { float px, py, pz, rx, ry, rz; int ccolor; } histcube;
typedef struct { rotator *rot; int ccolor; } subcube;

typedef struct {
    int style; float speed, thickness; int count, spin, wander, max_length;
    float bloom; int aa;
    cs_rng rng; cs_clock clk;
    int ncolors; XColor *colors;
    subcube sub[16];
    histcube hist[MAXHIST + 32]; int nhist;
    int clear_p;
    int w, h;
    cs_post post; int post_ok;
    GLuint prog, vao, vbo_mesh, vbo_inst[3]; int ring, nverts;
} cstate;
static cstate *C;

static const ncz_opt_def COPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"count", NCZ_OPT_INT, "4", 1, 12, NULL, NULL, NULL, "Cubes",
     "Number of cubes leading the trail (xscreensaver -count).", "Density"},
    {"length", NCZ_OPT_INT, "200", 20, 1000, NULL, NULL, NULL, "Trail length",
     "How many past cube positions are kept (xscreensaver -length).", "Density"},
    {"thickness", NCZ_OPT_FLOAT, "0.06", 0.01, 0.5, NULL, NULL, NULL, "Frame thickness",
     "Thickness of the cube frame edges (xscreensaver -thickness).", "Look"},
    {"spin", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Spin", "Tumble the cubes (xscreensaver -spin).", "Motion"},
    {"wander", NCZ_OPT_BOOL, "true", 0, 0, NULL, NULL, NULL, "Wander", "Let the trail wander around the screen (xscreensaver -wander).", "Motion"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof COPTS / sizeof COPTS[0]; *prefix = "NCZ_CUBESTORM_"; *group = "cubestorm"; return COPTS;
}

/* ---- geometry: replicates draw_faces()/draw_face() of the original ---- */
static float *g_mesh; static int g_nv;
static void emit_v(cs_mat4 m, const float p[3], const float n[3]) {
    float *o = g_mesh + g_nv * 6;
    o[0] = m.m[0] * p[0] + m.m[4] * p[1] + m.m[8] * p[2] + m.m[12];
    o[1] = m.m[1] * p[0] + m.m[5] * p[1] + m.m[9] * p[2] + m.m[13];
    o[2] = m.m[2] * p[0] + m.m[6] * p[1] + m.m[10] * p[2] + m.m[14];
    o[3] = m.m[0] * n[0] + m.m[4] * n[1] + m.m[8] * n[2];
    o[4] = m.m[1] * n[0] + m.m[5] * n[1] + m.m[9] * n[2];
    o[5] = m.m[2] * n[0] + m.m[6] * n[1] + m.m[10] * n[2];
    g_nv++;
}
static void emit_quad(cs_mat4 m, float q[4][3], const float n[3]) {
    int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) emit_v(m, q[idx[i]], n);
}
static void build_mesh(cstate *s) {
    float t = s->thickness / 2, a = -0.5f, b = 0.5f;
    if (t <= 0) t = 0.001f; else if (t > 0.5f) t = 0.5f;
    g_mesh = malloc(sizeof(float) * 6 * 300); g_nv = 0;
    cs_mat4 faces = cs_identity();
    for (int f = 0; f < 6; f++) {
        if (f >= 1 && f <= 3) faces = cs_mul(faces, cs_rotate(90, 0, 1, 0));
        else if (f == 4) faces = cs_mul(faces, cs_rotate(90, 1, 0, 0));
        else if (f == 5) faces = cs_mul(faces, cs_rotate(180, 1, 0, 0));
        cs_mat4 fm = faces;
        for (int i = 0; i < 4; i++) {
            float q1[4][3] = {{a, a, a}, {b, a, a}, {b - t, a + t, a}, {a + t, a + t, a}};
            float n1[3] = {0, 0, -1};
            emit_quad(fm, q1, n1);
            float q2[4][3] = {{b - t, a + t, a}, {b - t, a + t, a + t}, {a + t, a + t, a + t}, {a + t, a + t, a}};
            float n2[3] = {0, 1, 0};
            emit_quad(fm, q2, n2);
            fm = cs_mul(fm, cs_rotate(90, 0, 0, 1));
        }
    }
    s->nverts = g_nv;
}

static const char *VS = CS_GLSL(
    layout(location = 0) in vec3 aPos; layout(location = 1) in vec3 aNrm;
    layout(location = 2) in vec4 aA;      /* px py pz age */
    layout(location = 3) in vec4 aB;      /* rx ry rz - (turns) */
    layout(location = 4) in vec4 aCol;
    uniform mat4 uP; uniform mat4 uV;
    out vec3 vN; out vec3 vCol; out vec3 vEye; out float vAge;
    mat3 rotX(float a) { float c = cos(a), s = sin(a); return mat3(1.0, 0.0, 0.0, 0.0, c, s, 0.0, -s, c); }
    mat3 rotY(float a) { float c = cos(a), s = sin(a); return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c); }
    mat3 rotZ(float a) { float c = cos(a), s = sin(a); return mat3(c, s, 0.0, -s, c, 0.0, 0.0, 0.0, 1.0); }
    void main() {
        mat3 R = rotX(aB.x * 6.2831853) * rotY(aB.y * 6.2831853) * rotZ(aB.z * 6.2831853);
        vec3 wp = 1.1 * (vec3((aA.x - 0.5) * 15.0, (aA.y - 0.5) * 15.0, (aA.z - 0.5) * 30.0) + 4.0 * (R * aPos));
        vec4 ev = uV * vec4(wp, 1.0);
        gl_Position = uP * ev;
        vN = mat3(uV) * (R * aNrm); vCol = aCol.rgb; vEye = ev.xyz; vAge = aA.w;
    });
static const char *FS = CS_GLSL(
    in vec3 vN; in vec3 vCol; in vec3 vEye; in float vAge; out vec4 o;
    uniform float uEnh;
    void main() {
        vec3 N = normalize(vN);
        vec3 L = normalize(vec3(1.0, 1.0, 1.0));
        float nl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
        float sp = pow(max(dot(N, H), 0.0), 128.0);
        vec3 col = vCol * (0.2 + nl) + vec3(0.0, 1.0, 1.0) * sp;
        if (uEnh > 0.5) {
            vec3 V = normalize(-vEye);
            float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
            col = vCol * (0.16 + 0.95 * nl) + vec3(0.55, 0.9, 1.0) * sp * 1.4 + vCol * rim * 0.55;
            float fog = exp(-pow(length(vEye) / 95.0, 2.0));
            col *= mix(0.5, 1.0, fog) * mix(0.55, 1.0, vAge);
            col = col / (1.0 + 0.25 * col);          /* soft shoulder */
        }
        o = vec4(col, 1.0);
    });

static void new_colors(cstate *s) {
    s->ncolors = 128;
    make_smooth_colormap(0, 0, 0, s->colors, &s->ncolors, False, False, False);
    for (int i = 0; i < s->count; i++) s->sub[i].ccolor = cs_rand_int(&s->rng, s->ncolors);
}

static void init_cubestorm(ModeInfo *mi) {
    cstate *s = calloc(1, sizeof *s); C = s;
    s->style = cs_style(); s->speed = (float)cs_opt_f("speed", 1);
    s->count = (int)cs_opt_i("count", 4); if (s->count > 12) s->count = 12;
    s->max_length = (int)cs_opt_i("length", 200); s->thickness = (float)cs_opt_f("thickness", 0.06);
    s->spin = cs_opt_b("spin", 1); s->wander = cs_opt_b("wander", 1);
    s->bloom = (float)cs_opt_f("bloom", 1); s->aa = cs_opt_b("antialias", 1);
    uint32_t seed = cs_seed_from_options(); cs_rng_seed(&s->rng, seed); srandom(seed);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    s->colors = calloc(128, sizeof(XColor));
    for (int i = 0; i < s->count; i++) {
        double wander = i == 0 ? 0.05 : 0, spin = i == 0 ? 10.0 : 4.0, accel = i == 0 ? 4.0 : 2.0;
        s->sub[i].rot = make_rotator(s->spin ? spin : 0, s->spin ? spin : 0, s->spin ? spin : 0, accel,
                                     s->wander ? wander : 0, True);
    }
    new_colors(s);
    build_mesh(s);
    s->prog = cs_program(VS, FS, "cubestorm");
    if (!s->prog) { free(g_mesh); g_mesh = NULL; return; }
    glGenVertexArrays(1, &s->vao); glBindVertexArray(s->vao);
    glGenBuffers(1, &s->vbo_mesh); glBindBuffer(GL_ARRAY_BUFFER, s->vbo_mesh);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * s->nverts, g_mesh, GL_STATIC_DRAW); free(g_mesh);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    glGenBuffers(3, s->vbo_inst);
    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo_inst[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12 * (MAXHIST + 32), NULL, GL_STREAM_DRAW);
    }
    glBindVertexArray(0);
    if (s->style) s->post_ok = cs_post_init(&s->post, s->w, s->h, 1);
    fprintf(stderr, "[diag] cubestorm: style=%s seed=%u count=%d length=%d\n", s->style ? "enhanced" : "classic", seed, s->count, s->max_length);
}

static void reshape_cubestorm(ModeInfo *mi, int w, int h) {
    if (!C) return;
    C->w = w; C->h = h; glViewport(0, 0, w, h);
    if (C->post_ok) cs_post_resize(&C->post, w, h);
}

static void push_hist(cstate *s) {
    if (s->nhist > s->max_length && s->nhist > s->count) {
        memmove(s->hist, s->hist + s->count, (s->nhist - s->count) * sizeof *s->hist);
        s->nhist -= s->count;
    }
    if (s->nhist + s->count >= MAXHIST) {           /* safety valve for very long trails */
        memmove(s->hist, s->hist + s->count, (s->nhist - s->count) * sizeof *s->hist);
        s->nhist -= s->count;
    }
    double px, py, pz, rx = 0, ry = 0, rz = 0;
    get_position(s->sub[0].rot, &px, &py, &pz, True);
    for (int i = 0; i < s->count; i++) {
        subcube *sc = &s->sub[i]; histcube *hc = &s->hist[s->nhist];
        double rx2, ry2, rz2;
        get_rotation(sc->rot, &rx2, &ry2, &rz2, True);
        if (i == 0) { rx = rx2; ry = ry2; rz = rz2; } else { rx2 += rx; ry2 += ry; rz2 += rz; }
        hc->px = (float)px; hc->py = (float)py; hc->pz = (float)pz;
        hc->rx = (float)rx2; hc->ry = (float)ry2; hc->rz = (float)rz2; hc->ccolor = sc->ccolor;
        if (++sc->ccolor >= s->ncolors) sc->ccolor = 0;
        s->nhist++;
    }
}

static void step_cubes(cstate *s) {
    if (s->clear_p) {
        s->nhist = 0;
        if (cs_rand_int(&s->rng, 25) == 0) s->clear_p = 0;
    } else if (cs_rand_int(&s->rng, 200) == 0) {
        s->clear_p = 1;
        new_colors(s);
    }
    push_hist(s);
}

static void draw_cubestorm(ModeInfo *mi) {
    cstate *s = C; if (!s || !s->prog) return;
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, 6);
    for (int i = 0; i < steps; i++) step_cubes(s);

    float *inst = malloc(sizeof(float) * 12 * (s->nhist + 1));
    for (int i = 0; i < s->nhist; i++) {
        histcube *hc = &s->hist[i]; float *q = inst + i * 12;
        /* age 1 = newest .. 0 = oldest; the classic style ignores it */
        float age = s->nhist > 1 ? (float)i / (float)(s->nhist - 1) : 1.0f;
        q[0] = hc->px; q[1] = hc->py; q[2] = hc->pz; q[3] = age;
        q[4] = hc->rx; q[5] = hc->ry; q[6] = hc->rz; q[7] = 0;
        q[8] = s->colors[hc->ccolor].red / 65536.0f; q[9] = s->colors[hc->ccolor].green / 65536.0f;
        q[10] = s->colors[hc->ccolor].blue / 65536.0f; q[11] = 1;
    }
    s->ring = (s->ring + 1) % 3;
    glBindVertexArray(s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_inst[s->ring]);
    if (s->nhist) glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * 12 * s->nhist, inst);
    free(inst);
    for (int k = 0; k < 3; k++) {
        glEnableVertexAttribArray(2 + k);
        glVertexAttribPointer(2 + k, 4, GL_FLOAT, GL_FALSE, 48, (void *)(sizeof(float) * 4 * k));
        glVertexAttribDivisor(2 + k, 1);
    }
    float aspect = (float)s->w / (float)s->h;
    float sc = (s->w < s->h) ? (float)s->w / (float)s->h : 1.0f;
    cs_mat4 P = cs_perspective(30, aspect, 1, 100);
    cs_mat4 V = cs_mul(cs_translate(0, 0, -45), cs_scale(sc, sc, sc));

    int enh = s->style && s->post_ok;
    if (enh) cs_post_begin(&s->post); else glViewport(0, 0, s->w, s->h);
    if (s->style) glClearColor(0.008f, 0.008f, 0.016f, 1); else glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CW);
    glDisable(GL_BLEND);
    glUseProgram(s->prog);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uP"), 1, GL_FALSE, P.m);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uV"), 1, GL_FALSE, V.m);
    glUniform1f(glGetUniformLocation(s->prog, "uEnh"), s->style ? 1.f : 0.f);
    if (s->nhist) glDrawArraysInstanced(GL_TRIANGLES, 0, s->nverts, s->nhist);
    glBindVertexArray(0);
    glFrontFace(GL_CCW);
    if (enh) cs_post_end(&s->post, s->bloom * 0.5f, 1.0f, 0.4f, s->aa, 0);
}

static void free_cubestorm(ModeInfo *mi) {
    cstate *s = C; if (!s) return;
    if (s->prog) glDeleteProgram(s->prog);
    if (s->vao) glDeleteVertexArrays(1, &s->vao);
    glDeleteBuffers(1, &s->vbo_mesh); glDeleteBuffers(3, s->vbo_inst);
    if (s->post_ok) cs_post_free(&s->post);
    for (int i = 0; i < s->count; i++) free_rotator(s->sub[i].rot);
    free(s->colors); free(s); C = NULL;
}
static Bool cubestorm_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_cubestorm(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt cubestorm_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table cubestorm_xscreensaver_function_table = {
    .name = "cubestorm", .class_ = "CubeStorm",
    .init_cb = init_cubestorm, .draw_cb = draw_cubestorm, .reshape_cb = reshape_cubestorm,
    .event_cb = cubestorm_handle_event, .free_cb = free_cubestorm, .release_cb = release_cubestorm,
    .opts = &cubestorm_opts, .defaults_str = "",
};
