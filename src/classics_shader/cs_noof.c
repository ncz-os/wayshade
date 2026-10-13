/* cs_noof.c - shader-engine port of xscreensaver "noof".
 *
 * noof, Copyright (c) 2004-2018 Bill Torzewski <billt@worksitez.com>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * Originally a demo included with GLUT; (Apparently this was called
 * "diatoms" on Irix.) ported to raw GL and xscreensaver by jwz, 12-Feb-2004.
 *
 * Kept from the original: seven wandering "flowers" of 2..19 petals whose
 * petal shape breathes with two beating sines, the mutual gravity, the slow
 * hue/saturation/value drift, the rebirth of a flower as something else when
 * its petals close, and the essential effect: nothing is ever cleared.  Each
 * petal is a translucent black fill with a colored outline, drawn on top of
 * everything already there, so the trails accumulate until covered.
 *
 * Changed: the GL1 version copied the whole framebuffer into a texture every
 * frame (glCopyTexSubImage2D) to fake accumulation.  Here the accumulation
 * lives in a persistent offscreen buffer that is only drawn into; the
 * petals of every 10 ms simulation step (the original frame rate was 100 per
 * second) are instanced and the buffer is shown with one full-screen pass.
 * enhanced style draws anti-aliased glowing outlines, lets very old trails
 * fade slowly so the picture never turns to mud, and adds soft glow.
 */
#include "cs_common.h"
#include "cs_post.h"

#define N_SHAPES 7
#define STEP_DT 0.01            /* original delay 10000 us */
#define MAXSTEPS 8
#define MAXINST (N_SHAPES * 19)

typedef struct {
    float pos[N_SHAPES * 3], dir[N_SHAPES * 3], acc[N_SHAPES * 3], col[N_SHAPES * 3];
    float hsv[N_SHAPES * 3], hpr[N_SHAPES * 3];
    float ang[N_SHAPES], spn[N_SHAPES], sca[N_SHAPES], geep[N_SHAPES], peep[N_SHAPES], speedsq[N_SHAPES];
    int blad[N_SHAPES];
    float ht, wd;
    int tko;
} noof_sim;

typedef struct {
    int style; float speed, bloom, fade; int aa;
    cs_clock clk;
    noof_sim sim;
    float inst[MAXSTEPS][MAXINST * 12]; int ninst[MAXSTEPS];
    int w, h;
    GLuint accum_fbo, accum_tex;
    GLuint prog_fill, prog_line, prog_lineq, prog_present, prog_fade, vao, vao_empty, vbo[3]; int ring;
    cs_post post; int post_ok;
    int cleared;
} nstate;
static nstate *N;

static const ncz_opt_def NOPTS[] = {
    CS_OPT_STYLE, CS_OPT_SPEED, CS_OPT_SEED,
    {"trail-fade", NCZ_OPT_FLOAT, "-1", -1, 1, NULL, NULL, NULL, "Trail fade",
     "How fast old trails fade (0 = never, the xscreensaver behavior; -1 = 0 for classic, gentle for enhanced).", "Look"},
    CS_OPT_BLOOM, CS_OPT_AA,
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof NOPTS / sizeof NOPTS[0]; *prefix = "NCZ_NOOF_"; *group = "noof"; return NOPTS;
}

static double rnd(void) { return random() / (double)RAND_MAX; }

