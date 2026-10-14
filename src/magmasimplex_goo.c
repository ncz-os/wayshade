/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_goo.c - GL side of engine=goo: runs three layers of the wax
 * simulation (magmasimplex_goo_sim.c) at different depths, schedules the
 * views (inside the lamp, eye against the glass, liquid light show, classic
 * field) as slow chapters, and draws vendor/magmasimplex/goo.frag in one
 * full-screen pass. Settings: magmasimplex_goo_cfg.h. Design and change log:
 * docs/MAGMASIMPLEX-60S.md. */
#define _POSIX_C_SOURCE 200809L
#include "magmasimplex_goo.h"
#include "magmasimplex_goo_sim.h"
#include "magmasimplex_beat.h"
#include "magmasimplex_pattern.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ncz_gl.h"

#define LAYERS 3
#define PERL 12 /* bodies per layer the shader takes */
#define PERS 6  /* strands per layer */
#define FADE_S 30.0
#define WARM_SIM_S 120.f /* startup: simulated seconds from cold to steady */
#define LAYER_SALT 0x51ED27u

/* Depth layers: far, mid, near. Scale is how much larger the layer's
 * simulation appears (the spread grows with the depth setting); drift is
 * how far its camera wanders. */
static const float LSCALE[LAYERS] = {1.02f, 1.5f, 2.6f};
static const float LBASEY[LAYERS] = {0.02f, 0.05f, 0.10f};
static const float LDRIFTX[LAYERS] = {0.10f, 0.25f, 0.45f};
static const float LDRIFTY[LAYERS] = {0.04f, 0.10f, 0.22f};

struct MsGoo {
    GLuint prog, vbo;
    GLint u_res, u_time, u_seed, u_quality, u_exposure, u_flicker, u_ball, u_shape, u_sa, u_sb, u_nl, u_ns, u_cam,
        u_world, u_pool, u_cap, u_liquid, u_wax, u_secondary, u_bulb, u_modeA, u_modeB, u_mmix, u_kal, u_look, u_look2,
        u_mist, u_mask, u_mask_size, u_overlay, u_poster, u_dendrites, u_dendrite, u_pattern, u_warp, u_heat_x;
    /* Beat/audio layer (all views; docs/AUDIO-BUS.md). */
    GLint u_dyn, u_beat, u_snare, u_bass, u_mid, u_phase, u_radius, u_squeeze, u_burst, u_flow, u_breath, u_bloom, u_glow;
    MsBeat *beat;
    float pattern[4], warp[4];
    float heat_r;   /* render hot-spot x for tank/dish/drift lamps */
    /* Seeded pool geometry: surface level offset, wave amplitude multiplier
     * and wave/cap phase, so the lamp's pool and cap are not the same shape
     * for every seed (the field view is otherwise dominated by them). */
    float pool[3];
    MsGooConfig cfg;
    MsGooSim sim[LAYERS];
    int sim_ready;
    uint32_t seed, rng;
    double prev, diag_last, el;
    float seedf, phase[8], dish_angle;
    int palette_fixed, chapter_no;
    /* chapters */
    int modeA, modeB;
    double fade_start, next_change;
    MsPalette palA, palB;
};

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static float clampf(float v, float a, float b) {
    if (!(v == v)) return a; /* NaN */
    return v < a ? a : v > b ? b : v;
}
static float urandf(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xffffffu) / 16777216.f;
}
static float frandg(MsGoo *g, float a, float b) { return a + (b - a) * urandf(&g->rng); }

static char *load_goo_shader(void) {
    const char *p[] = {"vendor/magmasimplex/goo.frag", "../vendor/magmasimplex/goo.frag",
                       "../../vendor/magmasimplex/goo.frag",
                       "/usr/share/ncz-screensavers/shaders/magmasimplex/goo.frag"};
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
        /* GLSL ES requires #version on the first line; strict compilers (Arm
         * Mali) reject the licence comment above it. Start at #version. */
        char *ver = strstr(b, "#version");
        if (ver && ver != b) memmove(b, ver, strlen(ver) + 1);
        fprintf(stderr, "[diag] magmasimplex shader=%s\n", p[i]);
        return b;
    }
    fprintf(stderr, "magmasimplex: cannot locate goo.frag\n");
    return NULL;
}
static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char l[4096];
        glGetShaderInfoLog(s, sizeof l, NULL, l);
        fprintf(stderr, "magmasimplex goo compile: %s\n", l);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* Simulation aspect for a world: the width of simulation space the screen
 * shows, so bodies spread over what is seen. */
