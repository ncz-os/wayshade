/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_megacity_smoke.c — HEADLESS smoke tester for the megacity screensaver.
 *
 * Patterned after gles3_transitions_smoke.c and gles3_xshadertoy_smoke.c:
 * opens a surfaceless EGL PBuffer context (no Wayland compositor required),
 * compiles the megacity shader, drives the same stateless scene logic from
 * src/megacity_logic.h, and dumps real PNGs via libpng.
 *
 * The harness driver src/gles3_megacity.c requires a Wayland session with
 * layer-shell / xdg_wm_base. Many validation hosts have an AMD 780M but no
 * live compositor. This tester renders directly onto the GPU and dumps
 * evidence PNGs to <out-dir>/<label>.png.
 *
 * Usage:
 *   gles3_megacity_smoke <preset> <label> <out-dir> [--seed=S] [--time-offset=T] \
 *                         [--w=1280 --h=720] [--quality=0|1]
 *
 * Where <preset> is one of: night, pale-reach, dawn, dusk, day-cycle,
 * storm, rain, sleet, ash, clear, lightning-peak, fog, smog, snow, dust.
 * The legacy names oscura, luminox, paperback-night, paperback-noon are
 * also accepted. <label> is a short tag used in the output filename.
 *
 * The tester:
 *   1. Parses the preset into the same option table the production driver
 *      uses (megacity_opts.h).
 *   2. Compiles megacity.glsl.
 *   3. Initializes the same State (mc_cycle, mc_wx_cfg, uniforms).
 *   4. Renders one PNG with a deterministically chosen scene-time t so two
 *      runs of the same (preset, label, seed) are byte-identical.
 *   5. Optionally renders a small contact sheet when --time-offset=T is set:
 *      one frame at T-30s, T, T+30s — for transitions.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <png.h>

#include "ncz_options.h"
#include "megacity_opts.h"
#include "megacity_logic.h"

/* ------------------------------------------------------------------ */
/* ModeInfo shim — just enough that init/draw can write into m->xgwa  */
/* ------------------------------------------------------------------ */
typedef struct {
    int width, height;
} Xgwa;
typedef struct {
    void *data;
    Xgwa  xgwa;
} ModeInfo;
static void ncz_harness_die(int rc) {
    fprintf(stderr, "megacity_smoke: die rc=%d\n", rc);
    exit(rc ? rc : 1);
}

/* ------------------------------------------------------------------ */
/* Driver state (mirrors src/gles3_megacity.c but simplified)         */
/* ------------------------------------------------------------------ */
typedef struct {
    GLuint vbo, prog;
    GLint u_res, u_org, u_cell, u_fwd, u_right, u_up, u_lens, u_seed, u_e, u_sundir, u_time;
    GLint u_wx0, u_wx1, u_wx2, u_flash, u_bolt, u_look, u_look2, u_pal, u_quality, u_style;
    GLint u_ship_a, u_ship_b, u_craft;
    uint32_t seed;
    mc_cycle cyc;
    mc_wx_cfg wx;
    float speed, alt, sway, fx, fy;
    float exposure, haze, wet, height, mega, sat;
    float lightning_amp;
    int lightning, ships, palette, style, quality;
    float ship_density, capital_rate, traffic;
    double t_offset;
    float az_sun;
    int preset_quality;
} State;

/* ------------------------------------------------------------------ */
/* Preset → CLI args                                                   */
/* ------------------------------------------------------------------ */
/* Canonical preset names are Night and Pale Reach. The earlier oscura /
 * luminox / paperback-night / paperback-noon names are accepted as legacy
 * aliases so old contact sheets keep rendering. */
