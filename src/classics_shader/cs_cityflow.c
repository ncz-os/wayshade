/* cs_cityflow.c - shader-engine port of xscreensaver "cityflow".
 *
 * cityflow, Copyright (c) 2014-2017 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Kept from the original: a field of skewed square-ish towers whose heights
 * follow moving interference waves ("Adapted from hacks/interference.c by
 * Hannu Mallat"), colored by height through a smooth random colormap, viewed
 * from above at a slant, lit by one light fixed to the model, background the
 * first colormap entry.
 *
 * Why this port exists: the GL1 version set one glMaterialfv per tower inside
 * a single glBegin(GL_QUADS).  The compatibility layer applies a material as
 * a per-batch uniform, so every tower took the last tower's color and the
 * flat tops merged into one polygon.  Here each tower is an instance with its
 * own color, drawn in a single instanced call (the original 800 towers, not
 * the 300 the compatibility build had to cap it at).
 */
#include "cs_common.h"
#include "cs_post.h"

#define STEP_DT 0.02       /* original delay 20000 us */
#define TEX 512
#define MAXC 1600

typedef struct { float x, y, z, w, h, d, cth, sth; } cube;
typedef struct { int x, y; double xth, yth; } wave_src;

typedef struct {
    int style; float speed; int ncubes, nwaves, wave_speed, radius, skew;
    float bloom; int aa;
    cs_rng rng; cs_clock clk;
    cube cubes[MAXC];
    float min_x, max_x, min_y, max_y;
    int ncolors; XColor *colors;
    wave_src srcs[16]; int *heights;
    int w, h;
    cs_post post; int post_ok;
    GLuint prog, vao, vbo_inst[3]; int ring;
} cstate;
static cstate *C;

static const ncz_opt_def COPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"count", NCZ_OPT_INT, "800", 50, 1600, NULL, NULL, NULL, "Towers", "Number of towers (xscreensaver -count).", "Density"},
    {"waves", NCZ_OPT_INT, "6", 1, 16, NULL, NULL, NULL, "Waves", "Number of interference wave sources (xscreensaver -waves).", "Motion"},
    {"wave-speed", NCZ_OPT_INT, "25", 1, 200, NULL, NULL, NULL, "Wave speed", "Speed of the wave sources (xscreensaver -wave-speed).", "Motion"},
    {"wave-radius", NCZ_OPT_INT, "256", 32, 512, NULL, NULL, NULL, "Wave radius", "Reach of each wave source (xscreensaver -wave-radius).", "Motion"},
    {"skew", NCZ_OPT_INT, "12", 0, 90, NULL, NULL, NULL, "Skew", "Maximum random rotation of each tower in degrees (xscreensaver -skew).", "Look"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof COPTS / sizeof COPTS[0]; *prefix = "NCZ_CITYFLOW_"; *group = "cityflow"; return COPTS;
}

static void init_waves(cstate *s) {
    s->heights = calloc(s->radius, sizeof(int));
    for (int i = 0; i < s->radius; i++) {
        float max = (s->ncolors * (s->radius - i) / (float)s->radius);
        s->heights[i] = (int)((max + max * cos(i / 50.0)) / 2.0);
    }
    for (int i = 0; i < s->nwaves; i++) {
        s->srcs[i].xth = cs_rand(&s->rng) * 2.0 * M_PI;
        s->srcs[i].yth = cs_rand(&s->rng) * 2.0 * M_PI;
    }
}
static void move_waves(cstate *s) {
    for (int i = 0; i < s->nwaves; i++) {
        s->srcs[i].xth += s->wave_speed / 1000.0;
        if (s->srcs[i].xth > 2 * M_PI) s->srcs[i].xth -= 2 * M_PI;
        s->srcs[i].yth += s->wave_speed / 1000.0;
        if (s->srcs[i].yth > 2 * M_PI) s->srcs[i].yth -= 2 * M_PI;
        s->srcs[i].x = (int)(TEX / 2 + cos(s->srcs[i].xth) * TEX / 2);
        s->srcs[i].y = (int)(TEX / 2 + cos(s->srcs[i].yth) * TEX / 2);
    }
}
/* the original works in integers; enhanced style uses the same table with
 * linear interpolation and no truncation, so heights change smoothly */
