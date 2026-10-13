/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_megacity.c - megacity, a flight through an endless future city.
 *
 * Two lightings of the same city: night (dark, rain-soaked, neon) and
 * pale-reach (bright, high-key, pale haze), joined by a sunrise and a sunset
 * that are one continuous sun elevation. The earlier "oscura"/"luminox" names
 * are still accepted as legacy aliases. Weather runs in seeded regimes of
 * three to eight minutes. Freighters and the occasional colossal capital
 * ship cross the sky, drawn like 1970s-80s paperback cover art; small craft
 * run the lanes.
 *
 * Everything on screen is a pure function of (seed, t): see megacity_logic.h.
 * There is no state carried from frame to frame, so a capture at t = 700 s is
 * the same picture as running for 700 s. All geometry is original: a lattice of
 * blocks with boulevard rims, mega-structures (slab arcology, terraced mass,
 * spire cluster with sky bridges) and invented hulls. No text, no logos, no
 * markings, no franchise designs.
 *
 * Photosensitivity: lightning is at most 3 flashes per second with a soft
 * 100 to 300 ms quintic rise and a peak luminance delta of 0.05 by default
 * (0.2 ceiling), added in display space so the bound holds whatever the scene
 * is. Sunrise and sunset take at least 120 s. Nothing else changes fast.
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
#include "megacity_opts.h"
#include "megacity_logic.h"
#include "ncz_harness_cfg.h"
#include "ncz_hack_shim.h"
#include "ncz_render.h"

typedef struct {
    GLuint vbo, prog;
    GLint u_res, u_org, u_cell, u_fwd, u_right, u_up, u_lens, u_seed, u_e, u_sundir, u_time;
    GLint u_wx0, u_wx1, u_wx2, u_flash, u_bolt, u_look, u_look2, u_pal, u_quality, u_style;
    GLint u_ship_a, u_ship_b, u_craft;
    /* resolved configuration */
    uint32_t seed;
    mc_cycle cyc;
    mc_wx_cfg wx;
    float speed, alt, sway, fx, fy;
    float exposure, haze, wet, height, mega, sat;
    float lightning_amp;
    int lightning, ships, palette, style, quality, flash_hold;
    float ship_density, capital_rate, traffic;
    double t_offset, started, last_log;
    unsigned long frame;
    float az_sun;
} State;

const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = MC_NOPTS; *prefix = "NCZ_MEGACITY_"; *group = "megacity";
    return MC_OPTS;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
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

/* ---- shader --------------------------------------------------------------- */
static char *load_src(void) {
    const char *p[] = {"vendor/megacity/megacity.glsl", "../vendor/megacity/megacity.glsl",
                       "../../vendor/megacity/megacity.glsl",
                       "/usr/share/ncz-screensavers/shaders/megacity.glsl"};
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
        fprintf(stderr, "[diag] megacity shader=%s\n", p[i]);
        return b;
    }
    fprintf(stderr, "megacity: cannot locate shader\n");
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
        char l[8192];
        glGetShaderInfoLog(s, sizeof l, NULL, l);
        fprintf(stderr, "megacity compile %s: %s\n", pass, l);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
static GLuint program(const char *body) {
    GLuint v = compile(GL_VERTEX_SHADER, "VS", body), f = compile(GL_FRAGMENT_SHADER, "FS", body);
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
        char l[8192];
        glGetProgramInfoLog(p, sizeof l, NULL, l);
        fprintf(stderr, "megacity link: %s\n", l);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}
#define U(n) glGetUniformLocation(s->prog, n)