/* ---- simulation: line for line from noof.c ---- */
static void initshapes(noof_sim *bp, int i) {
    int k; float f;
    for (k = i * 3; k <= i * 3 + 2; k++) {
        f = rnd(); bp->pos[k] = f;
        f = rnd(); f = (f - 0.5f) * 0.05f; bp->dir[k] = f;
        f = rnd(); f = (f - 0.5f) * 0.0002f; bp->acc[k] = f;
        f = rnd(); bp->col[k] = f;
    }
    bp->speedsq[i] = bp->dir[i * 3] * bp->dir[i * 3] + bp->dir[i * 3 + 1] * bp->dir[i * 3 + 1];
    f = rnd(); bp->blad[i] = 2 + (int)(f * 17.0);
    f = rnd(); bp->ang[i] = f;
    f = rnd(); bp->spn[i] = (f - 0.5f) * 40.0f / (10 + bp->blad[i]);
    f = rnd(); bp->sca[i] = (f * 0.1f + 0.08f);
    bp->dir[i * 3] *= bp->sca[i]; bp->dir[i * 3 + 1] *= bp->sca[i];
    f = rnd(); bp->hsv[i * 3] = f * 360.0f;
    f = rnd(); bp->hsv[i * 3 + 1] = f * 0.6f + 0.4f;
    f = rnd(); bp->hsv[i * 3 + 2] = f * 0.7f + 0.3f;
    f = rnd(); bp->hpr[i * 3] = f * 0.005f * 360.0f;
    f = rnd(); bp->hpr[i * 3 + 1] = f * 0.03f;
    f = rnd(); bp->hpr[i * 3 + 2] = f * 0.02f;
    bp->geep[i] = 0;
    f = rnd(); bp->peep[i] = 0.01f + f * 0.2f;
}
static const float bladeratio[] = {
    0.0, 0.0, 3.00000, 1.73205, 1.00000, 0.72654, 0.57735, 0.48157,
    0.41421, 0.36397, 0.19076, 0.29363, 0.26795, 0.24648,
    0.22824, 0.21256, 0.19891, 0.18693, 0.17633, 0.16687,
};
/* Emits the petals of shape l into inst (12 floats each); returns the count. */
static int drawleaf(noof_sim *bp, int l, float *inst) {
    int blades = bp->blad[l], n = 0;
    float x, y, wobble;
    y = 0.10f * sinf(bp->geep[l] * M_PI / 180.0f) + 0.099f * sinf(bp->geep[l] * 5.12f * M_PI / 180.0f);
    if (y < 0) y = -y;
    x = 0.15f * cosf(bp->geep[l] * M_PI / 180.0f) + 0.149f * cosf(bp->geep[l] * 5.12f * M_PI / 180.0f);
    if (x < 0.0f) x = 0.0f - x;
    if (y < 0.001f && x > 0.000002f && ((bp->tko & 0x1) == 0)) {
        initshapes(bp, l);
        bp->tko++;
        return 0;
    }
    {
        float w1 = sinf(bp->geep[l] * 15.3f * M_PI / 180.0f);
        wobble = 3.0f + 2.00f * sinf(bp->geep[l] * 0.4f * M_PI / 180.0f) + 3.94261f * w1;
    }
    if (y > x * bladeratio[blades]) y = x * bladeratio[blades];
    for (int b = 0; b < blades; b++) {
        float *q = inst + n * 12;
        q[0] = bp->pos[l * 3]; q[1] = bp->pos[l * 3 + 1];
        q[2] = (bp->ang[l] + b * (360.0f / blades)) * (float)M_PI / 180.0f;
        q[3] = wobble * bp->sca[l];
        q[4] = x; q[5] = y; q[6] = bp->sca[l]; q[7] = 0;
        q[8] = bp->col[l * 3]; q[9] = bp->col[l * 3 + 1]; q[10] = bp->col[l * 3 + 2]; q[11] = 1;
        n++;
    }
    return n;
}
static void motionUpdate(noof_sim *bp, int t) {
    if (bp->pos[t * 3] < -bp->sca[t] * bp->wd && bp->dir[t * 3] < 0.0f) bp->dir[t * 3] = -bp->dir[t * 3];
    else if (bp->pos[t * 3] > (1 + bp->sca[t]) * bp->wd && bp->dir[t * 3] > 0.0f) bp->dir[t * 3] = -bp->dir[t * 3];
    else if (bp->pos[t * 3 + 1] < -bp->sca[t] * bp->ht && bp->dir[t * 3 + 1] < 0.0f) bp->dir[t * 3 + 1] = -bp->dir[t * 3 + 1];
    else if (bp->pos[t * 3 + 1] > (1 + bp->sca[t]) * bp->ht && bp->dir[t * 3 + 1] > 0.0f) bp->dir[t * 3 + 1] = -bp->dir[t * 3 + 1];
    bp->pos[t * 3] += bp->dir[t * 3];
    bp->pos[t * 3 + 1] += bp->dir[t * 3 + 1];
    bp->ang[t] += bp->spn[t];
    bp->geep[t] += bp->peep[t];
    if (bp->geep[t] > 360 * 5.0f) bp->geep[t] -= 360 * 5.0f;
    if (bp->ang[t] < 0.0f) bp->ang[t] += 360.0f;
    if (bp->ang[t] > 360.0f) bp->ang[t] -= 360.0f;
}
static void colorUpdate(noof_sim *bp, int i) {
    if (bp->hsv[i * 3 + 1] <= 0.5f && bp->hpr[i * 3 + 1] < 0.0f) bp->hpr[i * 3 + 1] = -bp->hpr[i * 3 + 1];
    if (bp->hsv[i * 3 + 1] >= 1.0f && bp->hpr[i * 3 + 1] > 0.0f) bp->hpr[i * 3 + 1] = -bp->hpr[i * 3 + 1];
    if (bp->hsv[i * 3 + 2] <= 0.4f && bp->hpr[i * 3 + 2] < 0.0f) bp->hpr[i * 3 + 2] = -bp->hpr[i * 3 + 2];
    if (bp->hsv[i * 3 + 2] >= 1.0f && bp->hpr[i * 3 + 2] > 0.0f) bp->hpr[i * 3 + 2] = -bp->hpr[i * 3 + 2];
    bp->hsv[i * 3] += bp->hpr[i * 3];
    bp->hsv[i * 3 + 1] += bp->hpr[i * 3 + 1];
    bp->hsv[i * 3 + 2] += bp->hpr[i * 3 + 2];
#define H(hhh) hhh[i*3  ]
#define S(hhh) hhh[i*3+1]
#define V(hhh) hhh[i*3+2]
#define R(hhh) hhh[i*3  ]
#define G(hhh) hhh[i*3+1]
#define B(hhh) hhh[i*3+2]
    if (V(bp->hsv) < 0.0f) V(bp->hsv) = 0.0f;
    if (V(bp->hsv) > 1.0f) V(bp->hsv) = 1.0f;
    if (S(bp->hsv) <= 0.0f) {
        R(bp->col) = V(bp->hsv); G(bp->col) = V(bp->hsv); B(bp->col) = V(bp->hsv);
    } else {
        float f, h, p, q, t, v; int hi;
        while (H(bp->hsv) < 0.0f) H(bp->hsv) += 360.0f;
        while (H(bp->hsv) >= 360.0f) H(bp->hsv) -= 360.0f;
        if (S(bp->hsv) < 0.0f) S(bp->hsv) = 0.0f;
        if (S(bp->hsv) > 1.0f) S(bp->hsv) = 1.0f;
        h = H(bp->hsv) / 60.0f; hi = (int)h; f = h - hi;
        v = V(bp->hsv);
        p = V(bp->hsv) * (1 - S(bp->hsv));
        q = V(bp->hsv) * (1 - S(bp->hsv) * f);
        t = V(bp->hsv) * (1 - S(bp->hsv) * (1 - f));
        if (hi <= 0) { R(bp->col) = v; G(bp->col) = t; B(bp->col) = p; }
        else if (hi == 1) { R(bp->col) = q; G(bp->col) = v; B(bp->col) = p; }
        else if (hi == 2) { R(bp->col) = p; G(bp->col) = v; B(bp->col) = t; }
        else if (hi == 3) { R(bp->col) = p; G(bp->col) = q; B(bp->col) = v; }
        else if (hi == 4) { R(bp->col) = t; G(bp->col) = p; B(bp->col) = v; }
        else { R(bp->col) = v; G(bp->col) = p; B(bp->col) = q; }
    }
#undef H
#undef S
#undef V
#undef R
#undef G
#undef B
}
static void gravity(noof_sim *bp, float fx) {
    for (int a = 0; a < N_SHAPES; a++) {
        for (int b = 0; b < a; b++) {
            float t, d2;
            t = bp->pos[b * 3] - bp->pos[a * 3]; d2 = t * t;
            t = bp->pos[b * 3 + 1] - bp->pos[a * 3 + 1]; d2 += t * t;
            if (d2 < 0.000001f) d2 = 0.00001f;
            if (d2 < 0.1f) {
                float v0, v1, z;
                v0 = bp->pos[b * 3] - bp->pos[a * 3];
                v1 = bp->pos[b * 3 + 1] - bp->pos[a * 3 + 1];
                z = 0.00000001f * fx / d2;
                bp->dir[a * 3] += v0 * z * bp->sca[b];
                bp->dir[b * 3] += -v0 * z * bp->sca[a];
                bp->dir[a * 3 + 1] += v1 * z * bp->sca[b];
                bp->dir[b * 3 + 1] += -v1 * z * bp->sca[a];
            }
        }
    }
}

