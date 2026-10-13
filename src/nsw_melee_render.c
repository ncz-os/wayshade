/* SPDX-License-Identifier: Apache-2.0 */
/* nsw_melee_render.c - draws the Melee world (nsw_melee_sim.c) as vector
 * line art: every hull, shot, spark and piece of wreckage is a list of line
 * segments. Each segment is drawn sharp at the output resolution and again
 * as a soft glow into a smaller buffer (half size, a quarter on weak GPUs)
 * that is added underneath, so edges never soften. Widths are in screen
 * units (a 1080-line reference scaled by the frame), so a close zoom shows
 * more detail rather than fatter lines: the hull lattice, damage and engine
 * detail fade in as a hull grows on screen. The camera is nsw_melee_camera.c.
 */
#define _POSIX_C_SOURCE 200809L
#include "nsw_melee_render.h"
#include "nsw_melee_camera.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ncz_gl.h"
#include "ncz_platform.h"

#define MAXL 12288
#define PI 3.14159265f

typedef struct { float x0, y0, x1, y1, r, g, b, a, w, gr; } LineInst;

struct NswRender {
    GLuint p_sharp, p_glow, p_bg, p_comp;
    /* uniform locations, looked up once */
    GLint u_line[2][3];  /* [sharp, glow][target, scale, glow] */
    GLint u_bg_res, u_bg_cam, u_bg_rot, u_bg_ta, u_bg_tb, u_bg_q, u_bg_seed, u_comp_tex, u_comp_res, u_comp_gain;
    GLuint quad_vbo, inst_vbo;
    GLuint glow_tex, glow_fbo;
    int gw, gh, w, h, weak;
    int overflow_logged;
    float glow_scale;
    LineInst *lines;
    int nl;
    NswCam cam;
    NswLook look;
    float lag;           /* draw moving things this far behind the stepped state */
    float k;             /* output pixels per 1080-line pixel */
    float seed;
};

static void hsl(float h, float s, float l, float out[3]) {
    h = h - floorf(h);
    for (int i = 0; i < 3; i++) {
        float t = h + (1 - i) / 3.f;
        t -= floorf(t);
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q, v;
        if (t < 1.f / 6) v = p + (q - p) * 6 * t;
        else if (t < 0.5f) v = q;
        else if (t < 2.f / 3) v = p + (q - p) * (2.f / 3 - t) * 6;
        else v = p;
        out[i] = v;
    }
}

/* The installed copy (NCZ_ASSET_DIR or the shader directory) first, like the
 * classic mode's shaders, then the source tree for runs from a build dir. */