static float sim_asp(const MsGoo *g, float screen_asp) {
    /* The dish shows the middle of a 4:3 column turned on its side: a column
     * as narrow as the screen is tall (0.56) jams (blob gate, 45 of 120
     * runs failed), and a square one still failed 13. */
    if (g->cfg.world == MS_WORLD_DISH) return 4.f / 3.f;
    if (g->cfg.world == MS_WORLD_RADIAL) return 1.78f;
    return screen_asp;
}

static int is_show(int m) { return m == MS_VIEW_SHOW || m == MS_VIEW_BUTTERFLY; }
/* Map the render view onto the beat adapter's per-mode mapping. */
static int beat_mode(int view) {
    switch (view) {
    case MS_VIEW_GLASS: return MS_BEAT_MODE_LAVA_GLASS;
    case MS_VIEW_SHOW: return MS_BEAT_MODE_SHOW;
    case MS_VIEW_FIELD: return MS_BEAT_MODE_LAVA_FIELD;
    case MS_VIEW_BUTTERFLY: return MS_BEAT_MODE_BUTTERFLY;
    default: return MS_BEAT_MODE_LAVA_INSIDE;
    }
}
/* Red-heavy saturated palettes get the harder limiter damping. */
static int red_heavy_pal(const MsPalette *p) {
    float r = p->liquid[0] + p->wax[0] + p->secondary[0];
    float g = p->liquid[1] + p->wax[1] + p->secondary[1];
    float b = p->liquid[2] + p->wax[2] + p->secondary[2];
    return (r > 0.9f && r > 1.6f * g && r > 1.6f * b);
}
static double chapter_len(MsGoo *g, int mode) {
    double s = is_show(mode) ? frandg(g, 120.f, 240.f) : frandg(g, 360.f, 600.f);
    return s * g->cfg.chapter;
}
/* Chapter transitions: a light show (the ink show, or the Flowerchild dye
 * show, 2026-10-05) alternates with the lamp views; from glass or field the
 * lamp returns inside (the default immersive view). */
static int next_mode(MsGoo *g, int m) {
    float r = frandg(g, 0, 1);
    if (is_show(m)) return r < 0.55f ? MS_VIEW_INSIDE : (r < 0.85f ? MS_VIEW_GLASS : MS_VIEW_FIELD);
    if (r < 0.45f) return r < 0.29f ? MS_VIEW_SHOW : MS_VIEW_BUTTERFLY;
    if (m == MS_VIEW_INSIDE) return r < 0.8f ? MS_VIEW_GLASS : MS_VIEW_FIELD;
    return MS_VIEW_INSIDE;
}

/* A new palette for the next chapter, from the same era, or the same one. */
static void chapter_palette(MsGoo *g) {
    g->palB = g->palA;
    if (g->palette_fixed || frandg(g, 0, 1) >= 0.5f) return;
    MsGooConfig c = g->cfg;
    c.palette = MS_UNSET_I;
    c.randomize = 1;
    ms_goo_cfg_resolve(&c, g->seed + 7919u * (uint32_t)(++g->chapter_no));
    ms_goo_cfg_palette(&c, g->seed + 7919u * (uint32_t)g->chapter_no, &g->palB);
}