/* ---- shaders ---- */
#define NOOF_COMMON \
    "layout(location = 0) in vec4 aA; layout(location = 1) in vec4 aB; layout(location = 2) in vec4 aC;\n" \
    "uniform vec2 uWH;   /* world size (wd, ht) */\n" \
    "vec2 petal(int i) { /* 0..3: (x*sca,0) (x,y) (0.3,0) (x,-y) */\n" \
    "  vec2 v = i == 0 ? vec2(aB.x * aB.z, 0.0) : (i == 1 ? vec2(aB.x, aB.y) : (i == 2 ? vec2(0.3, 0.0) : vec2(aB.x, -aB.y)));\n" \
    "  float c = cos(aA.z), s = sin(aA.z); v *= aA.w;\n" \
    "  return aA.xy + vec2(c * v.x - s * v.y, s * v.x + c * v.y); }\n" \
    "vec4 toclip(vec2 p) { return vec4(p / uWH * 2.0 - 1.0, 0.0, 1.0); }\n"

static const char *VS_FILL = NOOF_COMMON "void main() {\n"
    "  int id = gl_VertexID; /* strip order (x*sca,0) (x,y) (x,-y) (0.3,0) */\n"
    "  int k = id == 0 ? 0 : (id == 1 ? 1 : (id == 2 ? 3 : 2));\n"
    "  gl_Position = toclip(petal(k)); }\n";