static char *load_src(void) {
    char installed[512];
    if (!ncz_asset_path("neonspacewar-melee.glsl", installed, sizeof installed)) installed[0] = 0;
    const char *p[] = {installed, "vendor/neonspacewar/melee.glsl", "../vendor/neonspacewar/melee.glsl",
                       "../../vendor/neonspacewar/melee.glsl"};
    for (unsigned i = 0; i < sizeof p / sizeof p[0]; i++) {
        if (!p[i][0]) continue;
        FILE *f = fopen(p[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        rewind(f);
        char *b = n > 0 ? malloc((size_t)n + 1) : NULL;
        if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
        fclose(f);
        b[n] = 0;
        fprintf(stderr, "[diag] neonspacewar melee shader=%s\n", p[i]);
        return b;
    }
    fprintf(stderr, "neonspacewar: cannot locate melee.glsl\n");
    return NULL;
}
static GLuint compile(GLenum type, const char *pass, const char *body) {
    char head[80];
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
        fprintf(stderr, "neonspacewar melee compile %s: %s\n", pass, l);
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
    glBindAttribLocation(p, 1, "a_seg");
    glBindAttribLocation(p, 2, "a_col");
    glBindAttribLocation(p, 3, "a_wid");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char l[4096];
        glGetProgramInfoLog(p, sizeof l, NULL, l);
        fprintf(stderr, "neonspacewar melee link %s: %s\n", fs, l);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void nsw_look_default(NswLook *l) {
    l->cam_fixed = 0;
    l->max_frac = 0.28f;
    l->palette = NSW_PAL_RACE;
    l->glow = 1.f;
    l->trails = 1.f;
}

/* A race hue in the chosen palette. The families keep the races apart by
 * spreading their hues across a narrower band. */
static float rh(const NswRender *r, float h) {
    h -= floorf(h);
    switch (r->look.palette) {
    case NSW_PAL_EMBER: return 0.96f + 0.17f * h;
    case NSW_PAL_ICE: return 0.46f + 0.26f * h;
    case NSW_PAL_PHOSPHOR: return 0.27f + 0.10f * h;
    default: return h;
    }
}

NswRender *nsw_render_create(int weak, uint32_t seed, const NswLook *look) {
    NswRender *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->lines = calloc(MAXL, sizeof(LineInst));
    char *body = load_src();
    if (!r->lines || !body) { free(body); nsw_render_destroy(r); return NULL; }
    r->weak = weak;
    r->k = 1.f;
    r->seed = (float)(seed % 997u);
    if (look) r->look = *look;
    else nsw_look_default(&r->look);
    r->look.glow = isfinite(r->look.glow) ? fminf(fmaxf(r->look.glow, 0.f), 2.f) : 1.f;
    r->look.trails = isfinite(r->look.trails) ? fminf(fmaxf(r->look.trails, 0.f), 2.f) : 1.f;
    if (r->look.palette < NSW_PAL_RACE || r->look.palette > NSW_PAL_PHOSPHOR) r->look.palette = NSW_PAL_RACE;
    nsw_cam_init(&r->cam, r->look.cam_fixed, r->look.max_frac, r->seed);
    r->p_sharp = program(body, "PASS_VS_LINE", "PASS_SHARP");
    r->p_glow = program(body, "PASS_VS_LINE", "PASS_GLOW");
    r->p_bg = program(body, "PASS_VS_FULL", "PASS_BG");
    r->p_comp = program(body, "PASS_VS_FULL", "PASS_COMPOSITE");
    free(body);
    if (!r->p_sharp || !r->p_glow || !r->p_bg || !r->p_comp) { nsw_render_destroy(r); return NULL; }
    GLuint lp[2] = {r->p_sharp, r->p_glow};
    for (int i = 0; i < 2; i++) {
        r->u_line[i][0] = glGetUniformLocation(lp[i], "u_target");
        r->u_line[i][1] = glGetUniformLocation(lp[i], "u_scale");
        r->u_line[i][2] = glGetUniformLocation(lp[i], "u_glow");
    }
    r->u_bg_res = glGetUniformLocation(r->p_bg, "u_res");
    r->u_bg_cam = glGetUniformLocation(r->p_bg, "u_cam");
    r->u_bg_rot = glGetUniformLocation(r->p_bg, "u_rot");
    r->u_bg_ta = glGetUniformLocation(r->p_bg, "u_tint_a");
    r->u_bg_tb = glGetUniformLocation(r->p_bg, "u_tint_b");
    r->u_bg_q = glGetUniformLocation(r->p_bg, "u_quality");
    r->u_bg_seed = glGetUniformLocation(r->p_bg, "u_seed");
    r->u_comp_tex = glGetUniformLocation(r->p_comp, "u_glowtex");
    r->u_comp_res = glGetUniformLocation(r->p_comp, "u_res");
    r->u_comp_gain = glGetUniformLocation(r->p_comp, "u_gain");
    static const float q[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    glGenBuffers(1, &r->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glGenBuffers(1, &r->inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAXL * sizeof(LineInst)), NULL, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return r;
}

void nsw_render_destroy(NswRender *r) {
    if (!r) return;
    GLuint p[] = {r->p_sharp, r->p_glow, r->p_bg, r->p_comp};
    for (unsigned i = 0; i < 4; i++) if (p[i]) glDeleteProgram(p[i]);
    if (r->quad_vbo) glDeleteBuffers(1, &r->quad_vbo);
    if (r->inst_vbo) glDeleteBuffers(1, &r->inst_vbo);
    if (r->glow_tex) glDeleteTextures(1, &r->glow_tex);
    if (r->glow_fbo) glDeleteFramebuffers(1, &r->glow_fbo);
    free(r->lines);
    free(r);
}

static void make_glow(NswRender *r, int w, int h) {
    r->glow_scale = r->weak ? 0.25f : 0.5f;
    int gw = (int)(w * r->glow_scale), gh = (int)(h * r->glow_scale);
    if (gw < 16) gw = 16;
    if (gh < 16) gh = 16;
    if (r->glow_tex && gw == r->gw && gh == r->gh) return;
    if (r->glow_tex) glDeleteTextures(1, &r->glow_tex);
    if (!r->glow_fbo) glGenFramebuffers(1, &r->glow_fbo);
    glGenTextures(1, &r->glow_tex);
    glBindTexture(GL_TEXTURE_2D, r->glow_tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, gw, gh);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, r->glow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->glow_tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "neonspacewar melee: glow framebuffer incomplete (0x%x); no glow\n", st);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    r->gw = gw;
    r->gh = gh;
}

/* ---------------------------------------------------------------- camera */
static void world_to_px(const NswRender *r, float x, float y, float *px, float *py) {
    nsw_cam_to_px(&r->cam, r->w, r->h, x, y, px, py);
}
/* Where a moving thing is drawn this frame (see nsw_lag). */
static NswV2 at(const NswRender *r, NswV2 p, NswV2 v) { return nsw_cam_lagged(p, v, r->lag); }

/* ---------------------------------------------------------------- lines */
static void line_px(NswRender *r, float x0, float y0, float x1, float y1, const float c[3], float a, float wpx, float glow) {
    if (a <= 0.002f) return;
    if (r->nl >= MAXL) {
        if (!r->overflow_logged) { fprintf(stderr, "[diag] neonspacewar melee: over %d lines in a frame; the rest dropped\n", MAXL); r->overflow_logged = 1; }
        return;
    }
    LineInst *L = &r->lines[r->nl++];
    L->x0 = x0; L->y0 = y0; L->x1 = x1; L->y1 = y1;
    L->r = c[0]; L->g = c[1]; L->b = c[2]; L->a = a;
    L->w = wpx * r->k; L->gr = glow * r->k;
}
static void line_w(NswRender *r, NswV2 a, NswV2 b, const float c[3], float al, float wpx, float glow) {
    float x0, y0, x1, y1;
    world_to_px(r, a.x, a.y, &x0, &y0);
    world_to_px(r, b.x, b.y, &x1, &y1);
    line_px(r, x0, y0, x1, y1, c, al, wpx, glow);
}
static NswV2 rot2(NswV2 p, float a) {
    NswV2 q = {cosf(a) * p.x - sinf(a) * p.y, sinf(a) * p.x + cosf(a) * p.y};
    return q;
}
static NswV2 P(float x, float y) { NswV2 q = {x, y}; return q; }
static void ring(NswRender *r, NswV2 c0, float rad, int k, const float c[3], float al, float wpx, float glow, float rot) {
    for (int i = 0; i < k; i++) {
        float a0 = rot + 2 * PI * i / k, a1 = rot + 2 * PI * (i + 1) / k;
        line_w(r, P(c0.x + rad * cosf(a0), c0.y + rad * sinf(a0)), P(c0.x + rad * cosf(a1), c0.y + rad * sinf(a1)), c, al, wpx, glow);
    }
}

/* Small deterministic noise for flicker and cracks (no state, no rand()). */
static float hash1(unsigned a, unsigned b) {
    unsigned h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffu) / 65535.f;
}

/* A hull in its damage state. Outline segments (bright >= 0.9) carry the
 * silhouette and are drawn at full strength with a tight glow, so the shape
 * reads with the glow off. The lattice inside fades from 60% hull down to
 * 30% and, like the engine detail, only appears once the hull is large
 * enough on screen to show it. Below 30% the outline flickers between full
 * and 55% brightness (never off, at most five changes a second), dim a
 * quarter of the time at stage 1 and three quarters at stage 3 (under 10%),
 * and shot-away segments are gone. A cloaked hull is a faint ghost with a ripple, not invisible. */
static void draw_hull(NswRender *r, const NswWorld *w, const NswShip *sh, NswV2 sp, const float c[3], float appear) {
    const NswRace *race = &NSW_RACE[sh->race];
    float hpf = fmaxf(sh->hp / race->hp, 0.f);
    float px_len = 2.f * race->scale * r->cam.zoom / r->k;
    float lod = fminf(1.f, fmaxf(0.f, (px_len - 30.f) / 30.f));
    float lattice = fminf(1.f, fmaxf(0.f, (hpf - 0.3f) / 0.3f)) * lod;
    float ghost = sh->cloak_t > 0 ? 0.3f : 1.f;
    unsigned tick = (unsigned)fmod(w->t * 5.0, 4294967296.0);   /* flicker: at most 5 changes a second */
    for (int k = 0; k < race->nhull; k++) {
        if (sh->lost[k >> 6] >> (k & 63) & 1u) continue;
        const NswSeg *g = &race->hull[k];
        int outline = g->bright >= 0.9f;
        float al = outline ? 1.f : 0.35f + 0.5f * g->bright;
        if (!outline) al *= lattice;
        if (outline && sh->dmg_stage > 0) al *= hash1((unsigned)k + 17u * (unsigned)sh->race, tick) < 0.25f * sh->dmg_stage ? 0.55f : 1.f;
        al *= appear * ghost;
        if (al < 0.01f) continue;
        float rip = sh->cloak_t > 0 ? 0.08f * race->scale * sinf((float)w->t * 9.f + k) : 0.f;
        NswV2 a = rot2(P(g->x0 * race->scale + rip, g->y0 * race->scale), sh->a);
        NswV2 b = rot2(P(g->x1 * race->scale + rip, g->y1 * race->scale), sh->a);
        line_w(r, P(sp.x + a.x, sp.y + a.y), P(sp.x + b.x, sp.y + b.y), c, al, outline ? 2.4f : 1.5f, outline ? 4.f : 2.5f);
    }
}

/* A shield is a second outline around the hull that cracks as it is hit. */
static void draw_shield(NswRender *r, const NswShip *sh, NswV2 sp) {
    const NswRace *race = &NSW_RACE[sh->race];
    float c[3];
    hsl(rh(r, race->hue + 0.45f), 0.5f, 0.8f, c);
    float fade = fminf(1.f, sh->shield_t / 0.4f);
    for (int k = 0; k < race->nhull; k++) {
        const NswSeg *g = &race->hull[k];
        if (g->bright < 0.9f) continue;
        if (hash1((unsigned)k, 7u) < 0.18f * sh->shield_cracks) continue;   /* cracked away */
        float s1 = race->scale * 1.35f;
        NswV2 a = rot2(P(g->x0 * s1, g->y0 * s1), sh->a), b = rot2(P(g->x1 * s1, g->y1 * s1), sh->a);
        line_w(r, P(sp.x + a.x, sp.y + a.y), P(sp.x + b.x, sp.y + b.y), c, 0.75f * fade, 1.6f, 5);
    }
}

static void draw_world(NswRender *r, const NswWorld *w) {
    float c[3], white[3] = {1, 1, 1};
    /* The arena boundary: dim dashes, warmer and brighter while it closes. The
     * planet: a wire globe that turns. */
    float closing = w->phase == PH_FIGHT && w->calm_t > 12.f ? 1.f : 0.f;
    hsl(0.6f - 0.55f * closing * w->aggro, 0.4f + 0.4f * closing, 0.35f + 0.15f * closing, c);
    for (int i = 0; i < 96; i += 2) {
        float ar = w->arena_r, a0 = 2 * PI * i / 96 + (float)w->t * 0.05f * closing, a1 = a0 + 2 * PI / 96;
        line_w(r, P(ar * cosf(a0), ar * sinf(a0)), P(ar * cosf(a1), ar * sinf(a1)), c, 0.35f + 0.3f * closing, 1.2f, 4);
    }
    hsl(0.58f, 0.5f, 0.55f, c);
    ring(r, P(0, 0), w->planet_r, 40, c, 0.55f, 1.4f, 5, 0);   /* the stage, not the star: dimmer than any hull */
    float spin = (float)w->t * 0.15f;
    for (int m = 0; m < 5; m++) {
        float ph = spin + m * PI / 5, sx = cosf(ph);
        for (int i = 0; i < 16; i++) {
            float a0 = -PI / 2 + PI * i / 16, a1 = -PI / 2 + PI * (i + 1) / 16;
            line_w(r, P(w->planet_r * sx * cosf(a0), w->planet_r * sinf(a0)), P(w->planet_r * sx * cosf(a1), w->planet_r * sinf(a1)),
                   c, 0.22f, 1.0f, 2);
        }
    }
    for (int j = 1; j < 4; j++) {
        float y = w->planet_r * (j - 2) * 0.5f, x = sqrtf(w->planet_r * w->planet_r - y * y);
        line_w(r, P(-x, y), P(x, y), c, 0.2f, 1.0f, 2);
    }
    /* Gravity field lines: faint spokes that shorten toward the planet. */
    hsl(0.62f, 0.4f, 0.4f, c);
    for (int i = 0; i < 24; i++) {
        float a = 2 * PI * i / 24 + spin * 0.3f;
        line_w(r, P(cosf(a) * w->planet_r * 1.4f, sinf(a) * w->planet_r * 1.4f), P(cosf(a) * w->planet_r * 2.6f, sinf(a) * w->planet_r * 2.6f),
               c, 0.12f, 1.0f, 2);
    }

    /* Debris: the hulls' own segments tumbling apart. */
    for (int i = 0; i < NSW_MAX_DEBRIS; i++) {
        const NswDebris *d = &w->debris[i];
        if (!d->alive) continue;
        float f = d->life / d->max_life;
        hsl(rh(r, d->hue + 0.03f * (1 - f)), 0.9f, 0.55f, c);
        NswV2 a = rot2(P(d->seg.x0, d->seg.y0), d->a), b = rot2(P(d->seg.x1, d->seg.y1), d->a), dp = at(r, d->p, d->v);
        line_w(r, P(dp.x + a.x, dp.y + a.y), P(dp.x + b.x, dp.y + b.y), c, f * (0.4f + 0.6f * d->seg.bright), 1.8f, 7 * f);
    }
    /* Particles: short streaks along their motion. */
    for (int i = 0; i < NSW_MAX_PARTS; i++) {
        const NswPart *q = &w->part[i];
        if (!q->alive) continue;
        float f = q->life / q->max_life;
        hsl(rh(r, q->hue), 0.9f, 0.45f + 0.15f * f, c);
        NswV2 qp = at(r, q->p, q->v), tail = P(qp.x - q->v.x * 0.05f, qp.y - q->v.y * 0.05f);
        line_w(r, tail, qp, c, f * q->bright, 1.4f, 5);
    }
    /* Ship trails, fading back along the path. */
    for (int s = 0; s < w->nships; s++) {
        const NswShip *sh = &w->ship[s];
        if (!sh->alive || sh->ntrail < 2 || sh->cloak_t > 0) continue;
        hsl(rh(r, NSW_RACE[sh->race].hue), 0.9f, 0.5f, c);
        for (int i = 1; i < sh->ntrail; i++) {
            float f = (float)i / sh->ntrail;
            line_w(r, sh->trail[i - 1], sh->trail[i], c, 0.35f * f * f * r->look.trails, 1.2f + 1.2f * f, 5);
        }
        line_w(r, sh->trail[sh->ntrail - 1], at(r, sh->p, sh->v), c, 0.35f * r->look.trails, 2.4f, 5);
    }
    /* Shots. */
    for (int i = 0; i < NSW_MAX_SHOTS; i++) {
        const NswShot *q = &w->shot[i];
        if (!q->alive) continue;
        const NswRace *race = &NSW_RACE[w->ship[q->owner].race];
        hsl(rh(r, race->hue + 0.04f), 1.f, 0.62f, c);
        NswShot qs = *q;
        qs.p = at(r, q->p, q->v);
        q = &qs;
        NswV2 v = q->v;
        float sp = sqrtf(v.x * v.x + v.y * v.y) + 1e-5f;
        NswV2 d = P(v.x / sp, v.y / sp);
        switch (q->type) {
        case SH_BOLT: case SH_PELLET:
            line_w(r, P(q->p.x - d.x * 0.03f, q->p.y - d.y * 0.03f), q->p, c, 1.f, 2.2f, 7);
            break;
        case SH_LANCE:
            line_w(r, P(q->p.x - d.x * 0.14f, q->p.y - d.y * 0.14f), q->p, white, 1.f, 2.0f, 10);
            line_w(r, P(q->p.x - d.x * 0.14f, q->p.y - d.y * 0.14f), q->p, c, 1.f, 4.0f, 12);
            break;
        case SH_PLASMA:
            ring(r, q->p, 0.016f, 8, c, 1.f, 2.2f, 16, (float)w->t * 4);
            ring(r, q->p, 0.008f, 6, white, 0.9f, 1.6f, 6, -(float)w->t * 6);
            break;
        case SH_DART: case SH_BOOMERANG:
            for (int k = 1; k < q->nprev; k++) line_w(r, q->prev[k - 1], q->prev[k], c, 0.2f + 0.8f * k / q->nprev, 1.6f, 5);
            if (q->type == SH_BOOMERANG) {
                float a = (float)w->t * 14;
                NswV2 e0 = rot2(P(0.016f, 0), a), e1 = rot2(P(0.016f, 0), a + 2.3f), e2 = rot2(P(0.016f, 0), a - 2.3f);
                line_w(r, P(q->p.x + e1.x, q->p.y + e1.y), P(q->p.x + e0.x, q->p.y + e0.y), c, 1, 2, 6);
                line_w(r, P(q->p.x + e0.x, q->p.y + e0.y), P(q->p.x + e2.x, q->p.y + e2.y), c, 1, 2, 6);
            }
            break;
        case SH_MINE: {
            float arm = q->age > 1.f ? 0.65f + 0.35f * sinf(q->age * 3.f) : 0.35f;   /* a slow pulse, under 1 Hz */
            ring(r, q->p, 0.014f, 4, c, arm, 1.6f, 6, q->age);
            ring(r, q->p, 0.006f, 4, c, arm, 1.2f, 3, -q->age);
            break;
        }
        case SH_DRONE:
            ring(r, q->p, 0.011f, 3, c, 1, 1.6f, 6, q->age * 3);
            break;
        }
    }
    /* Effects. */
    for (int i = 0; i < NSW_MAX_FX; i++) {
        const NswFx *f = &w->fx[i];
        if (!f->alive) continue;
        float u = f->t / f->dur;
        hsl(rh(r, f->hue), 0.9f, 0.65f, c);
        switch (f->type) {
        case FX_CHAIN: {
            /* A jagged arc between the two ships, re-drawn along the same kinks. */
            NswV2 a = f->p0, b = f->p1, prev = a;
            NswV2 d = P(b.x - a.x, b.y - a.y), nrm = P(-d.y, d.x);
            float L = sqrtf(d.x * d.x + d.y * d.y) + 1e-5f;
            nrm = P(nrm.x / L, nrm.y / L);
            for (int k = 1; k <= 8; k++) {
                float t = k / 8.f, j = k == 8 ? 0 : sinf(k * 7.3f + i * 3.1f) * 0.035f;
                NswV2 p = P(a.x + d.x * t + nrm.x * j, a.y + d.y * t + nrm.y * j);
                line_w(r, prev, p, white, 1 - u, 1.6f, 4);
                line_w(r, prev, p, c, 1 - u, 3.0f, 10);
                prev = p;
            }
            break;
        }
        case FX_REPULSE: ring(r, f->p0, 0.5f * u, 32, c, 1 - u, 2.4f, 10, 0); break;
        case FX_BLINK:
            ring(r, f->p0, 0.02f + 0.08f * u, 12, c, 1 - u, 1.6f, 6, 0);
            ring(r, f->p1, 0.1f * (1 - u), 12, c, 1 - u, 1.6f, 6, 0);
            break;
        case FX_EXPLOSION:
            ring(r, f->p0, 0.03f + 0.25f * u, 24, c, (1 - u) * 0.9f, 2.4f, 14, u);
            ring(r, f->p0, 0.02f + 0.12f * u, 16, white, (1 - u) * 0.8f, 1.6f, 10, -u);
            break;
        case FX_SHIELD_HIT: ring(r, f->p0, 0.012f, 6, white, 1 - u, 1.4f, 5, 0); break;
        default: break;
        }
    }
    /* Ships. */
    for (int s = 0; s < w->nships; s++) {
        const NswShip *sh = &w->ship[s];
        if (!sh->alive) continue;
        const NswRace *race = &NSW_RACE[sh->race];
        NswV2 sp = at(r, sh->p, sh->v);
        float appear = 1.f;
        if (w->phase == PH_INTRO) appear = fminf(1.f, fmaxf(0.f, (w->phase_t - 0.6f) / 1.0f));
        hsl(rh(r, race->hue), 0.95f, sh->hit_t > 0 ? 0.75f : 0.58f, c);
        if (appear < 1.f) {
            /* Arrival from hyperspace: a streak collapsing onto the hull. */
            NswV2 back = P(-cosf(sh->a) * (1 - appear) * 0.8f, -sinf(sh->a) * (1 - appear) * 0.8f);
            line_w(r, P(sp.x + back.x, sp.y + back.y), sp, c, appear * 0.9f, 2.4f, 10);
        }
        draw_hull(r, w, sh, sp, c, appear);
        float px_len = 2.f * race->scale * r->cam.zoom / r->k;   /* hull length in 1080-line pixels */
        float detail = fminf(1.f, fmaxf(0.f, (px_len - 60.f) / 60.f));
        float cloak = sh->cloak_t > 0 ? 0.2f : 1.f;
        if (sh->thrusting && appear >= 1.f) {
            float flame[3];
            hsl(rh(r, race->hue + 0.08f), 1.f, 0.7f, flame);
            for (int k = 0; k < race->nengine; k++) {
                NswV2 e = rot2(P(race->engine[k].x * race->scale, race->engine[k].y * race->scale), sh->a);
                float fl = race->scale * (0.5f + 0.25f * sinf((float)w->t * 23.f + k));
                NswV2 t = rot2(P(-fl, 0), sh->a);
                line_w(r, P(sp.x + e.x, sp.y + e.y), P(sp.x + e.x + t.x, sp.y + e.y + t.y), flame, cloak, 2.2f, 6);
                if (detail > 0) {   /* close up: an inner core to the flame */
                    NswV2 t2 = rot2(P(-fl * 0.55f, 0), sh->a);
                    line_w(r, P(sp.x + e.x, sp.y + e.y), P(sp.x + e.x + t2.x, sp.y + e.y + t2.y), white, cloak * detail * 0.8f, 1.2f, 0);
                }
            }
        }
        if (sh->shield_t > 0) draw_shield(r, sh, sp);
        if (sh->tractor_t > 0 && sh->tractor_target >= 0 && w->ship[sh->tractor_target].alive) {
            NswV2 a = sp, b = at(r, w->ship[sh->tractor_target].p, w->ship[sh->tractor_target].v);
            for (int k = 0; k < 10; k += 2)
                line_w(r, P(a.x + (b.x - a.x) * k / 10.f, a.y + (b.y - a.y) * k / 10.f),
                       P(a.x + (b.x - a.x) * (k + 1) / 10.f, a.y + (b.y - a.y) * (k + 1) / 10.f), c, 0.7f, 1.4f, 6);
        }
    }
    /* Title card during the arrival: each side's emblem, large and geometric. */
    if (w->phase == PH_INTRO) {
        float u = w->phase_t / 2.4f, fade = fminf(1.f, fminf(u * 4.f, (1 - u) * 4.f));
        int sides = w->nships == 4 ? 2 : w->nships;
        for (int i = 0; i < sides; i++) {
            const NswRace *race = &NSW_RACE[w->ship[i].race];
            float cx = r->w * (sides == 2 ? (i == 0 ? 0.28f : 0.72f) : 0.2f + 0.3f * i), cy = r->h * 0.5f;
            float rad = r->h * 0.11f;
            hsl(rh(r, race->hue), 0.95f, 0.6f, c);
            for (int k = 0; k < race->nemblem; k++) {
                const NswSeg *g = &race->emblem[k];
                line_px(r, cx + g->x0 * rad, cy + g->y0 * rad, cx + g->x1 * rad, cy + g->y1 * rad, c, fade, 3.0f, 12);
            }
        }
        /* Between the sides: two short opposed bars, not a word. */
        float mid = r->w * 0.5f;
        line_px(r, mid - r->h * 0.03f, r->h * 0.47f, mid + r->h * 0.03f, r->h * 0.53f, white, fade * 0.8f, 2.4f, 8);
        line_px(r, mid - r->h * 0.03f, r->h * 0.53f, mid + r->h * 0.03f, r->h * 0.47f, white, fade * 0.8f, 2.4f, 8);
    }
}

static void draw_lines(NswRender *r, int glow_pass, float scale, int tw, int th) {
    if (r->nl == 0) return;
    const GLint *u = r->u_line[glow_pass];
    glUseProgram(glow_pass ? r->p_glow : r->p_sharp);
    glUniform2f(u[0], (float)tw, (float)th);
    glUniform1f(u[1], scale);
    glUniform1f(u[2], glow_pass ? 1.f : 0.f);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    glDisableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    GLsizei st = sizeof(LineInst);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, st, (void *)offsetof(LineInst, x0));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, st, (void *)offsetof(LineInst, r));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, st, (void *)offsetof(LineInst, w));
    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, r->nl);
    glVertexAttribDivisor(1, 0);
    glVertexAttribDivisor(2, 0);
    glVertexAttribDivisor(3, 0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
}