static float interference_point(cstate *s, int x, int y, int smooth) {
    float result = 0;
    for (int i = 0; i < s->nwaves; i++) {
        int dx = x - s->srcs[i].x, dy = y - s->srcs[i].y;
        if (smooth) {
            float dist = sqrtf((float)(dx * dx + dy * dy));
            if (dist < s->radius - 1) {
                int i0 = (int)dist; float f = dist - i0;
                result += s->heights[i0] * (1 - f) + s->heights[i0 + 1] * f;
            }
        } else {
            int dist = (int)sqrt(dx * dx + dy * dy);
            result += (dist >= s->radius ? 0 : s->heights[dist]);
        }
    }
    result *= 0.4f;
    if (!smooth) result = (float)(int)result;
    if (result > 255) result = 255;
    return result;
}
static int cmp_cubes(const void *aa, const void *bb) {
    const cube *a = aa, *b = bb;
    return ((int)(b->y * 10000) - (int)(a->y * 10000));
}
static void animate(cstate *s) {
    move_waves(s);
    for (int i = 0; i < s->ncubes; i++) {
        cube *c = &s->cubes[i];
        float dxr = s->max_x - s->min_x, dyr = s->max_y - s->min_y;
        if (dxr < 1e-6f) dxr = 1; if (dyr < 1e-6f) dyr = 1;
        float fx = (c->x - s->min_x) / dxr;
        float fy = (c->y - s->min_y) / dyr;
        int x = (int)(TEX * fx) % TEX, y = (int)(TEX * fy) % TEX;
        float v = interference_point(s, x, y, s->style);
        if (!s->style) v = (float)(unsigned char)(int)v;
        c->h = c->z + (v / 256.0f / 2.5f) + 0.1f;
    }
}

static const char *VS = CS_GLSL(
    layout(location = 0) in vec4 aA;       /* x', y', w, d */
    layout(location = 1) in vec4 aB;       /* h, cth, sth, - */
    layout(location = 2) in vec4 aC;       /* color */
    uniform mat4 uP; uniform mat4 uMV; uniform mat3 uN;
    out vec3 vCol; out vec3 vNrm; out vec3 vEye; out float vZ; out float vH;
    void main() {
        int id = gl_VertexID; int face = id / 6; int m = id % 6;
        int q = (m == 0) ? 0 : (m == 1) ? 1 : (m == 2) ? 2 : (m == 3) ? 0 : (m == 4) ? 2 : 3;
        float x = aA.x, y = aA.y, w = aA.z * 0.5, d = aA.w * 0.5, h = aB.x * 0.5, cth = aB.y, sth = aB.z;
        float bottom = 5.0;
        float xw = cth * w, xd = sth * d, yw = -sth * w, yd = cth * d;
        vec3 p; vec3 n;
        if (face == 0) {                                   /* top */
            n = vec3(0.0, 0.0, -1.0);
            float sa = (q == 0 || q == 1) ? 1.0 : -1.0;
            float sb = (q == 0 || q == 3) ? 1.0 : -1.0;
            p = vec3(x + sa * xw + sb * xd, y + sa * yw + sb * yd, -h);
        } else if (face == 1) {                            /* front */
            n = vec3(sth, cth, 0.0);
            float sa = (q == 0 || q == 1) ? 1.0 : -1.0;
            float z = (q == 0 || q == 3) ? bottom : -h;
            p = vec3(x + sa * xw + xd, y + sa * yw + yd, z);
        } else {                                           /* right */
            n = vec3(cth, -sth, 0.0);
            float sb = (q == 1 || q == 2) ? 1.0 : -1.0;
            float z = (q == 2 || q == 3) ? bottom : -h;
            p = vec3(x + xw + sb * xd, y + yw + sb * yd, z);
        }
        vec4 e = uMV * vec4(p, 1.0);
        gl_Position = uP * e;
        vEye = e.xyz; vCol = aC.rgb; vNrm = uN * n; vZ = p.z; vH = -h;
    });