static const char *FS_FILL = "out vec4 o; void main() { o = vec4(0.0, 0.0, 0.0, 0.376); }\n";
static const char *VS_LINE = NOOF_COMMON "out vec3 vCol; void main() { gl_Position = toclip(petal(gl_VertexID)); vCol = aC.rgb; }\n";
static const char *FS_LINE = "in vec3 vCol; out vec4 o; void main() { o = vec4(vCol, 1.0); }\n";
/* enhanced: outline as four anti-aliased quads */
static const char *VS_LINEQ = NOOF_COMMON "uniform float uHalf; uniform vec2 uRes; out vec3 vCol; out float vSide;\n"
    "void main() {\n"
    "  int seg = gl_VertexID / 6; int m = gl_VertexID % 6;\n"
    "  int q = (m == 0) ? 0 : (m == 1) ? 1 : (m == 2) ? 2 : (m == 3) ? 2 : (m == 4) ? 1 : 3;\n"
    "  vec2 a = petal(seg), b = petal((seg + 1) & 3);\n"
    "  vec2 sa = (a / uWH * 2.0 - 1.0) * uRes * 0.5, sb = (b / uWH * 2.0 - 1.0) * uRes * 0.5;\n"
    "  vec2 d = sb - sa; float l = max(length(d), 1e-4); d /= l; vec2 n = vec2(-d.y, d.x);\n"
    "  float side = (q == 0 || q == 2) ? -1.0 : 1.0;\n"
    "  vec2 s = ((q < 2) ? sa - d * uHalf : sb + d * uHalf) + n * side * (uHalf + 1.0);\n"
    "  gl_Position = vec4(s / (uRes * 0.5), 0.0, 1.0);\n"
    "  vCol = aC.rgb; vSide = side * (uHalf + 1.0); }\n";