static void full_quad(NswRender *r) {
    glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
}

void nsw_render_draw(NswRender *r, const NswWorld *w, int width, int height, float dt) {
    r->w = width;
    r->h = height;
    r->k = fminf((float)width, (float)height) / 1080.f;
    r->lag = nsw_lag(w);
    make_glow(r, width, height);
    nsw_cam_update(&r->cam, w, width, height, dt);
    r->nl = 0;
    draw_world(r, w);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    if (r->nl > 0) glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(r->nl * sizeof(LineInst)), r->lines);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    /* Glow buffer. */
    glBindFramebuffer(GL_FRAMEBUFFER, r->glow_fbo);
    glViewport(0, 0, r->gw, r->gh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    draw_lines(r, 1, r->glow_scale, r->gw, r->gh);
    /* Output: space, then the glow, then the sharp lines. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glUseProgram(r->p_bg);
    glUniform2f(r->u_bg_res, (float)width, (float)height);
    glUniform3f(r->u_bg_cam, r->cam.cx, r->cam.cy, r->cam.zoom);
    glUniform1f(r->u_bg_rot, -r->cam.rot);
    float ta[3], tb[3];
    hsl(rh(r, NSW_RACE[w->matchup[0]].hue), 0.7f, 0.35f, ta);
    hsl(rh(r, NSW_RACE[w->matchup[w->nships > 1 ? 1 : 0]].hue), 0.7f, 0.35f, tb);
    glUniform3fv(r->u_bg_ta, 1, ta);
    glUniform3fv(r->u_bg_tb, 1, tb);
    glUniform1f(r->u_bg_q, r->weak ? 0.f : 1.f);
    glUniform1f(r->u_bg_seed, r->seed);
    full_quad(r);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(r->p_comp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->glow_tex);
    glUniform1i(r->u_comp_tex, 0);
    glUniform2f(r->u_comp_res, (float)width, (float)height);
    glUniform1f(r->u_comp_gain, r->look.glow);
    full_quad(r);
    draw_lines(r, 0, 1.f, width, height);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

int nsw_render_lines(const NswRender *r) { return r ? r->nl : 0; }