/* ---- configuration ---------------------------------------------------------- */
static unsigned wx_mask(const char *list) {
    unsigned m = 0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", list);
    char *sv = NULL;
    for (char *tok = strtok_r(buf, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
        char names[128];
        snprintf(names, sizeof names, "%s", MC_WX_NAMES);
        int i = 0;
        char *sv2 = NULL;
        for (char *nm = strtok_r(names, ",", &sv2); nm; nm = strtok_r(NULL, ",", &sv2), i++)
            if (!strcmp(nm, tok)) m |= 1u << i;
    }
    return m;
}
static int wx_index(const char *name) {
    char names[128];
    snprintf(names, sizeof names, "%s", MC_WX_NAMES);
    int i = 0;
    char *sv = NULL;
    for (char *nm = strtok_r(names, ",", &sv); nm; nm = strtok_r(NULL, ",", &sv), i++)
        if (!strcmp(nm, name)) return i;
    return -1;
}

static mc_anchor cam_anchor(void *ctx, double t) {
    State *s = ctx;
    mc_cam c = mc_camera(s->seed, t, 1.f, s->alt, s->sway);
    mc_anchor a = {c.x, c.y, c.z};
    return a;
}

static void init_megacity(ModeInfo *m) {
    State *s = calloc(1, sizeof *s);
    if (!s) { ncz_harness_die(1); return; }
    m->data = s;
    const ncz_opts *o = ncz_harness_opts();
#define OF(k, d) (o ? (float)ncz_opts_get_float(o, k) : (d))
#define OB(k, d) (o ? ncz_opts_get_bool(o, k) : (d))
    s->seed = run_seed(o ? (uint32_t)strtoul(ncz_opts_get(o, "seed"), NULL, 0) : 0);
    const char *mode = o ? ncz_opts_get(o, "mode") : "night";
    const char *cyc = o ? ncz_opts_get(o, "cycle") : "off";
    /* Canonical: "night" and "pale-reach". Legacy "oscura" and "luminox"
     * parse to the same binaries, so a saved launch file from an older
     * release keeps doing what it always did. */
    s->cyc.start_luminox = !strcmp(mode, "pale-reach") || !strcmp(mode, "luminox");
    s->cyc.cycle = !strcmp(cyc, "dawn") ? 1 : !strcmp(cyc, "dusk") ? 2 : !strcmp(cyc, "day") ? 3 : 0;
    if (s->cyc.cycle == 1) s->cyc.start_luminox = 0;
    if (s->cyc.cycle == 2) s->cyc.start_luminox = 1;
    s->cyc.transition = OF("transition", 180.f);
    if (s->cyc.transition < MC_TRANSITION_MIN) s->cyc.transition = MC_TRANSITION_MIN;
    s->cyc.night_hold = OF("night-hold", 120.f);
    s->cyc.day_hold = OF("day-hold", 120.f);

    const char *wxo = o ? ncz_opts_get(o, "weather") : "auto";
    s->wx.seed = s->seed ^ 0x51ED270Bu;
    s->wx.rmin = OF("regime-min", 180.f);
    s->wx.rmax = OF("regime-max", 480.f);
    if (s->wx.rmax < s->wx.rmin) s->wx.rmax = s->wx.rmin;
    s->wx.intensity = OF("weather-intensity", 1.f);
    if (!strcmp(wxo, "auto")) {
        s->wx.allowed = wx_mask(o ? ncz_opts_get(o, "weather-set") : "clear,rain,storm,snow,sleet,fog,smog,dust,ash");
        if (!s->wx.allowed) s->wx.allowed = 1u << MC_CLEAR;
    } else {
        int i = wx_index(wxo);
        s->wx.allowed = 1u << (i < 0 ? 0 : i);
    }
    s->speed = OF("speed", 1.f);
    s->alt = OF("altitude", 1.f);
    s->sway = OF("sway", 1.f);
    s->fx = OF("frame-x", 0.f) * 0.5f;
    s->fy = OF("frame-y", 0.f) * 0.5f;
    s->exposure = OF("exposure", 1.f);
    s->haze = OF("haze", 1.f);
    s->wet = OF("wet", 1.f);
    s->height = OF("height", 1.f);
    s->mega = OF("mega", 1.f);
    s->sat = OF("ship-saturation", 1.f);
    s->lightning = OB("lightning", 1);
    s->lightning_amp = OF("lightning-intensity", MC_FLASH_DEFAULT);
    if (s->lightning_amp > MC_FLASH_CEILING) s->lightning_amp = MC_FLASH_CEILING;
    if (s->lightning_amp < 0.f) s->lightning_amp = 0.f;
    s->flash_hold = OB("flash-hold", 0);
    s->ships = OB("ships", 1);
    s->ship_density = OF("ship-density", 1.f);
    s->capital_rate = OF("capital-rate", 0.35f);
    s->traffic = OF("traffic", 1.f);
    {
        const char *pal = o ? ncz_opts_get(o, "palette") : "neon";
        s->palette = !strcmp(pal, "violet") ? 1 : !strcmp(pal, "refinery") ? 2 : !strcmp(pal, "mono") ? 3 : 0;
        const char *st = o ? ncz_opts_get(o, "ship-style") : "paperback";
        s->style = !strcmp(st, "paperback");
    }
    s->t_offset = OF("time-offset", 0.f);
    {
        const char *gc = ncz_gpu_class_current();
        s->quality = (gc && !strcmp(gc, "weak")) ? 0 : 1;
        const char *q = getenv("NCZ_MEGACITY_QUALITY");
        if (q && *q) s->quality = atoi(q) > 0;
    }
    {
        uint32_t h = mc_hash(s->seed, 900, 0);
        s->az_sun = (mc_u(h) < 0.5f ? -1.f : 1.f) * (0.4f + 1.0f * mc_u(mc_hash(s->seed, 901, 0)));
    }

    char *body = load_src();
    if (!body) { ncz_harness_die(1); return; }
    s->prog = program(body);
    free(body);
    if (!s->prog) { ncz_harness_die(1); return; }
    static const float q[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    s->u_res = U("u_res"); s->u_org = U("u_org"); s->u_cell = U("u_cell");
    s->u_fwd = U("u_fwd"); s->u_right = U("u_right"); s->u_up = U("u_up"); s->u_lens = U("u_lens");
    s->u_seed = U("u_seed"); s->u_e = U("u_e"); s->u_sundir = U("u_sundir"); s->u_time = U("u_time");
    s->u_wx0 = U("u_wx0"); s->u_wx1 = U("u_wx1"); s->u_wx2 = U("u_wx2");
    s->u_flash = U("u_flash"); s->u_bolt = U("u_bolt"); s->u_look = U("u_look"); s->u_look2 = U("u_look2");
    s->u_pal = U("u_pal"); s->u_quality = U("u_quality"); s->u_style = U("u_style");
    s->u_ship_a = U("u_ship_a[0]"); s->u_ship_b = U("u_ship_b[0]"); s->u_craft = U("u_craft[0]");
    s->started = now();
    fprintf(stderr, "[diag] megacity seed=%u mode=%s cycle=%s transition=%.0f weather=%s quality=%d GL=%s\n",
            s->seed, mode, cyc, s->cyc.transition, wxo, s->quality, glGetString(GL_VERSION));
}

static void draw_megacity(ModeInfo *m) {
    State *s = m->data;
    if (!s || !s->prog) return;
    int w = m->xgwa.width, h = m->xgwa.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    double t = s->t_offset + (now() - s->started) * s->speed;
    s->frame++;

    float e = mc_sun(&s->cyc, t);
    int wtype = 0;
    float wblend = 1.f;
    mc_wx wx = mc_weather(&s->wx, t, &wtype, &wblend);
    float flash = 0.f;
    if (s->lightning) {
        flash = mc_flash(s->seed, t, s->lightning_amp, wx.storm);
        if (s->flash_hold) flash = s->lightning_amp;
    }
    float bolt_az = mc_flash_az(s->seed, t);

    mc_cam c = mc_camera(s->seed, t, 1.f, s->alt, s->sway);
    long cx0 = (long)floor(c.x / MC_CELL), cz0 = (long)floor(c.z / MC_CELL);
    float ox = (float)(c.x - cx0 * MC_CELL), oz = (float)(c.z - cz0 * MC_CELL);
    /* wrap the z cell so it stays a small int (the city repeats every 2^20 cells) */
    int icx = (int)(cx0 & 0xfffff) - ((cx0 & 0x80000) ? 0x100000 : 0);
    int icz = (int)(cz0 & 0xfffff);
    float cyw = cosf(c.yaw), syw = sinf(c.yaw), cp = cosf(c.pitch), sp = sinf(c.pitch);
    float fwd[3] = {syw * cp, sp, cyw * cp};
    float rgt[3] = {cyw, 0.f, -syw};
    float up[3] = {fwd[1] * rgt[2] - fwd[2] * rgt[1], fwd[2] * rgt[0] - fwd[0] * rgt[2], fwd[0] * rgt[1] - fwd[1] * rgt[0]};
    {   /* roll about the view axis */
        float cr = cosf(c.roll), sr = sinf(c.roll);
        for (int i = 0; i < 3; i++) {
            float r0 = rgt[i], u0 = up[i];
            rgt[i] = r0 * cr + u0 * sr;
            up[i] = -r0 * sr + u0 * cr;
        }
    }
    float L = (e + 1.f) * 0.5f;
    float alpha = mc_mix(-0.30f, 1.10f, L);
    float sd[3] = {cosf(alpha) * sinf(s->az_sun), sinf(alpha), cosf(alpha) * cosf(s->az_sun)};

    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(s->prog);
    glUniform2f(s->u_res, (float)w, (float)h);
    glUniform3f(s->u_org, ox, (float)c.y, oz);
    glUniform2i(s->u_cell, icx, icz);
    glUniform3fv(s->u_fwd, 1, fwd);
    glUniform3fv(s->u_right, 1, rgt);
    glUniform3fv(s->u_up, 1, up);
    glUniform3f(s->u_lens, tanf(0.5f * 62.f * 3.14159265f / 180.f), s->fx, s->fy);
    glUniform1i(s->u_seed, (int)(s->seed & 0x7fffffffu));
    glUniform1f(s->u_e, e);
    glUniform3fv(s->u_sundir, 1, sd);
    glUniform1f(s->u_time, (float)fmod(t, 4096.0));
    glUniform4f(s->u_wx0, wx.rain, wx.snow, wx.sleet, mc_clamp(wx.fog, 0.f, 1.5f));
    glUniform4f(s->u_wx1, wx.smog, wx.dust, wx.ash, wx.cloud);
    glUniform4f(s->u_wx2, wx.wet, wx.wind, wx.storm, 0.f);
    glUniform1f(s->u_flash, flash);
    glUniform2f(s->u_bolt, bolt_az, mc_u(mc_hash(s->seed, (uint32_t)floor(t / MC_FLASH_BUCKET), 60)) * 50.f);
    glUniform4f(s->u_look, s->exposure, s->haze, s->wet, s->height);
    glUniform4f(s->u_look2, s->mega, s->sat, 0.f, 0.f);
    glUniform1i(s->u_pal, s->palette);
    glUniform1i(s->u_quality, s->quality);
    glUniform1i(s->u_style, s->style);

    float sa[MC_NSHIP][4], sb[MC_NSHIP][4], cr[MC_NCRAFT][4];
    memset(sa, 0, sizeof sa); memset(sb, 0, sizeof sb); memset(cr, 0, sizeof cr);
    if (s->ships) {
        for (int i = 0; i < MC_NSHIP; i++) {
            mc_ship sh = mc_ship_slot(s->seed, i, t, cam_anchor, s, s->ship_density, s->capital_rate);
            if (!sh.live) continue;
            sa[i][0] = (float)(sh.p[0] - c.x); sa[i][1] = (float)(sh.p[1] - c.y); sa[i][2] = (float)(sh.p[2] - c.z);
            sa[i][3] = sh.scale;
            sb[i][0] = sh.yaw; sb[i][1] = mc_u(sh.id); sb[i][2] = (float)sh.kind; sb[i][3] = sh.fade;
        }
    }
    if (s->traffic > 0.f) {
        for (int j = 0; j < MC_NCRAFT; j++) {
            mc_craft k = mc_craft_at(s->seed, j, t, c.x, c.z, s->traffic);
            if (k.fade <= 0.f) continue;
            cr[j][0] = (float)(k.x - c.x); cr[j][1] = (float)(k.y - c.y); cr[j][2] = (float)(k.z - c.z);
            cr[j][3] = k.dir * k.fade;
        }
    }
    glUniform4fv(s->u_ship_a, MC_NSHIP, &sa[0][0]);
    glUniform4fv(s->u_ship_b, MC_NSHIP, &sb[0][0]);
    glUniform4fv(s->u_craft, MC_NCRAFT, &cr[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    double tn = now();
    if (tn - s->last_log > 10.0 || s->frame == 1) {
        s->last_log = tn;
        static const char *nm[] = {"clear", "rain", "storm", "snow", "sleet", "fog", "smog", "dust", "ash"};
        fprintf(stderr, "[diag] megacity t=%.1f e=%.3f weather=%s blend=%.2f rain=%.2f snow=%.2f fog=%.2f flash=%.4f\n",
                t, e, nm[wtype], wblend, wx.rain, wx.snow, wx.fog, flash);
    }
}

static void free_megacity(ModeInfo *m) {
    State *s = m->data;
    if (!s) return;
    if (s->vbo) glDeleteBuffers(1, &s->vbo);
    if (s->prog) glDeleteProgram(s->prog);
    free(s);
    m->data = NULL;
}

static void reshape_megacity(ModeInfo *m, int w, int h) { (void)m; (void)w; (void)h; }
static Bool megacity_handle_event(ModeInfo *m, XEvent *e) { (void)m; (void)e; return False; }
static void release_megacity(ModeInfo *m) { (void)m; }
static ModeSpecOpt megacity_opts = {0, NULL, 0, NULL, NULL};
struct xscreensaver_function_table megacity_xscreensaver_function_table = {
    .name = "megacity", .class_ = "Megacity", .init_cb = init_megacity, .draw_cb = draw_megacity,
    .reshape_cb = reshape_megacity, .event_cb = megacity_handle_event, .free_cb = free_megacity,
    .release_cb = release_megacity, .opts = &megacity_opts, .defaults_str = DEFAULTS};