static const char *FS_LINEQ = "in vec3 vCol; in float vSide; uniform float uHalf; out vec4 o;\n"
    "void main() { float a = clamp(uHalf + 0.5 - abs(vSide), 0.0, 1.0); o = vec4(vCol * 1.15, a); }\n";
static const char *FS_PRESENT = CS_GLSL(
    in vec2 vUv; out vec4 o; uniform sampler2D uTex;
    void main() { o = vec4(texture(uTex, vUv).rgb, 1.0); });
static const char *FS_FADE = CS_GLSL(
    in vec2 vUv; out vec4 o; uniform float uK;
    void main() { o = vec4(0.0, 0.0, 0.0, uK); });

static void alloc_accum(nstate *s) {
    glBindTexture(GL_TEXTURE_2D, s->accum_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s->w, s->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    s->cleared = 0;
}
static void init_noof(ModeInfo *mi) {
    nstate *s = calloc(1, sizeof *s); N = s;
    s->style = cs_style(); s->speed = (float)cs_opt_f("speed", 1);
    s->bloom = (float)cs_opt_f("bloom", 1); s->aa = cs_opt_b("antialias", 1);
    float tf = (float)cs_opt_f("trail-fade", -1);
    s->fade = tf >= 0 ? tf * 0.001f : (s->style ? 0.0005f : 0.0f);
    uint32_t seed = cs_seed_from_options(); srandom(seed);
    s->w = mi->xgwa.width; s->h = mi->xgwa.height;
    for (int i = 0; i < N_SHAPES; i++) initshapes(&s->sim, i);
    s->prog_fill = cs_program(VS_FILL, FS_FILL, "noof fill");
    s->prog_line = cs_program(VS_LINE, FS_LINE, "noof line");
    s->prog_lineq = cs_program(VS_LINEQ, FS_LINEQ, "noof lineq");
    s->prog_present = cs_program(CS_FULLSCREEN_VS, FS_PRESENT, "noof present");
    s->prog_fade = cs_program(CS_FULLSCREEN_VS, FS_FADE, "noof fade");
    if (!s->prog_fill || !s->prog_line || !s->prog_present) return;
    glGenTextures(1, &s->accum_tex);
    glBindTexture(GL_TEXTURE_2D, s->accum_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    alloc_accum(s);
    glGenFramebuffers(1, &s->accum_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->accum_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->accum_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenVertexArrays(1, &s->vao); glGenVertexArrays(1, &s->vao_empty);
    glGenBuffers(3, s->vbo);
    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12 * MAXINST * MAXSTEPS, NULL, GL_STREAM_DRAW);
    }
    if (s->style) s->post_ok = cs_post_init(&s->post, s->w, s->h, 0);
    fprintf(stderr, "[diag] noof: style=%s seed=%u\n", s->style ? "enhanced" : "classic", seed);
}
static void reshape_noof(ModeInfo *mi, int w, int h) {
    nstate *s = N; if (!s) return;
    if (w != s->w || h != s->h) { s->w = w; s->h = h; if (s->accum_tex) alloc_accum(s); if (s->post_ok) cs_post_resize(&s->post, w, h); }
    glViewport(0, 0, w, h);
    if (w <= h) { s->sim.wd = 1.0f; s->sim.ht = (float)h / (float)w; }
    else { s->sim.wd = (float)w / (float)h; s->sim.ht = 1.0f; }
}

static void bind_inst(nstate *s, int step) {
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo[s->ring]);
    for (int k = 0; k < 3; k++) {
        glEnableVertexAttribArray(k);
        glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, 48, (void *)((size_t)step * MAXINST * 48 + sizeof(float) * 4 * k));
        glVertexAttribDivisor(k, 1);
    }
}