static const char *FS = CS_GLSL(
    in vec3 vCol; in vec3 vNrm; in vec3 vEye; in float vZ; in float vH; out vec4 o;
    uniform vec3 uLight; uniform float uEnh; uniform vec3 uFog;
    void main() {
        vec3 N = normalize(vNrm);
        float d = max(dot(N, uLight), 0.0);
        vec3 col = vCol * (0.4 + d);
        if (uEnh > 0.5) {
            float up = max(N.y, 0.0);
            float ao = mix(1.0, 0.30, smoothstep(vH, vH + 0.45, vZ));      /* sides darken toward the street */
            float rim = pow(1.0 - abs(N.z), 3.0);
            col = vCol * (0.28 + 0.9 * d + 0.18 * up) * ao + vCol * rim * 0.12;
            float f = 1.0 - exp(-pow(max(length(vEye) - 18.0, 0.0) / 40.0, 2.0));
            col = mix(col, uFog, clamp(f, 0.0, 0.85));
        }
        o = vec4(col, 1.0);
    });

static void init_cityflow(ModeInfo *mi) {
    cstate *s = calloc(1, sizeof *s); C = s;
    s->style = cs_style(); s->speed = (float)cs_opt_f("speed", 1);
    s->ncubes = (int)cs_opt_i("count", 800); if (s->ncubes > MAXC) s->ncubes = MAXC;
    s->nwaves = (int)cs_opt_i("waves", 6); s->wave_speed = (int)cs_opt_i("wave-speed", 25);
    s->radius = (int)cs_opt_i("wave-radius", 256); s->skew = (int)cs_opt_i("skew", 12);
    s->bloom = (float)cs_opt_f("bloom", 1); s->aa = cs_opt_b("antialias", 1);
    uint32_t seed = cs_seed_from_options(); cs_rng_seed(&s->rng, seed); srandom(seed);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    s->ncolors = 256; s->colors = calloc(s->ncolors, sizeof(XColor));
    make_smooth_colormap(0, 0, 0, s->colors, &s->ncolors, False, False, False);
    init_waves(s);
    float scale = 1.8f / sqrtf((float)s->ncubes);
    for (int i = 0; i < s->ncubes; i++) {
        cube *c = &s->cubes[i];
        double th = -(s->skew ? cs_rand(&s->rng) * s->skew : 0) * M_PI / 180;
        c->x = cs_rand(&s->rng) - 0.5f; c->y = cs_rand(&s->rng) - 0.5f;
        c->z = cs_rand(&s->rng) * 0.12f;
        c->cth = (float)cos(th); c->sth = (float)sin(th);
        c->w = scale * (cs_rand(&s->rng) + 0.2f); c->d = scale * (cs_rand(&s->rng) + 0.2f);
        if (c->x < s->min_x) s->min_x = c->x;
        if (c->y < s->min_y) s->min_y = c->y;
        if (c->x > s->max_x) s->max_x = c->x;
        if (c->y > s->max_y) s->max_y = c->y;
    }
    qsort(s->cubes, s->ncubes, sizeof *s->cubes, cmp_cubes);
    animate(s);
    s->prog = cs_program(VS, FS, "cityflow");
    if (!s->prog) return;
    glGenVertexArrays(1, &s->vao);
    glGenBuffers(3, s->vbo_inst);
    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo_inst[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12 * MAXC, NULL, GL_STREAM_DRAW);
    }
    if (s->style) s->post_ok = cs_post_init(&s->post, s->w, s->h, 1);
    fprintf(stderr, "[diag] cityflow: style=%s seed=%u towers=%d\n", s->style ? "enhanced" : "classic", seed, s->ncubes);
}

static void reshape_cityflow(ModeInfo *mi, int w, int h) {
    if (!C) return;
    C->w = w; C->h = h; glViewport(0, 0, w, h);
    if (C->post_ok) cs_post_resize(&C->post, w, h);
}