MsGoo *ms_goo_create(uint32_t seed, const MsGooConfig *cfg, int palette_fixed) {
    MsGoo *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->cfg = *cfg;
    if (g->cfg.view < MS_VIEW_AUTO || g->cfg.view > MS_VIEW_BUTTERFLY) g->cfg.view = MS_VIEW_AUTO;
    if (g->cfg.world < 0 || g->cfg.world > MS_WORLD_DRIFT) g->cfg.world = MS_WORLD_TANK;
    g->seed = seed;
    g->rng = seed ? seed * 2654435761u + 12345u : 0x9E3779B9u;
    g->seedf = (float)(seed & 0xffffu) * 0.001f;
    g->palette_fixed = palette_fixed;
    ms_goo_cfg_palette(&g->cfg, seed, &g->palA);
    g->palB = g->palA;
    g->dish_angle = frandg(g, 0, 1) < 0.5f ? -1.5707963f : 1.5707963f;
    for (int i = 0; i < 8; i++) g->phase[i] = frandg(g, 0, 6.2831853f);
    /* Light-show field layout (Flowerchild and the ink show): bounded and
     * seed-derived, so the same seed reproduces and every other seed lays the
     * dye out differently. Lamps (inside/glass/field) do not read these. */
    ms_goo_pattern_seed(seed, g->pattern, g->warp);
    {
        uint32_t ps = seed * 0x9E3779B1u + 0x0ABCDEFu;
        ps ^= ps >> 16; ps *= 0x7FEB352Du; ps ^= ps >> 15; ps *= 0x846CA68Bu; ps ^= ps >> 16;
        float p0 = (float)(ps & 0xffffu) / 65535.f;
        ps = ps * 1664525u + 1013904223u;
        float p1 = (float)(ps & 0xffffu) / 65535.f;
        ps = ps * 1664525u + 1013904223u;
        float p2 = (float)(ps & 0xffffu) / 65535.f;
        g->pool[0] = (p0 - 0.5f) * 0.08f;   /* surface level: +-0.04 */
        g->pool[1] = 0.80f + 0.40f * p1;    /* wave amplitude: 0.80..1.20 */
        g->pool[2] = p2 * 6.2831853f;       /* wave/cap phase */
        g->heat_r = (p0 - 0.5f) * 0.30f;    /* hot spot x: +-0.15 */
    }
    /* The radial world (amber-glow) is the operator's liked hot-centre look and
     * its realism gate is sensitive to the liquid geometry: keep it exactly as
     * it was, and draw only the tank/dish/drift lamps from the seed. */
    if (g->cfg.world == MS_WORLD_RADIAL) {
        g->pattern[0] = g->pattern[1] = 0.f;
        g->pool[0] = 0.f; g->pool[1] = 1.f; g->pool[2] = 0.f;
    }
    float r0 = frandg(g, 0, 1);
    g->modeA = g->cfg.view >= 0 ? g->cfg.view
                                : (r0 < 0.6f ? MS_VIEW_INSIDE : (r0 < 0.85f ? MS_VIEW_GLASS : MS_VIEW_SHOW));
    g->modeB = -1;

    char *fs = load_goo_shader();
    if (!fs) { free(g); return NULL; }
    static const char *vs = "#version 300 es\nlayout(location=0) in vec2 a_pos;\nvoid main(){gl_Position=vec4(a_pos,0.0,1.0);}\n";
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    free(fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); free(g); return NULL; }
    g->prog = glCreateProgram();
    glAttachShader(g->prog, v);
    glAttachShader(g->prog, f);
    glBindAttribLocation(g->prog, 0, "a_pos");
    glLinkProgram(g->prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(g->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char l[4096];
        glGetProgramInfoLog(g->prog, sizeof l, NULL, l);
        fprintf(stderr, "magmasimplex goo link: %s\n", l);
        glDeleteProgram(g->prog);
        free(g);
        return NULL;
    }
#define L(n) g->n = glGetUniformLocation(g->prog, #n)
    L(u_res); L(u_time); L(u_seed); L(u_quality); L(u_exposure); L(u_flicker); L(u_ball); L(u_shape);
    L(u_sa); L(u_sb); L(u_nl); L(u_ns); L(u_cam); L(u_world); L(u_pool); L(u_cap); L(u_liquid); L(u_wax);
    L(u_secondary); L(u_bulb); L(u_modeA); L(u_modeB); L(u_mmix); L(u_kal); L(u_look); L(u_look2); L(u_mist);
    L(u_mask); L(u_mask_size); L(u_overlay); L(u_poster); L(u_dendrites); L(u_dendrite); L(u_pattern); L(u_warp); L(u_heat_x);
    L(u_dyn); L(u_beat); L(u_snare); L(u_bass); L(u_mid); L(u_phase); L(u_radius); L(u_squeeze); L(u_burst);
    L(u_flow); L(u_breath); L(u_bloom); L(u_glow);
#undef L
    static const float q[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    glGenBuffers(1, &g->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* The beat/audio layer (all views). dynamics=0 still creates the engine
     * but every drive is 0, so the frame is unchanged. */
    {
        MsBeatOpts bo;
        bo.dynamics = g->cfg.dynamics;
        bo.tempo = g->cfg.tempo;
        bo.audio = g->cfg.audio;
        g->beat = ms_beat_create(seed, &bo);
    }

    g->prev = g->diag_last = now();
    /* The first chapter is short, so a brief idle still sees a change. */
    g->next_change = g->cfg.view >= 0 ? INFINITY : frandg(g, 60.f, 180.f) * g->cfg.chapter;
    char d[640];
    ms_goo_cfg_describe(&g->cfg, d, sizeof d);
    fprintf(stderr, "[diag] magmasimplex engine=goo quality=%s first-view=%s %s\n", g->cfg.quality ? "full" : "light",
            ms_goo_view_name(g->modeA), d);
    fprintf(stderr, "[diag] magmasimplex beat dynamics=%.2f tempo=%d audio=%s status=%s\n", g->cfg.dynamics, g->cfg.tempo,
            ms_goo_audio_name(g->cfg.audio), ms_beat_audio_status(g->beat));
    return g;
}

/* The simulations start on the first frame, at the real screen aspect. */
static void init_sims(MsGoo *g, float sasp) {
    float asp = sim_asp(g, sasp);
    int per = g->cfg.quality ? PERL : 9;
    for (int l = 0; l < LAYERS; l++) {
        MsGooSim *s = &g->sim[l];
        uint32_t sd = g->seed * (uint32_t)(l * 2 + 1) + LAYER_SALT * (uint32_t)l;
        int multi = frandg(g, 0, 1) < 0.6f;
        if (g->cfg.startup) ms_goo_sim_init_cold(s, sd, per, multi, asp, g->cfg.cap, WARM_SIM_S * (1.f + 0.25f * l));
        else ms_goo_sim_init_cap(s, sd, per, multi, asp, 90.f, g->cfg.cap);
        s->visc_k = g->cfg.viscosity;
        s->heat_k = g->cfg.heat;
        s->wax_k = g->cfg.wax;
    }
    g->sim_ready = 1;
    fprintf(stderr, "[diag] magmasimplex sims asp=%.3f bodies=%d/%d/%d startup=%d\n", asp, ms_goo_sim_alive(&g->sim[0]),
            ms_goo_sim_alive(&g->sim[1]), ms_goo_sim_alive(&g->sim[2]), g->cfg.startup);
}

static void chapters(MsGoo *g, float *mix) {
    double el = g->el;
    *mix = 0.f;
    if (g->cfg.view >= 0) return;
    if (g->modeB < 0 && el >= g->next_change) {
        g->modeB = next_mode(g, g->modeA);
        g->fade_start = el;
        /* The colours change only across a slow cross-fade, and not always. */
        chapter_palette(g);
        fprintf(stderr, "[diag] magmasimplex chapter t=%.0f %s -> %s palette %s -> %s\n", el, ms_goo_view_name(g->modeA),
                ms_goo_view_name(g->modeB), g->palA.id, g->palB.id);
    }
    if (g->modeB >= 0) {
        float x = (float)((el - g->fade_start) / FADE_S);
        if (x >= 1.f) {
            g->modeA = g->modeB;
            g->modeB = -1;
            g->palA = g->palB;
            g->next_change = el + chapter_len(g, g->modeA);
        } else {
            *mix = x * x * (3.f - 2.f * x);
        }
    }
}

static void lerp3(float *o, const float *a, const float *b, float t) {
    for (int c = 0; c < 3; c++) o[c] = a[c] + (b[c] - a[c]) * t;
}

void ms_goo_draw(MsGoo *g, int w, int h) {
    double tn = now();
    float dt = (float)(tn - g->prev);
    g->prev = tn;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    g->el += dt;
    float sasp = (float)w / (float)(h > 0 ? h : 1);
    if (!g->sim_ready) init_sims(g, sasp);
    /* The beat/audio layer: update before the simulation so a flow burst can
     * speed the wax and the dye for this frame. */
    const MsBeatDrive *bd = ms_beat_drive(g->beat);
    if (g->beat) {
        ms_beat_set_mode(g->beat, beat_mode(g->modeA), g->cfg.world);
        ms_beat_set_red_heavy(g->beat, red_heavy_pal(&g->palA));
        ms_beat_update(g->beat, dt);
        bd = ms_beat_drive(g->beat);
    }
    /* Simulated time runs at the real-lamp pace times the speed setting, plus
     * a bounded beat flow burst. */
    float sdt = dt * g->cfg.speed * ms_goo_time_scale() * (1.0f + 0.5f * bd->flow);
    for (int l = 0; l < LAYERS; l++) {
        g->sim[l].asp = sim_asp(g, sasp);
        for (int k = 0; k < 2; k++) ms_goo_sim_step(&g->sim[l], sdt * 0.5f);
    }
    float mix;
    chapters(g, &mix);
    float st = (float)g->sim[1].t;   /* simulated time: wax motion */
    float t = (float)g->el;          /* wall time: liquid, light, camera */
    if (tn - g->diag_last >= 10.0) {
        fprintf(stderr, "[diag] magmasimplex blobs t=%.0f sim=%.0f visible=%d/%d/%d bodies=%d/%d/%d snaps=%lu drips=%lu warm=%.2f\n",
                g->el, st, ms_goo_sim_visible(&g->sim[0]), ms_goo_sim_visible(&g->sim[1]),
                ms_goo_sim_visible(&g->sim[2]), ms_goo_sim_alive(&g->sim[0]), ms_goo_sim_alive(&g->sim[1]),
                ms_goo_sim_alive(&g->sim[2]), g->sim[1].snaps, g->sim[1].drips, g->sim[0].warm);
        g->diag_last = tn;
    }
    /* Layers are packed PERL bodies and PERS strands apart; slots past a
     * layer's count stay zero and the shader never reads them (u_nl, u_ns). */
    float ball[LAYERS * PERL][4], shape[LAYERS * PERL][4], sa[LAYERS * PERS][4], sb[LAYERS * PERS][4];
    int nl[LAYERS], ns[LAYERS];
    memset(ball, 0, sizeof ball); memset(shape, 0, sizeof shape);
    memset(sa, 0, sizeof sa); memset(sb, 0, sizeof sb);
    for (int l = 0; l < LAYERS; l++) {
        const MsGooSim *s = &g->sim[l];
        int n = 0;
        for (int i = 0; i < MS_GOO_MAXB && n < PERL; i++) {
            const MsGooBody *b = &s->b[i];
            if (!b->alive) continue;
            /* Never hand the shader a non-finite body (it would blank or
             * flash the frame); none has been seen, this is a guard. */
            if (!isfinite(b->x) || !isfinite(b->y) || !isfinite(b->r) || !isfinite(b->T) || !isfinite(b->vx) ||
                !isfinite(b->vy))
                continue;
            float sp = hypotf(b->vx, b->vy);
            float dx = sp > 1e-4f ? b->vx / sp : 0.f, dy = sp > 1e-4f ? b->vy / sp : 1.f;
            /* Stretch comes from the simulated speed (the shape a body takes
             * does not depend on how fast we watch it). */
            float stretch = clampf(sp * 7.f, 0.f, 0.8f) + 0.05f * sinf(st * 1.3f + b->wob);
            float *o = ball[l * PERL + n], *e = shape[l * PERL + n];
            o[0] = b->x; o[1] = b->y; o[2] = b->r; o[3] = b->T;
            e[0] = dx; e[1] = dy; e[2] = fmaxf(stretch, 0.f);
            /* hue in the fraction, texture id (0..63) + 64 * layer above it */
            e[3] = clampf(b->hue, 0.f, 0.999f) + 2.f * (float)((b->tex & 63) + 64 * l);
            n++;
        }
        nl[l] = n;
        int m = 0;
        for (int j = 0; j < MS_GOO_MAXS && m < PERS; j++) {
            const MsGooStrand *sd = &s->s[j];
            if (!sd->alive) continue;
            float *a = sa[l * PERS + m], *b = sb[l * PERS + m];
            a[0] = sd->x0; a[1] = sd->y0; a[2] = sd->x1; a[3] = sd->y1;
            b[0] = sd->r0; b[1] = sd->r1; b[2] = sd->bead; b[3] = sd->T;
            m++;
        }
        ns[l] = m;
    }
    /* Cameras: each layer wanders slowly, the near one most (parallax). */
    float cam[LAYERS][3];
    float dr = g->cfg.drift, dp = g->cfg.depth;
    for (int l = 0; l < LAYERS; l++) {
        float by = g->cfg.world == MS_WORLD_RADIAL ? 0.f : LBASEY[l];
        cam[l][0] = dr * LDRIFTX[l] * sinf(t * (0.011f + 0.004f * l) + g->phase[l]);
        cam[l][1] = by + dr * LDRIFTY[l] * sinf(t * (0.009f + 0.003f * l) + g->phase[l + 3]);
        cam[l][2] = 1.02f + (LSCALE[l] - 1.02f) * dp;
    }
    float angle = 0.f, zoom = 1.f;
    if (g->cfg.world == MS_WORLD_DISH) { angle = g->dish_angle; zoom = 1.f / sasp; }
    else if (g->cfg.world == MS_WORLD_DRIFT) { angle = 0.5f * sinf(t * 6.2831853f / 600.f + g->phase[6]); zoom = 1.15f; }
    /* Palette cross-fade rides on the chapter fade. */
    float pm = g->modeB >= 0 ? mix : 0.f, liq[3], wax[3], sec[3], bulb[3];
    lerp3(liq, g->palA.liquid, g->palB.liquid, pm);
    lerp3(wax, g->palA.wax, g->palB.wax, pm);
    lerp3(sec, g->palA.secondary, g->palB.secondary, pm);
    lerp3(bulb, g->palA.bulb, g->palB.bulb, pm);
    float mist = (float)g->palA.mist + (float)(g->palB.mist - g->palA.mist) * pm;
    /* Glitter kinds cannot blend: fade the old one out, then the new one in. */
    int glit = pm < 0.5f ? g->palA.glitter : g->palB.glitter;
    float gamt = g->cfg.glitter_amount;
    if (g->palA.glitter != g->palB.glitter) gamt *= pm < 0.5f ? 1.f - 2.f * pm : 2.f * pm - 1.f;
    /* The bulb breathes very slightly: two slow sines, both under 1 Hz, at
     * most 2.5 % together. */
    float flick = 1.f + 0.015f * sinf(t * 6.2831853f * 0.23f + g->phase[7]) + 0.01f * sinf(t * 6.2831853f * 0.61f);
    float warm = g->sim[0].warm;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(g->prog);
    glUniform2f(g->u_res, (float)w, (float)h);
    glUniform1f(g->u_time, t);
    glUniform1f(g->u_seed, g->seedf);
    glUniform1f(g->u_quality, (float)g->cfg.quality);
    glUniform1f(g->u_exposure, 1.0f);
    glUniform1f(g->u_flicker, flick);
    glUniform4fv(g->u_ball, LAYERS * PERL, &ball[0][0]);
    glUniform4fv(g->u_shape, LAYERS * PERL, &shape[0][0]);
    glUniform4fv(g->u_sa, LAYERS * PERS, &sa[0][0]);
    glUniform4fv(g->u_sb, LAYERS * PERS, &sb[0][0]);
    glUniform3i(g->u_nl, nl[0], nl[1], nl[2]);
    glUniform3i(g->u_ns, ns[0], ns[1], ns[2]);
    glUniform3fv(g->u_cam, LAYERS, &cam[0][0]);
    glUniform4f(g->u_world, (float)g->cfg.world, angle, zoom, sim_asp(g, sasp));
    glUniform4f(g->u_pool, MS_GOO_POOL_Y + g->pool[0], 0.03f * g->pool[1], st * 0.12f + g->pool[2], 0.9f * warm);
    glUniform1f(g->u_cap, (float)g->cfg.cap * (0.3f + 0.7f * warm));
    glUniform3fv(g->u_liquid, 1, liq);
    glUniform3fv(g->u_wax, 1, wax);
    glUniform3fv(g->u_secondary, 1, sec);
    glUniform3fv(g->u_bulb, 1, bulb);
    glUniform1i(g->u_modeA, g->modeA);
    glUniform1i(g->u_modeB, g->modeB >= 0 ? g->modeB : g->modeA);
    glUniform1f(g->u_mmix, g->modeB >= 0 ? mix : 0.f);
    glUniform1f(g->u_kal, (float)g->cfg.kaleidoscope);
    glUniform4f(g->u_look, (float)g->cfg.material, g->cfg.texture, g->cfg.motes, g->cfg.glass);
    glUniform4f(g->u_look2, g->cfg.depth, (float)glit, gamt, 0.25f + 0.75f * warm);
    glUniform1f(g->u_mist, mist);
    glUniform1f(g->u_mask, (float)g->cfg.projector_mask);
    glUniform1f(g->u_mask_size, g->cfg.projector_mask_size);
    glUniform1f(g->u_overlay, (float)g->cfg.overlay);
    glUniform1f(g->u_poster, (float)g->cfg.poster);
    /* Beat drives. All are 0 when dynamics=0, so every beat term in the shader
     * vanishes and the frame is identical to the pre-beat renderer. */
    glUniform1f(g->u_dyn, g->cfg.dynamics);
    glUniform1f(g->u_beat, bd->beat);
    glUniform1f(g->u_snare, bd->snare);
    glUniform1f(g->u_bass, bd->bass);
    glUniform1f(g->u_mid, bd->mid);
    glUniform1f(g->u_phase, bd->phase);
    glUniform1f(g->u_radius, bd->radius);
    glUniform1f(g->u_squeeze, bd->squeeze);
    glUniform1f(g->u_burst, bd->burst);
    glUniform1f(g->u_flow, bd->flow);
    glUniform1f(g->u_breath, bd->breath);
    glUniform1f(g->u_bloom, bd->bloom);
    glUniform1f(g->u_glow, bd->glow);
    glUniform1f(g->u_dendrites, (float)g->cfg.dendrites);
    glUniform1f(g->u_dendrite, g->cfg.dendrite_strength);
    glUniform4fv(g->u_pattern, 1, g->pattern);
    glUniform4fv(g->u_warp, 1, g->warp);
    /* The render hot spot follows the far layer's seeded heat column, except
     * in the radial world (amber-glow): there the hot centre is the defining
     * look, and moving it drops wax-over-liquid below the realism gate. */
    glUniform1f(g->u_heat_x, g->cfg.world == MS_WORLD_RADIAL ? 0.0f : g->heat_r);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void ms_goo_destroy(MsGoo *g) {
    if (!g) return;
    for (int l = 0; l < LAYERS && g->sim_ready; l++)
        fprintf(stderr, "[diag] magmasimplex goo layer=%d buds=%lu merges=%lu splits=%lu drains=%lu snaps=%lu drips=%lu\n",
                l, g->sim[l].buds, g->sim[l].merges, g->sim[l].splits, g->sim[l].drains, g->sim[l].snaps,
                g->sim[l].drips);
    if (g->vbo) glDeleteBuffers(1, &g->vbo);
    if (g->prog) glDeleteProgram(g->prog);
    if (g->beat) ms_beat_destroy(g->beat);
    free(g);
}