static void draw_noof(ModeInfo *mi) {
    nstate *s = N; if (!s || !s->prog_fill || !s->accum_fbo) return;
    if (s->sim.wd == 0) reshape_noof(mi, s->w, s->h);
    int steps = cs_clock_steps(&s->clk, STEP_DT, s->speed, MAXSTEPS);
    for (int st = 0; st < steps; st++) {
        gravity(&s->sim, -2.0f);
        int n = 0;
        for (int i = 0; i < N_SHAPES; i++) {
            motionUpdate(&s->sim, i);
            colorUpdate(&s->sim, i);
            n += drawleaf(&s->sim, i, s->inst[st] + n * 12);
        }
        s->ninst[st] = n;
    }
    s->ring = (s->ring + 1) % 3;
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo[s->ring]);
    for (int st = 0; st < steps; st++)
        if (s->ninst[st]) glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)st * MAXINST * 48, s->ninst[st] * 48, s->inst[st]);

    glBindFramebuffer(GL_FRAMEBUFFER, s->accum_fbo);
    glViewport(0, 0, s->w, s->h);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    if (!s->cleared) { glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); s->cleared = 1; }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(s->vao);
    float wh[2] = { s->sim.wd, s->sim.ht };
    for (int st = 0; st < steps; st++) {
        if (!s->ninst[st]) continue;
        bind_inst(s, st);
        glUseProgram(s->prog_fill);
        glUniform2fv(glGetUniformLocation(s->prog_fill, "uWH"), 1, wh);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, s->ninst[st]);
        if (s->style && s->prog_lineq) {
            glUseProgram(s->prog_lineq);
            glUniform2fv(glGetUniformLocation(s->prog_lineq, "uWH"), 1, wh);
            glUniform2f(glGetUniformLocation(s->prog_lineq, "uRes"), (float)s->w, (float)s->h);
            glUniform1f(glGetUniformLocation(s->prog_lineq, "uHalf"), 0.5f * (s->h / 1080.0f) * 1.6f + 0.35f);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 24, s->ninst[st]);
        } else {
            glUseProgram(s->prog_line);
            glUniform2fv(glGetUniformLocation(s->prog_line, "uWH"), 1, wh);
            glDrawArraysInstanced(GL_LINE_LOOP, 0, 4, s->ninst[st]);
        }
    }
    if (s->fade > 0 && steps > 0 && s->prog_fade) {        /* enhanced: very old trails sink slowly */
        glUseProgram(s->prog_fade);
        glUniform1f(glGetUniformLocation(s->prog_fade, "uK"), 1.0f - powf(1.0f - s->fade, (float)steps));
        glBindVertexArray(s->vao_empty);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
    }
    glDisable(GL_BLEND);
    glBindVertexArray(0);

    int enh = s->style && s->post_ok;
    if (enh) cs_post_begin(&s->post); else { glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, s->w, s->h); }
    glUseProgram(s->prog_present);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s->accum_tex);
    glUniform1i(glGetUniformLocation(s->prog_present, "uTex"), 0);
    glBindVertexArray(s->vao_empty);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
    glBindVertexArray(0);
    if (enh) cs_post_end(&s->post, s->bloom * 0.6f, 1.0f, 0.3f, 0, 0);
    else glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void free_noof(ModeInfo *mi) {
    nstate *s = N; if (!s) return;
    GLuint p[] = { s->prog_fill, s->prog_line, s->prog_lineq, s->prog_present, s->prog_fade };
    for (int i = 0; i < 5; i++) if (p[i]) glDeleteProgram(p[i]);
    if (s->accum_fbo) glDeleteFramebuffers(1, &s->accum_fbo);
    if (s->accum_tex) glDeleteTextures(1, &s->accum_tex);
    if (s->vao) glDeleteVertexArrays(1, &s->vao);
    if (s->vao_empty) glDeleteVertexArrays(1, &s->vao_empty);
    glDeleteBuffers(3, s->vbo);
    if (s->post_ok) cs_post_free(&s->post);
    free(s); N = NULL;
}
static Bool noof_handle_event(ModeInfo *mi, XEvent *e) { (void)mi; (void)e; return False; }
static void release_noof(ModeInfo *mi) { (void)mi; }
static ModeSpecOpt noof_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table noof_xscreensaver_function_table = {
    .name = "noof", .class_ = "Noof",
    .init_cb = init_noof, .draw_cb = draw_noof, .reshape_cb = reshape_noof,
    .event_cb = noof_handle_event, .free_cb = free_noof, .release_cb = release_noof,
    .opts = &noof_opts, .defaults_str = "",
};