static void draw_cityflow(ModeInfo *mi) {
    cstate *s = C; if (!s || !s->prog) return;
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, 6);
    for (int i = 0; i < steps; i++) animate(s);

    float *inst = malloc(sizeof(float) * 12 * s->ncubes);
    for (int i = 0; i < s->ncubes; i++) {
        cube *c = &s->cubes[i]; float *q = inst + i * 12;
        q[0] = c->cth * c->x + c->sth * c->y; q[1] = -c->sth * c->x + c->cth * c->y; q[2] = c->w; q[3] = c->d;
        q[4] = c->h; q[5] = c->cth; q[6] = c->sth; q[7] = 0;
        int ci = (int)(c->h * s->ncolors * 0.7f); ci %= s->ncolors; if (ci < 0) ci += s->ncolors;
        q[8] = s->colors[ci].red / 65536.0f; q[9] = s->colors[ci].green / 65536.0f; q[10] = s->colors[ci].blue / 65536.0f; q[11] = 1;
    }
    s->ring = (s->ring + 1) % 3;
    glBindVertexArray(s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo_inst[s->ring]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * 12 * s->ncubes, inst); free(inst);
    for (int k = 0; k < 3; k++) {
        glEnableVertexAttribArray(k);
        glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, 48, (void *)(sizeof(float) * 4 * k));
        glVertexAttribDivisor(k, 1);
    }
    float aspect = (float)s->w / (float)s->h;
    cs_mat4 P = cs_perspective(30, aspect, 10, 50);
    cs_mat4 M = cs_translate(0, 0, -30);
    M = cs_mul(M, cs_rotate(-180, 1, 0, 0));
    M = cs_mul(M, cs_scale(15, 15, 15));
    M = cs_mul(M, cs_rotate(-90, 1, 0, 0));
    M = cs_mul(M, cs_translate(-0.18f, 0, -0.18f));
    M = cs_mul(M, cs_rotate(37, 1, 0, 0));
    M = cs_mul(M, cs_rotate(20, 0, 0, 1));
    M = cs_mul(M, cs_scale(2.1f, 2.1f, 2.1f));
    float n3[9]; cs_mat3_of(&M, n3);
    /* light (0, 0.25, -1) is fixed to the model; normals are unit, scale removed by normalize */
    float lx = n3[0] * 0.0f + n3[3] * 0.25f + n3[6] * -1.0f;
    float ly = n3[1] * 0.0f + n3[4] * 0.25f + n3[7] * -1.0f;
    float lz = n3[2] * 0.0f + n3[5] * 0.25f + n3[8] * -1.0f;
    float ll = sqrtf(lx * lx + ly * ly + lz * lz); lx /= ll; ly /= ll; lz /= ll;
    float bg[3] = { s->colors[0].red / 65536.0f, s->colors[0].green / 65536.0f, s->colors[0].blue / 65536.0f };

    int enh = s->style && s->post_ok;
    if (enh) cs_post_begin(&s->post); else glViewport(0, 0, s->w, s->h);
    glClearColor(bg[0], bg[1], bg[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glUseProgram(s->prog);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uP"), 1, GL_FALSE, P.m);
    glUniformMatrix4fv(glGetUniformLocation(s->prog, "uMV"), 1, GL_FALSE, M.m);
    float nn[9]; for (int i = 0; i < 9; i++) nn[i] = n3[i] / 2.1f / 15.0f;
    glUniformMatrix3fv(glGetUniformLocation(s->prog, "uN"), 1, GL_FALSE, nn);
    float lv[3] = { lx, ly, lz };
    glUniform3fv(glGetUniformLocation(s->prog, "uLight"), 1, lv);
    glUniform3fv(glGetUniformLocation(s->prog, "uFog"), 1, bg);
    glUniform1f(glGetUniformLocation(s->prog, "uEnh"), s->style ? 1.f : 0.f);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 18, s->ncubes);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    if (enh) cs_post_end(&s->post, s->bloom * 0.25f, 1.0f, 0.35f, s->aa, 0);
}

static void free_cityflow(ModeInfo *mi) {
    cstate *s = C; if (!s) return;
    if (s->prog) glDeleteProgram(s->prog);
    if (s->vao) glDeleteVertexArrays(1, &s->vao);
    glDeleteBuffers(3, s->vbo_inst);
    if (s->post_ok) cs_post_free(&s->post);
    free(s->heights); free(s->colors); free(s); C = NULL;
}
static Bool cityflow_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_cityflow(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt cityflow_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table cityflow_xscreensaver_function_table = {
    .name = "cityflow", .class_ = "Cityflow",
    .init_cb = init_cityflow, .draw_cb = draw_cityflow, .reshape_cb = reshape_cityflow,
    .event_cb = cityflow_handle_event, .free_cb = free_cityflow, .release_cb = release_cityflow,
    .opts = &cityflow_opts, .defaults_str = "",
};