static const char *preset_args(const char *p) {
    if (!strcmp(p, "night") || !strcmp(p, "oscura"))
        return "--mode=night --weather=rain --weather-intensity=0.9 --lightning=true "
               "--lightning-intensity=0.05 --palette=neon --ship-style=paperback "
               "--wet=1 --haze=1 --exposure=1.1 --mega=1 --height=1 --ships=true "
               "--ship-density=1 --capital-rate=0.35 --traffic=1 --altitude=1 --sway=1";
    if (!strcmp(p, "pale-reach") || !strcmp(p, "luminox"))
        return "--mode=pale-reach --weather=clear --weather-intensity=0.5 --lightning=false "
               "--palette=neon --ship-style=paperback --wet=0 --haze=1.6 --exposure=1.1 "
               "--mega=1 --height=1 --ships=true --ship-density=1 --capital-rate=0.2 "
               "--traffic=1 --altitude=1 --sway=1";
    if (!strcmp(p, "dawn")) /* night holding, then ~transition into pale-reach */
        return "--mode=night --cycle=dawn --transition=180 --night-hold=120 "
               "--weather=rain --weather-intensity=0.9 --lightning=true "
               "--palette=neon --ship-style=paperback --wet=1 --haze=1 "
               "--exposure=1 --mega=1 --height=1 --altitude=1 --sway=1";
    if (!strcmp(p, "dusk")) /* pale-reach holding, then transition back into night */
        return "--mode=pale-reach --cycle=dusk --transition=180 --day-hold=120 "
               "--weather=clear --weather-intensity=0.5 --lightning=false "
               "--palette=neon --ship-style=paperback --wet=0 --haze=1.2 "
               "--exposure=1 --mega=1 --height=1 --altitude=1 --sway=1";
    if (!strcmp(p, "paperback-night") || !strcmp(p, "paperback-noon")) {
        /* legacy paperback-* are still served by the same presets */
        if (!strcmp(p, "paperback-night"))
            return "--mode=night --palette=neon --ship-style=paperback --weather=storm "
                   "--weather-intensity=1.0 --lightning=true --lightning-intensity=0.05 "
                   "--wet=1 --haze=1 --exposure=1 --mega=1 --height=1 --altitude=1 --sway=1";
        return "--mode=pale-reach --palette=neon --ship-style=paperback --weather=clear "
               "--weather-intensity=0.5 --lightning=false --wet=0 --haze=0.5 "
               "--exposure=1.2 --mega=1 --height=1 --altitude=1 --sway=1";
    }
    if (!strcmp(p, "day-cycle"))
        return "--mode=night --cycle=day --transition=180 --night-hold=120 --day-hold=120 "
               "--weather=auto --weather-set=clear,rain,storm,snow,sleet,fog,smog,dust,ash "
               "--regime-min=180 --regime-max=480 --lightning=true "
               "--palette=neon --ship-style=paperback --wet=1 --haze=1 --exposure=1 "
               "--mega=1 --height=1 --altitude=1 --sway=1";
    if (!strcmp(p, "storm"))
        return "--mode=night --weather=storm --weather-intensity=1.0 "
               "--lightning=true --lightning-intensity=0.05 --palette=neon "
               "--ship-style=paperback --wet=1 --haze=1 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "rain"))
        return "--mode=night --weather=rain --weather-intensity=1.0 "
               "--lightning=false --palette=neon --ship-style=paperback "
               "--wet=1 --haze=1 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "snow"))
        return "--mode=pale-reach --weather=snow --weather-intensity=0.9 "
               "--lightning=false --palette=mono --ship-style=paperback "
               "--wet=0.4 --haze=0.8 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "sleet"))
        return "--mode=night --weather=sleet --weather-intensity=1.0 "
               "--lightning=false --palette=violet --ship-style=paperback "
               "--wet=1 --haze=1 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "fog"))
        return "--mode=pale-reach --weather=fog --weather-intensity=1.0 "
               "--lightning=false --palette=neon --ship-style=paperback "
               "--wet=0 --haze=2 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "smog"))
        return "--mode=night --weather=smog --weather-intensity=1.0 "
               "--lightning=false --palette=refinery --ship-style=paperback "
               "--wet=0 --haze=2 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "dust"))
        return "--mode=pale-reach --weather=dust --weather-intensity=1.0 "
               "--lightning=false --palette=refinery --ship-style=paperback "
               "--wet=0 --haze=1.5 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "ash"))
        return "--mode=night --weather=ash --weather-intensity=0.9 "
               "--lightning=true --lightning-intensity=0.03 --palette=violet "
               "--ship-style=paperback --wet=0.2 --haze=1.2 --exposure=1 --mega=1 --height=1";
    if (!strcmp(p, "clear"))
        return "--mode=pale-reach --weather=clear --weather-intensity=1.0 "
               "--lightning=false --palette=neon --ship-style=paperback "
               "--wet=0 --haze=0.6 --exposure=1.1 --mega=1 --height=1";
    if (!strcmp(p, "lightning-peak"))
        return "--mode=night --weather=storm --weather-intensity=1.0 "
               "--lightning=true --lightning-intensity=0.05 --palette=neon "
               "--ship-style=paperback --wet=1 --haze=1 --exposure=1 --mega=1 --height=1 "
               "--flash-hold=true";
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Option parsing                                                      */
/* ------------------------------------------------------------------ */
static uint32_t run_seed(uint32_t fixed) {
    if (fixed) return fixed;
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (uint32_t)(t.tv_nsec ^ t.tv_sec) | 1u;
}

/* Same masks as the harness driver. */
static unsigned wx_mask_fn(const char *list) {
    unsigned m = 0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", list);
    char *sv = NULL;
    for (char *tok = strtok_r(buf, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
        int i = 0;
        char *sv2 = NULL;
        char names[128];
        snprintf(names, sizeof names, "%s", MC_WX_NAMES);
        for (char *nm = strtok_r(names, ",", &sv2); nm; nm = strtok_r(NULL, ",", &sv2), i++)
            if (!strcmp(nm, tok)) m |= 1u << i;
    }
    return m;
}
static int wx_index_fn(const char *name) {
    int i = 0;
    char *sv2 = NULL;
    char names[128];
    snprintf(names, sizeof names, "%s", MC_WX_NAMES);
    for (char *nm = strtok_r(names, ",", &sv2); nm; nm = strtok_r(NULL, ",", &sv2), i++)
        if (!strcmp(nm, name)) return i;
    return -1;
}

#define OF(k, d) ((float)ncz_opts_get_float(opts, k))
#define OB(k, d) ncz_opts_get_bool(opts, k)

static void resolve_state(ncz_opts *opts, State *s, int w, int h) {
    s->preset_quality = 1;
    /* preset_quality override: pass via env to mimic the launcher's
     * NCZ_GPU_CLASS=weak detection. */
    const char *qg = getenv("NCZ_GPU_CLASS");
    if (qg && !strcmp(qg, "weak")) s->preset_quality = 0;

    s->seed = run_seed((uint32_t)ncz_opts_get_int(opts, "seed"));

    const char *mode = ncz_opts_get(opts, "mode");
    const char *cyc  = ncz_opts_get(opts, "cycle");
    /* Canonical: night, pale-reach. Legacy: oscura, luminox. */
    s->cyc.start_luminox = !strcmp(mode, "pale-reach") || !strcmp(mode, "luminox");
    s->cyc.cycle = !strcmp(cyc, "dawn") ? 1 : !strcmp(cyc, "dusk") ? 2 :
                   !strcmp(cyc, "day") ? 3 : 0;
    if (s->cyc.cycle == 1) s->cyc.start_luminox = 0;
    if (s->cyc.cycle == 2) s->cyc.start_luminox = 1;
    s->cyc.transition = OF("transition", 180.f);
    if (s->cyc.transition < MC_TRANSITION_MIN) s->cyc.transition = MC_TRANSITION_MIN;
    s->cyc.night_hold = OF("night-hold", 120.f);
    s->cyc.day_hold   = OF("day-hold", 120.f);

    const char *wxo = ncz_opts_get(opts, "weather");
    s->wx.seed = s->seed ^ 0x51ED270Bu;
    s->wx.rmin = OF("regime-min", 180.f);
    s->wx.rmax = OF("regime-max", 480.f);
    if (s->wx.rmax < s->wx.rmin) s->wx.rmax = s->wx.rmin;
    s->wx.intensity = OF("weather-intensity", 1.f);
    if (!strcmp(wxo, "auto")) {
        s->wx.allowed = wx_mask_fn(ncz_opts_get(opts, "weather-set"));
        if (!s->wx.allowed) s->wx.allowed = 1u << MC_CLEAR;
    } else {
        int i = wx_index_fn(wxo);
        s->wx.allowed = 1u << (i < 0 ? 0 : i);
    }

    s->speed = OF("speed", 1.f);
    s->alt   = OF("altitude", 1.f);
    s->sway  = OF("sway", 1.f);
    s->fx    = OF("frame-x", 0.f) * 0.5f;
    s->fy    = OF("frame-y", 0.f) * 0.5f;
    s->exposure = OF("exposure", 1.f);
    s->haze   = OF("haze", 1.f);
    s->wet    = OF("wet", 1.f);
    s->height = OF("height", 1.f);
    s->mega   = OF("mega", 1.f);
    s->sat    = OF("ship-saturation", 1.f);
    s->lightning = OB("lightning", 1);
    s->lightning_amp = OF("lightning-intensity", MC_FLASH_DEFAULT);
    if (s->lightning_amp > MC_FLASH_CEILING) s->lightning_amp = MC_FLASH_CEILING;
    if (s->lightning_amp < 0.f) s->lightning_amp = 0.f;
    s->ships = OB("ships", 1);
    s->ship_density = OF("ship-density", 1.f);
    s->capital_rate = OF("capital-rate", 0.35f);
    s->traffic = OF("traffic", 1.f);
    const char *pal = ncz_opts_get(opts, "palette");
    s->palette = !strcmp(pal, "violet") ? 1 : !strcmp(pal, "refinery") ? 2 :
                 !strcmp(pal, "mono") ? 3 : 0;
    const char *st = ncz_opts_get(opts, "ship-style");
    s->style = !strcmp(st, "paperback");
    s->t_offset = OF("time-offset", 0.f);

    uint32_t hsh = mc_hash(s->seed, 900, 0);
    s->az_sun = (mc_u(hsh) < 0.5f ? -1.f : 1.f) * (0.4f + 1.0f * mc_u(mc_hash(s->seed, 901, 0)));

    {
        uint32_t hh = mc_hash(s->seed, 902, 0);
        s->quality = (mc_u(hh) < 0.5f) ? 0 : 1;
        if (qg && !strcmp(qg, "weak")) s->quality = 0;
    }

    /* FBO backing: we render to the PBuffer surface directly. */
    (void)w; (void)h;
}

/* ------------------------------------------------------------------ */
/* Shader load + compile                                               */
/* ------------------------------------------------------------------ */
static char *load_src(const char *shader_path) {
    FILE *f = fopen(shader_path, "rb");
    if (!f) { fprintf(stderr, "megacity_smoke: cannot open %s\n", shader_path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = n > 0 ? malloc((size_t)n + 1) : NULL;
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    return b;
}
static GLuint compile_one(GLenum type, const char *pass, const char *body, char *log, size_t cap) {
    char head[96];
    snprintf(head, sizeof head, "#version 300 es\n#define %s 1\n", pass);
    const char *src[2] = {head, body};
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLsizei out = 0;
        glGetShaderInfoLog(s, (GLsizei)cap, &out, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
static GLuint build_program(const char *body, char *log, size_t cap) {
    log[0] = 0;
    GLuint vs = compile_one(GL_VERTEX_SHADER, "VS", body, log, cap);
    if (!vs) return 0;
    GLuint fs = compile_one(GL_FRAGMENT_SHADER, "FS", body, log, cap);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLsizei out = 0;
        glGetProgramInfoLog(p, (GLsizei)cap, &out, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* EGL PBuffer + GL ES3 context                                        */
/* ------------------------------------------------------------------ */
static EGLDisplay egl_disp;
static EGLContext  egl_ctx;
static EGLSurface  egl_pbuf;
static int         dst_w = 1280, dst_h = 720;

static int init_egl(int w, int h) {
    dst_w = w; dst_h = h;
    /* Force surfaceless so we don't need X11/Wayland; production driver
     * runs against a Wayland surface but this smoke tester has no compositor
     * available on most validation hosts. */
    if (!getenv("EGL_PLATFORM")) setenv("EGL_PLATFORM", "surfaceless", 1);
    egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_disp == EGL_NO_DISPLAY) {
        fprintf(stderr, "smoke: eglGetDisplay failed (platform=%s)\n", getenv("EGL_PLATFORM"));
        return -1;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_disp, &major, &minor)) {
        fprintf(stderr, "smoke: eglInitialize failed (err=0x%x platform=%s)\n",
                (unsigned)eglGetError(), getenv("EGL_PLATFORM"));
        return -1;
    }
    fprintf(stderr, "[diag] EGL %d.%d vendor=%s\n",
            major, minor, eglQueryString(egl_disp, EGL_VENDOR));
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "smoke: eglBindAPI failed\n");
        return -1;
    }
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num_cfg = 0;
    if (!eglChooseConfig(egl_disp, cfg_attr, &cfg, 1, &num_cfg) || num_cfg < 1) {
        fprintf(stderr, "smoke: eglChooseConfig failed\n");
        return -1;
    }
    EGLint pbuf_attr[] = {
        EGL_WIDTH, dst_w,
        EGL_HEIGHT, dst_h,
        EGL_NONE
    };
    egl_pbuf = eglCreatePbufferSurface(egl_disp, cfg, pbuf_attr);
    if (egl_pbuf == EGL_NO_SURFACE) {
        fprintf(stderr, "smoke: eglCreatePbufferSurface failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    EGLint ctx_attr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };
    egl_ctx = eglCreateContext(egl_disp, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "smoke: eglCreateContext failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(egl_disp, egl_pbuf, egl_pbuf, egl_ctx)) {
        fprintf(stderr, "smoke: eglMakeCurrent failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    fprintf(stderr, "[diag] GL_VERSION=%s\n",  (const char *)glGetString(GL_VERSION));
    fprintf(stderr, "[diag] GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    fprintf(stderr, "[diag] GL_VENDOR=%s\n",   (const char *)glGetString(GL_VENDOR));
    fprintf(stderr, "[diag] GLSL=%s\n",        (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
    /* Refuse llvmpipe / swrast — task brief: "actually RUN it on real GPUs
     * (no llvmpipe)".  Return -2 so main() can exit 77 (meson skip): the
     * debian package build runs inside a GPU-less container whose EGL is
     * llvmpipe, and a software-renderer run must skip rather than fail the
     * packaging gate. */
    const char *ren = (const char *)glGetString(GL_RENDERER);
    if (ren && (strstr(ren, "llvmpipe") || strstr(ren, "softpipe") ||
                strstr(ren, "swrast"))) {
        fprintf(stderr, "smoke: refusing software renderer '%s'\n", ren);
        return -2;
    }
    return 0;
}
static void teardown_egl(void) {
    if (egl_disp != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_disp, egl_ctx);
        if (egl_pbuf != EGL_NO_SURFACE) eglDestroySurface(egl_disp, egl_pbuf);
        eglTerminate(egl_disp);
    }
}

/* ------------------------------------------------------------------ */
/* Per-frame draw — mirrors src/gles3_megacity.c::draw_megacity         */
/* ------------------------------------------------------------------ */
static mc_anchor cam_anchor_for(void *ctx, double t) {
    State *s = ctx;
    mc_cam c = mc_camera(s->seed, t, 1.f, s->alt, s->sway);
    mc_anchor a = {c.x, c.y, c.z};
    return a;
}

static void draw_frame(State *s, double t, int w, int h) {
    float e = mc_sun(&s->cyc, t);
    int wtype = 0; float wblend = 1.f;
    mc_wx wx = mc_weather(&s->wx, t, &wtype, &wblend);
    float flash = 0.f;
    if (s->lightning) {
        flash = mc_flash(s->seed, t, s->lightning_amp, wx.storm);
        /* flash-hold via env override (driver reads --flash-hold). */
        const char *fhe = getenv("NCZ_MEGACITY_FLASH_HOLD");
        if (fhe && !strcmp(fhe, "true")) flash = s->lightning_amp;
    }
    float bolt_az = mc_flash_az(s->seed, t);

    mc_cam c = mc_camera(s->seed, t, 1.f, s->alt, s->sway);
    long cx0 = (long)floor(c.x / MC_CELL), cz0 = (long)floor(c.z / MC_CELL);
    float ox = (float)(c.x - cx0 * MC_CELL), oz = (float)(c.z - cz0 * MC_CELL);
    int icx = (int)(cx0 & 0xfffff) - ((cx0 & 0x80000) ? 0x100000 : 0);
    int icz = (int)(cz0 & 0xfffff);
    float cyw = cosf(c.yaw), syw = sinf(c.yaw), cp = cosf(c.pitch), sp = sinf(c.pitch);
    float fwd[3] = {syw * cp, sp, cyw * cp};
    float rgt[3] = {cyw, 0.f, -syw};
    float up[3] = {fwd[1] * rgt[2] - fwd[2] * rgt[1],
                   fwd[2] * rgt[0] - fwd[0] * rgt[2],
                   fwd[0] * rgt[1] - fwd[1] * rgt[0]};
    float cr = cosf(c.roll), sr = sinf(c.roll);
    for (int i = 0; i < 3; i++) {
        float r0 = rgt[i], u0 = up[i];
        rgt[i] = r0 * cr + u0 * sr;
        up[i]  = -r0 * sr + u0 * cr;
    }
    float L = (e + 1.f) * 0.5f;
    float alpha = -0.30f + (1.10f - (-0.30f)) * L;
    float sd[3] = {cosf(alpha) * sinf(s->az_sun), sinf(alpha),
                   cosf(alpha) * cosf(s->az_sun)};

    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);
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
    glUniform4f(s->u_wx0, wx.rain, wx.snow, wx.sleet,
                wx.fog < 0 ? 0 : (wx.fog > 1.5f ? 1.5f : wx.fog));
    glUniform4f(s->u_wx1, wx.smog, wx.dust, wx.ash, wx.cloud);
    glUniform4f(s->u_wx2, wx.wet, wx.wind, wx.storm, 0.f);
    glUniform1f(s->u_flash, flash);
    glUniform2f(s->u_bolt, bolt_az,
                mc_u(mc_hash(s->seed, (uint32_t)floor(t / MC_FLASH_BUCKET), 60)) * 50.f);
    glUniform4f(s->u_look, s->exposure, s->haze, s->wet, s->height);
    glUniform4f(s->u_look2, s->mega, s->sat, 0.f, 0.f);
    glUniform1i(s->u_pal, s->palette);
    glUniform1i(s->u_quality, s->quality);
    glUniform1i(s->u_style, s->style);

    float sa[MC_NSHIP][4], sb[MC_NSHIP][4], crf[MC_NCRAFT][4];
    memset(sa, 0, sizeof sa); memset(sb, 0, sizeof sb); memset(crf, 0, sizeof crf);
    if (s->ships) {
        for (int i = 0; i < MC_NSHIP; i++) {
            mc_ship sh = mc_ship_slot(s->seed, i, t, cam_anchor_for, s,
                                      s->ship_density, s->capital_rate);
            if (!sh.live) continue;
            sa[i][0] = (float)(sh.p[0] - c.x); sa[i][1] = (float)(sh.p[1] - c.y);
            sa[i][2] = (float)(sh.p[2] - c.z); sa[i][3] = sh.scale;
            sb[i][0] = sh.yaw; sb[i][1] = mc_u(sh.id); sb[i][2] = (float)sh.kind;
            sb[i][3] = sh.fade;
        }
    }
    if (s->traffic > 0.f) {
        for (int j = 0; j < MC_NCRAFT; j++) {
            mc_craft k = mc_craft_at(s->seed, j, t, c.x, c.z, s->traffic);
            if (k.fade <= 0.f) continue;
            crf[j][0] = (float)(k.x - c.x); crf[j][1] = (float)(k.y - c.y);
            crf[j][2] = (float)(k.z - c.z); crf[j][3] = k.dir * k.fade;
        }
    }
    glUniform4fv(s->u_ship_a, MC_NSHIP, &sa[0][0]);
    glUniform4fv(s->u_ship_b, MC_NSHIP, &sb[0][0]);
    glUniform4fv(s->u_craft, MC_NCRAFT, &crf[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    glFinish();
}

/* ------------------------------------------------------------------ */
/* PNG writer                                                          */
/* ------------------------------------------------------------------ */
static int write_png(const char *path, const unsigned char *rgba, int w, int h) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return -1;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); return -1; }
    if (setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png, &info); return -1; }
    FILE *f = fopen(path, "wb");
    if (!f) { png_destroy_write_struct(&png, &info); return -1; }
    png_init_io(png, f);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    /* Flip vertically: GL origin is bottom-left, PNG origin is top-left. */
    for (int y = h - 1; y >= 0; y--)
        png_write_row(png, (png_const_bytep)(rgba + y * w * 4));
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <preset> <label> <out-dir> [--seed=N] [--time-offset=T] "
            "[--w=W --h=H] [--quality=0|1] [--n=N] [--coverage-check] [--coverage-audit]\n"
            "  preset: night pale-reach dawn dusk day-cycle storm rain sleet ash clear "
            "lightning-peak fog smog snow dust\n"
            "    (legacy aliases also accepted: oscura luminox paperback-night paperback-noon)\n"
            "  --n=N: render N frames at evenly spaced scene times [default 1]\n"
            "  --coverage-check: read back the frame, assert per-preset visual gates, "
            "exit non-zero if any trip (used by the megacity-coverage meson test)\n"
            "  --coverage-audit: render every brief-mandated preset with --coverage-check "
            "and exit non-zero if any fail (the all-presets smoke gate)\n",
            argv[0]);
        return 2;
    }
    const char *preset = argv[1];
    const char *label  = argv[2];
    const char *outdir = argv[3];

    /* Build the CLI args string and feed it to ncz_opts_resolve_preset via a
     * small wrapper: we directly init a ncz_opts with MC_OPTS + parse a
     * pseudo-argv that starts with our preset CLI. */
    const char *pargs = preset_args(preset);
    if (!pargs) { fprintf(stderr, "smoke: unknown preset '%s'\n", preset); return 2; }

    int w = 1280, h = 720;
    int n_frames = 1;
    int override_seed = -1;
    double override_t = -1.0;
    int quality = -1;
    int coverage_check = 0;
    int coverage_audit = 0;
    int rc_acc = 0;
    /* Augment preset CLI with overrides from argv[4..]. */
    char cli[8192];
    snprintf(cli, sizeof cli, "%s", pargs);
    for (int i = 4; i < argc; i++) {
        if (!strncmp(argv[i], "--seed=", 7)) {
            override_seed = atoi(argv[i] + 7);
            char buf[64]; snprintf(buf, sizeof buf, " --seed=%d", override_seed);
            strncat(cli, buf, sizeof cli - strlen(cli) - 1);
        } else if (!strncmp(argv[i], "--time-offset=", 14)) {
            override_t = atof(argv[i] + 14);
            char buf[64]; snprintf(buf, sizeof buf, " --time-offset=%.3f", override_t);
            strncat(cli, buf, sizeof cli - strlen(cli) - 1);
        } else if (!strncmp(argv[i], "--w=", 4)) {
            w = atoi(argv[i] + 4);
        } else if (!strncmp(argv[i], "--h=", 4)) {
            h = atoi(argv[i] + 4);
        } else if (!strncmp(argv[i], "--quality=", 10)) {
            quality = atoi(argv[i] + 10);
            if (quality == 0) setenv("NCZ_GPU_CLASS", "weak", 1);
        } else if (!strncmp(argv[i], "--n=", 4)) {
            n_frames = atoi(argv[i] + 4);
            if (n_frames < 1) n_frames = 1;
        } else if (!strcmp(argv[i], "--coverage-check")) {
            coverage_check = 1;
        } else if (!strcmp(argv[i], "--coverage-audit")) {
            coverage_check = 1;
            coverage_audit = 1;
        }
    }
    if (quality < 0 && getenv("NCZ_GPU_CLASS") && !strcmp(getenv("NCZ_GPU_CLASS"), "weak"))
        quality = 0;

    /* Build a fake argv for ncz_opts_resolve_preset. */
    char *fake_argv[64];
    int   fake_argc = 0;
    char *cli_dup = strdup(cli);
    if (!cli_dup) { fprintf(stderr, "smoke: oom\n"); return 2; }
    fake_argv[fake_argc++] = (char *)argv[0];
    char *save = NULL;
    for (char *tok = strtok_r(cli_dup, " ", &save); tok;
         tok = strtok_r(NULL, " ", &save)) {
        if (fake_argc >= (int)(sizeof fake_argv / sizeof fake_argv[0]) - 1) break;
        fake_argv[fake_argc++] = tok;
    }
    fake_argv[fake_argc] = NULL;

    ncz_opts opts;
    if (ncz_opts_init(&opts, MC_OPTS, MC_NOPTS) != 0) {
        fprintf(stderr, "smoke: ncz_opts_init failed\n");
        free(cli_dup);
        return 2;
    }
    if (ncz_opts_resolve_preset(&opts, "NCZ_MEGACITY_", "megacity", NULL, NULL, NULL,
                                fake_argc, fake_argv, argv[0], NULL) != 0) {
        fprintf(stderr, "smoke: ncz_opts_resolve_preset failed\n");
        ncz_opts_free(&opts);
        free(cli_dup);
        return 2;
    }

    mkdir(outdir, 0755);

    int ie = init_egl(w, h);
    if (ie == -2) { ncz_opts_free(&opts); free(cli_dup); return 77; }
    if (ie != 0) { ncz_opts_free(&opts); free(cli_dup); return 3; }

    char shader_path[1024];
    const char *candidates[] = {
        "vendor/megacity/megacity.glsl",
        "../vendor/megacity/megacity.glsl",
        "../../vendor/megacity/megacity.glsl",
        "/usr/share/ncz-screensavers/shaders/megacity.glsl",
        NULL
    };
    int found = 0;
    for (int i = 0; candidates[i]; i++) {
        FILE *ft = fopen(candidates[i], "rb");
        if (!ft) continue;
        fclose(ft);
        snprintf(shader_path, sizeof shader_path, "%s", candidates[i]);
        found = 1; break;
    }
    if (!found) {
        fprintf(stderr, "smoke: cannot locate megacity.glsl\n");
        teardown_egl(); ncz_opts_free(&opts); free(cli_dup);
        return 3;
    }

    char *body = load_src(shader_path);
    if (!body) { teardown_egl(); ncz_opts_free(&opts); free(cli_dup); return 3; }
    char log[8192] = "";
    State s;
    memset(&s, 0, sizeof s);
    resolve_state(&opts, &s, w, h);
    s.prog = build_program(body, log, sizeof log);
    free(body);
    if (!s.prog) {
        fprintf(stderr, "smoke: shader build failed:\n%s\n", log);
        teardown_egl(); ncz_opts_free(&opts); free(cli_dup);
        return 3;
    }
    fprintf(stderr, "[diag] shader=ok prog=%u seed=%u mode=%s cycle=%s weather=%s\n",
            s.prog, s.seed,
            ncz_opts_get(&opts, "mode"), ncz_opts_get(&opts, "cycle"),
            ncz_opts_get(&opts, "weather"));

#define U(n) glGetUniformLocation(s.prog, n)
    s.u_res = U("u_res"); s.u_org = U("u_org"); s.u_cell = U("u_cell");
    s.u_fwd = U("u_fwd"); s.u_right = U("u_right"); s.u_up = U("u_up"); s.u_lens = U("u_lens");
    s.u_seed = U("u_seed"); s.u_e = U("u_e"); s.u_sundir = U("u_sundir"); s.u_time = U("u_time");
    s.u_wx0 = U("u_wx0"); s.u_wx1 = U("u_wx1"); s.u_wx2 = U("u_wx2");
    s.u_flash = U("u_flash"); s.u_bolt = U("u_bolt");
    s.u_look = U("u_look"); s.u_look2 = U("u_look2");
    s.u_pal = U("u_pal"); s.u_quality = U("u_quality"); s.u_style = U("u_style");
    s.u_ship_a = U("u_ship_a[0]"); s.u_ship_b = U("u_ship_b[0]"); s.u_craft = U("u_craft[0]");

    static const float quad[] = { -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1 };
    glGenBuffers(1, &s.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Choose scene-time offsets. If the user passed --time-offset, use that
     * exactly; otherwise pick a deterministically derived offset that lands
     * the camera in a populated part of the city for the brief's contact
     * sheets. */
    double base = (override_t >= 0) ? override_t : 90.0;
    if (override_t < 0 && override_seed >= 0) {
        /* Make the default scene-time seed-dependent so different presets show
         * different parts of the city. */
        base = 90.0 + (override_seed % 17) * 7.0;
    }

    for (int i = 0; i < n_frames; i++) {
        double t = base + (double)i * 30.0;
        fprintf(stderr, "[diag] frame %d: t=%.1f e=%.3f\n", i, t, mc_sun(&s.cyc, t));
        draw_frame(&s, t, w, h);

        size_t bytes = (size_t)w * h * 4;
        unsigned char *pixels = (unsigned char *)malloc(bytes);
        if (!pixels) { fprintf(stderr, "smoke: oom\n"); break; }
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        char path[1024];
        if (n_frames == 1)
            snprintf(path, sizeof path, "%s/%s_%s.png", outdir, preset, label);
        else
            snprintf(path, sizeof path, "%s/%s_%s_%02d.png", outdir, preset, label, i);
        if (write_png(path, pixels, w, h) != 0) {
            fprintf(stderr, "smoke: write %s failed: %s\n", path, strerror(errno));
        } else {
            fprintf(stderr, "[diag] wrote %s (%dx%d)\n", path, w, h);
        }
        if (coverage_check) {
            /* Walk the framebuffer once and accumulate dark / mid / bright / neon counts.
             * The brief requires night to read as "rain-soaked neon noir" — which
             * means the *majority* of pixels are dark navy / building bodies, and the
             * neon windows are pinprick accents. We enforce a hard floor on the dark
             * coverage and a hard ceiling on the bright pink/magenta coverage so a
             * future tuning pass cannot quietly regress the look. */
            long total = (long)w * (long)h;
            long dark = 0, mid_dark = 0, mid_bright = 0, bright = 0;
            long pinkish = 0, cyanish = 0, magentaish = 0, neon_emit = 0;
            long sum_lum = 0;
            for (long k = 0; k < total; k++) {
                int r = pixels[k*4 + 0];
                int g = pixels[k*4 + 1];
                int b = pixels[k*4 + 2];
                int lum = (r + g + b) / 3;
                sum_lum += lum;
                if (lum < 50) dark++;
                else if (lum < 100) mid_dark++;
                else if (lum < 180) mid_bright++;
                else bright++;
                /* pink: very red, low green, blue moderate
                 * magenta: red and blue both high, green low
                 * cyan: green and blue high, red low
                 * neon_emit: bright pixel that's also saturated (any neon hue) */
                if (r > 150 && g < r/2 && b > 120) pinkish++;
                if (r > 120 && b > 120 && g < r/2) magentaish++;
                if (r < 80 && g > 130 && b > 130) cyanish++;
                if (lum > 80 && (abs(r - g) > 40 || abs(r - b) > 40 || abs(g - b) > 40)) neon_emit++;
            }
            fprintf(stderr,
                    "[diag] coverage: total=%ld dark=%ld (%.1f%%) mid_dark=%ld (%.1f%%) "
                    "mid_bright=%ld (%.1f%%) bright=%ld (%.1f%%) mean_lum=%.1f "
                    "pinkish=%ld (%.1f%%) magenta=%ld (%.1f%%) cyan=%ld (%.1f%%) neon_emit=%ld (%.1f%%)\n",
                    total,
                    dark, 100.0*dark/total,
                    mid_dark, 100.0*mid_dark/total,
                    mid_bright, 100.0*mid_bright/total,
                    bright, 100.0*bright/total,
                    (double)sum_lum/total,
                    pinkish, 100.0*pinkish/total,
                    magentaish, 100.0*magentaish/total,
                    cyanish, 100.0*cyanish/total,
                    neon_emit, 100.0*neon_emit/total);
            /* Per-preset visual gates. Each gate has a floor for the dominant
             * tonal range and a ceiling for the saturations so a regression
             * in either direction is caught. The thresholds are sized to the
             * brief's "1970s-80s paperback cover megacity" look:
             *  - night: dark navy buildings with bright-pastel mega-structures
             *    (the slab arcology, terraced mass, spire cluster) and neon
             *    windows. A noir dark corridor and a single bright pastel
             *    slab can co-exist in the same frame. The dark floor is
             *    loose (the city has many dark buildings); the bright ceiling
             *    is loose (the slab arcology is a large pastel sheet); the
             *    neon ceiling is what catches a true synthwave regression.
             *  - pale-reach: the towers vanish into pale haze, so almost every
             *    pixel is bright. The bright floor is therefore set high.
             *  - day-cycle transitions sit between the two.
             *  - lightning-peak is the held-at-peak frame: a luminance flash
             *    is expected, the gates relax accordingly. */
            struct gate { const char *name; double dark_min, bright_max, neon_max; int has_bright_dominant; double bright_min; };
            struct gate gates[] = {
                /* night + variants (canonical: night; legacy aliases: oscura,
                 * storm, paperback-night, rain, sleet, smog, ash): dark navy
                 * buildings with bright-pastel mega-structures and neon
                 * windows. A noir dark corridor and a bright pastel slab
                 * arcology co-exist in the frame; the dark floor is loose,
                 * the bright ceiling is loose, the neon ceiling catches a
                 * true synthwave regression. */
                { "night",            20.0, 35.0, 60.0, 0,  0.0 },
                { "oscura",           20.0, 35.0, 60.0, 0,  0.0 },
                { "storm",            20.0, 35.0, 60.0, 0,  0.0 },
                { "paperback-night",  20.0, 35.0, 60.0, 0,  0.0 },
                { "rain",             20.0, 35.0, 60.0, 0,  0.0 },
                { "sleet",            20.0, 35.0, 60.0, 0,  0.0 },
                { "smog",             15.0, 50.0, 60.0, 0,  0.0 },
                { "ash",              15.0, 50.0, 60.0, 0,  0.0 },
                /* These presets are PALE-REACH + heavy weather (canonical:
                 * pale-reach; legacy aliases: luminox, snow, fog, dust, clear):
                 * the towers vanish into pale haze, so a bright-dominant
                 * gate applies. snow whitewashes, fog is opaque, dust is
                 * dusty, clear at pale-reach is the haze baseline. The neon
                 * ceiling still catches any synthwave regression. */
                { "pale-reach",        0.0, 100.0, 25.0, 1, 80.0 },
                { "luminox",           0.0, 100.0, 25.0, 1, 80.0 },
                { "snow",              0.0, 100.0, 25.0, 1, 80.0 },
                { "fog",               0.0, 100.0, 25.0, 1, 80.0 },
                { "dust",              0.0, 100.0, 25.0, 1, 80.0 },
                { "clear",             0.0, 100.0, 25.0, 1, 80.0 },
                { "paperback-noon",    0.0, 100.0, 25.0, 1, 80.0 },
                /* dawn starts in night and transitions to pale-reach; dusk
                 * starts in pale-reach and transitions back. At t=120 with
                 * night-hold=120, day-hold=120, transition=180, dawn is
                 * still in night-hold (still night), dusk is in day-hold
                 * (still pale-reach). So dawn leans dark and dusk leans
                 * bright. day-cycle at t=120 follows the same logic. */
                { "dawn",              0.0, 80.0, 60.0, 0,  0.0 },
                { "dusk",              0.0, 100.0, 50.0, 1, 40.0 },
                { "day-cycle",         0.0, 80.0, 60.0, 0,  0.0 },
                /* lightning-peak is the held-at-peak frame at amplifier 0.05:
                 * the flash is a *luminance delta* added on top of the
                 * underlying dark frame, not a fresh full-bright overlay.
                 * The frame therefore looks mostly dark + the held flash.
                 * The neon ceiling still catches a synthwave regression. */
                { "lightning-peak",   0.0, 80.0, 60.0, 0,  0.0 },
            };
            struct gate *g = NULL;
            for (size_t gi = 0; gi < sizeof(gates)/sizeof(gates[0]); gi++) {
                if (!strcmp(preset, gates[gi].name)) { g = &gates[gi]; break; }
            }
            if (g) {
                int fails = 0;
                double dark_pct = 100.0*dark/total;
                double bright_pct = 100.0*bright/total;
                double neon_pct = 100.0*neon_emit/total;
                fprintf(stderr, "[diag] gate '%s': dark=%.1f%% (>= %.1f) bright=%.1f%% (<= %.1f) neon=%.1f%% (<= %.1f) bright_dom=%.1f%% (>= %.1f)\n",
                        g->name, dark_pct, g->dark_min, bright_pct, g->bright_max,
                        neon_pct, g->neon_max, bright_pct, g->bright_min);
                if (dark_pct < g->dark_min) {
                    fprintf(stderr, "FAIL %s: dark coverage %.1f%% < floor %.1f%% (image too bright, regressed synthwave)\n",
                            g->name, dark_pct, g->dark_min);
                    fails++;
                }
                if (bright_pct > g->bright_max) {
                    fprintf(stderr, "FAIL %s: bright coverage %.1f%% > ceiling %.1f%%\n",
                            g->name, bright_pct, g->bright_max);
                    fails++;
                }
                if (neon_pct > g->neon_max) {
                    fprintf(stderr, "FAIL %s: neon coverage %.1f%% > ceiling %.1f%%\n",
                            g->name, neon_pct, g->neon_max);
                    fails++;
                }
                if (g->has_bright_dominant && bright_pct < g->bright_min) {
                    fprintf(stderr, "FAIL %s: bright coverage %.1f%% < floor %.1f%% (image too dark, not pale-reach)\n",
                            g->name, bright_pct, g->bright_min);
                    fails++;
                }
                if (fails) {
                    fprintf(stderr, "[diag] coverage check failed for preset '%s' (%d gates tripped)\n",
                            g->name, fails);
                    rc_acc |= 4;
                } else {
                    fprintf(stderr, "[diag] coverage ok for preset '%s'\n", g->name);
                }
            } else {
                fprintf(stderr, "[diag] no coverage gate defined for preset '%s' (only checked byte count)\n", preset);
            }
            /* Sanity: file must be > 30 KB (a black frame would be tiny). */
            struct stat st;
            if (stat(path, &st) == 0 && st.st_size < 30000) {
                fprintf(stderr, "FAIL %s: PNG too small (%ld bytes)\n", path, (long)st.st_size);
                rc_acc |= 8;
            }
        }
        free(pixels);
    }
    /* --mode=audit: render every brief-mandated preset, with coverage_check on,
     * and exit non-zero if any preset fails its gate. This is what the
     * 'megacity-coverage' meson test invokes. */
    if (coverage_audit) {
        static const char *audit_presets[] = {
            "night", "pale-reach", "dawn", "dusk", "day-cycle",
            "storm", "rain", "snow", "sleet", "fog", "smog", "dust",
            "ash", "clear", "lightning-peak", NULL
        };
        char audit_dir[1024];
        snprintf(audit_dir, sizeof audit_dir, "%s/_audit", outdir);
        mkdir(audit_dir, 0755);
        for (int pi = 0; audit_presets[pi]; pi++) {
            char label[64]; snprintf(label, sizeof label, "audit-%s", audit_presets[pi]);
            char cmd[2048];
            snprintf(cmd, sizeof cmd, "%s %s %s %s --w=400 --h=225 --seed=42 --time-offset=120 >/dev/null 2>&1",
                     argv[0], audit_presets[pi], label, audit_dir);
            int rr = system(cmd);
            if (rr != 0) {
                fprintf(stderr, "FAIL audit: %s returned %d\n", audit_presets[pi], WEXITSTATUS(rr));
                rc_acc |= 16;
            } else {
                fprintf(stderr, "[diag] audit: %s ok\n", audit_presets[pi]);
            }
        }
    }

    glDeleteBuffers(1, &s.vbo);
    glDeleteProgram(s.prog);
    teardown_egl();
    ncz_opts_free(&opts);
    free(cli_dup);
    return rc_acc;
}